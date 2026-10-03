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
 * the folders) and runs the copy from there with CHILD_FLAG, which just keeps
 * it alive until its window is closed.
 *
 * Layout:
 *   main.c      prompt loop and child-mode switch
 *   commands.c  built-in commands (help, delete, clear, quit)
 *   spawn.c     building the target path and launching dummy games
 *   fs.c        filesystem helpers (mkdir -p, recursive delete)
 *   config.h    constants
 */
#include "commands.h"
#include "config.h"
#include "spawn.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Trim leading and trailing whitespace in place; return the trimmed start. */
static char *trim(char *s)
{
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

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], CHILD_FLAG) == 0) {
        return spawn_child_main();
    }

    printf("%s. Type 'h' for help, 'q' to quit.\n", APP_NAME);

    char line[INPUT_MAX];
    for (;;) {
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
            spawn_game(in);
        } else if (result == CMD_EXIT) {
            break;
        }
    }

    return 0;
}
