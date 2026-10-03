/*
 * spawn.c - creating and running dummy game processes.
 */
#include "spawn.h"

#include "config.h"
#include "fs.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

bool spawn_root(char *out, size_t cap)
{
    char dir[MAX_PATH];
    if (!fs_exe_dir(dir, sizeof(dir))) {
        return false;
    }
    int written = snprintf(out, cap, "%s\\%s", dir, SPAWN_DIR);
    return written >= 0 && (size_t)written < cap;
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

void spawn_game(const char *suffix)
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

    char cmdline[MAX_PATH + 32];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", target, CHILD_FLAG);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "error: could not start %s\\%s (Windows error %lu).\n",
                SPAWN_DIR, rel, (unsigned long)GetLastError());
        return;
    }

    printf("Started %s\\%s (PID %lu) in a new window.\n",
           SPAWN_DIR, rel, (unsigned long)pi.dwProcessId);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int spawn_child_main(void)
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    const char *name = strrchr(self, '\\');
    name = name ? name + 1 : self;

    /* Title the window after the game so multiple spawns are easy to tell apart. */
    SetConsoleTitleA(name);

    printf("Running as process: %s\n", name);
    printf("Full path: %s\n", self);
    printf("PID: %lu\n", (unsigned long)GetCurrentProcessId());
    printf("This process stays alive until you close this window (or Ctrl+C).\n\n");

    for (unsigned long seconds = 0;; seconds++) {
        printf("[%s] alive - %lu s\r", name, seconds);
        fflush(stdout);
        Sleep(1000);
    }
}
