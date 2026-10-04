/*
 * spawn.h - creating and running dummy game processes.
 */
#ifndef SPAWN_H
#define SPAWN_H

#include <stdbool.h>
#include <stddef.h>

/* Optional extras for spawn_game; a zeroed struct means a plain spawn. */
struct spawn_opts {
    unsigned long seconds;  /* close the window after this long; 0 = never */
    const char *args;       /* command-line arguments for the game; NULL or "" = none */
};

/* Write the absolute path of the SPAWN_DIR folder (next to the exe) into out. */
bool spawn_root(char *out, size_t cap);

/*
 * Parse a duration such as "90s", "15m", "2h" or "1h30m" into seconds.
 * Every number needs a unit. Returns false if s is not a duration (or is 0).
 */
bool spawn_parse_duration(const char *s, unsigned long *seconds);

/*
 * If the last space-separated word of text is a duration, cut it off (text is
 * modified in place, trailing space removed) and store it in *seconds; otherwise
 * leave text alone and set *seconds to 0. The first word is never cut.
 */
void spawn_split_duration(char *text, unsigned long *seconds);

/*
 * Spawn one dummy game whose exe path ends with suffix (e.g. "_retail_/wow.exe"):
 * copy this exe to SPAWN_DIR\<suffix>, creating any sub-folders, and launch it
 * in a new, minimized console window. opts may be NULL. Problems are reported;
 * this never exits the program.
 */
void spawn_game(const char *suffix, const struct spawn_opts *opts);

/* Entry point for a spawned copy: stay alive until its window is closed. */
int spawn_child_main(int argc, char **argv);

#endif /* SPAWN_H */
