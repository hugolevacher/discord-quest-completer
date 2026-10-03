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
 *   - Run with no arguments  -> interactive command loop. The window stays open
 *     so you can spawn several games in a row. At the prompt:
 *       * type a path suffix (e.g. "_retail_/wow.exe") to spawn a game;
 *       * type "h" for help, "d" to delete all spawned games, "q" to quit.
 *     Spawning copies this exe to the target path (creating folders) and
 *     launches the copy in a new console window.
 *   - The launched copy runs with the argument "--child", which just keeps the
 *     process alive (printing a heartbeat) until you close its window / Ctrl+C.
 *
 * Everything is created under a single folder (SPAWN_DIR, below), so the "d"
 * command can remove every spawned exe at once by deleting that one folder.
 * The child's full module path still ends with the suffix you entered, which is
 * what the detector matches against.
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

/* Trim leading and trailing whitespace in place; return the trimmed start. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
    return s;
}

/*
 * Spawn one game: take a path suffix (e.g. "_retail_/wow.exe"), place a copy
 * of this exe under SPAWN_DIR (creating any sub-folders), and launch it in a
 * new console window. Errors are reported but never abort the command loop.
 */
static void spawn_game(const char *input)
{
    char name[512];
    strncpy(name, input, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    /*
     * Normalize into a Windows relative path with a .exe extension, placed
     * under SPAWN_DIR. Any sub-folders in the input are preserved, so
     * "a/b/game.exe" becomes "SPAWN_DIR\a\b\game.exe" and all of a, b are made.
     */
    normalize_slashes(name);
    char target[512];
    snprintf(target, sizeof(target), "%s\\%s", SPAWN_DIR, name);
    ensure_exe_ext(target, sizeof(target));

    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    ensure_parent_dirs(target);
    if (!CopyFileA(self, target, FALSE)) {
        DWORD err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION) {
            fprintf(stderr, "'%s' already exists and is running.\n", target);
        } else {
            fprintf(stderr, "Failed to create %s (error %lu).\n",
                    target, (unsigned long)err);
        }
        return;
    }

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
        return;
    }

    printf("Started %s (PID %lu) in a new window.\n",
           target, (unsigned long)pi.dwProcessId);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

/*
 * Recursively delete everything inside "path", then the folder itself.
 * A file that can't be deleted (e.g. a spawned exe still running) is counted
 * in *failed and skipped; every other file is still removed. Folders that end
 * up empty are removed; folders still holding a locked file simply remain.
 */
static void delete_tree(const char *path, int *deleted, int *failed)
{
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            char child[512];
            snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree(child, deleted, failed);
            } else {
                SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileA(child)) {
                    (*deleted)++;
                } else {
                    (*failed)++;
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* Removes the folder only if it is now empty; failure is fine. */
    RemoveDirectoryA(path);
}

/* The "d" command: wipe the SPAWN_DIR folder, tolerating locked files. */
static void cmd_delete(void)
{
    if (GetFileAttributesA(SPAWN_DIR) == INVALID_FILE_ATTRIBUTES) {
        printf("Nothing to delete - \"%s\" does not exist.\n", SPAWN_DIR);
        return;
    }

    int deleted = 0, failed = 0;
    delete_tree(SPAWN_DIR, &deleted, &failed);

    printf("Deleted %d file(s).\n", deleted);
    if (failed > 0) {
        printf("%d file(s) are still in use and were skipped - close those "
               "windows and run 'd' again.\n", failed);
    }
}

/* The "c" command: clear the console screen (no subprocess). */
static void cmd_clear(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return;

    DWORD cells = (DWORD)csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    COORD home = { 0, 0 };
    FillConsoleOutputCharacterA(h, ' ', cells, home, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(h, home);
}

/*
 * Reserved commands, in one table so the help listing and the dispatcher stay
 * in sync. Anything the user types that is not one of these is treated as a
 * game path to spawn. To add a command: add a row here and a branch in
 * dispatch().
 */
struct command {
    const char *key;   /* short form, e.g. "h" */
    const char *name;  /* long form, e.g. "help" */
    const char *desc;  /* shown by the help command */
};

static const struct command COMMANDS[] = {
    { "h", "help",   "Show this help" },
    { "d", "delete", "Delete the \"" SPAWN_DIR "\" folder (skips exe's still running)" },
    { "c", "clear",  "Clear the screen" },
    { "q", "quit",   "Exit (spawned game windows keep running)" },
};
static const int NUM_COMMANDS = (int)(sizeof(COMMANDS) / sizeof(COMMANDS[0]));

/* The "h" command: list every command plus how to spawn. */
static void cmd_help(void)
{
    printf("\nCommands:\n");
    for (int i = 0; i < NUM_COMMANDS; i++) {
        printf("  %-2s / %-7s %s\n",
               COMMANDS[i].key, COMMANDS[i].name, COMMANDS[i].desc);
    }
    printf("\nAnything else is treated as a game path to spawn, for example:\n");
    printf("  _retail_/wow.exe        (folder \"_retail_\", exe \"wow.exe\")\n");
    printf("  RobloxPlayerBeta        (just a name, .exe optional)\n");
    printf("You can use / or \\, and the .exe is optional.\n");
}

/* True if the typed word matches a command's short or long form. */
static int matches(const char *in, const struct command *c)
{
    return _stricmp(in, c->key) == 0 || _stricmp(in, c->name) == 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--child") == 0) {
        return run_as_child();
    }

    printf("Game process spawner. Type 'h' for help, 'q' to quit.\n");

    char line[512];
    for (;;) {
        printf("\n> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;  /* EOF (Ctrl+Z, closed pipe) -> exit cleanly */
        }

        char *in = trim(line);
        if (*in == '\0') {
            continue;  /* blank line */
        }

        if (matches(in, &COMMANDS[0])) {        /* help */
            cmd_help();
        } else if (matches(in, &COMMANDS[1])) { /* delete */
            cmd_delete();
        } else if (matches(in, &COMMANDS[2])) { /* clear */
            cmd_clear();
        } else if (matches(in, &COMMANDS[3])) { /* quit */
            break;
        } else {
            spawn_game(in);
        }
    }

    return 0;
}
