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

#include "cJSON.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        fprintf(stderr, "error: the game list is not valid JSON - run 'refresh'.\n");
        return false;
    }

    size_t total = (size_t)cJSON_GetArraySize(root);
    struct game *games = calloc(total ? total : 1, sizeof(*games));
    if (!games) {
        cJSON_Delete(root);
        fprintf(stderr, "error: out of memory loading the game list.\n");
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
        fprintf(stderr, "error: cannot read %s.\n", path);
        return false;
    }
    bool ok = parse_list(text);
    free(text);
    return ok;
}

/* Absolute path of the cache file, next to the exe. */
static bool cache_path(char *out, size_t cap)
{
    char dir[MAX_PATH];
    if (!fs_exe_dir(dir, sizeof(dir))) {
        return false;
    }
    int written = snprintf(out, cap, "%s\\%s", dir, GAMES_CACHE_FILE);
    return written >= 0 && (size_t)written < cap;
}

static bool download(const char *path, bool verbose)
{
    if (verbose) {
        printf("Downloading Discord's game list...\n");
        fflush(stdout);
    }

    size_t bytes;
    if (!http_download(GAMES_HOST, GAMES_PATH, path, &bytes)) {
        return false;
    }
    if (verbose) {
        printf("Downloaded %.1f MB.\n", (double)bytes / (1024.0 * 1024.0));
    }
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
        fprintf(stderr, "error: cannot locate the game list cache.\n");
        return false;
    }

    long long age = 0;
    bool cached = fs_file_age(path, &age);
    if (!cached || age > GAMES_CACHE_MAX_AGE_SECONDS) {
        if (!download(path, true)) {
            if (!cached) {
                return false;
            }
            fprintf(stderr, "Using the saved list from %lu hour(s) ago instead.\n",
                    (unsigned long)(age / 3600));
        }
    }
    return load_file(path);
}

bool games_refresh(void)
{
    char path[MAX_PATH];
    if (!cache_path(path, sizeof(path))) {
        fprintf(stderr, "error: cannot locate the game list cache.\n");
        return false;
    }
    if (!download(path, true) || !load_file(path)) {
        return false;
    }
    printf("%lu games loaded.\n", (unsigned long)g_count);
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

#define LABEL_WIDTH 13  /* longest label ("Also called:") plus a space */

/* Print a labelled field whose '\n'-separated values go one per line. */
static void print_lines(const char *label, const char *lines)
{
    printf("  %-*s", LABEL_WIDTH, label);
    bool first = true;
    const char *p = lines;
    while (*p) {
        const char *end = strchr(p, '\n');
        int len = end ? (int)(end - p) : (int)strlen(p);
        printf("%*s%.*s\n", first ? 0 : LABEL_WIDTH + 2, "", len, p);
        first = false;
        if (!end) {
            break;
        }
        p = end + 1;
    }
}

static void print_game(const struct game *g)
{
    printf("\n%s\n", g->name);
    if (*g->exes) {
        print_lines("Spawn as:", g->exes);
    } else {
        printf("  %-*s(none - Discord can't detect this game by its process,\n"
               "  %-*s so spawning it won't work)\n",
               LABEL_WIDTH, "Spawn as:", LABEL_WIDTH, "");
    }
    if (*g->aliases) {
        print_lines("Also called:", g->aliases);
    }
    if (*g->stores) {
        printf("  %-*s%s\n", LABEL_WIDTH, "Stores:", g->stores);
    }
    printf("  %-*s%s\n", LABEL_WIDTH, "Discord ID:", g->id);
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

/* Gather every exe of the shown games into a malloc'd array of choices. */
static void collect_choices(const struct match *matches, size_t shown,
                            struct game_choice **out, size_t *n_out)
{
    size_t total = 0;
    for (size_t i = 0; i < shown; i++) {
        total += count_lines(matches[i].game->exes);
    }
    if (total == 0) {
        return;
    }

    struct game_choice *list = malloc(total * sizeof(*list));
    if (!list) {
        return;  /* the results are already printed; just no menu */
    }

    size_t n = 0;
    for (size_t i = 0; i < shown; i++) {
        const struct game *g = matches[i].game;
        const char *p = g->exes;
        while (*p) {
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);

            struct exe_ref exe = parse_exe(p, len);
            if (exe.path_len < sizeof(list[n].path) && exe.args_len < sizeof(list[n].args)) {
                memcpy(list[n].path, exe.path, exe.path_len);
                list[n].path[exe.path_len] = '\0';
                if (exe.args) {
                    memcpy(list[n].args, exe.args, exe.args_len);
                }
                list[n].args[exe.args_len] = '\0';
                list[n].game = g->name;
                list[n].launcher = exe.launcher;
                n++;
            }
            if (!end) {
                break;
            }
            p = end + 1;
        }
    }

    *out = list;
    *n_out = n;
}

void games_find(const char *query, struct game_choice **choices, size_t *n_choices)
{
    *choices = NULL;
    *n_choices = 0;

    if (!ensure_loaded()) {
        return;
    }
    if (g_count == 0) {
        printf("The game list is empty - try 'refresh'.\n");
        return;
    }

    struct match *matches = malloc(g_count * sizeof(*matches));
    if (!matches) {
        fprintf(stderr, "error: out of memory.\n");
        return;
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

    if (n == 0) {
        printf("No game matching \"%s\".\n", query);
    } else {
        qsort(matches, n, sizeof(*matches), compare_matches);
        size_t shown = n < FIND_MAX_RESULTS ? n : FIND_MAX_RESULTS;
        for (size_t i = 0; i < shown; i++) {
            print_game(matches[i].game);
        }
        if (n > shown) {
            printf("\n...and %lu more. Try a more specific name.\n",
                   (unsigned long)(n - shown));
        }
        collect_choices(matches, shown, choices, n_choices);
    }
    free(matches);
}

/* ---- watching --------------------------------------------------------- */

/* The game whose name or alias matches q best (exe paths don't count), or NULL. */
static const struct game *best_by_name(const char *q)
{
    size_t qn = strlen(q);
    const struct game *best = NULL;
    int best_rank = 0;
    for (size_t i = 0; i < g_count; i++) {
        int rank = match_rank(&g_games[i], q, qn);
        if (rank < 0 || rank > 2) {
            continue;
        }
        if (!best || rank < best_rank ||
            (rank == best_rank && strlen(g_games[i].name) < strlen(best->name))) {
            best = &g_games[i];
            best_rank = rank;
        }
    }
    return best;
}

static const struct game *game_by_id(const char *id)
{
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_games[i].id, id) == 0) {
            return &g_games[i];
        }
    }
    return NULL;
}

/* True if Esc was pressed since the last call. Always false off a console. */
static bool esc_pressed(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(in, &mode)) {
        return false;
    }

    INPUT_RECORD rec;
    DWORD got;
    while (PeekConsoleInputW(in, &rec, 1, &got) && got > 0) {
        ReadConsoleInputW(in, &rec, 1, &got);
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown &&
            rec.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
            return true;
        }
    }
    return false;
}

static void print_clock(void)
{
    time_t now = time(NULL);
    char text[16];
    strftime(text, sizeof(text), "%H:%M", localtime(&now));
    printf("[%s] ", text);
}

void games_watch(const char *query)
{
    if (!ensure_loaded()) {
        return;
    }
    const struct game *g = best_by_name(query);
    if (!g) {
        printf("No game matching \"%s\".\n", query);
        return;
    }
    if (*g->exes) {
        print_game(g);
        printf("\nDiscord already detects %s - use 'find' to spawn it.\n", g->name);
        return;
    }

    /* The list is replaced on every check, so remember the game by its id. */
    char id[64];
    char name[256];
    snprintf(id, sizeof(id), "%s", g->id);
    snprintf(name, sizeof(name), "%s", g->name);

    printf("Watching %s: Discord has no exe for it yet.\n"
           "Checking Discord's list every %d minute(s). Press Esc to stop.\n",
           name, WATCH_INTERVAL_SECONDS / 60);
    esc_pressed();  /* drop keys typed before now */

    for (;;) {
        for (int waited = 0; waited < WATCH_INTERVAL_SECONDS * 1000; waited += 250) {
            if (esc_pressed()) {
                printf("Stopped watching %s.\n", name);
                return;
            }
            Sleep(250);
        }

        print_clock();
        printf("Checking...\n");

        char path[MAX_PATH];
        if (!cache_path(path, sizeof(path)) || !download(path, false) ||
            !load_file(path)) {
            printf("Check failed - will try again.\n");
            continue;
        }
        g = game_by_id(id);
        if (!g) {
            printf("%s is no longer in Discord's list - stopped watching.\n", name);
            return;
        }
        if (*g->exes) {
            printf("\a");
            print_clock();
            printf("Discord now detects %s!\n", name);
            print_game(g);
            printf("\nUse 'find %s' to spawn it.\n", name);
            return;
        }
    }
}
