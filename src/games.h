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

/*
 * Print the games whose name or alias contains query (case-insensitive), best
 * matches first, with the exe paths to spawn. Loads the list on first use,
 * downloading it if the cache is missing or older than a day.
 *
 * Every exe of the printed games is returned in *choices (an array the caller
 * must free(), NULL if there are none) with its length in *n_choices.
 */
void games_find(const char *query, struct game_choice **choices, size_t *n_choices);

/* Download the list now, replacing the cached copy. */
bool games_refresh(void);

/* Release the in-memory list. */
void games_free(void);

#endif /* GAMES_H */
