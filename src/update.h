/*
 * update.h - checking GitHub for a newer release of this tool.
 */
#ifndef UPDATE_H
#define UPDATE_H

/*
 * Start checking for a newer release in the background. Never prints and never
 * blocks; a failed check (offline, rate limited...) is simply ignored.
 */
void update_check_start(void);

/* Print a one-line notice if the background check found a newer release (once). */
void update_announce(void);

/* The update command: wait for the check (or run it) and always say the result. */
void update_report(void);

#endif /* UPDATE_H */
