/*
 * main.c - entry point and interactive prompt.
 *
 * Creates dummy processes at a path you choose, so that a process-name based
 * detector (such as Discord's game detection) sees that "game" as running.
 *
 * Detectors match a process by a PATH SUFFIX, not just the file name: e.g. for
 * World of Warcraft the registered executable is "_retail_/wow.exe", so the
 * running exe must live in a folder named "_retail_" and be called "wow.exe".
 * This tool takes such a suffix, copies itself to SPAWN_DIR\<suffix> (creating
 * the folders) and runs the copy from there with CHILD_FLAG, which opens a game
 * window and keeps it alive until that window is closed.
 *
 * Layout:
 *   main.c      prompt loop, command-line mode and child-mode switch
 *   commands.c  the prompt's commands (help, spawn, find, list, stop, ...)
 *   spawn.c     building the target path and launching dummy games
 *   child.c     the game window a spawned copy shows
 *   procs.c     finding and stopping running spawned games
 *   recent.c    remembering spawned games for the recent command
 *   games.c     Discord's detectable-games list: download, cache, search
 *   update.c    checking GitHub for a newer release
 *   menu.c      arrow-key selection menu
 *   http.c      HTTPS download (WinHTTP)
 *   fs.c        filesystem helpers (data folder, mkdir -p, recursive delete)
 *   config.h    constants
 *
 * Third-party: cJSON (third_party/cjson, MIT licence) parses the games list.
 */
#include "child.h"
#include "commands.h"
#include "config.h"
#include "games.h"
#include "gui.h"
#include "update.h"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
 * Trim leading and trailing whitespace in place; return the trimmed start.
 * A leading UTF-8 byte-order mark is dropped too: PowerShell and some editors
 * put one at the start of piped input, which would otherwise make the first
 * command unrecognised.
 */
static char *trim(char *s)
{
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) {
        s += 3;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
    return s;
}

/*
 * Read one line of input into buf. Returns false on EOF. A line too long for
 * buf is discarded entirely (instead of leaking into the next read) and buf is
 * left empty.
 */
static bool read_line(char *buf, size_t cap)
{
    if (!fgets(buf, (int)cap, stdin)) {
        return false;
    }
    if (!strchr(buf, '\n') && !feof(stdin)) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
        }
        fprintf(stderr, "error: input too long (max %u characters).\n",
                (unsigned)(cap - 2));
        buf[0] = '\0';
    }
    return true;
}

/*
 * Run the command line as a single prompt line, then exit:
 *   spawner.exe find minecraft 15m
 *   spawner.exe spawn _retail_/wow.exe 15m
 * Only commands are accepted, as at the prompt.
 */
static int run_once(int argc, char **argv)
{
    if (argv[1][0] == '-') {
        printf("usage: spawner.exe <command> [arguments]\n"
               "       spawner.exe " GUI_FLAG "   open the window\n"
               "       spawner.exe " CLI_FLAG "   open the prompt\n"
               "Run from a terminal without arguments for the prompt; 'h' lists the "
               "commands.\n");
        return 1;
    }

    char line[INPUT_MAX];
    size_t len = 0;
    for (int i = 1; i < argc; i++) {
        int written = snprintf(line + len, sizeof(line) - len, "%s%s", i > 1 ? " " : "",
                               argv[i]);
        if (written < 0 || (size_t)written >= sizeof(line) - len) {
            fprintf(stderr, "error: input too long (max %u characters).\n",
                    (unsigned)(sizeof(line) - 2));
            return 1;
        }
        len += (size_t)written;
    }

    cmd_result result;
    if (!command_run(line, &result)) {
        fprintf(stderr, "Unknown command. Run 'spawner.exe h' for help.\n");
        return 1;
    }
    return 0;
}

/*
 * True when the exe was double-clicked rather than run from a terminal: Windows
 * then gives it a console of its own (no other process attached to it), and
 * its input is that console rather than a pipe (e.g. an IDE's run window).
 */
static bool started_by_double_click(void)
{
    DWORD pids[2];
    DWORD mode;
    return GetConsoleProcessList(pids, 2) == 1 &&
           GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode);
}

/* Drop the console Windows opened for us, so only the window shows. */
static int run_gui(void)
{
    HWND console = GetConsoleWindow();
    if (console) {
        ShowWindow(console, SW_HIDE);
    }
    FreeConsole();
    return gui_main();
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], CHILD_FLAG) == 0) {
        return child_main(argc, argv);
    }

    /*
     * Double-clicked: the window. From a terminal: the prompt, or the command
     * given on the command line. --gui and --cli force either one.
     */
    bool force_cli = argc == 2 && strcmp(argv[1], CLI_FLAG) == 0;
    if ((argc == 2 && strcmp(argv[1], GUI_FLAG) == 0) ||
        (argc == 1 && started_by_double_click())) {
        return run_gui();
    }

    /* Game names in Discord's list are UTF-8 (e.g. "Pokémon"). */
    SetConsoleOutputCP(CP_UTF8);

    if (argc > 1 && !force_cli) {
        int status = run_once(argc, argv);
        games_free();
        return status;
    }

    printf("%s v%s. Type 'h' for help, 'q' to quit.\n", APP_NAME, APP_VERSION);
    update_check_start();

    char line[INPUT_MAX];
    for (;;) {
        command_update_notice();
        printf("\n> ");
        fflush(stdout);

        if (!read_line(line, sizeof(line))) {
            break;  /* EOF (Ctrl+Z, closed pipe) -> exit cleanly */
        }

        char *in = trim(line);
        if (*in == '\0') {
            continue;
        }

        cmd_result result;
        if (!command_run(in, &result)) {
            printf("Unknown command. Type 'h' for help.\n");
        } else if (result == CMD_EXIT) {
            break;
        }
    }

    games_free();
    return 0;
}
