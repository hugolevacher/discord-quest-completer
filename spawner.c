/*
 * spawner.c
 *
 * Testing helper: create a running process at a path you choose, so that
 * detection software can be checked against its "known exe" list.
 *
 * Discord (and similar) match a process by a PATH SUFFIX, not just the file
 * name: e.g. for World of Warcraft the registered executable is
 * "_retail_/wow.exe", meaning the running exe must live in a folder
 * named "_retail_" and be called "wow.exe". A flat renamed exe will
 * not match. So this tool accepts a full suffix, creates the needed folder(s),
 * places the exe inside, and runs it from there.
 *
 * How it works:
 *   - Run with no arguments  -> interactive mode: asks for a path suffix such
 *     as "_retail_/wow.exe", copies this exe there (creating folders),
 *     and launches that copy in a new console window.
 *   - The launched copy runs with the argument "--child", which just keeps the
 *     process alive (printing a heartbeat) until you close its window / Ctrl+C.
 *
 * Everything is created under a single folder (SPAWN_DIR, below), so you can
 * remove every spawned exe at once by deleting that one folder. The child's
 * full module path still ends with the suffix you entered, which is what the
 * detector matches against.
 *
 * Build (MinGW):   gcc -O2 -o spawner.exe spawner.c
 * Build (MSVC):    cl spawner.c
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/*
 * All spawned exe's and their folder structures are created under this single
 * folder (auto-created if missing), so cleanup is just "delete this folder".
 */
#define SPAWN_DIR "spawned_games"

/* Child mode: just stay alive until the user closes the window. */
static int run_as_child(void)
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    /* Strip directory, keep just the file name for display. */
    const char *name = strrchr(self, '\\');
    name = name ? name + 1 : self;

    printf("Running as process: %s\n", name);
    printf("Full path: %s\n", self);
    printf("PID: %lu\n", (unsigned long)GetCurrentProcessId());
    printf("This process stays alive until you close this window (or Ctrl+C).\n\n");

    unsigned long seconds = 0;
    for (;;) {
        printf("[%s] alive - %lu s\r", name, seconds);
        fflush(stdout);
        Sleep(1000);
        seconds++;
    }
    return 0; /* never reached */
}

/* Replace every '/' with '\\' so the input can use either separator. */
static void normalize_slashes(char *s)
{
    for (; *s; s++) {
        if (*s == '/') *s = '\\';
    }
}

/* Append ".exe" (case-insensitive) if it is not already there. */
static void ensure_exe_ext(char *buf, size_t cap)
{
    size_t n = strlen(buf);
    if (n >= 4 && _stricmp(buf + n - 4, ".exe") == 0) return;
    strncat(buf, ".exe", cap - n - 1);
}

/*
 * Create every parent directory of "path" (a "mkdir -p" for the folder part).
 * The filename after the last '\\' is ignored; drive roots like "C:" are
 * skipped. Existing directories are fine (CreateDirectory just fails silently).
 */
static void ensure_parent_dirs(const char *path)
{
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *last = strrchr(tmp, '\\');
    if (!last) return;      /* no folder part -> nothing to create */
    *last = '\0';           /* tmp is now just the directory portion */

    for (char *p = tmp; *p; p++) {
        if (*p == '\\' && p != tmp) {
            *p = '\0';
            if (!(strlen(tmp) == 2 && tmp[1] == ':')) {
                CreateDirectoryA(tmp, NULL);
            }
            *p = '\\';
        }
    }
    if (!(strlen(tmp) == 2 && tmp[1] == ':')) {
        CreateDirectoryA(tmp, NULL);
    }
}

/*
 * Parent mode: ask for a path suffix (e.g. "_retail_/wow.exe"),
 * create the folders, clone self into it, and launch it from there.
 */
static int run_interactive(void)
{
    char name[512];

    printf("Enter the executable path the detector expects.\n");
    printf("Examples:  _retail_/wow.exe   or just   RobloxPlayerBeta\n");
    printf("(you can use / or \\, and the .exe is optional)\n> ");
    if (!fgets(name, sizeof(name), stdin)) {
        fprintf(stderr, "No input.\n");
        return 1;
    }

    /* Trim trailing newline / whitespace. */
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == '\n' || name[len - 1] == '\r' ||
                       name[len - 1] == ' ' || name[len - 1] == '\t')) {
        name[--len] = '\0';
    }
    if (len == 0) {
        fprintf(stderr, "Name cannot be empty.\n");
        return 1;
    }

    /*
     * Normalize into a Windows relative path with a .exe extension, placed
     * under SPAWN_DIR. Any sub-folders in the user's input are preserved, so
     * "a/b/game.exe" becomes "SPAWN_DIR\a\b\game.exe" and all of a, b are made.
     */
    normalize_slashes(name);
    char target[512];
    snprintf(target, sizeof(target), "%s\\%s", SPAWN_DIR, name);
    ensure_exe_ext(target, sizeof(target));

    /* Path to this running executable. */
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    /* Create the folder(s) the suffix requires, then copy self into place. */
    ensure_parent_dirs(target);
    if (!CopyFileA(self, target, FALSE)) {
        fprintf(stderr, "Failed to create %s (error %lu).\n",
                target, (unsigned long)GetLastError());
        return 1;
    }
    printf("Created %s\n", target);

    /* Launch the copy in child mode, in its own new console window. */
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" --child", target);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to start %s (error %lu).\n",
                target, (unsigned long)GetLastError());
        return 1;
    }

    printf("Started %s (PID %lu) in a new window.\n",
           target, (unsigned long)pi.dwProcessId);
    printf("Close that window to stop it.\n");
    printf("To clean up every spawned exe, delete the \"%s\" folder.\n", SPAWN_DIR);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--child") == 0) {
        return run_as_child();
    }
    return run_interactive();
}
