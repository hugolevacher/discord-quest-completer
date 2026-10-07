/*
 * games.h - Discord's list of detectable games: download, cache and search.
 */
#ifndef GAMES_H
#define GAMES_H

#include <stdbool.h>
#include <stddef.h>

#define GAME_PATH_MAX 260
#define GAME_ARGS_MAX 256

/* One spawnable Windows exe from a search result. */
struct game_choice {
    const char *game;          /* game name; valid until the list is refreshed or freed */
    char path[GAME_PATH_MAX];  /* e.g. "_retail_/wow.exe" */
    char args[GAME_ARGS_MAX];  /* command-line arguments Discord needs, "" if none */
    bool launcher;             /* Discord marks this exe as the game's launcher */
};

/* One game found by games_search. Its strings live until the list is refreshed or freed. */
struct game_result {
    const char *id;            /* Discord application id */
    const char *name;
    const char *aliases;       /* other names, '\n'-separated; "" if none */
    const char *stores;        /* e.g. "steam 3405690, epic"; "" if none */
    struct game_choice *exes;  /* the exes Discord detects it by */
    size_t n_exes;             /* 0: Discord can't detect this game by its process */
};

/*
 * Find the games whose name, alias or exe path contains query (ignoring case),
 * best matches first. Loads the list on first use, downloading it if the cache
 * is missing or older than a day; this can take a few seconds.
 *
 * Stores up to max results in *results (free with games_results_free; NULL if
 * none) and how many games matched in all in *total. Returns false, after
 * reporting why through msg, if the list couldn't be loaded.
 */
bool games_search(const char *query, size_t max, struct game_result **results,
                  size_t *n_results, size_t *total);

void games_results_free(struct game_result *results, size_t n);

/* Download the list now, replacing the cached copy. */
bool games_refresh(void);

/* Release the in-memory list. */
void games_free(void);

#endif /* GAMES_H */
