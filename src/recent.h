/*
 * recent.h - the games spawned before, so one can be spawned again from a menu.
 *
 * Kept in RECENT_FILE in the data folder, most recent first, without duplicates.
 */
#ifndef RECENT_H
#define RECENT_H

#include "games.h"

#include <stddef.h>

struct recent_entry {
    char path[GAME_PATH_MAX];  /* e.g. "_retail_\wow.exe" */
    char args[GAME_ARGS_MAX];  /* command-line arguments, "" if none */
};

/* Remember a spawned game (moving it to the front if already there). */
void recent_add(const char *path, const char *args);

/* Read up to max entries, most recent first. Returns how many. */
size_t recent_load(struct recent_entry *entries, size_t max);

#endif /* RECENT_H */
