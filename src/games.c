/*
 * games.c - Discord's list of detectable games: download, cache and search.
 *
 * The list is a ~13 MB JSON array (one object per game). Parsed as a full JSON
 * tree it would take on the order of 100 MB, so after parsing we keep only the
 * few fields "find" shows and free the tree.
 */
#include "games.h"

#include "config.h"
#include "fs.h"
#include "http.h"
#include "msg.h"

#include "cJSON.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * One game, reduced to what "find" prints. Multi-value fields are
 * '\n'-separated strings; every field is non-NULL ("" when empty).
 */
struct game {
    char *id;       /* Discord application id */
    char *name;
    char *aliases;  /* other names */
    char *exes;     /* Windows exe paths Discord matches, e.g. "_retail_/wow.exe" */
    char *stores;   /* e.g. "steam 3405690, epic, xbox" */
};

static struct game *g_games;
static size_t g_count;

/*
 * An entry of struct game.exes is "path", optionally followed by ARGS_OPEN,
 * the command-line arguments and ")", and then, if it is the game's launcher,
 * LAUNCHER_TAG. Some games (Minecraft, Team Fortress 2...) are only detected
 * when the process was started with certain arguments.
 */
static const char LAUNCHER_TAG[] = "  [launcher]";
#define LAUNCHER_TAG_LEN (sizeof(LAUNCHER_TAG) - 1)
static const char ARGS_OPEN[] = "  (args: ";
#define ARGS_OPEN_LEN (sizeof(ARGS_OPEN) - 1)

/* An exes entry taken apart; the pointers point into the entry. */
struct exe_ref {
    const char *path;
    size_t path_len;
    const char *args;  /* NULL if the exe needs no arguments */
    size_t args_len;
    bool launcher;
};

static struct exe_ref parse_exe(const char *entry, size_t len)
{
    struct exe_ref r = { entry, len, NULL, 0, false };

    if (len >= LAUNCHER_TAG_LEN &&
        memcmp(entry + len - LAUNCHER_TAG_LEN, LAUNCHER_TAG, LAUNCHER_TAG_LEN) == 0) {
        r.launcher = true;
        len -= LAUNCHER_TAG_LEN;
        r.path_len = len;
    }
    if (len > ARGS_OPEN_LEN && entry[len - 1] == ')') {
        for (size_t i = 0; i + ARGS_OPEN_LEN < len; i++) {
            if (memcmp(entry + i, ARGS_OPEN, ARGS_OPEN_LEN) == 0) {
                r.path_len = i;
                r.args = entry + i + ARGS_OPEN_LEN;
                r.args_len = len - 1 - (i + ARGS_OPEN_LEN);
                break;
            }
        }
    }
    return r;
}

/* True if entry is exactly one of the '\n'-separated lines of list. */
static bool lines_have(const char *list, const char *entry)
{
    size_t n = strlen(entry);
    const char *p = list;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, entry, n) == 0) {
            return true;
        }
        p = end ? end + 1 : NULL;
    }
    return false;
}

/* ---- small growable string ------------------------------------------- */

struct strbuf {
    char *s;
    size_t len;
    size_t cap;
};

static bool sb_append(struct strbuf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 32;
        while (b->len + n + 1 > cap) {
            cap *= 2;
        }
        char *p = realloc(b->s, cap);
        if (!p) {
            return false;
        }
        b->s = p;
        b->cap = cap;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
    return true;
}

/* Append s, preceded by sep unless the buffer is still empty. */
static bool sb_add(struct strbuf *b, const char *sep, const char *s)
{
    if (b->len > 0 && !sb_append(b, sep, strlen(sep))) {
        return false;
    }
    return sb_append(b, s, strlen(s));
}

/* Hand over the built string ("" if nothing was added); NULL if out of memory. */
static char *sb_take(struct strbuf *b)
{
    char *s = b->s ? b->s : _strdup("");
    b->s = NULL;
    b->len = b->cap = 0;
    return s;
}

/* True if entry is exactly one of the ", "-separated items in list. */
static bool list_has(const char *list, const char *entry)
{
    size_t n = strlen(entry);
    const char *p = list;
    while (p && *p) {
        const char *end = strstr(p, ", ");
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, entry, n) == 0) {
            return true;
        }
        p = end ? end + 2 : NULL;
    }
    return false;
}

/* ---- loading ---------------------------------------------------------- */

static void free_game(struct game *g)
{
    free(g->id);
    free(g->name);
    free(g->aliases);
    free(g->exes);
    free(g->stores);
    memset(g, 0, sizeof(*g));
}

/* Copy of a JSON string value, or "" if the item is missing / not a string. */
static char *dup_string(const cJSON *item)
{
    return _strdup(cJSON_IsString(item) ? item->valuestring : "");
}

/* Fill g from one game object of the list. False only when out of memory. */
static bool load_game(const cJSON *json, struct game *g)
{
    struct strbuf aliases = { 0 };
    struct strbuf exes = { 0 };
    struct strbuf stores = { 0 };
    const cJSON *it;
    bool ok = true;

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(json, "aliases")) {
        if (cJSON_IsString(it)) {
            ok = ok && sb_add(&aliases, "\n", it->valuestring);
        }
    }

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(json, "executables")) {
        const cJSON *os = cJSON_GetObjectItemCaseSensitive(it, "os");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(it, "name");
        if (!cJSON_IsString(os) || strcmp(os->valuestring, "win32") != 0 ||
            !cJSON_IsString(name)) {
            continue;
        }

        /*
         * A leading '>' in Discord's list marks an exe matched by its arguments
         * rather than its path; we spawn it under its plain name with them.
         */
        const char *path = name->valuestring;
        if (path[0] == '>') {
            path++;
        }
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(it, "arguments");
        bool has_args = cJSON_IsString(args) && args->valuestring[0];
        bool launcher = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "is_launcher"));

        char entry[GAME_PATH_MAX + GAME_ARGS_MAX + 32];
        snprintf(entry, sizeof(entry), "%s%s%s%s%s", path,
                 has_args ? ARGS_OPEN : "", has_args ? args->valuestring : "",
                 has_args ? ")" : "", launcher ? LAUNCHER_TAG : "");

        /* "hl2.exe" and ">hl2.exe" collapse into one entry once the '>' is gone. */
        if (!lines_have(exes.s, entry)) {
            ok = ok && sb_add(&exes, "\n", entry);
        }
    }

    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(json, "third_party_skus")) {
        const cJSON *dist = cJSON_GetObjectItemCaseSensitive(it, "distributor");
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id");
        if (!cJSON_IsString(dist)) {
            continue;
        }
        /* Steam ids are worth showing (SteamDB lookups); others just by name. */
        char entry[128];
        if (strcmp(dist->valuestring, "steam") == 0 && cJSON_IsString(id)) {
            snprintf(entry, sizeof(entry), "steam %s", id->valuestring);
        } else {
            snprintf(entry, sizeof(entry), "%s", dist->valuestring);
        }
        if (!list_has(stores.s, entry)) {  /* e.g. many xbox SKUs -> one "xbox" */
            ok = ok && sb_add(&stores, ", ", entry);
        }
    }

    g->id = dup_string(cJSON_GetObjectItemCaseSensitive(json, "id"));
    g->name = dup_string(cJSON_GetObjectItemCaseSensitive(json, "name"));
    g->aliases = sb_take(&aliases);
    g->exes = sb_take(&exes);
    g->stores = sb_take(&stores);

    if (!ok || !g->id || !g->name || !g->aliases || !g->exes || !g->stores) {
        free_game(g);
        return false;
    }
    return true;
}

/* Parse the list's JSON text and replace the in-memory list with it. */
static bool parse_list(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        msg_error("The game list is damaged - refresh it.");
        return false;
    }

    size_t total = (size_t)cJSON_GetArraySize(root);
    struct game *games = calloc(total ? total : 1, sizeof(*games));
    if (!games) {
        cJSON_Delete(root);
        msg_error("Out of memory loading the game list.");
        return false;
    }

    size_t count = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, root) {
        if (load_game(it, &games[count])) {
            count++;
        }
    }
    cJSON_Delete(root);

    games_free();
    g_games = games;
    g_count = count;
    return true;
}

static bool load_file(const char *path)
{
    char *text = fs_read_file(path, NULL);
    if (!text) {
        msg_error("Cannot read %s.", path);
        return false;
    }
    bool ok = parse_list(text);
    free(text);
    return ok;
}

/* Absolute path of the cache file, in the data folder. */
static bool cache_path(char *out, size_t cap)
{
    return fs_data_path(out, cap, GAMES_CACHE_FILE);
}

static bool download(const char *path)
{
    msg_info("Downloading Discord's game list...");

    size_t bytes;
    if (!http_download(GAMES_HOST, GAMES_PATH, path, &bytes)) {
        return false;
    }
    msg_info("Downloaded %.1f MB.", (double)bytes / (1024.0 * 1024.0));
    return true;
}

/*
 * Make sure the list is in memory. Uses the cache, downloading it first when
 * missing or stale; if that download fails, an old cache is still used.
 */
static bool ensure_loaded(void)
{
    if (g_games) {
        return true;
    }

    char path[MAX_PATH];
    if (!cache_path(path, sizeof(path))) {
        msg_error("Cannot locate the game list cache.");
        return false;
    }

    long long age = 0;
    bool cached = fs_file_age(path, &age);
    if (!cached || age > GAMES_CACHE_MAX_AGE_SECONDS) {
        if (!download(path)) {
            if (!cached) {
                return false;
            }
            msg_info("Using the saved list from %lu hour(s) ago instead.",
                    (unsigned long)(age / 3600));
        }
    }
    return load_file(path);
}

bool games_refresh(void)
{
    char path[MAX_PATH];
    if (!cache_path(path, sizeof(path))) {
        msg_error("Cannot locate the game list cache.");
        return false;
    }
    if (!download(path) || !load_file(path)) {
        return false;
    }
    msg_info("%lu games loaded.", (unsigned long)g_count);
    return true;
}

void games_free(void)
{
    for (size_t i = 0; i < g_count; i++) {
        free_game(&g_games[i]);
    }
    free(g_games);
    g_games = NULL;
    g_count = 0;
}

/* ---- searching -------------------------------------------------------- */

/* Case-insensitive: does the first len chars of s contain q? */
static bool contains_ci(const char *s, size_t len, const char *q, size_t qn)
{
    for (size_t i = 0; i + qn <= len; i++) {
        if (_strnicmp(s + i, q, qn) == 0) {
            return true;
        }
    }
    return false;
}

/* Rank one name: 0 = exactly q, 1 = starts with q, 2 = contains q, -1 = no. */
static int rank_text(const char *s, size_t len, const char *q, size_t qn)
{
    if (len == qn && _strnicmp(s, q, qn) == 0) {
        return 0;
    }
    if (len >= qn && _strnicmp(s, q, qn) == 0) {
        return 1;
    }
    return contains_ci(s, len, q, qn) ? 2 : -1;
}

/*
 * Best rank of q for a game (lower is better): its name and aliases rank 0-2
 * as in rank_text(); failing those, an exe path containing q ranks 3, so
 * "find wow" still finds World of Warcraft through "_retail_/wow.exe".
 */
static int match_rank(const struct game *g, const char *q, size_t qn)
{
    int best = rank_text(g->name, strlen(g->name), q, qn);

    const char *p = g->aliases;
    while (*p && best != 0) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        int r = rank_text(p, len, q, qn);
        if (r >= 0 && (best < 0 || r < best)) {
            best = r;
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    if (best >= 0) {
        return best;
    }

    p = g->exes;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        struct exe_ref exe = parse_exe(p, len);  /* search the path only */
        if (contains_ci(exe.path, exe.path_len, q, qn)) {
            return 3;
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    return -1;
}

struct match {
    const struct game *game;
    int rank;
};

/* Best rank first, then shorter names (closer to the query), then A-Z. */
static int compare_matches(const void *a, const void *b)
{
    const struct match *x = a;
    const struct match *y = b;
    if (x->rank != y->rank) {
        return x->rank - y->rank;
    }
    size_t lx = strlen(x->game->name);
    size_t ly = strlen(y->game->name);
    if (lx != ly) {
        return lx < ly ? -1 : 1;
    }
    return _stricmp(x->game->name, y->game->name);
}

/* Number of '\n'-separated values in s ("" has none). */
static size_t count_lines(const char *s)
{
    if (!*s) {
        return 0;
    }
    size_t n = 1;
    for (; *s; s++) {
        n += *s == '\n';
    }
    return n;
}

/* Fill r from g, taking its exes apart into spawnable choices. */
static bool make_result(const struct game *g, struct game_result *r)
{
    r->id = g->id;
    r->name = g->name;
    r->aliases = g->aliases;
    r->stores = g->stores;
    r->exes = NULL;
    r->n_exes = 0;

    size_t total = count_lines(g->exes);
    if (total == 0) {
        return true;
    }
    r->exes = malloc(total * sizeof(*r->exes));
    if (!r->exes) {
        return false;
    }

    const char *p = g->exes;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        struct exe_ref exe = parse_exe(p, len);
        struct game_choice *c = &r->exes[r->n_exes];
        if (exe.path_len < sizeof(c->path) && exe.args_len < sizeof(c->args)) {
            memcpy(c->path, exe.path, exe.path_len);
            c->path[exe.path_len] = '\0';
            if (exe.args) {
                memcpy(c->args, exe.args, exe.args_len);
            }
            c->args[exe.args_len] = '\0';
            c->game = g->name;
            c->launcher = exe.launcher;
            r->n_exes++;
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    return true;
}

bool games_search(const char *query, size_t max, struct game_result **results,
                  size_t *n_results, size_t *total)
{
    *results = NULL;
    *n_results = 0;
    *total = 0;

    if (!ensure_loaded()) {
        return false;
    }
    if (g_count == 0) {
        msg_error("The game list is empty - refresh it.");
        return false;
    }

    struct match *matches = malloc(g_count * sizeof(*matches));
    if (!matches) {
        msg_error("Out of memory.");
        return false;
    }

    size_t qn = strlen(query);
    size_t n = 0;
    for (size_t i = 0; i < g_count; i++) {
        int rank = match_rank(&g_games[i], query, qn);
        if (rank >= 0) {
            matches[n].game = &g_games[i];
            matches[n].rank = rank;
            n++;
        }
    }
    qsort(matches, n, sizeof(*matches), compare_matches);
    *total = n;

    size_t shown = n < max ? n : max;
    bool ok = true;
    if (shown > 0) {
        struct game_result *list = calloc(shown, sizeof(*list));
        ok = list != NULL;
        for (size_t i = 0; ok && i < shown; i++) {
            ok = make_result(matches[i].game, &list[i]);
        }
        if (ok) {
            *results = list;
            *n_results = shown;
        } else {
            games_results_free(list, shown);
            msg_error("Out of memory.");
        }
    }
    free(matches);
    return ok;
}

void games_results_free(struct game_result *results, size_t n)
{
    if (!results) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        free(results[i].exes);
    }
    free(results);
}
