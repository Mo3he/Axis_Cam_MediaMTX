/*
 * Host-side unit tests for the pure parsing code in app/config.c: the MP4
 * box walker used by the MSE fragment index, URL/query helpers, and the
 * same-origin CSRF guard.
 *
 * config.c is included directly (with its main() renamed) so its static
 * functions are testable without changing the production layout. Build with
 * the stub FastCGI header:
 *
 *   cc -Wall -Wextra -Werror -Itests/fcgi_stub tests/test_mp4.c -o tests/test_mp4
 *   ./tests/test_mp4
 */

#define main mediamtx_config_main
#include "../app/config.c"
#undef main

#include <assert.h>

static int tests_run = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        tests_run++;                                                           \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- helpers to build MP4 box structures in memory ---------------------- */

typedef struct {
    unsigned char b[4096];
    size_t        len;
} buf_t;

static void put8(buf_t* x, unsigned v) {
    x->b[x->len++] = (unsigned char)v;
}

static void put32(buf_t* x, uint32_t v) {
    put8(x, v >> 24);
    put8(x, (v >> 16) & 0xff);
    put8(x, (v >> 8) & 0xff);
    put8(x, v & 0xff);
}

static void put64(buf_t* x, uint64_t v) {
    put32(x, (uint32_t)(v >> 32));
    put32(x, (uint32_t)v);
}

static void puttag(buf_t* x, const char* t) {
    memcpy(x->b + x->len, t, 4);
    x->len += 4;
}

/* Open a box; returns the offset of its size field for box_end(). */
static size_t box_begin(buf_t* x, const char* type) {
    size_t at = x->len;
    put32(x, 0);
    puttag(x, type);
    return at;
}

static void box_end(buf_t* x, size_t at) {
    uint32_t size = (uint32_t)(x->len - at);
    x->b[at]      = size >> 24;
    x->b[at + 1]  = (size >> 16) & 0xff;
    x->b[at + 2]  = (size >> 8) & 0xff;
    x->b[at + 3]  = size & 0xff;
}

/* ---- individual test groups --------------------------------------------- */

static int test_url_helpers(void) {
    char out[128];

    url_decode("a%20b+c%2Fd", out, sizeof(out));
    CHECK(strcmp(out, "a b c/d") == 0);

    url_decode("plain", out, sizeof(out));
    CHECK(strcmp(out, "plain") == 0);

    /* malformed escapes pass through untouched */
    url_decode("bad%zz%2", out, sizeof(out));
    CHECK(strcmp(out, "bad%zz%2") == 0);

    CHECK(query_param("action=recording&file=stream%2Ffile.mp4", "file", out,
                      sizeof(out)) == 1);
    CHECK(strcmp(out, "stream/file.mp4") == 0);

    CHECK(query_param("action=recording", "file", out, sizeof(out)) == 0);

    /* key must match exactly, not as a prefix of another key */
    CHECK(query_param("filename=x&file=y", "file", out, sizeof(out)) == 1);
    CHECK(strcmp(out, "y") == 0);

    return 0;
}

static int test_same_origin(void) {
    FCGX_Request rq;
    memset(&rq, 0, sizeof(rq));

    char* ok_env[] = {(char*)"HTTP_HOST=cam.local",
                      (char*)"HTTP_ORIGIN=https://cam.local", NULL};
    rq.envp        = ok_env;
    CHECK(same_origin(&rq) == 1);

    char* evil_env[] = {(char*)"HTTP_HOST=cam.local",
                        (char*)"HTTP_ORIGIN=https://evil.example", NULL};
    rq.envp          = evil_env;
    CHECK(same_origin(&rq) == 0);

    /* prefix tricks must not pass */
    char* prefix_env[] = {(char*)"HTTP_HOST=cam.local",
                          (char*)"HTTP_ORIGIN=https://cam.local.evil.example",
                          NULL};
    rq.envp            = prefix_env;
    CHECK(same_origin(&rq) == 0);

    /* Referer is the fallback when Origin is missing */
    char* ref_env[] = {(char*)"HTTP_HOST=cam.local",
                       (char*)"HTTP_REFERER=https://cam.local/index.html",
                       NULL};
    rq.envp         = ref_env;
    CHECK(same_origin(&rq) == 1);

    char* ref_evil[] = {(char*)"HTTP_HOST=cam.local",
                        (char*)"HTTP_REFERER=https://evil.example/cam.local",
                        NULL};
    rq.envp          = ref_evil;
    CHECK(same_origin(&rq) == 0);

    /* non-browser clients send neither header */
    char* bare_env[] = {(char*)"HTTP_HOST=cam.local", NULL};
    rq.envp          = bare_env;
    CHECK(same_origin(&rq) == 1);

    /* explicit ports must match exactly */
    char* port_env[] = {(char*)"HTTP_HOST=cam.local:8443",
                        (char*)"HTTP_ORIGIN=https://cam.local:8443", NULL};
    rq.envp          = port_env;
    CHECK(same_origin(&rq) == 1);

    return 0;
}

static int test_mp4_next(void) {
    buf_t x = {{0}, 0};
    size_t at = box_begin(&x, "ftyp");
    put32(&x, 0x69736f6d); /* isom */
    box_end(&x, at);

    uint32_t             type;
    const unsigned char* body;
    uint64_t             bodylen;
    const unsigned char* p =
        mp4_next(x.b, x.b + x.len, &type, &body, &bodylen);
    CHECK(p == x.b + x.len);
    CHECK(type == MP4_BOX('f', 't', 'y', 'p'));
    CHECK(bodylen == 4);
    CHECK(mp4_be32(body) == 0x69736f6d);

    /* 64-bit size (size field == 1) */
    buf_t y = {{0}, 0};
    put32(&y, 1);
    puttag(&y, "mdat");
    put64(&y, 16 + 4); /* full box size including the 16-byte header */
    put32(&y, 0xdeadbeef);
    p = mp4_next(y.b, y.b + y.len, &type, &body, &bodylen);
    CHECK(p == y.b + y.len);
    CHECK(type == MP4_BOX('m', 'd', 'a', 't'));
    CHECK(bodylen == 4);

    /* truncated header */
    CHECK(mp4_next(x.b, x.b + 4, &type, &body, &bodylen) == NULL);

    /* declared size larger than the buffer */
    buf_t z = {{0}, 0};
    put32(&z, 100);
    puttag(&z, "free");
    CHECK(mp4_next(z.b, z.b + z.len, &type, &body, &bodylen) == NULL);

    return 0;
}

/* Build a moov *body* (the payload send_fragidx hands to mp4_parse_moov)
 * containing mvhd + one video trak + mvex/trex. */
static void build_moov_body(buf_t* x) {
    size_t at = box_begin(x, "mvhd");
    put32(x, 0);     /* version 0, flags */
    put32(x, 0);     /* creation_time */
    put32(x, 0);     /* modification_time */
    put32(x, 1000);  /* timescale */
    put32(x, 5000);  /* duration */
    box_end(x, at);

    size_t trak = box_begin(x, "trak");
    at          = box_begin(x, "tkhd");
    put32(x, 0);    /* version 0, flags */
    put32(x, 0);    /* creation_time */
    put32(x, 0);    /* modification_time */
    put32(x, 1);    /* track_ID */
    put32(x, 0);    /* reserved */
    put32(x, 5000); /* duration */
    box_end(x, at);

    size_t mdia = box_begin(x, "mdia");
    at          = box_begin(x, "mdhd");
    put32(x, 0);     /* version 0, flags */
    put32(x, 0);     /* creation_time */
    put32(x, 0);     /* modification_time */
    put32(x, 90000); /* timescale */
    put32(x, 0);     /* duration (MediaMTX leaves this zero) */
    box_end(x, at);

    at = box_begin(x, "hdlr");
    put32(x, 0); /* version, flags */
    put32(x, 0); /* pre_defined */
    puttag(x, "vide");
    box_end(x, at);
    box_end(x, mdia);
    box_end(x, trak);

    size_t mvex = box_begin(x, "mvex");
    at          = box_begin(x, "trex");
    put32(x, 0);    /* version, flags */
    put32(x, 1);    /* track_ID */
    put32(x, 1);    /* default_sample_description_index */
    put32(x, 3000); /* default_sample_duration */
    put32(x, 0);    /* default_sample_size */
    put32(x, 0);    /* default_sample_flags */
    box_end(x, at);
    box_end(x, mvex);
}

static int test_parse_moov(void) {
    buf_t x = {{0}, 0};
    build_moov_body(&x);

    mp4_track tracks[MP4_MAX_TRACKS];
    int       ntracks      = 0;
    uint32_t  movie_ts     = 0;
    uint64_t  movie_dur    = 0;
    size_t    mvhd_dur_rel = 0;
    int       mvhd_dur_len = 0;
    mp4_parse_moov(x.b, x.b, x.len, &movie_ts, &movie_dur, &mvhd_dur_rel,
                   &mvhd_dur_len, tracks, &ntracks);

    CHECK(movie_ts == 1000);
    CHECK(movie_dur == 5000);
    CHECK(mvhd_dur_len == 4);
    /* duration lives 16 bytes into the mvhd body, after the 8-byte header */
    CHECK(mvhd_dur_rel == 8 + 16);
    CHECK(mp4_be32(x.b + mvhd_dur_rel) == 5000);

    CHECK(ntracks == 1);
    CHECK(tracks[0].id == 1);
    CHECK(tracks[0].timescale == 90000);
    CHECK(tracks[0].is_video == 1);
    CHECK(tracks[0].default_dur == 3000);
    CHECK(tracks[0].tkhd_dur_len == 4);
    CHECK(tracks[0].mdhd_dur_len == 4);

    return 0;
}

/* Build a moof *body* with one traf. first_flags selects keyframe-ness:
 * bit 0x00010000 set means sample_is_non_sync_sample (not a keyframe). */
static void build_moof_body(buf_t* x, uint32_t first_flags, int per_sample_dur) {
    size_t traf = box_begin(x, "traf");

    size_t at = box_begin(x, "tfhd");
    put32(x, 0x000008); /* version 0, flags: default-sample-duration */
    put32(x, 1);        /* track_ID */
    put32(x, 3000);     /* default_sample_duration */
    box_end(x, at);

    at = box_begin(x, "tfdt");
    put32(x, 0x01000000); /* version 1 */
    put64(x, 90000);      /* baseMediaDecodeTime */
    box_end(x, at);

    at = box_begin(x, "trun");
    if (per_sample_dur) {
        put32(x, 0x000105); /* data-offset | first-sample-flags | sample-duration */
        put32(x, 2);        /* sample_count */
        put32(x, 0);        /* data_offset */
        put32(x, first_flags);
        put32(x, 3000);     /* sample 1 duration */
        put32(x, 3300);     /* sample 2 duration */
    } else {
        put32(x, 0x000005); /* data-offset | first-sample-flags */
        put32(x, 2);        /* sample_count */
        put32(x, 0);        /* data_offset */
        put32(x, first_flags);
    }
    box_end(x, at);

    box_end(x, traf);
}

static int test_moof_video(void) {
    uint64_t tfdt = 0, fragdur = 0;

    /* keyframe fragment, duration from tfhd default * sample count */
    buf_t a = {{0}, 0};
    build_moof_body(&a, 0x00000000, 0);
    CHECK(mp4_moof_video(a.b, a.len, 1, 0, &tfdt, &fragdur) == 1);
    CHECK(tfdt == 90000);
    CHECK(fragdur == 2 * 3000);

    /* non-keyframe fragment, duration summed from per-sample entries */
    buf_t b = {{0}, 0};
    build_moof_body(&b, 0x00010000, 1);
    CHECK(mp4_moof_video(b.b, b.len, 1, 0, &tfdt, &fragdur) == 0);
    CHECK(fragdur == 3000 + 3300);

    /* wrong track id: no matching traf */
    CHECK(mp4_moof_video(a.b, a.len, 7, 0, &tfdt, &fragdur) == -1);

    return 0;
}

static int test_detect_codecs(void) {
    char codecs[64];

    unsigned char avc[32] = {0};
    memcpy(avc + 4, "avcC", 4);
    avc[9]  = 0x64; /* profile */
    avc[10] = 0x00; /* compat */
    avc[11] = 0x32; /* level */
    mp4_detect_codecs(avc, sizeof(avc), codecs, sizeof(codecs));
    CHECK(strcmp(codecs, "avc1.640032") == 0);

    unsigned char both[48] = {0};
    memcpy(both + 4, "avcC", 4);
    both[9]  = 0x4d;
    both[10] = 0x40;
    both[11] = 0x1f;
    memcpy(both + 20, "mp4a", 4);
    mp4_detect_codecs(both, sizeof(both), codecs, sizeof(codecs));
    CHECK(strcmp(codecs, "avc1.4d401f,mp4a.40.2") == 0);

    unsigned char none[16] = {0};
    mp4_detect_codecs(none, sizeof(none), codecs, sizeof(codecs));
    CHECK(codecs[0] == '\0');

    return 0;
}

static int test_resolve_recording(void) {
    FCGX_Request rq;
    memset(&rq, 0, sizeof(rq));
    char real[PATH_MAX];

    /* traversal and absolute paths are rejected before touching the fs */
    CHECK(resolve_recording(&rq, "/tmp", "../etc/passwd", real) == -1);
    CHECK(resolve_recording(&rq, "/tmp", "a/../../etc/passwd", real) == -1);
    CHECK(resolve_recording(&rq, "/tmp", "/etc/passwd", real) == -1);
    CHECK(resolve_recording(&rq, "/tmp", "", real) == -1);

    return 0;
}

int main(void) {
    if (test_url_helpers())      return 1;
    if (test_same_origin())      return 1;
    if (test_mp4_next())         return 1;
    if (test_parse_moov())       return 1;
    if (test_moof_video())       return 1;
    if (test_detect_codecs())    return 1;
    if (test_resolve_recording()) return 1;
    printf("all %d checks passed\n", tests_run);
    return 0;
}
