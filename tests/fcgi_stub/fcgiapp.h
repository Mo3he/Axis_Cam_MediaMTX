/*
 * Minimal stand-in for <fcgiapp.h> used by the host-side unit tests
 * (tests/test_mp4.c), so they build and run without libfcgi installed.
 * Only the declarations config.c actually uses are provided; the I/O
 * functions are no-ops except FCGX_GetParam, which the tests rely on to
 * exercise same_origin() against a fabricated environment.
 *
 * The real FastCGI library is used for the device build (see app/Makefile);
 * this header is never shipped.
 */
#ifndef TEST_FCGIAPP_STUB_H
#define TEST_FCGIAPP_STUB_H

#include <string.h>

typedef struct FCGX_Stream FCGX_Stream;
typedef char** FCGX_ParamArray;

typedef struct {
    FCGX_Stream*    in;
    FCGX_Stream*    out;
    FCGX_Stream*    err;
    FCGX_ParamArray envp;
} FCGX_Request;

static inline int FCGX_FPrintF(FCGX_Stream* s, const char* fmt, ...) {
    (void)s;
    (void)fmt;
    return 0;
}

static inline int FCGX_PutStr(const char* str, int n, FCGX_Stream* s) {
    (void)str;
    (void)s;
    return n;
}

static inline int FCGX_PutChar(int c, FCGX_Stream* s) {
    (void)s;
    return c;
}

static inline int FCGX_GetStr(char* str, int n, FCGX_Stream* s) {
    (void)str;
    (void)n;
    (void)s;
    return 0;
}

static inline char* FCGX_GetParam(const char* name, FCGX_ParamArray envp) {
    size_t n = strlen(name);
    for (int i = 0; envp && envp[i]; i++)
        if (strncmp(envp[i], name, n) == 0 && envp[i][n] == '=')
            return envp[i] + n + 1;
    return NULL;
}

static inline int FCGX_Init(void) {
    return 0;
}

static inline int FCGX_OpenSocket(const char* path, int backlog) {
    (void)path;
    (void)backlog;
    return -1;
}

static inline int FCGX_InitRequest(FCGX_Request* r, int sock, int flags) {
    (void)r;
    (void)sock;
    (void)flags;
    return -1;
}

static inline int FCGX_Accept_r(FCGX_Request* r) {
    (void)r;
    return -1;
}

static inline void FCGX_Finish_r(FCGX_Request* r) {
    (void)r;
}

#endif /* TEST_FCGIAPP_STUB_H */
