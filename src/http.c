/*
 * http.c - minimal HTTPS download using WinHTTP (built into Windows).
 */
#include "http.h"

#include "config.h"
#include "msg.h"

#include <windows.h>
#include <winhttp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_CHUNK (64 * 1024)  /* bytes read per call */

/* Report an error through msg unless quiet. */
static void report(bool quiet, const char *fmt, ...)
{
    if (quiet) {
        return;
    }
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    msg_error("%s", text);
}

static bool download(const wchar_t *host, const wchar_t *path,
                     const char *dest, size_t *bytes, bool quiet)
{
    bool ok = false;
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;
    FILE *out = NULL;
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    char *buf = NULL;  /* per call, so two downloads can run at once */
    char tmp[MAX_PATH];

    *bytes = 0;

    int written = snprintf(tmp, sizeof(tmp), "%s.tmp", dest);
    if (written < 0 || (size_t)written >= sizeof(tmp)) {
        report(quiet, "Download path is too long.");
        return false;
    }
    buf = malloc(HTTP_CHUNK);
    if (!buf) {
        report(quiet, "Out of memory.");
        return false;
    }

    session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        goto winhttp_error;
    }

#ifdef WINHTTP_OPTION_DECOMPRESSION
    /* Let the server send gzip; WinHTTP inflates it transparently. */
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION,
                     &decompress, sizeof(decompress));
#endif

    connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        goto winhttp_error;
    }

    request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        goto winhttp_error;
    }

    if (status != 200) {
        report(quiet, "Download failed (HTTP %lu).", (unsigned long)status);
        goto cleanup;
    }

    out = fopen(tmp, "wb");
    if (!out) {
        report(quiet, "Cannot write %s.", tmp);
        goto cleanup;
    }

    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buf, (DWORD)HTTP_CHUNK, &got)) {
            goto winhttp_error;
        }
        if (got == 0) {
            break;  /* end of body */
        }
        if (fwrite(buf, 1, got, out) != got) {
            report(quiet, "Cannot write %s.", tmp);
            goto cleanup;
        }
        *bytes += got;
    }

    int close_failed = fclose(out);
    out = NULL;
    if (close_failed) {
        report(quiet, "Cannot write %s.", tmp);
        goto cleanup;
    }

    if (!MoveFileExA(tmp, dest, MOVEFILE_REPLACE_EXISTING)) {
        report(quiet, "Cannot replace %s (Windows error %lu).",
                dest, (unsigned long)GetLastError());
        goto cleanup;
    }

    ok = true;
    goto cleanup;

winhttp_error:
    report(quiet, "Download failed (Windows error %lu) - "
                    "check your internet connection.",
            (unsigned long)GetLastError());

cleanup:
    free(buf);
    if (out) {
        fclose(out);
    }
    if (!ok) {
        DeleteFileA(tmp);
    }
    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connect) {
        WinHttpCloseHandle(connect);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    return ok;
}

bool http_download(const wchar_t *host, const wchar_t *path,
                   const char *dest, size_t *bytes)
{
    return download(host, path, dest, bytes, false);
}

bool http_download_quiet(const wchar_t *host, const wchar_t *path,
                         const char *dest, size_t *bytes)
{
    return download(host, path, dest, bytes, true);
}
