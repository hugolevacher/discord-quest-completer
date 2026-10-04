/*
 * child.c - what a spawned copy runs: a window standing in for the game.
 *
 * Discord counts a process as a running game only if it has a real window: a
 * classic console window doesn't count (one hosted by Windows Terminal happens
 * to, but that depends on the user's default terminal). So the copy runs
 * without a console and opens its own window. Being a normal window, it can
 * also be streamed with Go Live; a small animation keeps the stream moving.
 */
#include "child.h"

#include "config.h"
#include "spawn.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS 50         /* redraw interval: 20 frames a second */
#define SWEEP_MS 3000       /* the moving block crosses the window in this long */
#define WINDOW_WIDTH 560    /* initial size, at 100% display scaling */
#define WINDOW_HEIGHT 300

static const char WINDOW_CLASS[] = "DiscordQuestGameWindow";

static struct {
    char name[MAX_PATH];  /* exe file name, e.g. "wow.exe" */
    char path[MAX_PATH];  /* full exe path */
    DWORD start;          /* GetTickCount() when the window opened */
    unsigned long limit;  /* seconds to run for; 0 = until closed */
    int dpi;              /* screen DPI, to scale sizes */
    HFONT title_font;
    HFONT text_font;
    HFONT small_font;
} g;

/* Scale a size given at 100% display scaling to the screen's DPI. */
static int px(int value)
{
    return MulDiv(value, g.dpi, 96);
}

static HFONT make_font(int height, int weight)
{
    return CreateFontA(-px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
}

/* Draw one line of text at (x, *y) and move *y below it. */
static void draw_line(HDC dc, HFONT font, COLORREF color, int x, int *y, int right,
                      const char *text)
{
    SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT r = { x, *y, right, *y + px(200) };
    DrawTextA(dc, text, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX |
                                DT_END_ELLIPSIS | DT_CALCRECT);
    r.right = right;
    DrawTextA(dc, text, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX |
                                DT_END_ELLIPSIS);
    *y = r.bottom + px(6);
}

static void fill(HDC dc, int left, int top, int right, int bottom, COLORREF color)
{
    RECT r = { left, top, right, bottom };
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void draw(HDC dc, int width, int height)
{
    DWORD ms = GetTickCount() - g.start;
    unsigned long seconds = ms / 1000;

    fill(dc, 0, 0, width, height, RGB(30, 31, 34));
    SetBkMode(dc, TRANSPARENT);

    int margin = px(24);
    int y = margin;
    char text[128];
    char time[32];

    draw_line(dc, g.title_font, RGB(242, 243, 245), margin, &y, width - margin, g.name);

    spawn_format_duration(seconds, time, sizeof(time));
    snprintf(text, sizeof(text), "Running for %s", time);
    draw_line(dc, g.text_font, RGB(219, 222, 225), margin, &y, width - margin, text);

    if (g.limit > 0) {
        unsigned long left = seconds < g.limit ? g.limit - seconds : 0;
        spawn_format_duration(left, time, sizeof(time));
        snprintf(text, sizeof(text), "Closes itself in %s", time);
    } else {
        snprintf(text, sizeof(text), "Close this window to stop the game");
    }
    draw_line(dc, g.text_font, RGB(219, 222, 225), margin, &y, width - margin, text);

    snprintf(text, sizeof(text), "PID %lu", (unsigned long)GetCurrentProcessId());
    draw_line(dc, g.small_font, RGB(148, 155, 164), margin, &y, width - margin, text);
    draw_line(dc, g.small_font, RGB(148, 155, 164), margin, &y, width - margin, g.path);

    /* A block sweeping back and forth along the bottom, so a stream visibly moves. */
    int track_top = height - margin - px(8);
    int block = px(60);
    int travel = width - 2 * margin - block;
    if (travel > 0) {
        DWORD phase = ms % (2 * SWEEP_MS);
        DWORD along = phase < SWEEP_MS ? phase : 2 * SWEEP_MS - phase;
        int x = margin + (int)((long long)travel * along / SWEEP_MS);
        fill(dc, margin, track_top, width - margin, track_top + px(8), RGB(43, 45, 49));
        fill(dc, x, track_top, x + block, track_top + px(8), RGB(88, 101, 242));
    }
}

/* Paint through an off-screen bitmap so the animation doesn't flicker. */
static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old_bitmap = SelectObject(mem, bitmap);
    HGDIOBJ old_font = SelectObject(mem, g.text_font);

    draw(mem, rc.right, rc.bottom);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_font);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (g.limit > 0 && (GetTickCount() - g.start) / 1000 >= g.limit) {
            DestroyWindow(hwnd);  /* time is up */
        } else {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;  /* paint() covers the whole window */
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = px(320);
        mm->ptMinTrackSize.y = px(200);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

int child_main(int argc, char **argv)
{
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], CHILD_TIMER_FLAG) == 0) {
            g.limit = strtoul(argv[i + 1], NULL, 10);
        }
    }

    GetModuleFileNameA(NULL, g.path, sizeof(g.path));
    const char *name = strrchr(g.path, '\\');
    snprintf(g.name, sizeof(g.name), "%s", name ? name + 1 : g.path);

    /* Draw at the screen's real resolution instead of being blurrily scaled. */
    SetProcessDPIAware();
    HDC screen = GetDC(NULL);
    g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);

    g.title_font = make_font(28, FW_BOLD);
    g.text_font = make_font(17, FW_NORMAL);
    g.small_font = make_font(13, FW_NORMAL);

    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassA(&wc)) {
        return 1;
    }

    /* Titled after the exe, like a real game window. */
    HWND hwnd = CreateWindowExA(0, WINDOW_CLASS, g.name, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                px(WINDOW_WIDTH), px(WINDOW_HEIGHT),
                                NULL, NULL, instance, NULL);
    if (!hwnd) {
        return 1;
    }

    g.start = GetTickCount();
    SetTimer(hwnd, 1, FRAME_MS, NULL);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);  /* don't steal focus from the prompt */

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    DeleteObject(g.title_font);
    DeleteObject(g.text_font);
    DeleteObject(g.small_font);
    return 0;
}
