/*
 * update.h - checking GitHub for a newer release of this tool.
 */
#ifndef UPDATE_H
#define UPDATE_H

#include <stdbool.h>

enum update_status {
    UPDATE_FAILED,      /* offline, rate limited... */
    UPDATE_UP_TO_DATE,
    UPDATE_NEWER,
};

struct update_info {
    enum update_status status;
    char tag[64];   /* the latest release's tag, e.g. "v1.2.0" */
    char url[256];  /* its page on GitHub */
};

/* Start checking for a newer release in the background. Never blocks. */
void update_check_start(void);

/* True once the background check has finished, with its result in *info. Never blocks. */
bool update_poll(struct update_info *info);

/* Wait for the background check (or run one, retrying a failed one) into *info. */
void update_check_now(struct update_info *info);

#endif /* UPDATE_H */
