/*
 * games.h - Discord's list of detectable games: download, cache and search.
 */
#ifndef GAMES_H
#define GAMES_H

#include <stdbool.h>

/*
 * Print the games whose name or alias contains query (case-insensitive), best
 * matches first, with the exe paths to spawn. Loads the list on first use,
 * downloading it if the cache is missing or older than a day.
 */
void games_find(const char *query);

/* Download the list now, replacing the cached copy. */
bool games_refresh(void);

/* Release the in-memory list. */
void games_free(void);

#endif /* GAMES_H */
