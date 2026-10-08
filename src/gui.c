/*
 * gui.c - the window interface, for people who'd rather not use the prompt.
 *
 * Plain Win32 controls: search for a game, pick one of its exes, spawn it
 * (optionally with a timer), and watch / stop the running games. It uses the
 * same core as the prompt (games, spawn, procs, recent, update).
 *
 * Searching can download the 12 MB game list, so it runs on a worker thread
 * and reports back with a posted message; everything else is quick and runs
 * on the window's thread. Core messages (msg_info / msg_error) may come from
 * either thread, so they are posted to the window and shown in the status bar.
 *
 * Text is UTF-8 in the core (game names like "Pokémon") and UTF-16 in the
 * window, converted at the boundary with widen() / narrow().
 */
#include "gui.h"

#include "config.h"
#include "fs.h"
#include "games.h"
#include "msg.h"
#include "procs.h"
#include "recent.h"
#include "spawn.h"
#include "update.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUI_MAX_RESULTS 25       /* games listed for one search */
#define RUNNING_REFRESH_MS 1000  /* how often the running list is updated */

enum {
    IDC_SEARCH_LABEL = 100,
    IDC_SEARCH_EDIT,
    IDC_SEARCH_BUTTON,
    IDC_RESULTS,
    IDC_TIMER_LABEL,
    IDC_TIMER,
    IDC_RECENT,
    IDC_SPAWN,
    IDC_RUNNING_LABEL,
    IDC_RUNNING,
    IDC_STOP,
    IDC_STOP_ALL,
    IDC_DELETE,
    IDC_TIP,
    IDC_STATUS,
    ID_RECENT_FIRST = 1000,  /* recent-games menu items */
};

enum {
    WM_APP_MSG = WM_APP + 1,  /* wParam: enum msg_kind, lParam: malloc'd wchar_t text */
    WM_APP_SEARCH_DONE,       /* lParam: struct search_job * */
};

#define TIMER_RUNNING 1
#define TIMER_UPDATE 2

/* One row of the results list: a game and one of its exes (or none). */
struct row {
    wchar_t game[256];
    bool has_exe;            /* false: Discord can't detect this game */
    struct game_choice exe;  /* exe.game is not used: the list may be refreshed */
};

/* A search running on the worker thread. */
struct search_job {
    char query[INPUT_MAX];
    struct row *rows;
    size_t n_rows;
    size_t total;  /* games that matched in all */
    bool ok;
};

/* Choices of the "Close the game after" box. */
static const struct {
    const wchar_t *label;
    unsigned long seconds;
} TIMERS[] = {
    { L"Keep running",  0 },
    { L"15 minutes",    15 * 60 },
    { L"20 minutes",    20 * 60 },
    { L"30 minutes",    30 * 60 },
    { L"1 hour",        60 * 60 },
    { L"2 hours",       2 * 60 * 60 },
};
#define NUM_TIMERS (sizeof(TIMERS) / sizeof(TIMERS[0]))

static struct {
    HWND hwnd;
    HWND search_edit, search_button, results, timer, recent, spawn;
    HWND running, stop, stop_all, del, tip, status;
    HWND labels[3];
    HFONT font;
    HFONT bold_font;        /* for the Start game button */
    int dpi;
    bool start_is_default;  /* Enter starts the selected game rather than searching */

    struct row *rows;  /* what the results list shows */
    size_t n_rows;
    bool searching;

    unsigned long *running_pids;  /* what the running list shows, in order */
    size_t n_running;

    struct update_info update;
    bool update_known;

    struct recent_entry recent_entries[RECENT_MAX];
} g;

/* ---- helpers --------------------------------------------------------- */

static int px(int value)
{
    return MulDiv(value, g.dpi, 96);
}

/* UTF-8 -> UTF-16 into out (always terminated). */
static void widen(const char *s, wchar_t *out, int cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap)) {
        out[0] = L'\0';
    }
    out[cap - 1] = L'\0';
}

/* UTF-16 -> UTF-8 into out (always terminated). */
static void narrow(const wchar_t *s, char *out, int cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, s, -1, out, cap, NULL, NULL)) {
        out[0] = '\0';
    }
    out[cap - 1] = '\0';
}

static void set_status(const wchar_t *text)
{
    SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)text);
}

/* Messages can come from the search thread, so hand them to the window's thread. */
static void gui_msg_handler(enum msg_kind kind, const char *text)
{
    size_t cap = strlen(text) + 1;
    wchar_t *wide = malloc(cap * sizeof(wchar_t));
    if (!wide) {
        return;
    }
    widen(text, wide, (int)cap);
    if (!PostMessageW(g.hwnd, WM_APP_MSG, (WPARAM)kind, (LPARAM)wide)) {
        free(wide);
    }
}

static HWND add_control(const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                             g.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, FALSE);
    return h;
}

static void add_column(HWND list, int index, const wchar_t *title)
{
    LVCOLUMNW col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (wchar_t *)title;
    col.cx = px(100);
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&col);
}

static void set_cell(HWND list, int item, int sub, const wchar_t *text)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.iSubItem = sub;
    it.pszText = (wchar_t *)text;
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)item, (LPARAM)&it);
}

static int add_item(HWND list, int item, const wchar_t *text, LPARAM data)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = item;
    it.pszText = (wchar_t *)text;
    it.lParam = data;
    return (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&it);
}

static int selected_item(HWND list)
{
    return (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

static unsigned long selected_timer(void)
{
    LRESULT i = SendMessageW(g.timer, CB_GETCURSEL, 0, 0);
    return i >= 0 && (size_t)i < NUM_TIMERS ? TIMERS[i].seconds : 0;
}

/* ---- results ---------------------------------------------------------- */

static const struct row *selected_row(void)
{
    int i = selected_item(g.results);
    return i >= 0 && (size_t)i < g.n_rows ? &g.rows[i] : NULL;
}

/* True if the spawned-games folder holds anything to delete. */
static bool have_spawned_games(void)
{
    char root[MAX_PATH];
    char pattern[MAX_PATH];
    if (!spawn_root(root, sizeof(root)) ||
        snprintf(pattern, sizeof(pattern), "%s\\*", root) >= (int)sizeof(pattern)) {
        return false;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool found = false;
    do {
        found = strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0;
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static void update_buttons(void)
{
    const struct row *r = selected_row();
    EnableWindow(g.spawn, r && r->has_exe);
    EnableWindow(g.search_button, !g.searching);

    bool any_running = g.n_running > 0;
    EnableWindow(g.stop, selected_item(g.running) >= 0);
    EnableWindow(g.stop_all, any_running);
    EnableWindow(g.del, have_spawned_games());
}

/*
 * Make Start game or Search the highlighted default button: Start game once
 * there are results to start, Search while typing a new search.
 */
static void set_default_button(bool start)
{
    g.start_is_default = start;
    SendMessageW(g.spawn, BM_SETSTYLE, start ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    SendMessageW(g.search_button, BM_SETSTYLE, start ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON, TRUE);
}

static void show_results(struct row *rows, size_t n)
{
    free(g.rows);
    g.rows = rows;
    g.n_rows = n;

    set_default_button(false);
    SendMessageW(g.results, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.results, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < n; i++) {
        const struct row *r = &rows[i];
        wchar_t text[GAME_PATH_MAX + GAME_ARGS_MAX + 32];

        int item = add_item(g.results, (int)i, r->game, (LPARAM)i);
        if (r->has_exe) {
            widen(r->exe.path, text, GAME_PATH_MAX);
            set_cell(g.results, item, 1, text);

            char note[GAME_ARGS_MAX + 32];
            snprintf(note, sizeof(note), "%s%s%s%s", r->exe.args[0] ? "args: " : "",
                     r->exe.args, r->exe.args[0] && r->exe.launcher ? ", " : "",
                     r->exe.launcher ? "launcher" : "");
            widen(note, text, (int)(sizeof(text) / sizeof(text[0])));
            set_cell(g.results, item, 2, text);
        } else {
            set_cell(g.results, item, 1, L"(none)");
            set_cell(g.results, item, 2, L"Discord can't detect this game");
        }
    }
    if (n > 0) {
        /* Preselect the first spawnable exe. */
        for (size_t i = 0; i < n; i++) {
            if (rows[i].has_exe) {
                ListView_SetItemState(g.results, (int)i, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g.results, (int)i, FALSE);
                set_default_button(true);
                SetFocus(g.results);  /* Enter now starts the game */
                break;
            }
        }
    }
    SendMessageW(g.results, WM_SETREDRAW, TRUE, 0);
    update_buttons();
}

/* Turn search results into rows: one per exe, or one per game without any. */
static void build_rows(struct search_job *job, const struct game_result *res, size_t n)
{
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        count += res[i].n_exes ? res[i].n_exes : 1;
    }
    job->rows = count ? calloc(count, sizeof(*job->rows)) : NULL;
    if (!job->rows) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        size_t exes = res[i].n_exes ? res[i].n_exes : 1;
        for (size_t j = 0; j < exes; j++) {
            struct row *r = &job->rows[job->n_rows++];
            widen(res[i].name, r->game, (int)(sizeof(r->game) / sizeof(r->game[0])));
            r->has_exe = res[i].n_exes > 0;
            if (r->has_exe) {
                r->exe = res[i].exes[j];
                r->exe.game = NULL;
            }
        }
    }
}

static DWORD WINAPI search_thread(LPVOID param)
{
    struct search_job *job = param;
    struct game_result *res;
    size_t n;
    job->ok = games_search(job->query, GUI_MAX_RESULTS, &res, &n, &job->total);
    if (job->ok) {
        build_rows(job, res, n);
        games_results_free(res, n);
    }
    PostMessageW(g.hwnd, WM_APP_SEARCH_DONE, 0, (LPARAM)job);
    return 0;
}

static void start_search(void)
{
    if (g.searching) {
        return;
    }
    wchar_t wquery[INPUT_MAX];
    GetWindowTextW(g.search_edit, wquery, INPUT_MAX);

    struct search_job *job = calloc(1, sizeof(*job));
    if (!job) {
        return;
    }
    narrow(wquery, job->query, sizeof(job->query));

    /* Trim spaces; an empty search would list everything. */
    char *q = job->query;
    while (*q == ' ') {
        q++;
    }
    size_t len = strlen(q);
    while (len > 0 && q[len - 1] == ' ') {
        q[--len] = '\0';
    }
    if (len == 0) {
        set_status(L"Type the name of a game first.");
        free(job);
        return;
    }
    memmove(job->query, q, len + 1);

    HANDLE thread = CreateThread(NULL, 0, search_thread, job, 0, NULL);
    if (!thread) {
        free(job);
        return;
    }
    CloseHandle(thread);
    g.searching = true;
    set_status(L"Searching...");
    update_buttons();
}

static void search_done(struct search_job *job)
{
    g.searching = false;
    if (job->ok) {
        show_results(job->rows, job->n_rows);
        job->rows = NULL;

        wchar_t text[INPUT_MAX + 128];
        wchar_t wquery[INPUT_MAX];
        widen(job->query, wquery, INPUT_MAX);
        if (job->total == 0) {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                     L"No game matching \"%ls\".", wquery);
        } else if (job->total > GUI_MAX_RESULTS) {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                     L"Showing %d of %lu games - type more of the name to narrow it down.",
                     GUI_MAX_RESULTS, (unsigned long)job->total);
        } else {
            swprintf(text, sizeof(text) / sizeof(text[0]),
                     L"Found %lu game(s). Pick an exe and click Start game.",
                     (unsigned long)job->total);
        }
        set_status(text);
    }
    free(job->rows);
    free(job);
    update_buttons();
}

/* ---- running games ---------------------------------------------------- */

static void refresh_running(void)
{
    struct running_game *games;
    size_t n = procs_list(&games);

    bool same = n == g.n_running;
    for (size_t i = 0; same && i < n; i++) {
        same = games[i].pid == g.running_pids[i];
    }

    if (!same) {
        /* The set of games changed: rebuild, keeping the selection. */
        int sel = selected_item(g.running);
        unsigned long sel_pid = sel >= 0 && (size_t)sel < g.n_running
                                ? g.running_pids[sel] : 0;

        free(g.running_pids);
        g.running_pids = n ? malloc(n * sizeof(*g.running_pids)) : NULL;
        g.n_running = g.running_pids ? n : 0;

        SendMessageW(g.running, WM_SETREDRAW, FALSE, 0);
        SendMessageW(g.running, LVM_DELETEALLITEMS, 0, 0);
        for (size_t i = 0; i < g.n_running; i++) {
            wchar_t path[GAME_PROC_PATH_MAX];
            widen(games[i].path, path, GAME_PROC_PATH_MAX);
            add_item(g.running, (int)i, path, (LPARAM)games[i].pid);
            g.running_pids[i] = games[i].pid;
            if (games[i].pid == sel_pid) {
                ListView_SetItemState(g.running, (int)i, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            }
        }
        SendMessageW(g.running, WM_SETREDRAW, TRUE, 0);
    }

    /* Running times change every second. */
    for (size_t i = 0; i < g.n_running; i++) {
        char up[32];
        wchar_t text[32];
        spawn_format_duration(games[i].seconds, up, sizeof(up));
        widen(up, text, 32);
        set_cell(g.running, (int)i, 1, text);
        swprintf(text, 32, L"%lu", games[i].pid);
        set_cell(g.running, (int)i, 2, text);
    }
    free(games);
    update_buttons();
}

static void stop_pids(const unsigned long *pids, size_t n)
{
    size_t failed = 0;
    for (size_t i = 0; i < n; i++) {
        if (!procs_stop(pids[i])) {
            failed++;
        }
    }
    if (failed) {
        set_status(L"Some games could not be stopped.");
    } else {
        set_status(n == 1 ? L"Stopped the game." : L"Stopped the games.");
    }
    refresh_running();
}

static void stop_selected(void)
{
    int i = selected_item(g.running);
    if (i >= 0 && (size_t)i < g.n_running) {
        unsigned long pid = g.running_pids[i];
        stop_pids(&pid, 1);
    }
}

static void stop_all(void)
{
    if (g.n_running == 0) {
        return;
    }
    size_t n = g.n_running;
    unsigned long *pids = malloc(n * sizeof(*pids));
    if (pids) {
        memcpy(pids, g.running_pids, n * sizeof(*pids));
        stop_pids(pids, n);
        free(pids);
    }
}

/* Delete the spawned games, offering to stop the running ones first. */
static void delete_spawned(void)
{
    char root[MAX_PATH];
    if (!spawn_root(root, sizeof(root)) || !fs_exists(root)) {
        set_status(L"There are no spawned games to delete.");
        return;
    }
    if (g.n_running > 0) {
        int answer = MessageBoxW(g.hwnd,
                                 L"Some games are still running. Stop them and delete "
                                 L"all spawned games?",
                                 L"Delete spawned games", MB_OKCANCEL | MB_ICONQUESTION);
        if (answer != IDOK) {
            return;
        }
        stop_all();
    }

    int deleted = 0;
    int failed = 0;
    fs_delete_tree(root, &deleted, &failed);
    wchar_t text[128];
    if (failed > 0) {
        swprintf(text, 128, L"Deleted %d file(s); %d still in use were skipped.",
                 deleted, failed);
    } else {
        swprintf(text, 128, L"Deleted %d file(s).", deleted);
    }
    set_status(text);
}

/* ---- spawning --------------------------------------------------------- */

static void spawn_with_timer(const char *path, const char *args)
{
    struct spawn_opts opts = { 0 };
    opts.seconds = selected_timer();
    opts.args = args;
    if (spawn_game(path, &opts)) {
        refresh_running();
    }
}

static void spawn_selected(void)
{
    const struct row *r = selected_row();
    if (r && r->has_exe) {
        spawn_with_timer(r->exe.path, r->exe.args);
    }
}

static void show_recent_menu(void)
{
    size_t n = recent_load(g.recent_entries, RECENT_MAX);
    HMENU menu = CreatePopupMenu();
    if (n == 0) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No games spawned yet");
    }
    for (size_t i = 0; i < n; i++) {
        char label[GAME_PATH_MAX + GAME_ARGS_MAX + 16];
        wchar_t wlabel[GAME_PATH_MAX + GAME_ARGS_MAX + 16];
        snprintf(label, sizeof(label), "%s%s%s%s", g.recent_entries[i].path,
                 g.recent_entries[i].args[0] ? "  (args: " : "", g.recent_entries[i].args,
                 g.recent_entries[i].args[0] ? ")" : "");
        widen(label, wlabel, (int)(sizeof(wlabel) / sizeof(wlabel[0])));
        AppendMenuW(menu, MF_STRING, ID_RECENT_FIRST + i, wlabel);
    }

    RECT rc;
    GetWindowRect(g.recent, &rc);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                             rc.left, rc.bottom, 0, g.hwnd, NULL);
    DestroyMenu(menu);
    if (cmd >= ID_RECENT_FIRST && (size_t)(cmd - ID_RECENT_FIRST) < n) {
        const struct recent_entry *e = &g.recent_entries[cmd - ID_RECENT_FIRST];
        spawn_with_timer(e->path, e->args);
    }
}

/* ---- update notice ---------------------------------------------------- */

static void show_version(void)
{
    wchar_t text[128];
    if (g.update_known && g.update.status == UPDATE_NEWER) {
        wchar_t tag[64];
        widen(g.update.tag, tag, 64);
        swprintf(text, 128, L"Update available: %ls (click)", tag);
    } else {
        wchar_t version[32];
        widen(APP_VERSION, version, 32);
        swprintf(text, 128, L"v%ls", version);
    }
    SendMessageW(g.status, SB_SETTEXTW, 1, (LPARAM)text);
}

static void open_update_page(void)
{
    if (g.update_known && g.update.status == UPDATE_NEWER && g.update.url[0]) {
        wchar_t url[256];
        widen(g.update.url, url, 256);
        ShellExecuteW(g.hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    }
}

/* ---- layout ----------------------------------------------------------- */

static void layout(int width, int height)
{
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.status, &sr);
    height -= sr.bottom - sr.top;

    int m = px(12);       /* margin */
    int gap = px(8);
    int label_h = px(20);
    int ctl_h = px(26);
    int btn_w = px(96);
    int inner = width - 2 * m;

    /* Fixed-height rows; the two lists share what is left (60/40). */
    int fixed = m + label_h + ctl_h + gap      /* search */
              + gap + ctl_h + gap              /* timer / spawn row */
              + label_h                        /* running label */
              + gap + ctl_h + gap + label_h + m;  /* stop row, tip */
    int lists = height - fixed;
    if (lists < px(160)) {
        lists = px(160);
    }
    int results_h = lists * 6 / 10;
    int running_h = lists - results_h;

    int y = m;
    MoveWindow(g.labels[0], m, y, inner, label_h, TRUE);
    y += label_h;
    MoveWindow(g.search_edit, m, y, inner - btn_w - gap, ctl_h, TRUE);
    MoveWindow(g.search_button, m + inner - btn_w, y, btn_w, ctl_h, TRUE);
    y += ctl_h + gap;

    MoveWindow(g.results, m, y, inner, results_h, TRUE);
    y += results_h + gap;

    int timer_label_w = px(150);
    MoveWindow(g.labels[1], m, y + px(4), timer_label_w, label_h, TRUE);
    MoveWindow(g.timer, m + timer_label_w, y, px(140), px(200), TRUE);
    int start_w = px(140);
    MoveWindow(g.recent, m + inner - btn_w, y, btn_w, ctl_h, TRUE);
    MoveWindow(g.spawn, m + inner - btn_w - gap - start_w, y, start_w, ctl_h, TRUE);
    y += ctl_h + gap;

    MoveWindow(g.labels[2], m, y, inner, label_h, TRUE);
    y += label_h;
    MoveWindow(g.running, m, y, inner, running_h, TRUE);
    y += running_h + gap;

    MoveWindow(g.stop, m, y, btn_w, ctl_h, TRUE);
    MoveWindow(g.stop_all, m + btn_w + gap, y, btn_w, ctl_h, TRUE);
    MoveWindow(g.del, m + inner - px(170), y, px(170), ctl_h, TRUE);
    y += ctl_h + gap;
    MoveWindow(g.tip, m, y, inner, label_h, TRUE);

    /* Columns follow the list widths (minus room for a scrollbar). */
    int w = inner - GetSystemMetrics(SM_CXVSCROLL) - px(4);
    ListView_SetColumnWidth(g.results, 0, w * 35 / 100);
    ListView_SetColumnWidth(g.results, 1, w * 35 / 100);
    ListView_SetColumnWidth(g.results, 2, w - 2 * (w * 35 / 100));
    ListView_SetColumnWidth(g.running, 0, w * 60 / 100);
    ListView_SetColumnWidth(g.running, 1, w * 25 / 100);
    ListView_SetColumnWidth(g.running, 2, w - w * 60 / 100 - w * 25 / 100);

    int parts[2] = { width - px(220), -1 };
    SendMessageW(g.status, SB_SETPARTS, 2, (LPARAM)parts);
}

static void create_controls(void)
{
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    g.bold_font = CreateFontIndirectW(&ncm.lfMessageFont);

    g.labels[0] = add_control(L"STATIC", L"Search for the game your quest is about:", 0,
                              IDC_SEARCH_LABEL);
    g.search_edit = add_control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                IDC_SEARCH_EDIT);
    g.search_button = add_control(L"BUTTON", L"Search", WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  IDC_SEARCH_BUTTON);
    SendMessageW(g.search_edit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"e.g. World of Warcraft, Minecraft, Roblox");

    DWORD list_style = WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                       LVS_SHOWSELALWAYS;
    DWORD list_ex = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;

    g.results = add_control(WC_LISTVIEWW, L"", list_style, IDC_RESULTS);
    ListView_SetExtendedListViewStyle(g.results, list_ex);
    add_column(g.results, 0, L"Game");
    add_column(g.results, 1, L"Exe Discord looks for");
    add_column(g.results, 2, L"Notes");

    g.labels[1] = add_control(L"STATIC", L"Close the game after:", 0, IDC_TIMER_LABEL);
    g.timer = add_control(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                          IDC_TIMER);
    for (size_t i = 0; i < NUM_TIMERS; i++) {
        SendMessageW(g.timer, CB_ADDSTRING, 0, (LPARAM)TIMERS[i].label);
    }
    SendMessageW(g.timer, CB_SETCURSEL, 0, 0);
    g.spawn = add_control(L"BUTTON", L"Start game", WS_TABSTOP, IDC_SPAWN);
    SendMessageW(g.spawn, WM_SETFONT, (WPARAM)g.bold_font, FALSE);
    g.recent = add_control(L"BUTTON", L"Recent...", WS_TABSTOP, IDC_RECENT);

    g.labels[2] = add_control(L"STATIC", L"Running games:", 0, IDC_RUNNING_LABEL);
    g.running = add_control(WC_LISTVIEWW, L"", list_style, IDC_RUNNING);
    ListView_SetExtendedListViewStyle(g.running, list_ex);
    add_column(g.running, 0, L"Game exe");
    add_column(g.running, 1, L"Running for");
    add_column(g.running, 2, L"PID");

    g.stop = add_control(L"BUTTON", L"Stop", WS_TABSTOP, IDC_STOP);
    g.stop_all = add_control(L"BUTTON", L"Stop all", WS_TABSTOP, IDC_STOP_ALL);
    g.del = add_control(L"BUTTON", L"Delete spawned games", WS_TABSTOP, IDC_DELETE);
    g.tip = add_control(L"STATIC",
                        L"Games start minimized. To stream one, open its window from the "
                        L"taskbar, then pick it in Discord's Go Live.",
                        0, IDC_TIP);

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                               0, 0, 0, 0, g.hwnd, (HMENU)(INT_PTR)IDC_STATUS,
                               GetModuleHandleW(NULL), NULL);
    SendMessageW(g.status, WM_SETFONT, (WPARAM)g.font, FALSE);
}

/* ---- window procedure ------------------------------------------------- */

static LRESULT on_notify(NMHDR *nm)
{
    if (nm->idFrom == IDC_RESULTS) {
        if (nm->code == LVN_ITEMCHANGED) {
            update_buttons();
        } else if (nm->code == NM_DBLCLK) {
            spawn_selected();
        }
    } else if (nm->idFrom == IDC_RUNNING && nm->code == LVN_ITEMCHANGED) {
        update_buttons();
    } else if (nm->idFrom == IDC_STATUS && nm->code == NM_CLICK) {
        NMMOUSE *mouse = (NMMOUSE *)nm;
        if (mouse->dwItemSpec == 1) {
            open_update_page();
        }
    }
    return 0;
}

static void on_command(int id)
{
    switch (id) {
    case IDOK:  /* Enter: search from the box, otherwise the default button */
        if (GetFocus() != g.search_edit && g.start_is_default) {
            spawn_selected();
        } else {
            start_search();
        }
        break;
    case IDC_SEARCH_BUTTON:
        start_search();
        break;
    case IDC_SPAWN:
        spawn_selected();
        break;
    case IDC_RECENT:
        show_recent_menu();
        break;
    case IDC_STOP:
        stop_selected();
        break;
    case IDC_STOP_ALL:
        stop_all();
        break;
    case IDC_DELETE:
        delete_spawned();
        break;
    default:
        break;
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        create_controls();
        return 0;
    case WM_SIZE:
        layout(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = px(560);
        mm->ptMinTrackSize.y = px(480);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SEARCH_EDIT && HIWORD(wp) == EN_SETFOCUS) {
            set_default_button(false);  /* typing a new search: Enter searches */
        }
        on_command(LOWORD(wp));
        return 0;
    case WM_NOTIFY:
        return on_notify((NMHDR *)lp);
    case WM_TIMER:
        if (wp == TIMER_RUNNING) {
            refresh_running();
        } else if (wp == TIMER_UPDATE && update_poll(&g.update)) {
            g.update_known = true;
            KillTimer(hwnd, TIMER_UPDATE);
            show_version();
        }
        return 0;
    case WM_APP_MSG: {
        wchar_t *text = (wchar_t *)lp;
        if (wp == MSG_ERROR) {
            size_t cap = wcslen(text) + 8;
            wchar_t *full = malloc(cap * sizeof(wchar_t));
            if (full) {
                swprintf(full, cap, L"Error: %ls", text);
                set_status(full);
                free(full);
            }
        } else {
            set_status(text);
        }
        free(text);
        return 0;
    }
    case WM_APP_SEARCH_DONE:
        search_done((struct search_job *)lp);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int gui_main(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                              ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(NULL);
    g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);

    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"DiscordQuestCompleterMain";
    if (!RegisterClassW(&wc)) {
        return 1;
    }

    wchar_t title[64];
    widen(APP_NAME, title, 64);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, px(720), px(640),
                                NULL, NULL, instance, NULL);
    if (!hwnd) {
        return 1;
    }

    msg_set_handler(gui_msg_handler);
    show_version();
    set_status(L"Search for a game to get started.");
    refresh_running();
    SetTimer(hwnd, TIMER_RUNNING, RUNNING_REFRESH_MS, NULL);
    update_check_start();
    SetTimer(hwnd, TIMER_UPDATE, 500, NULL);

    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetFocus(g.search_edit);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {  /* Tab between controls, Enter = IDOK */
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    msg_set_handler(NULL);
    DeleteObject(g.font);
    DeleteObject(g.bold_font);
    return 0;
}
