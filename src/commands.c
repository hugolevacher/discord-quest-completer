/*
 * commands.c - built-in commands of the interactive prompt.
 *
 * To add a command: write a handler below and add one row to COMMANDS.
 * Dispatch and the help listing both read the table, so nothing else changes.
 */
#include "commands.h"

#include "config.h"
#include "fs.h"
#include "games.h"
#include "menu.h"
#include "spawn.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cmd_result cmd_help(const char *args);
static cmd_result cmd_spawn(const char *args);
static cmd_result cmd_find(const char *args);
static cmd_result cmd_refresh(const char *args);
static cmd_result cmd_delete(const char *args);
static cmd_result cmd_clear(const char *args);
static cmd_result cmd_quit(const char *args);

struct command {
    const char *key;   /* short form, e.g. "h" */
    const char *name;  /* long form, e.g. "help" */
    const char *args;  /* argument placeholder shown in help, NULL if none */
    const char *desc;  /* shown by the help command */
    cmd_result (*run)(const char *args);
};

static const struct command COMMANDS[] = {
    { "h", "help",    NULL,     "Show this help",                                  cmd_help    },
    { "s", "spawn",   "<path>", "Spawn a game by exe path, e.g. spawn _retail_/wow.exe",
                                                                                   cmd_spawn   },
    { "f", "find",    "<name>", "Look up a game in Discord's list, then pick one to spawn",
                                                                                   cmd_find    },
    { "r", "refresh", NULL,     "Re-download Discord's game list",                 cmd_refresh },
    { "d", "delete",  NULL,     "Delete the \"" SPAWN_DIR "\" folder (skips running games)",
                                                                                   cmd_delete  },
    { "c", "clear",   NULL,     "Clear the screen",                                cmd_clear   },
    { "q", "quit",    NULL,     "Exit (spawned game windows keep running)",        cmd_quit    },
};

#define NUM_COMMANDS (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

/* True if the len-char word equals s, ignoring case. */
static bool word_is(const char *word, size_t len, const char *s)
{
    return strlen(s) == len && _strnicmp(word, s, len) == 0;
}

bool command_run(const char *input, cmd_result *result)
{
    /* The first word picks the command; the rest of the line is its argument. */
    size_t word_len = strcspn(input, " \t");
    const char *rest = input + word_len;
    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }

    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        const struct command *c = &COMMANDS[i];
        if (!word_is(input, word_len, c->key) && !word_is(input, word_len, c->name)) {
            continue;
        }

        if (!c->args && *rest) {
            printf("'%s' takes no arguments.\n", c->name);
            *result = CMD_CONTINUE;
            return true;
        }
        if (c->args && !*rest) {
            printf("usage: %s %s\n", c->name, c->args);
            *result = CMD_CONTINUE;
            return true;
        }

        *result = c->run(rest);
        return true;
    }
    return false;
}

/* List every command plus examples of spawn paths. */
static cmd_result cmd_help(const char *args)
{
    (void)args;

    printf("\nCommands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        const struct command *c = &COMMANDS[i];
        char label[32];
        snprintf(label, sizeof(label), "%s / %s%s%s", c->key, c->name,
                 c->args ? " " : "", c->args ? c->args : "");
        printf("  %-18s %s\n", label, c->desc);
    }
    printf("\nSpawn paths, for example:\n");
    printf("  spawn _retail_/wow.exe   (folder \"_retail_\", exe \"wow.exe\")\n");
    printf("  spawn RobloxPlayerBeta   (just a name, .exe optional)\n");
    printf("You can use / or \\, and the .exe is optional.\n");
    printf("Easiest: 'find <game>', then pick a result with the arrow keys and Enter.\n");
    return CMD_CONTINUE;
}

static cmd_result cmd_spawn(const char *args)
{
    spawn_game(args);
    return CMD_CONTINUE;
}

/* Menu text for one search result: "path [launcher]  (Game name)". */
static void choice_label(size_t i, char *buf, size_t cap, void *ctx)
{
    const struct game_choice *c = (const struct game_choice *)ctx + i;
    snprintf(buf, cap, "%s%s  (%s)", c->path, c->launcher ? " [launcher]" : "", c->game);
}

/* Search the list, then let the user pick one of the results to spawn. */
static cmd_result cmd_find(const char *args)
{
    struct game_choice *choices = NULL;
    size_t n = 0;
    games_find(args, &choices, &n);

    int pick = menu_pick("Pick an exe to spawn (Up/Down to move, Enter to spawn, "
                         "Esc to cancel):", n, choice_label, choices);
    if (pick >= 0) {
        spawn_game(choices[pick].path);
    }
    free(choices);
    return CMD_CONTINUE;
}

static cmd_result cmd_refresh(const char *args)
{
    (void)args;
    games_refresh();
    return CMD_CONTINUE;
}

/* Wipe the SPAWN_DIR folder, skipping (and reporting) games still running. */
static cmd_result cmd_delete(const char *args)
{
    (void)args;

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
static cmd_result cmd_clear(const char *args)
{
    (void)args;

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

static cmd_result cmd_quit(const char *args)
{
    (void)args;
    return CMD_EXIT;
}
