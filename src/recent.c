/*
 * recent.c - the games spawned before.
 *
 * One game per line: the path, a tab, then the arguments (possibly empty).
 */
#include "recent.h"

#include "config.h"
#include "fs.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static bool recent_path(char *out, size_t cap)
{
    char dir[MAX_PATH];
    if (!fs_exe_dir(dir, sizeof(dir))) {
        return false;
    }
    int written = snprintf(out, cap, "%s\\%s", dir, RECENT_FILE);
    return written >= 0 && (size_t)written < cap;
}

size_t recent_load(struct recent_entry *entries, size_t max)
{
    char file[MAX_PATH];
    if (!recent_path(file, sizeof(file))) {
        return 0;
    }
    FILE *f = fopen(file, "r");
    if (!f) {
        return 0;
    }

    size_t n = 0;
    char line[GAME_PATH_MAX + GAME_ARGS_MAX + 2];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab || tab == line) {
            continue;  /* damaged line */
        }
        *tab = '\0';
        snprintf(entries[n].path, sizeof(entries[n].path), "%.*s",
                 (int)sizeof(entries[n].path) - 1, line);
        snprintf(entries[n].args, sizeof(entries[n].args), "%.*s",
                 (int)sizeof(entries[n].args) - 1, tab + 1);
        n++;
    }
    fclose(f);
    return n;
}

void recent_add(const char *path, const char *args)
{
    if (!args) {
        args = "";
    }

    /* The new entry first, then the old ones that aren't the same game. */
    struct recent_entry entries[RECENT_MAX];
    size_t n = 0;
    snprintf(entries[0].path, sizeof(entries[0].path), "%s", path);
    snprintf(entries[0].args, sizeof(entries[0].args), "%s", args);
    n = 1;

    struct recent_entry old[RECENT_MAX];
    size_t old_n = recent_load(old, RECENT_MAX);
    for (size_t i = 0; i < old_n && n < RECENT_MAX; i++) {
        if (_stricmp(old[i].path, entries[0].path) != 0 ||
            strcmp(old[i].args, entries[0].args) != 0) {
            entries[n++] = old[i];
        }
    }

    char file[MAX_PATH];
    if (!recent_path(file, sizeof(file))) {
        return;
    }
    FILE *f = fopen(file, "w");
    if (!f) {
        return;  /* not worth bothering the user: it's only a convenience */
    }
    for (size_t i = 0; i < n; i++) {
        fprintf(f, "%s\t%s\n", entries[i].path, entries[i].args);
    }
    fclose(f);
}
