/*
 * http.h - minimal HTTPS download using WinHTTP (built into Windows).
 */
#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/*
 * GET https://<host><path> and save the body to dest. The body is written to
 * "<dest>.tmp" first and only moved over dest once complete, so a failed
 * download never clobbers a good file. Stores the byte count in *bytes.
 * Problems are reported on stderr.
 */
bool http_download(const wchar_t *host, const wchar_t *path,
                   const char *dest, size_t *bytes);

/* Same, but problems are not printed (for background checks); just returns false. */
bool http_download_quiet(const wchar_t *host, const wchar_t *path,
                         const char *dest, size_t *bytes);

#endif /* HTTP_H */
