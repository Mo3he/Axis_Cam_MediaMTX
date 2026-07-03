/*
 * MediaMTX ACAP configuration backend (FastCGI).
 *
 * Exposed by the device web server at /local/MediaMTX/config.cgi (admin only).
 *
 *   GET  config.cgi                   -> return current mediamtx.yml (text/plain)
 *   GET  config.cgi?action=defaults   -> return bundled mediamtx.defaults.yml
 *   GET  config.cgi?action=backup     -> return previous mediamtx.yml (pre-save)
 *   GET  config.cgi?action=status     -> JSON: MediaMTX running / crash count
 *   GET  config.cgi?action=storage    -> JSON: recordings disk usage
 *   GET  config.cgi?action=recordings -> JSON list of recorded .mp4 segments
 *   GET  config.cgi?action=streams    -> JSON list of recorded stream names
 *   GET  config.cgi?action=fragidx&file=<rel>   -> fragment index for MSE
 *   GET  config.cgi?action=recording&file=<rel> -> stream a recording (Range)
 *   GET  config.cgi?action=timeline&path=<name>[&start=..&end=..]
 *                                     -> recorded timespans (proxies the
 *                                        MediaMTX playback server's /list)
 *   GET  config.cgi?action=clip&path=<name>&start=<RFC3339>&duration=<secs>
 *                   [&format=fmp4|mp4][&download=1]
 *                                     -> extract footage by time (proxies /get)
 *   POST config.cgi                   -> overwrite mediamtx.yml with request body
 *   POST config.cgi?action=restart    -> restart the MediaMTX process
 *   POST config.cgi?action=delete&file=<rel>    -> delete a recording
 *
 * Requests are served by a small pool of threads: clip playback keeps a
 * connection busy for as long as the viewer watches, and must not block the
 * status/timeline requests the recordings page issues meanwhile.
 */

#define _FILE_OFFSET_BITS 64

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcgiapp.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define APPDIR       "/usr/local/packages/MediaMTX"
#define LOCALDATA    APPDIR "/localdata"
#define CONF_FILE    LOCALDATA "/mediamtx.yml"
#define DEFAULT_FILE APPDIR "/mediamtx.defaults.yml"
#define PID_FILE     APPDIR "/mediamtx.pid"
#define FAILS_FILE   APPDIR "/mediamtx.fails"
#define TMP_FILE     LOCALDATA "/mediamtx.yml.tmp" /* .<tid> appended per thread */
#define BAK_FILE     LOCALDATA "/mediamtx.yml.bak"

#define RECORD_BASE_DEFAULT "/var/spool/storage/areas/SD_DISK/MediaMTX/recordings"

#define NUM_WORKERS 4
#define PLAYBACK_PORT_DEFAULT 9996

static void send_text_file(FCGX_Request* r, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        FCGX_FPrintF(r->out,
                     "Status: 404 Not Found\r\n"
                     "Content-Type: text/plain\r\n\r\n"
                     "file not found\n");
        return;
    }
    FCGX_FPrintF(r->out,
                 "Content-Type: text/plain; charset=utf-8\r\n"
                 "Cache-Control: no-store\r\n\r\n");
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        if (FCGX_PutStr(buf, (int)n, r->out) < 0)
            break; /* client went away */
    fclose(f);
}

static void send_json(FCGX_Request* r, const char* status, const char* json) {
    FCGX_FPrintF(r->out,
                 "Status: %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Cache-Control: no-store\r\n\r\n"
                 "%s",
                 status,
                 json);
}

/* CSRF guard for state-changing requests. The device authenticates with
 * ambient credentials (Basic/Digest), which browsers attach to cross-origin
 * form posts too, so a malicious page visited by a logged-in admin could
 * otherwise rewrite the config. Browsers always send Origin (or at least
 * Referer) on POST; when one is present it must match the Host we were
 * reached on. Requests with neither header (curl, scripts) pass. */
static int same_origin(FCGX_Request* r) {
    const char* host = FCGX_GetParam("HTTP_HOST", r->envp);
    if (!host || !*host)
        return 1; /* nothing to compare against */
    const char* src = FCGX_GetParam("HTTP_ORIGIN", r->envp);
    if (!src || !*src)
        src = FCGX_GetParam("HTTP_REFERER", r->envp);
    if (!src || !*src)
        return 1; /* non-browser client */
    const char* p = strstr(src, "://");
    p             = p ? p + 3 : src;
    size_t hl     = strlen(host);
    if (strncmp(p, host, hl) != 0)
        return 0;
    return p[hl] == '\0' || p[hl] == '/';
}

/* Clear the supervisor's consecutive-failure counter. A deliberate restart
 * means the admin is applying a (possibly fixed) config, so past crash history
 * is no longer relevant and the web UI should judge the new config fresh. The
 * supervisor re-reads this file at the top of each loop, so the reset sticks
 * even when it is currently backing off from a crash-loop. */
static void reset_failcount(void) {
    FILE* f = fopen(FAILS_FILE, "w");
    if (f) {
        fputs("0\n", f);
        fclose(f);
    }
}

/* Restart MediaMTX by signaling the pid written by the startup script.
 * The supervising loop in the startup script relaunches it. */
static int restart_mediamtx(void) {
    FILE* f = fopen(PID_FILE, "r");
    if (!f)
        return -1;
    long pid = 0;
    int matched = fscanf(f, "%ld", &pid);
    fclose(f);
    if (matched != 1 || pid <= 1)
        return -1;
    reset_failcount();
    if (kill((pid_t)pid, SIGTERM) != 0) {
        /* ESRCH: MediaMTX has already exited (e.g. crash-looping and currently
         * in the supervisor's backoff sleep). The supervisor will relaunch the
         * current config on its next cycle, so the restart is still honored. */
        if (errno == ESRCH)
            return 0;
        syslog(LOG_ERR, "failed to signal pid %ld: %s", pid, strerror(errno));
        return -1;
    }
    return 0;
}

/* Keep a copy of the current config so a bad save can be recovered from the
 * web UI (GET action=backup). Best effort: a failed backup never blocks the
 * save itself. */
static void backup_config(void) {
    FILE* in = fopen(CONF_FILE, "rb");
    if (!in)
        return;
    FILE* out = fopen(BAK_FILE, "wb");
    if (!out) {
        fclose(in);
        return;
    }
    char   buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n)
            break;
    fclose(in);
    fclose(out);
}

/* Stream the POST body to a temp file then atomically replace mediamtx.yml.
 * The temp file name carries the worker thread id so concurrent saves cannot
 * interleave into the same file. */
static int save_config(FCGX_Request* r, int tid) {
    const char* cl = FCGX_GetParam("CONTENT_LENGTH", r->envp);
    long remaining  = cl ? atol(cl) : -1;

    char tmppath[PATH_MAX];
    snprintf(tmppath, sizeof(tmppath), TMP_FILE ".%d", tid);
    FILE* f = fopen(tmppath, "wb");
    if (!f) {
        syslog(LOG_ERR, "cannot open %s: %s", tmppath, strerror(errno));
        return -1;
    }

    char buf[4096];
    int n;
    if (remaining >= 0) {
        while (remaining > 0) {
            int want = remaining > (long)sizeof(buf) ? (int)sizeof(buf) : (int)remaining;
            n        = FCGX_GetStr(buf, want, r->in);
            if (n <= 0)
                break;
            if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
                fclose(f);
                return -1;
            }
            remaining -= n;
        }
    } else {
        while ((n = FCGX_GetStr(buf, sizeof(buf), r->in)) > 0) {
            if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
                fclose(f);
                return -1;
            }
        }
    }

    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);

    backup_config(); /* only after the new config arrived intact */
    if (rename(tmppath, CONF_FILE) != 0) {
        syslog(LOG_ERR, "rename to %s failed: %s", CONF_FILE, strerror(errno));
        return -1;
    }
    return 0;
}

/* Determine the base directory where recordings are stored by reading the
 * recordPath setting from mediamtx.yml and truncating at the first placeholder
 * (%path / strftime). Falls back to the compiled-in default. */
static void get_record_base(char* out, size_t outsz) {
    out[0] = '\0';
    FILE* f = fopen(CONF_FILE, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            const char* p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '#')
                continue;
            if (strncmp(p, "recordPath:", 11) != 0)
                continue;
            p += 11;
            while (*p == ' ' || *p == '\t')
                p++;
            size_t i = 0;
            while (*p && *p != '%' && *p != '\r' && *p != '\n' && i + 1 < outsz)
                out[i++] = *p++;
            out[i] = '\0';
            while (i > 0 && (out[i - 1] == '/' || out[i - 1] == ' ' || out[i - 1] == '\t'))
                out[--i] = '\0';
            break;
        }
        fclose(f);
    }
    if (out[0] == '\0')
        snprintf(out, outsz, "%s", RECORD_BASE_DEFAULT);
}

/* Parse a top-level "key: value" line from mediamtx.yml. Returns 1 and fills
 * out (comment stripped, whitespace trimmed, surrounding quotes removed) when
 * the line defines exactly this key at indent level zero. */
static int conf_scalar_line(const char* line, const char* key, char* out,
                            size_t outsz) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0 || line[klen] != ':')
        return 0;
    const char* p = line + klen + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    size_t i = 0;
    while (*p && *p != '#' && *p != '\r' && *p != '\n' && i + 1 < outsz)
        out[i++] = *p++;
    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t'))
        i--;
    out[i] = '\0';
    if (i >= 2 && ((out[0] == '"' && out[i - 1] == '"') ||
                   (out[0] == '\'' && out[i - 1] == '\''))) {
        memmove(out, out + 1, i - 2);
        out[i - 2] = '\0';
    }
    return 1;
}

/* Read a top-level scalar setting from mediamtx.yml; empty when absent. */
static void conf_scalar(const char* key, char* out, size_t outsz) {
    out[0]  = '\0';
    FILE* f = fopen(CONF_FILE, "r");
    if (!f)
        return;
    char line[1024];
    while (fgets(line, sizeof(line), f))
        if (conf_scalar_line(line, key, out, outsz))
            break;
    fclose(f);
}

static int truthy(const char* v) {
    return v[0] && strcasecmp(v, "no") != 0 && strcasecmp(v, "false") != 0 &&
           strcasecmp(v, "off") != 0 && strcmp(v, "0") != 0;
}

/* Port of the MediaMTX playback server per mediamtx.yml, or -1 when the
 * server is disabled (the upstream default). The proxy always connects to
 * 127.0.0.1 regardless of the configured bind address. */
static int playback_port(void) {
    char v[256];
    conf_scalar("playback", v, sizeof(v));
    if (!truthy(v))
        return -1;
    conf_scalar("playbackAddress", v, sizeof(v));
    const char* c = strrchr(v, ':');
    long port     = c ? atol(c + 1) : 0;
    if (port <= 0 || port > 65535)
        port = PLAYBACK_PORT_DEFAULT;
    return (int)port;
}

/* Report whether MediaMTX is running and how often it has crashed recently.
 * The pid comes from the file the supervisor writes; the consecutive-failure
 * count is maintained by the supervisor loop in the startup script. The web
 * UI polls this after a restart to detect a config that crash-loops. */
static void send_status(FCGX_Request* r) {
    long  pid = 0;
    FILE* f   = fopen(PID_FILE, "r");
    if (f) {
        if (fscanf(f, "%ld", &pid) != 1)
            pid = 0;
        fclose(f);
    }
    int running = pid > 1 && kill((pid_t)pid, 0) == 0;

    int failures = 0;
    f            = fopen(FAILS_FILE, "r");
    if (f) {
        if (fscanf(f, "%d", &failures) != 1)
            failures = 0;
        fclose(f);
    }

    FCGX_FPrintF(r->out,
                 "Content-Type: application/json\r\n"
                 "Cache-Control: no-store\r\n\r\n"
                 "{\"ok\":true,\"running\":%s,\"failures\":%d}",
                 running ? "true" : "false",
                 failures);
}

/* Disk usage of the filesystem holding the recordings directory. */
static void send_storage(FCGX_Request* r, const char* base) {
    struct statvfs vfs;
    if (statvfs(base, &vfs) != 0) {
        send_json(r, "200 OK", "{\"ok\":false,\"error\":\"storage unavailable\"}");
        return;
    }
    unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
    unsigned long long avail = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
    FCGX_FPrintF(r->out,
                 "Content-Type: application/json\r\n"
                 "Cache-Control: no-store\r\n\r\n"
                 "{\"ok\":true,\"total\":%llu,\"free\":%llu}",
                 total,
                 avail);
}

/* Write a JSON-escaped string (without surrounding quotes) to the stream. */
static void json_puts_escaped(FCGX_Stream* out, const char* s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  FCGX_PutStr("\\\"", 2, out); break;
        case '\\': FCGX_PutStr("\\\\", 2, out); break;
        case '\n': FCGX_PutStr("\\n", 2, out); break;
        case '\r': FCGX_PutStr("\\r", 2, out); break;
        case '\t': FCGX_PutStr("\\t", 2, out); break;
        default:
            if (c < 0x20)
                FCGX_FPrintF(out, "\\u%04x", c);
            else
                FCGX_PutChar(c, out);
        }
    }
}

/*
 * Minimal MP4 duration probe.
 *
 * Computes a recording's length on the device so the web UI does not have to
 * download and parse each file in the browser. Handles both plain MP4 (mvhd /
 * mdhd duration) and the fragmented MP4 that MediaMTX records (sum of fragment
 * sample durations via moof/traf/tfdt/trun). Only box headers and the small
 * moov/moof boxes are read; the large mdat payloads are skipped with a seek.
 *
 * MediaMTX writes the moov up front but leaves the mvhd/tkhd/mdhd duration
 * fields at zero (it is an "empty moov" with no mehd). A browser <video> then
 * has to download the whole file to discover the length before it can start,
 * so large recordings appear to never play. mp4_probe() therefore also records
 * the byte offsets of those duration fields so the streamer can patch in the
 * real value (which has the same width, so no offsets shift) on the fly.
 */

#define MP4_MAX_TRACKS 8
#define MP4_MAX_BOX    (16u * 1024 * 1024)
#define MP4_BOX(a, b, c, d)                                                    \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) |    \
     (uint32_t)(d))

typedef struct {
    uint32_t id;
    uint32_t timescale;
    uint64_t media_duration;
    uint32_t default_dur; /* trex default_sample_duration */
    uint64_t frag_end;    /* max baseMediaDecodeTime + fragment duration */
    int      has_frag;
    int      is_video;
    size_t   tkhd_dur_rel; /* offset of tkhd duration field within moov buffer */
    int      tkhd_dur_len; /* 4 or 8, 0 if absent */
    size_t   mdhd_dur_rel; /* offset of mdhd duration field within moov buffer */
    int      mdhd_dur_len;
} mp4_track;

static uint32_t mp4_be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t mp4_be64(const unsigned char* p) {
    return ((uint64_t)mp4_be32(p) << 32) | (uint64_t)mp4_be32(p + 4);
}

/* Advance over one box in [p, end). On success returns the box end and sets
 * type/body/bodylen; returns NULL when no more (or a malformed) box remains. */
static const unsigned char* mp4_next(const unsigned char* p,
                                     const unsigned char* end, uint32_t* type,
                                     const unsigned char** body,
                                     uint64_t* bodylen) {
    if (p + 8 > end)
        return NULL;
    uint64_t size            = mp4_be32(p);
    uint32_t t               = mp4_be32(p + 4);
    const unsigned char* b   = p + 8;
    if (size == 1) {
        if (p + 16 > end)
            return NULL;
        size = mp4_be64(p + 8);
        b    = p + 16;
    } else if (size == 0) {
        size = (uint64_t)(end - p);
    }
    if (size < (uint64_t)(b - p))
        return NULL;
    const unsigned char* boxend = p + size;
    if (boxend > end || boxend < p)
        return NULL;
    *type    = t;
    *body    = b;
    *bodylen = (uint64_t)(boxend - b);
    return boxend;
}

static void mp4_parse_trak(const unsigned char* base, const unsigned char* body,
                           uint64_t len, mp4_track* tk) {
    const unsigned char* p   = body;
    const unsigned char* end = body + len;
    uint32_t type;
    const unsigned char* b;
    uint64_t bl;
    while ((p = mp4_next(p, end, &type, &b, &bl))) {
        if (type == MP4_BOX('t', 'k', 'h', 'd') && bl >= 1) {
            int    v8  = (b[0] == 1);
            size_t ido = v8 ? 4 + 8 + 8 : 4 + 4 + 4;     /* track_ID */
            size_t dro = v8 ? 4 + 8 + 8 + 4 + 4          /* duration */
                            : 4 + 4 + 4 + 4 + 4;
            int    drl = v8 ? 8 : 4;
            if (bl >= ido + 4)
                tk->id = mp4_be32(b + ido);
            if (bl >= dro + (size_t)drl) {
                tk->tkhd_dur_rel = (size_t)((b + dro) - base);
                tk->tkhd_dur_len = drl;
            }
        } else if (type == MP4_BOX('m', 'd', 'i', 'a')) {
            const unsigned char* q   = b;
            const unsigned char* qe  = b + bl;
            uint32_t t2;
            const unsigned char* b2;
            uint64_t bl2;
            while ((q = mp4_next(q, qe, &t2, &b2, &bl2))) {
                if (t2 == MP4_BOX('m', 'd', 'h', 'd') && bl2 >= 1) {
                    int    v8  = (b2[0] == 1);
                    size_t tso = v8 ? 4 + 8 + 8 : 4 + 4 + 4;      /* timescale */
                    size_t dro = v8 ? 4 + 8 + 8 + 4 : 4 + 4 + 4 + 4;
                    int    drl = v8 ? 8 : 4;
                    if (bl2 >= dro + (size_t)drl) {
                        tk->timescale      = mp4_be32(b2 + tso);
                        tk->media_duration = v8 ? mp4_be64(b2 + dro)
                                                : mp4_be32(b2 + dro);
                        tk->mdhd_dur_rel   = (size_t)((b2 + dro) - base);
                        tk->mdhd_dur_len   = drl;
                    }
                } else if (t2 == MP4_BOX('h', 'd', 'l', 'r') && bl2 >= 12) {
                    if (mp4_be32(b2 + 8) == MP4_BOX('v', 'i', 'd', 'e'))
                        tk->is_video = 1;
                }
            }
        }
    }
}

static void mp4_parse_moov(const unsigned char* base, const unsigned char* body,
                           uint64_t len, uint32_t* movie_ts,
                           uint64_t* movie_dur, size_t* mvhd_dur_rel,
                           int* mvhd_dur_len, mp4_track* tracks, int* ntracks) {
    const unsigned char* p   = body;
    const unsigned char* end = body + len;
    uint32_t type;
    const unsigned char* b;
    uint64_t bl;
    while ((p = mp4_next(p, end, &type, &b, &bl))) {
        if (type == MP4_BOX('m', 'v', 'h', 'd') && bl >= 1) {
            int    v8  = (b[0] == 1);
            size_t tso = v8 ? 4 + 8 + 8 : 4 + 4 + 4;          /* timescale */
            size_t dro = v8 ? 4 + 8 + 8 + 4 : 4 + 4 + 4 + 4;  /* duration */
            int    drl = v8 ? 8 : 4;
            if (bl >= dro + (size_t)drl) {
                *movie_ts     = mp4_be32(b + tso);
                *movie_dur    = v8 ? mp4_be64(b + dro) : mp4_be32(b + dro);
                *mvhd_dur_rel = (size_t)((b + dro) - base);
                *mvhd_dur_len = drl;
            }
        } else if (type == MP4_BOX('t', 'r', 'a', 'k')) {
            if (*ntracks < MP4_MAX_TRACKS) {
                mp4_track* tk = &tracks[*ntracks];
                memset(tk, 0, sizeof(*tk));
                mp4_parse_trak(base, b, bl, tk);
                (*ntracks)++;
            }
        } else if (type == MP4_BOX('m', 'v', 'e', 'x')) {
            const unsigned char* q  = b;
            const unsigned char* qe = b + bl;
            uint32_t t2;
            const unsigned char* b2;
            uint64_t bl2;
            while ((q = mp4_next(q, qe, &t2, &b2, &bl2))) {
                if (t2 == MP4_BOX('t', 'r', 'e', 'x') && bl2 >= 16) {
                    uint32_t tid = mp4_be32(b2 + 4);
                    for (int i = 0; i < *ntracks; i++)
                        if (tracks[i].id == tid)
                            tracks[i].default_dur = mp4_be32(b2 + 12);
                }
            }
        }
    }
}

/*
 * Fragment index for Media Source Extensions playback.
 *
 * MediaMTX records "empty moov" fragmented MP4 with no duration in the header
 * and no trailing mfra index, so a browser <video src> has to download the
 * entire file (up to ~1 GB) just to learn the length and build a seek index
 * before it can start. The web UI therefore plays via MSE: it appends the
 * small init segment (ftyp+moov) plus only the fragments it currently needs.
 *
 * action=fragidx returns the data MSE needs without scanning the file in the
 * browser:
 *   {"ok":true,"duration":<s>,"size":<bytes>,"init":<bytes>,
 *    "codecs":"avc1.640032","frags":[[<t>,<byteOffset>],...]}
 * where "init" is the byte length of the init segment (offset of the first
 * moof) and "frags" lists keyframe (sync-sample) fragments as
 * [timeSeconds, fileByteOffset] so the client can seek by jumping straight to
 * the right fragment. Only box headers and the small moov/moof boxes are read.
 */

/* Build an MSE codecs string (e.g. "avc1.640032" or "avc1.640032,mp4a.40.2")
 * by scanning the moov buffer for the avcC config and any mp4a sample entry. */
static void mp4_detect_codecs(const unsigned char* moov, uint64_t len,
                              char* out, size_t outsz) {
    out[0] = '\0';
    for (uint64_t i = 0; i + 8 <= len; i++) {
        if (moov[i] == 'a' && moov[i + 1] == 'v' && moov[i + 2] == 'c' &&
            moov[i + 3] == 'C') {
            /* avcC payload follows the 4-byte type label:
             * [0]=configurationVersion [1]=profile [2]=compat [3]=level */
            unsigned prof = moov[i + 5];
            unsigned comp = moov[i + 6];
            unsigned lvl  = moov[i + 7];
            snprintf(out, outsz, "avc1.%02x%02x%02x", prof, comp, lvl);
            break;
        }
    }
    for (uint64_t i = 0; i + 4 <= len; i++) {
        if (moov[i] == 'm' && moov[i + 1] == 'p' && moov[i + 2] == '4' &&
            moov[i + 3] == 'a') {
            size_t cur = strlen(out);
            snprintf(out + cur, outsz - cur, "%smp4a.40.2", cur ? "," : "");
            break;
        }
    }
}

/* Parse one moof for the video track. Returns -1 when no matching traf is
 * found, 0 for a non-keyframe fragment, 1 when its first sample is a sync
 * sample (keyframe). On 0/1 it also reports the fragment's tfdt and duration. */
static int mp4_moof_video(const unsigned char* body, uint64_t len,
                          uint32_t video_id, uint32_t video_default_dur,
                          uint64_t* tfdt_out, uint64_t* fragdur_out) {
    const unsigned char* p   = body;
    const unsigned char* end = body + len;
    uint32_t type;
    const unsigned char* b;
    uint64_t bl;
    while ((p = mp4_next(p, end, &type, &b, &bl))) {
        if (type != MP4_BOX('t', 'r', 'a', 'f'))
            continue;
        uint32_t track_id = 0, tfhd_dur = 0, tfhd_dsf = 0;
        uint64_t base_dt = 0, trun_total = 0;
        uint32_t trun_count = 0, first_flags = 0;
        int have_tfdt = 0, have_trun = 0, trun_has_dur = 0;
        int have_dsf = 0, have_first_flags = 0;
        const unsigned char* q  = b;
        const unsigned char* qe = b + bl;
        uint32_t t2;
        const unsigned char* b2;
        uint64_t bl2;
        while ((q = mp4_next(q, qe, &t2, &b2, &bl2))) {
            if (t2 == MP4_BOX('t', 'f', 'h', 'd') && bl2 >= 8) {
                uint32_t flags = mp4_be32(b2) & 0x00FFFFFF;
                track_id       = mp4_be32(b2 + 4);
                size_t off     = 8;
                if (flags & 0x000001)
                    off += 8;
                if (flags & 0x000002)
                    off += 4;
                if (flags & 0x000008) {
                    if (bl2 >= off + 4)
                        tfhd_dur = mp4_be32(b2 + off);
                    off += 4;
                }
                if ((flags & 0x000020) && bl2 >= off + 4) {
                    tfhd_dsf = mp4_be32(b2 + off);
                    have_dsf = 1;
                }
            } else if (t2 == MP4_BOX('t', 'f', 'd', 't') && bl2 >= 5) {
                if (b2[0] == 1) {
                    if (bl2 >= 12)
                        base_dt = mp4_be64(b2 + 4);
                } else {
                    base_dt = mp4_be32(b2 + 4);
                }
                have_tfdt = 1;
            } else if (t2 == MP4_BOX('t', 'r', 'u', 'n') && bl2 >= 8) {
                uint32_t flags = mp4_be32(b2) & 0x00FFFFFF;
                trun_count     = mp4_be32(b2 + 4);
                have_trun      = 1;
                size_t off     = 8;
                if (flags & 0x000001)
                    off += 4; /* data_offset */
                if (flags & 0x000004) {
                    if (off + 4 <= bl2) {
                        first_flags      = mp4_be32(b2 + off);
                        have_first_flags = 1;
                    }
                    off += 4; /* first_sample_flags */
                }
                /* per-sample record layout */
                size_t dur_off  = 0;
                size_t rec      = 0;
                if (flags & 0x000100) {
                    dur_off = rec;
                    rec += 4;
                }
                if (flags & 0x000200)
                    rec += 4;
                size_t flag_off = rec;
                if (flags & 0x000400)
                    rec += 4;
                if (flags & 0x000800)
                    rec += 4;
                size_t recsz = rec;
                if ((flags & 0x000400) && !have_first_flags &&
                    off + flag_off + 4 <= bl2) {
                    first_flags      = mp4_be32(b2 + off + flag_off);
                    have_first_flags = 1;
                }
                if (flags & 0x000100) {
                    trun_has_dur = 1;
                    size_t o     = off;
                    for (uint32_t i = 0; i < trun_count; i++) {
                        if (o + dur_off + 4 > bl2)
                            break;
                        trun_total += mp4_be32(b2 + o + dur_off);
                        o += recsz;
                    }
                }
            }
        }
        if (video_id != 0 && track_id != video_id)
            continue;
        if (!have_tfdt)
            continue;
        uint64_t fragdur = 0;
        if (trun_has_dur)
            fragdur = trun_total;
        else if (have_trun) {
            uint32_t d = tfhd_dur ? tfhd_dur : video_default_dur;
            fragdur    = (uint64_t)d * trun_count;
        }
        *tfdt_out    = base_dt;
        *fragdur_out = fragdur;
        uint32_t sflags  = 0;
        int      have_sf = 0;
        if (have_first_flags) {
            sflags  = first_flags;
            have_sf = 1;
        } else if (have_dsf) {
            sflags  = tfhd_dsf;
            have_sf = 1;
        }
        if (!have_sf)
            return 1; /* no flag info: treat as a seek point */
        return (sflags & 0x00010000) ? 0 : 1;
    }
    return -1;
}

/* Confine a requested recording path to the recordings base directory.
 * Returns 0 and fills realfull on success; sends an error response and
 * returns -1 otherwise. */
static int resolve_recording(FCGX_Request* r, const char* base,
                             const char* file, char realfull[PATH_MAX]) {
    if (!file[0] || strstr(file, "..") || file[0] == '/') {
        send_json(r, "400 Bad Request",
                  "{\"ok\":false,\"error\":\"invalid file\"}");
        return -1;
    }
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", base, file);
    char realbase[PATH_MAX];
    if (!realpath(base, realbase) || !realpath(full, realfull)) {
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return -1;
    }
    size_t bl = strlen(realbase);
    if (strncmp(realfull, realbase, bl) != 0 ||
        (realfull[bl] != '/' && realfull[bl] != '\0')) {
        send_json(r, "403 Forbidden", "{\"ok\":false,\"error\":\"forbidden\"}");
        return -1;
    }
    return 0;
}

static void send_fragidx(FCGX_Request* r, const char* base, const char* file) {
    char realfull[PATH_MAX];
    if (resolve_recording(r, base, file, realfull) != 0)
        return;

    FILE* f = fopen(realfull, "rb");
    if (!f) {
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    long long fsize = (long long)st.st_size;

    mp4_track tracks[MP4_MAX_TRACKS];
    int       ntracks      = 0;
    uint32_t  movie_ts     = 0;
    uint64_t  movie_dur    = 0;
    size_t    mvhd_dur_rel = 0;
    int       mvhd_dur_len = 0;
    char      codecs[64]   = {0};
    long long init_len     = -1;
    uint32_t  vid_id = 0, vid_ts = 0, vid_default_dur = 0;
    uint64_t  frag_end = 0;
    int       have_frag = 0;

    const size_t KF_MAX = 200000;
    double*    kt   = NULL;
    long long* ko   = NULL;
    size_t     nkf  = 0;
    size_t     capkf = 0;

    unsigned char hdr[16];
    for (;;) {
        off_t pos = ftello(f);
        if (pos < 0 || fread(hdr, 1, 8, f) < 8)
            break;
        uint64_t size  = mp4_be32(hdr);
        uint32_t type  = mp4_be32(hdr + 4);
        uint64_t hsize = 8;
        if (size == 1) {
            if (fread(hdr + 8, 1, 8, f) < 8)
                break;
            size  = mp4_be64(hdr + 8);
            hsize = 16;
        } else if (size == 0) {
            break;
        }
        if (size < hsize)
            break;
        uint64_t payload = size - hsize;
        if (type == MP4_BOX('m', 'o', 'o', 'v') && payload > 0 &&
            payload <= MP4_MAX_BOX) {
            unsigned char* buf = malloc(payload);
            if (buf && fread(buf, 1, payload, f) == payload) {
                mp4_parse_moov(buf, buf, payload, &movie_ts, &movie_dur,
                               &mvhd_dur_rel, &mvhd_dur_len, tracks, &ntracks);
                mp4_detect_codecs(buf, payload, codecs, sizeof(codecs));
                for (int i = 0; i < ntracks; i++)
                    if (tracks[i].is_video) {
                        vid_id          = tracks[i].id;
                        vid_ts          = tracks[i].timescale;
                        vid_default_dur = tracks[i].default_dur;
                        break;
                    }
                if (!vid_ts && ntracks > 0) {
                    vid_id          = tracks[0].id;
                    vid_ts          = tracks[0].timescale;
                    vid_default_dur = tracks[0].default_dur;
                }
            }
            free(buf);
        } else if (type == MP4_BOX('m', 'o', 'o', 'f') && payload > 0 &&
                   payload <= MP4_MAX_BOX) {
            if (init_len < 0)
                init_len = (long long)pos; /* first moof ends the init segment */
            unsigned char* buf = malloc(payload);
            if (buf && fread(buf, 1, payload, f) == payload) {
                uint64_t tfdt = 0, fragdur = 0;
                int kf = mp4_moof_video(buf, payload, vid_id, vid_default_dur,
                                        &tfdt, &fragdur);
                if (kf >= 0) {
                    uint64_t endt = tfdt + fragdur;
                    if (endt > frag_end)
                        frag_end = endt;
                    have_frag = 1;
                    if (kf == 1 && vid_ts > 0 && nkf < KF_MAX) {
                        if (nkf == capkf) {
                            size_t nc = capkf ? capkf * 2 : 256;
                            double*    nt = realloc(kt, nc * sizeof(double));
                            long long* no = realloc(ko, nc * sizeof(long long));
                            if (nt)
                                kt = nt;
                            if (no)
                                ko = no;
                            if (!nt || !no)
                                break;
                            capkf = nc;
                        }
                        kt[nkf] = (double)tfdt / (double)vid_ts;
                        ko[nkf] = (long long)pos;
                        nkf++;
                    }
                }
            }
            free(buf);
        }
        if (fseeko(f, pos + (off_t)size, SEEK_SET) != 0)
            break;
    }
    fclose(f);

    double duration = -1.0;
    if (have_frag && vid_ts > 0)
        duration = (double)frag_end / (double)vid_ts;
    else if (movie_dur > 0 && movie_ts > 0)
        duration = (double)movie_dur / (double)movie_ts;
    if (init_len < 0)
        init_len = 0;

    FCGX_FPrintF(r->out,
                 "Content-Type: application/json\r\n"
                 "Cache-Control: no-store\r\n\r\n");
    FCGX_FPrintF(r->out,
                 "{\"ok\":true,\"duration\":%.3f,\"size\":%lld,\"init\":%lld,"
                 "\"codecs\":\"%s\",\"frags\":[",
                 duration > 0 ? duration : 0.0, fsize, init_len, codecs);
    for (size_t i = 0; i < nkf; i++)
        FCGX_FPrintF(r->out, "%s[%.3f,%lld]", i ? "," : "", kt[i], ko[i]);
    FCGX_FPrintF(r->out, "]}");

    free(kt);
    free(ko);
}

/* Recursively emit JSON objects for every .mp4 file under base/rel. */
static void list_recordings(FCGX_Request* r, const char* base, const char* rel,
                            int depth, int* first) {
    if (depth > 8)
        return;
    char dirpath[PATH_MAX];
    if (rel[0])
        snprintf(dirpath, sizeof(dirpath), "%s/%s", base, rel);
    else
        snprintf(dirpath, sizeof(dirpath), "%s", base);

    DIR* d = opendir(dirpath);
    if (!d)
        return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char childrel[PATH_MAX];
        if (rel[0])
            snprintf(childrel, sizeof(childrel), "%s/%s", rel, e->d_name);
        else
            snprintf(childrel, sizeof(childrel), "%s", e->d_name);
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", base, childrel);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            list_recordings(r, base, childrel, depth + 1, first);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(e->d_name);
            if (len < 4 || strcasecmp(e->d_name + len - 4, ".mp4") != 0)
                continue;
            FCGX_FPrintF(r->out, "%s{\"path\":\"", *first ? "" : ",");
            json_puts_escaped(r->out, childrel);
            FCGX_FPrintF(r->out,
                         "\",\"size\":%lld,\"mtime\":%lld}",
                         (long long)st.st_size, (long long)st.st_mtime);
            *first = 0;
        }
    }
    closedir(d);
}

/* Emit JSON strings for every directory under base that directly contains
 * .mp4 recordings; with the default recordPath layout those are exactly the
 * stream path names the recorder writes (%path may itself contain slashes,
 * hence the recursion). */
static void streams_walk(FCGX_Request* r, const char* base, const char* rel,
                         int depth, int* first) {
    if (depth > 8)
        return;
    char dirpath[PATH_MAX];
    if (rel[0])
        snprintf(dirpath, sizeof(dirpath), "%s/%s", base, rel);
    else
        snprintf(dirpath, sizeof(dirpath), "%s", base);

    DIR* d = opendir(dirpath);
    if (!d)
        return;
    struct dirent* e;
    int has_mp4 = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        size_t len = strlen(e->d_name);
        if (len > 4 && strcasecmp(e->d_name + len - 4, ".mp4") == 0) {
            has_mp4 = 1;
            break;
        }
    }
    if (has_mp4 && rel[0]) {
        FCGX_FPrintF(r->out, "%s\"", *first ? "" : ",");
        json_puts_escaped(r->out, rel);
        FCGX_FPrintF(r->out, "\"");
        *first = 0;
        closedir(d);
        return;
    }
    rewinddir(d);
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char childrel[PATH_MAX];
        if (rel[0])
            snprintf(childrel, sizeof(childrel), "%s/%s", rel, e->d_name);
        else
            snprintf(childrel, sizeof(childrel), "%s", e->d_name);
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", base, childrel);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            streams_walk(r, base, childrel, depth + 1, first);
    }
    closedir(d);
}

static int hexval(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static void url_decode(const char* in, char* out, size_t outsz) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < outsz; i++) {
        if (in[i] == '%' && hexval((unsigned char)in[i + 1]) >= 0 &&
            hexval((unsigned char)in[i + 2]) >= 0) {
            out[o++] = (char)(hexval((unsigned char)in[i + 1]) * 16 +
                              hexval((unsigned char)in[i + 2]));
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

/* Extract and URL-decode a query-string parameter. Returns 1 if found. */
static int query_param(const char* query, const char* key, char* out, size_t outsz) {
    size_t klen = strlen(key);
    const char* p = query;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char* v   = p + klen + 1;
            const char* amp = strchr(v, '&');
            size_t vlen     = amp ? (size_t)(amp - v) : strlen(v);
            char raw[PATH_MAX];
            if (vlen >= sizeof(raw))
                vlen = sizeof(raw) - 1;
            memcpy(raw, v, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, outsz);
            return 1;
        }
        p = strchr(p, '&');
        if (p)
            p++;
    }
    return 0;
}

/* Extract a query-string parameter without URL-decoding it, for values that
 * are forwarded verbatim to the playback server. Returns 1 if found. */
static int query_param_raw(const char* query, const char* key, char* out,
                           size_t outsz) {
    size_t klen   = strlen(key);
    const char* p = query;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char* v   = p + klen + 1;
            const char* amp = strchr(v, '&');
            size_t vlen     = amp ? (size_t)(amp - v) : strlen(v);
            if (vlen >= outsz)
                vlen = outsz - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return 1;
        }
        p = strchr(p, '&');
        if (p)
            p++;
    }
    return 0;
}

/* Validate a raw (still percent-encoded) value before embedding it in the
 * upstream HTTP request line: URL-safe characters only, so the request cannot
 * be corrupted (no spaces, CR/LF, '&', '?', etc.). */
static int is_safe_rawparam(const char* s) {
    if (!*s)
        return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || strchr("-._~%+:/", c))
            continue;
        return 0;
    }
    return 1;
}

static int write_all(int fd, const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int playback_connect(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    /* Never let a wedged playback server hang a worker thread; streaming
     * reads normally return long before this. */
    struct timeval tv = {30, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * Proxy one request to the MediaMTX playback server on localhost and stream
 * the response back. This keeps the playback server unexposed (it is bound to
 * 127.0.0.1), reuses the device's admin authentication, and avoids the CORS
 * and mixed-content problems of pointing the browser at another port.
 *
 * The request is sent as HTTP/1.0 so the response is never chunk-encoded (the
 * device web server applies its own framing); the body simply ends at EOF.
 */
static void proxy_playback(FCGX_Request* r, const char* upath,
                           const char* query, const char* dlname) {
    int port = playback_port();
    if (port < 0) {
        send_json(r, "200 OK",
                  "{\"ok\":false,\"error\":\"playback server disabled\"}");
        return;
    }
    int fd = playback_connect(port);
    if (fd < 0) {
        send_json(r, "200 OK",
                  "{\"ok\":false,\"error\":\"playback server unreachable\"}");
        return;
    }

    char req[2048];
    int m = snprintf(req, sizeof(req),
                     "GET %s?%s HTTP/1.0\r\n"
                     "Host: 127.0.0.1\r\n"
                     "Connection: close\r\n\r\n",
                     upath, query);
    if (m < 0 || m >= (int)sizeof(req) || write_all(fd, req, (size_t)m) != 0) {
        close(fd);
        send_json(r, "502 Bad Gateway",
                  "{\"ok\":false,\"error\":\"playback request failed\"}");
        return;
    }

    /* Read the response headers (and possibly the start of the body). */
    char   hdr[8192];
    size_t hlen = 0;
    char*  body = NULL;
    while (hlen < sizeof(hdr) - 1) {
        ssize_t n = read(fd, hdr + hlen, sizeof(hdr) - 1 - hlen);
        if (n <= 0)
            break;
        hlen += (size_t)n;
        hdr[hlen] = '\0';
        if ((body = strstr(hdr, "\r\n\r\n"))) {
            body += 4;
            break;
        }
    }
    if (!body) {
        close(fd);
        send_json(r, "502 Bad Gateway",
                  "{\"ok\":false,\"error\":\"bad playback response\"}");
        return;
    }
    size_t leftover = hlen - (size_t)(body - hdr);
    body[-4]        = '\0'; /* confine header parsing to the header block */

    /* Status line: "HTTP/1.x 200 OK" -> forward "200 OK". */
    char status[64]  = "200 OK";
    const char* sp   = strchr(hdr, ' ');
    if (sp) {
        sp++;
        const char* eol = strstr(sp, "\r\n");
        size_t sl       = eol ? (size_t)(eol - sp) : strlen(sp);
        if (sl >= sizeof(status))
            sl = sizeof(status) - 1;
        memcpy(status, sp, sl);
        status[sl] = '\0';
    }

    char ctype[128] = "application/octet-stream";
    for (const char* h = strstr(hdr, "\r\n"); h; h = strstr(h, "\r\n")) {
        h += 2;
        if (strncasecmp(h, "Content-Type:", 13) == 0) {
            const char* v = h + 13;
            while (*v == ' ')
                v++;
            const char* eol = strstr(v, "\r\n");
            size_t vl       = eol ? (size_t)(eol - v) : strlen(v);
            if (vl >= sizeof(ctype))
                vl = sizeof(ctype) - 1;
            memcpy(ctype, v, vl);
            ctype[vl] = '\0';
            break;
        }
    }

    FCGX_FPrintF(r->out,
                 "Status: %s\r\n"
                 "Content-Type: %s\r\n"
                 "Cache-Control: no-store\r\n",
                 status, ctype);
    if (dlname)
        FCGX_FPrintF(r->out, "Content-Disposition: attachment; filename=\"%s\"\r\n",
                     dlname);
    FCGX_FPrintF(r->out, "\r\n");

    if (leftover > 0 && FCGX_PutStr(body, (int)leftover, r->out) < 0) {
        close(fd);
        return; /* client went away */
    }
    char    buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        if (FCGX_PutStr(buf, (int)n, r->out) < 0)
            break;
    close(fd);
}

/* Build a safe download filename from the (decoded) stream path and start
 * time, e.g. "cam1_2026-07-02T14-30-00Z.mp4". */
static void clip_filename(const char* path, const char* start, char* out,
                          size_t outsz) {
    size_t o = 0;
    const char* parts[2] = {path, start};
    for (int i = 0; i < 2; i++) {
        if (i && o + 1 < outsz - 5)
            out[o++] = '_';
        for (const char* p = parts[i]; *p && o + 1 < outsz - 5; p++) {
            unsigned char c = (unsigned char)*p;
            int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z') || c == '-' || c == '.';
            out[o++] = ok ? (char)c : '-';
        }
    }
    snprintf(out + o, outsz - o, ".mp4");
}

/* GET action=timeline&path=<name>[&start=..&end=..]: recorded timespans. */
static void handle_timeline(FCGX_Request* r, const char* query) {
    char path[512], start[128], end[128], q[1024];
    if (!query_param_raw(query, "path", path, sizeof(path)) ||
        !is_safe_rawparam(path)) {
        send_json(r, "400 Bad Request", "{\"ok\":false,\"error\":\"bad path\"}");
        return;
    }
    if (!query_param_raw(query, "start", start, sizeof(start)) ||
        !is_safe_rawparam(start))
        start[0] = '\0';
    if (!query_param_raw(query, "end", end, sizeof(end)) ||
        !is_safe_rawparam(end))
        end[0] = '\0';
    snprintf(q, sizeof(q), "path=%s%s%s%s%s", path,
             start[0] ? "&start=" : "", start,
             end[0] ? "&end=" : "", end);
    proxy_playback(r, "/list", q, NULL);
}

/* GET action=clip&path=..&start=..&duration=..[&format=..][&download=1]:
 * footage for an arbitrary time window, stitched across segment files by the
 * playback server. */
static void handle_clip(FCGX_Request* r, const char* query) {
    char path[512], start[128], dur[64], fmt[16], q[1024];
    if (!query_param_raw(query, "path", path, sizeof(path)) ||
        !is_safe_rawparam(path) ||
        !query_param_raw(query, "start", start, sizeof(start)) ||
        !is_safe_rawparam(start) ||
        !query_param_raw(query, "duration", dur, sizeof(dur)) ||
        !is_safe_rawparam(dur)) {
        send_json(r, "400 Bad Request",
                  "{\"ok\":false,\"error\":\"need path, start, duration\"}");
        return;
    }
    if (!query_param_raw(query, "format", fmt, sizeof(fmt)) ||
        (strcmp(fmt, "mp4") != 0 && strcmp(fmt, "fmp4") != 0))
        snprintf(fmt, sizeof(fmt), "fmp4");
    snprintf(q, sizeof(q), "path=%s&start=%s&duration=%s&format=%s", path,
             start, dur, fmt);

    char  flag[8], dlname[512];
    char* dl = NULL;
    if (query_param_raw(query, "download", flag, sizeof(flag))) {
        char dpath[512], dstart[128];
        url_decode(path, dpath, sizeof(dpath));
        url_decode(start, dstart, sizeof(dstart));
        clip_filename(dpath, dstart, dlname, sizeof(dlname));
        dl = dlname;
    }
    proxy_playback(r, "/get", q, dl);
}

/* Stream a single recording file, honouring an optional HTTP Range request so
 * the browser can seek. The web UI plays via MSE (action=fragidx), which
 * issues many small Range requests, so this path stays a plain byte server
 * with no per-request parsing. The path is confined to the recordings base
 * directory via realpath() to prevent traversal. */
static void send_recording(FCGX_Request* r, const char* base, const char* file) {
    char realfull[PATH_MAX];
    if (resolve_recording(r, base, file, realfull) != 0)
        return;

    FILE* f = fopen(realfull, "rb");
    if (!f) {
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    long long total = (long long)st.st_size;
    long long start = 0, end = total - 1;
    int partial = 0;

    const char* range = FCGX_GetParam("HTTP_RANGE", r->envp);
    if (range && strncmp(range, "bytes=", 6) == 0) {
        const char* s    = range + 6;
        const char* dash = strchr(s, '-');
        long long rs     = (*s != '-') ? atoll(s) : -1;
        long long re     = (dash && *(dash + 1)) ? atoll(dash + 1) : -1;
        if (rs < 0 && re >= 0) {
            start = total - re;
            if (start < 0)
                start = 0;
            end = total - 1;
        } else {
            start = rs < 0 ? 0 : rs;
            end   = re < 0 ? total - 1 : re;
        }
        if (end >= total)
            end = total - 1;
        if (total == 0 || start > end || start >= total) {
            FCGX_FPrintF(r->out,
                         "Status: 416 Range Not Satisfiable\r\n"
                         "Content-Range: bytes */%lld\r\n\r\n",
                         total);
            fclose(f);
            return;
        }
        partial = 1;
    }

    long long length = end - start + 1;
    if (partial) {
        FCGX_FPrintF(r->out,
                     "Status: 206 Partial Content\r\n"
                     "Content-Type: video/mp4\r\n"
                     "Accept-Ranges: bytes\r\n"
                     "Content-Range: bytes %lld-%lld/%lld\r\n"
                     "Content-Length: %lld\r\n"
                     "Cache-Control: no-store\r\n\r\n",
                     start, end, total, length);
    } else {
        FCGX_FPrintF(r->out,
                     "Content-Type: video/mp4\r\n"
                     "Accept-Ranges: bytes\r\n"
                     "Content-Length: %lld\r\n"
                     "Cache-Control: no-store\r\n\r\n",
                     length);
    }

    /* MediaMTX records an "empty moov" (zero duration in the header), but the
     * web UI plays via MSE and gets the real duration from action=fragidx, so
     * this path just streams the requested byte range verbatim. */
    if (fseeko(f, (off_t)start, SEEK_SET) != 0) {
        fclose(f);
        return;
    }
    char buf[65536];
    long long remaining = length;
    while (remaining > 0) {
        size_t want = remaining > (long long)sizeof(buf) ? sizeof(buf) : (size_t)remaining;
        size_t n    = fread(buf, 1, want, f);
        if (n == 0)
            break;
        if (FCGX_PutStr(buf, (int)n, r->out) < 0)
            break; /* client disconnected (e.g. MSE aborted the range) */
        remaining -= n;
    }
    fclose(f);
}

/* Delete a single recording. The path is confined to the recordings base
 * directory by resolve_recording(), same as playback. */
static void delete_recording(FCGX_Request* r, const char* base, const char* file) {
    char realfull[PATH_MAX];
    if (resolve_recording(r, base, file, realfull) != 0)
        return;
    struct stat st;
    if (stat(realfull, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    if (unlink(realfull) != 0) {
        syslog(LOG_ERR, "delete %s failed: %s", realfull, strerror(errno));
        send_json(r, "500 Internal Server Error",
                  "{\"ok\":false,\"error\":\"delete failed\"}");
        return;
    }
    send_json(r, "200 OK", "{\"ok\":true,\"action\":\"delete\"}");
}

static void handle_request(FCGX_Request* r, int tid) {
    const char* method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    const char* query  = FCGX_GetParam("QUERY_STRING", r->envp);
    if (!method)
        method = "";
    if (!query)
        query = "";

    if (strcmp(method, "GET") == 0) {
        if (strstr(query, "action=defaults")) {
            send_text_file(r, DEFAULT_FILE);
        } else if (strstr(query, "action=backup")) {
            send_text_file(r, BAK_FILE);
        } else if (strstr(query, "action=status")) {
            send_status(r);
        } else if (strstr(query, "action=storage")) {
            char base[PATH_MAX];
            get_record_base(base, sizeof(base));
            send_storage(r, base);
        } else if (strstr(query, "action=recordings")) {
            char base[PATH_MAX];
            get_record_base(base, sizeof(base));
            FCGX_FPrintF(r->out,
                         "Content-Type: application/json\r\n"
                         "Cache-Control: no-store\r\n\r\n[");
            int first = 1;
            list_recordings(r, base, "", 0, &first);
            FCGX_FPrintF(r->out, "]");
        } else if (strstr(query, "action=streams")) {
            char base[PATH_MAX];
            get_record_base(base, sizeof(base));
            FCGX_FPrintF(r->out,
                         "Content-Type: application/json\r\n"
                         "Cache-Control: no-store\r\n\r\n[");
            int first = 1;
            streams_walk(r, base, "", 0, &first);
            FCGX_FPrintF(r->out, "]");
        } else if (strstr(query, "action=timeline")) {
            handle_timeline(r, query);
        } else if (strstr(query, "action=clip")) {
            handle_clip(r, query);
        } else if (strstr(query, "action=fragidx")) {
            char base[PATH_MAX], file[PATH_MAX];
            get_record_base(base, sizeof(base));
            if (query_param(query, "file", file, sizeof(file)))
                send_fragidx(r, base, file);
            else
                send_json(r, "400 Bad Request",
                          "{\"ok\":false,\"error\":\"missing file\"}");
        } else if (strstr(query, "action=recording")) {
            char base[PATH_MAX], file[PATH_MAX];
            get_record_base(base, sizeof(base));
            if (query_param(query, "file", file, sizeof(file)))
                send_recording(r, base, file);
            else
                send_json(r, "400 Bad Request",
                          "{\"ok\":false,\"error\":\"missing file\"}");
        } else {
            send_text_file(r, CONF_FILE);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (!same_origin(r)) {
            send_json(r, "403 Forbidden",
                      "{\"ok\":false,\"error\":\"cross-origin request rejected\"}");
        } else if (strstr(query, "action=restart")) {
            if (restart_mediamtx() == 0)
                send_json(r, "200 OK", "{\"ok\":true,\"action\":\"restart\"}");
            else
                send_json(r,
                          "500 Internal Server Error",
                          "{\"ok\":false,\"error\":\"restart failed\"}");
        } else if (strstr(query, "action=delete")) {
            char base[PATH_MAX], file[PATH_MAX];
            get_record_base(base, sizeof(base));
            if (query_param(query, "file", file, sizeof(file)))
                delete_recording(r, base, file);
            else
                send_json(r, "400 Bad Request",
                          "{\"ok\":false,\"error\":\"missing file\"}");
        } else {
            if (save_config(r, tid) == 0)
                send_json(r, "200 OK", "{\"ok\":true,\"action\":\"save\"}");
            else
                send_json(r,
                          "500 Internal Server Error",
                          "{\"ok\":false,\"error\":\"save failed\"}");
        }
    } else {
        send_json(r,
                  "405 Method Not Allowed",
                  "{\"ok\":false,\"error\":\"method not allowed\"}");
    }
}

static int g_sock = -1;

/* One accept loop per worker thread; FCGX_Accept_r is thread-safe over a
 * shared listening socket. */
static void* request_loop(void* arg) {
    int          tid = (int)(intptr_t)arg;
    FCGX_Request request;
    if (FCGX_InitRequest(&request, g_sock, 0) != 0) {
        syslog(LOG_ERR, "FCGX_InitRequest failed (worker %d)", tid);
        return NULL;
    }
    while (FCGX_Accept_r(&request) == 0) {
        handle_request(&request, tid);
        FCGX_Finish_r(&request);
    }
    return NULL;
}

int main(void) {
    openlog("mediamtx_config", LOG_PID, LOG_USER);

    /* A disconnected client (the MSE player aborts range fetches on every
     * seek) must not raise SIGPIPE and kill the worker; writes then fail
     * cleanly and the streaming loops stop via the FCGX_PutStr error check. */
    signal(SIGPIPE, SIG_IGN);

    const char* socket_path = getenv("FCGI_SOCKET_NAME");
    if (!socket_path) {
        syslog(LOG_ERR, "FCGI_SOCKET_NAME not set");
        return EXIT_FAILURE;
    }
    syslog(LOG_INFO, "config.cgi starting on socket %s", socket_path);

    if (FCGX_Init() != 0) {
        syslog(LOG_ERR, "FCGX_Init failed");
        return EXIT_FAILURE;
    }

    g_sock = FCGX_OpenSocket(socket_path, 16);
    if (g_sock < 0) {
        syslog(LOG_ERR, "FCGX_OpenSocket failed");
        return EXIT_FAILURE;
    }
    /* The device web server runs as a different user and needs write access
     * to connect; execute bits are meaningless on a socket, so drop them. */
    chmod(socket_path,
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    for (long tid = 1; tid < NUM_WORKERS; tid++) {
        pthread_t t;
        if (pthread_create(&t, NULL, request_loop, (void*)(intptr_t)tid) != 0) {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(errno));
            return EXIT_FAILURE;
        }
        pthread_detach(t);
    }
    request_loop((void*)(intptr_t)0);

    return EXIT_SUCCESS;
}
