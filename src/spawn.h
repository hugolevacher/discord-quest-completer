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

/* Write the absolute path of the SPAWN_DIR folder (inside DATA_DIR) into out. */
bool spawn_root(char *out, size_t cap);

/*
 * Parse a duration such as "90s", "15m", "2h" or "1h30m" into seconds.
 * Every number needs a unit. Returns false if s is not a duration (or is 0).
 */
bool spawn_parse_duration(const char *s, unsigned long *seconds);

/* "1h 30m 5s"-style text for a number of seconds (zero parts are left out). */
void spawn_format_duration(unsigned long long seconds, char *out, size_t cap);

/*
 * If the last space-separated word of text is a duration, cut it off (text is
 * modified in place, trailing space removed) and store it in *seconds; otherwise
 * leave text alone and set *seconds to 0. The first word is never cut.
 */
void spawn_split_duration(char *text, unsigned long *seconds);

/*
 * Spawn one dummy game whose exe path ends with suffix (e.g. "_retail_/wow.exe"):
 * copy this exe to SPAWN_DIR\<suffix>, creating any sub-folders, and launch it
 * without a console; the copy opens its own game window (see child.c).
 * opts may be NULL. Reports the outcome through msg and returns whether the
 * game started.
 */
bool spawn_game(const char *suffix, const struct spawn_opts *opts);

#endif /* SPAWN_H */
