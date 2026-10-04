/*
 * spawn.c - creating and running dummy game processes.
 */
#include "spawn.h"

#include "config.h"
#include "fs.h"
#include "recent.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool spawn_root(char *out, size_t cap)
{
    return fs_data_path(out, cap, SPAWN_DIR);
}

/* "1h 30m 5s"-style text for a number of seconds (zero parts are left out). */
void spawn_format_duration(unsigned long long seconds, char *out, size_t cap)
{
    unsigned long h = (unsigned long)(seconds / 3600);
    unsigned long m = (unsigned long)(seconds / 60 % 60);
    unsigned long s = (unsigned long)(seconds % 60);
    int n = 0;
    out[0] = '\0';
    if (h) {
        n += snprintf(out + n, cap - (size_t)n, "%luh ", h);
    }
    if (m) {
        n += snprintf(out + n, cap - (size_t)n, "%lum ", m);
    }
    if (s || n == 0) {
        n += snprintf(out + n, cap - (size_t)n, "%lus ", s);
    }
    out[n - 1] = '\0';  /* drop the trailing space */
}

/* Replace every '/' with '\' so the input can use either separator. */
static void normalize_slashes(char *s)
{
    for (; *s; s++) {
        if (*s == '/') {
            *s = '\\';
        }
    }
}

/* Append ".exe" (case-insensitive) unless already there. False if no room. */
static bool ensure_exe_ext(char *buf, size_t cap)
{
    size_t n = strlen(buf);
    if (n >= 4 && _stricmp(buf + n - 4, ".exe") == 0) {
        return true;
    }
    if (n + 4 >= cap) {
        return false;
    }
    memcpy(buf + n, ".exe", 5);
    return true;
}

/*
 * Check that a normalized, non-empty suffix is a legal relative Windows path
 * that stays inside SPAWN_DIR: no leading '\', no drive letter, no ".." and no
 * empty folder names. Returns NULL if fine, otherwise a short reason.
 */
static const char *validate_suffix(const char *s)
{
    if (s[0] == '\\') {
        return "path must be relative (no leading slash)";
    }
    if (s[strlen(s) - 1] == '\\') {
        return "path must end with a file name";
    }

    for (const char *p = s; *p; p++) {
        if ((unsigned char)*p < 32 || strchr("<>:\"|?*", *p)) {
            return "path contains a character Windows does not allow (<>:\"|?*)";
        }
    }

    const char *start = s;
    for (const char *p = s;; p++) {
        if (*p == '\\' || *p == '\0') {
            size_t len = (size_t)(p - start);
            if (len == 0) {
                return "path contains an empty folder name";
            }
            if (len == 2 && start[0] == '.' && start[1] == '.') {
                return "\"..\" is not allowed";
            }
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    return NULL;
}

bool spawn_parse_duration(const char *s, unsigned long *seconds)
{
    unsigned long total = 0;
    if (!*s) {
        return false;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        unsigned long n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (unsigned long)(*s++ - '0');
            if (n > 1000000UL) {
                return false;  /* absurdly long; also keeps the sums from overflowing */
            }
        }
        unsigned long unit;
        switch (*s++) {
        case 's': case 'S': unit = 1;    break;
        case 'm': case 'M': unit = 60;   break;
        case 'h': case 'H': unit = 3600; break;
        default:            return false;  /* missing or unknown unit */
        }
        total += n * unit;
    }
    if (total == 0) {
        return false;
    }
    *seconds = total;
    return true;
}

void spawn_split_duration(char *text, unsigned long *seconds)
{
    *seconds = 0;
    char *last = strrchr(text, ' ');
    unsigned long parsed;
    if (last && last > text && spawn_parse_duration(last + 1, &parsed)) {
        *seconds = parsed;
        while (last > text && last[-1] == ' ') {
            last--;
        }
        *last = '\0';
    }
}

void spawn_game(const char *suffix, const struct spawn_opts *opts)
{
    char rel[MAX_PATH];
    int written = snprintf(rel, sizeof(rel), "%s", suffix);
    if (written < 0 || (size_t)written >= sizeof(rel)) {
        fprintf(stderr, "error: path is too long.\n");
        return;
    }
    normalize_slashes(rel);

    const char *problem = validate_suffix(rel);
    if (problem) {
        fprintf(stderr, "error: %s.\n", problem);
        return;
    }

    char root[MAX_PATH];
    char target[MAX_PATH];
    if (!ensure_exe_ext(rel, sizeof(rel)) || !spawn_root(root, sizeof(root))) {
        fprintf(stderr, "error: path is too long.\n");
        return;
    }
    written = snprintf(target, sizeof(target), "%s\\%s", root, rel);
    if (written < 0 || (size_t)written >= sizeof(target)) {
        fprintf(stderr, "error: path is too long (Windows limit is %d characters).\n",
                MAX_PATH - 1);
        return;
    }

    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        fprintf(stderr, "error: cannot locate this program's exe.\n");
        return;
    }

    fs_make_parent_dirs(target);
    if (!CopyFileA(self, target, FALSE)) {
        DWORD err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION) {
            fprintf(stderr, "error: %s\\%s is already running.\n", SPAWN_DIR, rel);
        } else {
            fprintf(stderr, "error: could not create %s\\%s (Windows error %lu).\n",
                    SPAWN_DIR, rel, (unsigned long)err);
        }
        return;
    }

    unsigned long seconds = opts ? opts->seconds : 0;
    char cmdline[MAX_PATH + 64 + 512];
    int len = snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", target, CHILD_FLAG);
    if (seconds > 0) {
        snprintf(cmdline + len, sizeof(cmdline) - (size_t)len, " %s %lu",
                 CHILD_TIMER_FLAG, seconds);
        len = (int)strlen(cmdline);
    }
    if (opts && opts->args && opts->args[0]) {
        int room = (int)sizeof(cmdline) - len;
        int added = snprintf(cmdline + len, (size_t)room, " %s", opts->args);
        if (added < 0 || added >= room) {
            fprintf(stderr, "error: the arguments are too long.\n");
            return;
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "error: could not start %s\\%s (Windows error %lu).\n",
                SPAWN_DIR, rel, (unsigned long)GetLastError());
        return;
    }

    printf("Started %s\\%s (PID %lu), minimized in the taskbar.\n",
           SPAWN_DIR, rel, (unsigned long)pi.dwProcessId);
    if (seconds > 0) {
        char text[32];
        spawn_format_duration(seconds, text, sizeof(text));
        printf("It will close itself after %s.\n", text);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    recent_add(rel, opts ? opts->args : NULL);
}
