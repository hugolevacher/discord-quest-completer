/*
 * spawn.h - creating and running dummy game processes.
 */
#ifndef SPAWN_H
#define SPAWN_H

#include <stdbool.h>
#include <stddef.h>

/* Write the absolute path of the SPAWN_DIR folder (next to the exe) into out. */
bool spawn_root(char *out, size_t cap);

/*
 * Spawn one dummy game whose exe path ends with suffix (e.g. "_retail_/wow.exe"):
 * copy this exe to SPAWN_DIR\<suffix>, creating any sub-folders, and launch it
 * in a new console window. Problems are reported; this never exits the program.
 */
void spawn_game(const char *suffix);

/* Entry point for a spawned copy: stay alive until its window is closed. */
int spawn_child_main(void);

#endif /* SPAWN_H */
