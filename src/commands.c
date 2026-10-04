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
#include "procs.h"
#include "recent.h"
#include "spawn.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cmd_result cmd_help(const char *args);
static cmd_result cmd_spawn(const char *args);
static cmd_result cmd_find(const char *args);
static cmd_result cmd_recent(const char *args);
static cmd_result cmd_list(const char *args);
static cmd_result cmd_stop(const char *args);
static cmd_result cmd_refresh(const char *args);
static cmd_result cmd_delete(const char *args);
static cmd_result cmd_clear(const char *args);
static cmd_result cmd_quit(const char *args);

struct command {
    const char *key;   /* short form, e.g. "h" */
    const char *name;  /* long form, e.g. "help" */
    const char *args;  /* argument placeholder shown in help, NULL if none; one
                          starting with '[' is optional, any other is required */
    const char *desc;  /* shown by the help command */
    cmd_result (*run)(const char *args);
};

static const struct command COMMANDS[] = {
    { "h", "help",    NULL,     "Show this help",                                  cmd_help    },
    { "s", "spawn",   "<path> [time]",
                                "Spawn a game by exe path; time (15m, 2h) closes it later",
                                                                                   cmd_spawn   },
    { "f", "find",    "<name> [time]",
                                "Look up a game in Discord's list, then pick one to spawn",
                                                                                   cmd_find    },
    { "rc", "recent", "[time]", "Pick a game you spawned before and spawn it again",
                                                                                   cmd_recent  },
    { "l", "list",    NULL,    "Show the running games and how long they have run",
                                                                                   cmd_list    },
    { "x", "stop",    "[all]",  "Stop a running game (pick from a menu), or all of them",
                                                                                   cmd_stop    },
    { "r", "refresh", NULL,     "Re-download Discord's game list",                 cmd_refresh },
    { "d", "delete",  NULL,     "Delete the \"" SPAWN_DIR "\" folder (offers to stop running games)",
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
        if (c->args && c->args[0] != '[' && !*rest) {
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
        printf("  %-24s %s\n", label, c->desc);
    }
    printf("\nSpawn paths, for example:\n");
    printf("  spawn _retail_/wow.exe   (folder \"_retail_\", exe \"wow.exe\")\n");
    printf("  spawn RobloxPlayerBeta   (just a name, .exe optional)\n");
    printf("You can use / or \\, and the .exe is optional.\n");
    printf("Easiest: 'find <game>', then pick a result with the arrow keys and Enter.\n");
    return CMD_CONTINUE;
}

/*
 * Copy args into text, cutting a trailing duration ("15m") into opts.
 * False if args is too long for text.
 */
static bool split_args(const char *args, char *text, size_t cap, struct spawn_opts *opts)
{
    int written = snprintf(text, cap, "%s", args);
    if (written < 0 || (size_t)written >= cap) {
        fprintf(stderr, "error: input too long.\n");
        return false;
    }
    spawn_split_duration(text, &opts->seconds);
    return true;
}

static cmd_result cmd_spawn(const char *args)
{
    char text[INPUT_MAX];
    struct spawn_opts opts = { 0 };
    if (split_args(args, text, sizeof(text), &opts)) {
        spawn_game(text, &opts);
    }
    return CMD_CONTINUE;
}

/* Menu text for one search result: "path [launcher]  (Game name)". */
static void choice_label(size_t i, char *buf, size_t cap, void *ctx)
{
    const struct game_choice *c = (const struct game_choice *)ctx + i;
    snprintf(buf, cap, "%s%s%s%s%s  (%s)", c->path, c->args[0] ? " (args: " : "", c->args,
             c->args[0] ? ")" : "", c->launcher ? " [launcher]" : "", c->game);
}

/* Search the list, then let the user pick one of the results to spawn. */
static cmd_result cmd_find(const char *args)
{
    char query[INPUT_MAX];
    struct spawn_opts opts = { 0 };
    if (!split_args(args, query, sizeof(query), &opts)) {
        return CMD_CONTINUE;
    }

    struct game_choice *choices = NULL;
    size_t n = 0;
    games_find(query, &choices, &n);

    int pick = menu_pick("Pick an exe to spawn (Up/Down to move, Enter to spawn, "
                         "Esc to cancel):", n, choice_label, choices);
    if (pick >= 0) {
        opts.args = choices[pick].args;
        spawn_game(choices[pick].path, &opts);
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

static void recent_label(size_t i, char *buf, size_t cap, void *ctx)
{
    const struct recent_entry *e = (const struct recent_entry *)ctx + i;
    snprintf(buf, cap, "%s%s%s%s", e->path, e->args[0] ? "  (args: " : "", e->args,
             e->args[0] ? ")" : "");
}

/* Pick one of the games spawned before and spawn it again. */
static cmd_result cmd_recent(const char *args)
{
    struct spawn_opts opts = { 0 };
    if (*args && !spawn_parse_duration(args, &opts.seconds)) {
        printf("usage: recent [time]   (a time looks like 15m, 2h or 1h30m)\n");
        return CMD_CONTINUE;
    }

    struct recent_entry entries[RECENT_MAX];
    size_t n = recent_load(entries, RECENT_MAX);
    if (n == 0) {
        printf("No games spawned yet.\n");
        return CMD_CONTINUE;
    }

    int pick = menu_pick("Pick a game to spawn again (Up/Down to move, Enter to spawn, "
                         "Esc to cancel):", n, recent_label, entries);
    if (pick >= 0) {
        opts.args = entries[pick].args;
        spawn_game(entries[pick].path, &opts);
    }
    return CMD_CONTINUE;
}

static void print_running(const struct running_game *games, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char up[32];
        spawn_format_duration(games[i].seconds, up, sizeof(up));
        printf("  PID %-6lu %-11s %s\n", games[i].pid, up, games[i].path);
    }
}

static cmd_result cmd_list(const char *args)
{
    (void)args;

    struct running_game *games;
    size_t n = procs_list(&games);
    if (n == 0) {
        printf("No spawned games are running.\n");
        return CMD_CONTINUE;
    }
    printf("%lu running (PID, running for, path):\n", (unsigned long)n);
    print_running(games, n);
    free(games);
    return CMD_CONTINUE;
}

/* Stop games[from..to) and report each. Returns how many are gone. */
static size_t stop_games(const struct running_game *games, size_t from, size_t to)
{
    size_t stopped = 0;
    for (size_t i = from; i < to; i++) {
        if (procs_stop(games[i].pid)) {
            printf("Stopped %s (PID %lu).\n", games[i].path, games[i].pid);
            stopped++;
        } else {
            fprintf(stderr, "error: could not stop %s (PID %lu).\n",
                    games[i].path, games[i].pid);
        }
    }
    return stopped;
}

/* Menu text for stop: one row per running game, then "stop all". */
struct stop_menu {
    const struct running_game *games;
    size_t n;
};

static void stop_label(size_t i, char *buf, size_t cap, void *ctx)
{
    const struct stop_menu *m = ctx;
    if (i >= m->n) {
        snprintf(buf, cap, "Stop all %lu games", (unsigned long)m->n);
        return;
    }
    char up[32];
    spawn_format_duration(m->games[i].seconds, up, sizeof(up));
    snprintf(buf, cap, "%s  (PID %lu, running %s)", m->games[i].path, m->games[i].pid, up);
}

static cmd_result cmd_stop(const char *args)
{
    if (*args && _stricmp(args, "all") != 0) {
        printf("usage: stop [all]\n");
        return CMD_CONTINUE;
    }

    struct running_game *games;
    size_t n = procs_list(&games);
    if (n == 0) {
        printf("No spawned games are running.\n");
        return CMD_CONTINUE;
    }

    if (*args) {
        stop_games(games, 0, n);
    } else {
        struct stop_menu menu = { games, n };
        int pick = menu_pick("Pick a game to stop (Up/Down to move, Enter to stop, "
                             "Esc to cancel):", n + 1, stop_label, &menu);
        if (pick >= 0 && (size_t)pick < n) {
            stop_games(games, (size_t)pick, (size_t)pick + 1);
        } else if (pick >= 0) {
            stop_games(games, 0, n);
        }
    }
    free(games);
    return CMD_CONTINUE;
}

/* Ask a yes/no question on the prompt; anything but "y"/"yes" means no. */
static bool confirm(const char *question)
{
    printf("%s [y/N] ", question);
    fflush(stdout);

    char answer[16];
    if (!fgets(answer, sizeof(answer), stdin)) {
        return false;
    }
    return answer[0] == 'y' || answer[0] == 'Y';
}

/*
 * Wipe the SPAWN_DIR folder. Running games can't be deleted, so offer to stop
 * them first; any that are left are skipped and reported.
 */
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

    struct running_game *games;
    size_t running = procs_list(&games);
    if (running > 0) {
        printf("%lu game(s) are still running:\n", (unsigned long)running);
        print_running(games, running);
        if (confirm("Stop them so they can be deleted?")) {
            stop_games(games, 0, running);
        }
        free(games);
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
