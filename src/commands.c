/*
 * commands.c - built-in commands of the interactive prompt.
 *
 * To add a command: write a handler below and add one row to COMMANDS.
 * Dispatch and the help listing both read the table, so nothing else changes.
 */
#include "commands.h"

#include "config.h"
#include "fs.h"
#include "spawn.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static cmd_result cmd_help(void);
static cmd_result cmd_delete(void);
static cmd_result cmd_clear(void);
static cmd_result cmd_quit(void);

struct command {
    const char *key;          /* short form, e.g. "h" */
    const char *name;         /* long form, e.g. "help" */
    const char *desc;         /* shown by the help command */
    cmd_result (*run)(void);  /* handler */
};

static const struct command COMMANDS[] = {
    { "h", "help",   "Show this help",                                        cmd_help   },
    { "d", "delete", "Delete the \"" SPAWN_DIR "\" folder (skips running games)", cmd_delete },
    { "c", "clear",  "Clear the screen",                                      cmd_clear  },
    { "q", "quit",   "Exit (spawned game windows keep running)",              cmd_quit   },
};

#define NUM_COMMANDS (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

bool command_run(const char *input, cmd_result *result)
{
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        const struct command *c = &COMMANDS[i];
        if (_stricmp(input, c->key) == 0 || _stricmp(input, c->name) == 0) {
            *result = c->run();
            return true;
        }
    }
    return false;
}

/* List every command plus how to spawn a game. */
static cmd_result cmd_help(void)
{
    printf("\nCommands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        printf("  %-2s / %-7s %s\n",
               COMMANDS[i].key, COMMANDS[i].name, COMMANDS[i].desc);
    }
    printf("\nAnything else is treated as a game path to spawn, for example:\n");
    printf("  _retail_/wow.exe        (folder \"_retail_\", exe \"wow.exe\")\n");
    printf("  RobloxPlayerBeta        (just a name, .exe optional)\n");
    printf("You can use / or \\, and the .exe is optional.\n");
    return CMD_CONTINUE;
}

/* Wipe the SPAWN_DIR folder, skipping (and reporting) games still running. */
static cmd_result cmd_delete(void)
{
    char root[MAX_PATH];
    if (!spawn_root(root, sizeof(root))) {
        fprintf(stderr, "error: cannot locate the \"%s\" folder.\n", SPAWN_DIR);
        return CMD_CONTINUE;
    }
    if (!fs_exists(root)) {
        printf("Nothing to delete - \"%s\" does not exist.\n", SPAWN_DIR);
        return CMD_CONTINUE;
    }

    int deleted = 0;
    int failed = 0;
    fs_delete_tree(root, &deleted, &failed);

    printf("Deleted %d file(s).\n", deleted);
    if (failed > 0) {
        printf("%d file(s) are still in use and were skipped - close those "
               "windows and run 'd' again.\n", failed);
    }
    return CMD_CONTINUE;
}

/* Clear the console screen through the Console API (no "cls" subprocess). */
static cmd_result cmd_clear(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) {
        return CMD_CONTINUE;  /* not a console (e.g. redirected output) */
    }

    DWORD cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
    DWORD written;
    COORD home = { 0, 0 };
    FillConsoleOutputCharacterA(h, ' ', cells, home, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(h, home);
    return CMD_CONTINUE;
}

static cmd_result cmd_quit(void)
{
    return CMD_EXIT;
}
