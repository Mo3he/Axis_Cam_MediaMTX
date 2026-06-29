/*
 * MediaMTX ACAP configuration backend (FastCGI).
 *
 * Exposed by the device web server at /local/MediaMTX/config.cgi (admin only).
 *
 *   GET  config.cgi                 -> return current mediamtx.yml (text/plain)
 *   GET  config.cgi?action=defaults -> return bundled mediamtx.defaults.yml
 *   POST config.cgi                 -> overwrite mediamtx.yml with request body
 *   POST config.cgi?action=restart  -> restart the MediaMTX process
 */

#include <errno.h>
#include <fcgiapp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define APPDIR       "/usr/local/packages/MediaMTX"
#define CONF_FILE    APPDIR "/mediamtx.yml"
#define DEFAULT_FILE APPDIR "/mediamtx.defaults.yml"
#define PID_FILE     APPDIR "/mediamtx.pid"
#define TMP_FILE     APPDIR "/mediamtx.yml.tmp"

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
            if (strstr(query, "action=defaults"))
                send_text_file(&request, DEFAULT_FILE);
            else
                send_text_file(&request, CONF_FILE);
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
