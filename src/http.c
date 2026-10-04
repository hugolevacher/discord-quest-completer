/*
 * http.c - minimal HTTPS download using WinHTTP (built into Windows).
 */
#include "http.h"

#include "config.h"

#include <windows.h>
#include <winhttp.h>
#include <stdarg.h>
#include <stdio.h>

/* Print an error on stderr unless quiet. */
static void report(bool quiet, const char *fmt, ...)
{
    if (quiet) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
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
    static char buf[64 * 1024];
    char tmp[MAX_PATH];

    *bytes = 0;

    int written = snprintf(tmp, sizeof(tmp), "%s.tmp", dest);
    if (written < 0 || (size_t)written >= sizeof(tmp)) {
        report(quiet, "error: download path is too long.\n");
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
        report(quiet, "error: download failed (HTTP %lu).\n", (unsigned long)status);
        goto cleanup;
    }

    out = fopen(tmp, "wb");
    if (!out) {
        report(quiet, "error: cannot write %s.\n", tmp);
        goto cleanup;
    }

    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buf, (DWORD)sizeof(buf), &got)) {
            goto winhttp_error;
        }
        if (got == 0) {
            break;  /* end of body */
        }
        if (fwrite(buf, 1, got, out) != got) {
            report(quiet, "error: cannot write %s.\n", tmp);
            goto cleanup;
        }
        *bytes += got;
    }

    int close_failed = fclose(out);
    out = NULL;
    if (close_failed) {
        report(quiet, "error: cannot write %s.\n", tmp);
        goto cleanup;
    }

    if (!MoveFileExA(tmp, dest, MOVEFILE_REPLACE_EXISTING)) {
        report(quiet, "error: cannot replace %s (Windows error %lu).\n",
                dest, (unsigned long)GetLastError());
        goto cleanup;
    }

    ok = true;
    goto cleanup;

winhttp_error:
    report(quiet, "error: download failed (Windows error %lu) - "
                    "check your internet connection.\n",
            (unsigned long)GetLastError());

cleanup:
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
