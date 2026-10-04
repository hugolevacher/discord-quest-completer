/*
 * procs.h - finding and stopping the games this tool has spawned.
 *
 * A spawned game is any running process whose exe lives inside SPAWN_DIR, so
 * this also finds games started by an earlier run of the spawner.
 */
#ifndef PROCS_H
#define PROCS_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>

struct running_game {
    unsigned long pid;
    char path[GAME_PROC_PATH_MAX];  /* relative to SPAWN_DIR, e.g. "_retail_\wow.exe" */
    unsigned long long seconds;     /* how long it has been running */
};

/*
 * List the running spawned games, longest-running first, in a malloc'd array
 * the caller must free() (NULL when there are none). Returns how many.
 */
size_t procs_list(struct running_game **games);

/* Terminate the process and wait briefly for it to go. True once it is gone. */
bool procs_stop(unsigned long pid);

#endif /* PROCS_H */
