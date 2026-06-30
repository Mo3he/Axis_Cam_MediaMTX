/*
 * MediaMTX ACAP configuration backend (FastCGI).
 *
 * Exposed by the device web server at /local/MediaMTX/config.cgi (admin only).
 *
 *   GET  config.cgi                   -> return current mediamtx.yml (text/plain)
 *   GET  config.cgi?action=defaults   -> return bundled mediamtx.defaults.yml
 *   GET  config.cgi?action=recordings -> JSON list of recorded .mp4 segments
 *   GET  config.cgi?action=recording&file=<rel> -> stream a recording (Range)
 *   POST config.cgi                   -> overwrite mediamtx.yml with request body
 *   POST config.cgi?action=restart    -> restart the MediaMTX process
 */

#define _FILE_OFFSET_BITS 64

#include <dirent.h>
#include <errno.h>
#include <fcgiapp.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define APPDIR       "/usr/local/packages/MediaMTX"
#define LOCALDATA    APPDIR "/localdata"
#define CONF_FILE    LOCALDATA "/mediamtx.yml"
#define DEFAULT_FILE APPDIR "/mediamtx.defaults.yml"
#define PID_FILE     APPDIR "/mediamtx.pid"
#define TMP_FILE     LOCALDATA "/mediamtx.yml.tmp"

#define RECORD_BASE_DEFAULT "/var/spool/storage/areas/SD_DISK/MediaMTX/recordings"

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
        FCGX_PutStr(buf, (int)n, r->out);
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
    if (kill((pid_t)pid, SIGTERM) != 0) {
        syslog(LOG_ERR, "failed to signal pid %ld: %s", pid, strerror(errno));
        return -1;
    }
    return 0;
}

/* Stream the POST body to a temp file then atomically replace mediamtx.yml. */
static int save_config(FCGX_Request* r) {
    const char* cl = FCGX_GetParam("CONTENT_LENGTH", r->envp);
    long remaining  = cl ? atol(cl) : -1;

    FILE* f = fopen(TMP_FILE, "wb");
    if (!f) {
        syslog(LOG_ERR, "cannot open %s: %s", TMP_FILE, strerror(errno));
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

    if (rename(TMP_FILE, CONF_FILE) != 0) {
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

/* Stream a single recording file, honouring an optional HTTP Range request so
 * the browser <video> element can seek. The requested path is confined to the
 * recordings base directory via realpath() to prevent traversal. */
static void send_recording(FCGX_Request* r, const char* base, const char* file) {
    if (!file[0] || strstr(file, "..") || file[0] == '/') {
        send_json(r, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid file\"}");
        return;
    }
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", base, file);

    char realbase[PATH_MAX], realfull[PATH_MAX];
    if (!realpath(base, realbase) || !realpath(full, realfull)) {
        send_json(r, "404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    size_t bl = strlen(realbase);
    if (strncmp(realfull, realbase, bl) != 0 ||
        (realfull[bl] != '/' && realfull[bl] != '\0')) {
        send_json(r, "403 Forbidden", "{\"ok\":false,\"error\":\"forbidden\"}");
        return;
    }

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
        FCGX_PutStr(buf, (int)n, r->out);
        remaining -= n;
    }
    fclose(f);
}

int main(void) {
    openlog("mediamtx_config", LOG_PID, LOG_USER);

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

    int sock = FCGX_OpenSocket(socket_path, 5);
    if (sock < 0) {
        syslog(LOG_ERR, "FCGX_OpenSocket failed");
        return EXIT_FAILURE;
    }
    chmod(socket_path, S_IRWXU | S_IRWXG | S_IRWXO);

    FCGX_Request request;
    if (FCGX_InitRequest(&request, sock, 0) != 0) {
        syslog(LOG_ERR, "FCGX_InitRequest failed");
        return EXIT_FAILURE;
    }

    while (FCGX_Accept_r(&request) == 0) {
        const char* method = FCGX_GetParam("REQUEST_METHOD", request.envp);
        const char* query  = FCGX_GetParam("QUERY_STRING", request.envp);
        if (!method)
            method = "";
        if (!query)
            query = "";

        if (strcmp(method, "GET") == 0) {
            if (strstr(query, "action=defaults")) {
                send_text_file(&request, DEFAULT_FILE);
            } else if (strstr(query, "action=recordings")) {
                char base[PATH_MAX];
                get_record_base(base, sizeof(base));
                FCGX_FPrintF(request.out,
                             "Content-Type: application/json\r\n"
                             "Cache-Control: no-store\r\n\r\n[");
                int first = 1;
                list_recordings(&request, base, "", 0, &first);
                FCGX_FPrintF(request.out, "]");
            } else if (strstr(query, "action=recording")) {
                char base[PATH_MAX], file[PATH_MAX];
                get_record_base(base, sizeof(base));
                if (query_param(query, "file", file, sizeof(file)))
                    send_recording(&request, base, file);
                else
                    send_json(&request, "400 Bad Request",
                              "{\"ok\":false,\"error\":\"missing file\"}");
            } else {
                send_text_file(&request, CONF_FILE);
            }
        } else if (strcmp(method, "POST") == 0) {
            if (strstr(query, "action=restart")) {
                if (restart_mediamtx() == 0)
                    send_json(&request, "200 OK", "{\"ok\":true,\"action\":\"restart\"}");
                else
                    send_json(&request,
                              "500 Internal Server Error",
                              "{\"ok\":false,\"error\":\"restart failed\"}");
            } else {
                if (save_config(&request) == 0)
                    send_json(&request, "200 OK", "{\"ok\":true,\"action\":\"save\"}");
                else
                    send_json(&request,
                              "500 Internal Server Error",
                              "{\"ok\":false,\"error\":\"save failed\"}");
            }
        } else {
            send_json(&request,
                      "405 Method Not Allowed",
                      "{\"ok\":false,\"error\":\"method not allowed\"}");
        }

        FCGX_Finish_r(&request);
    }

    return EXIT_SUCCESS;
}
