/*
 * update.c - checking GitHub for a newer release of this tool.
 *
 * Asks GitHub's API for the latest release and compares its tag ("v1.2.3")
 * with APP_VERSION. The check runs on a thread so startup never waits on the
 * network. Nothing here prints; the interface shows the result.
 */
#include "update.h"

#include "config.h"
#include "fs.h"
#include "http.h"

#include "cJSON.h"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE g_thread;  /* the running background check, or NULL */
static struct update_info g_result;
static bool g_have_result;

/* Read "v1.2.3" (or "1.2") into three numbers; missing parts are 0. */
static void parse_version(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
}

/* True if version a is newer than version b. */
static bool is_newer(const char *a, const char *b)
{
    int x[3], y[3];
    parse_version(a, x);
    parse_version(b, y);
    for (int i = 0; i < 3; i++) {
        if (x[i] != y[i]) {
            return x[i] > y[i];
        }
    }
    return false;
}

/* Ask GitHub for the latest release. Quiet: any problem is UPDATE_FAILED. */
static struct update_info fetch_latest(void)
{
    struct update_info r = { UPDATE_FAILED, "", "" };

    char file[MAX_PATH];
    if (!fs_data_path(file, sizeof(file), UPDATE_CHECK_FILE)) {
        return r;
    }

    size_t bytes;
    if (!http_download_quiet(UPDATE_HOST, UPDATE_PATH, file, &bytes)) {
        return r;
    }
    char *text = fs_read_file(file, NULL);
    DeleteFileA(file);
    if (!text) {
        return r;
    }

    cJSON *json = cJSON_Parse(text);
    free(text);
    const cJSON *tag = cJSON_GetObjectItemCaseSensitive(json, "tag_name");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "html_url");
    if (cJSON_IsString(tag)) {
        snprintf(r.tag, sizeof(r.tag), "%s", tag->valuestring);
        snprintf(r.url, sizeof(r.url), "%s", cJSON_IsString(url) ? url->valuestring : "");
        r.status = is_newer(r.tag, APP_VERSION) ? UPDATE_NEWER : UPDATE_UP_TO_DATE;
    }
    cJSON_Delete(json);
    return r;
}

static DWORD WINAPI check_thread(LPVOID unused)
{
    (void)unused;
    g_result = fetch_latest();
    return 0;
}

void update_check_start(void)
{
    g_thread = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
}

/* Collect the background check's result if it has finished (or wait for it). */
static void collect(bool wait)
{
    if (g_thread && WaitForSingleObject(g_thread, wait ? INFINITE : 0) == WAIT_OBJECT_0) {
        CloseHandle(g_thread);
        g_thread = NULL;
        g_have_result = true;
    }
}

bool update_poll(struct update_info *info)
{
    collect(false);
    if (g_have_result) {
        *info = g_result;
    }
    return g_have_result;
}

void update_check_now(struct update_info *info)
{
    collect(true);
    if (!g_have_result || g_result.status == UPDATE_FAILED) {
        g_result = fetch_latest();  /* never started, or failed: try once more */
        g_have_result = true;
    }
    *info = g_result;
}
