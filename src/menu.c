/*
 * menu.c - arrow-key selection menu for the console.
 *
 * The items are printed once as normal lines (scrolling the screen if needed),
 * then only the rows whose highlight changes are redrawn in place.
 */
#include "menu.h"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

#define MENU_LABEL_MAX 512

struct menu {
    HANDLE out;
    WORD normal;  /* console colours to restore */
    SHORT top;    /* screen row of the first item */
    int width;    /* columns available for an item's text */
    menu_label_fn label;
    void *ctx;
};

/* Swap foreground and background colours for the highlighted row. */
static WORD inverted(WORD attr)
{
    return (WORD)((attr & 0xFF00) | ((attr & 0x0F) << 4) | ((attr & 0xF0) >> 4));
}

static void draw_item(const struct menu *m, size_t i, bool selected)
{
    char text[MENU_LABEL_MAX];
    m->label(i, text, sizeof(text), m->ctx);

    COORD pos = { 0, (SHORT)(m->top + (SHORT)i) };
    SetConsoleCursorPosition(m->out, pos);
    SetConsoleTextAttribute(m->out, selected ? inverted(m->normal) : m->normal);
    printf("%s %.*s", selected ? ">" : " ", m->width, text);
    fflush(stdout);
    SetConsoleTextAttribute(m->out, m->normal);
}

int menu_pick(const char *title, size_t n, menu_label_fn label, void *ctx)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD in_mode;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (n == 0 || !GetConsoleMode(in, &in_mode) ||
        !GetConsoleScreenBufferInfo(out, &csbi)) {
        return -1;
    }

    struct menu m;
    m.out = out;
    m.normal = csbi.wAttributes;
    m.width = csbi.dwSize.X - 3;  /* 2 for the marker, 1 so lines never wrap */
    if (m.width < 10) {
        m.width = 10;
    }
    m.label = label;
    m.ctx = ctx;

    /* First pass: plain lines, so the console scrolls to fit the whole menu. */
    printf("\n%s\n", title);
    char text[MENU_LABEL_MAX];
    for (size_t i = 0; i < n; i++) {
        label(i, text, sizeof(text), ctx);
        printf("  %.*s\n", m.width, text);
    }
    fflush(stdout);

    GetConsoleScreenBufferInfo(out, &csbi);
    int top = csbi.dwCursorPosition.Y - (int)n;
    m.top = (SHORT)(top > 0 ? top : 0);

    /* Hide the blinking cursor while the menu is active. */
    CONSOLE_CURSOR_INFO cursor;
    bool have_cursor = GetConsoleCursorInfo(out, &cursor);
    if (have_cursor) {
        CONSOLE_CURSOR_INFO hidden = cursor;
        hidden.bVisible = FALSE;
        SetConsoleCursorInfo(out, &hidden);
    }

    /* Drop keys typed while the results were loading. */
    FlushConsoleInputBuffer(in);

    size_t sel = 0;
    int result = -1;
    bool done = false;
    draw_item(&m, sel, true);

    while (!done) {
        INPUT_RECORD rec;
        DWORD got = 0;
        if (!ReadConsoleInputW(in, &rec, 1, &got)) {
            break;
        }
        if (got == 0 || rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) {
            continue;
        }

        size_t next = sel;
        switch (rec.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:
            next = sel > 0 ? sel - 1 : n - 1;  /* wrap to the bottom */
            break;
        case VK_DOWN:
            next = sel + 1 < n ? sel + 1 : 0;  /* wrap to the top */
            break;
        case VK_HOME:
            next = 0;
            break;
        case VK_END:
            next = n - 1;
            break;
        case VK_RETURN:
            result = (int)sel;
            done = true;
            break;
        case VK_ESCAPE:
            done = true;
            break;
        default:
            break;
        }

        if (next != sel) {
            draw_item(&m, sel, false);
            draw_item(&m, next, true);
            sel = next;
        }
    }

    /* Park the cursor below the menu and restore it. */
    COORD below = { 0, (SHORT)(m.top + (SHORT)n) };
    SetConsoleCursorPosition(out, below);
    if (have_cursor) {
        SetConsoleCursorInfo(out, &cursor);
    }
    if (result < 0) {
        printf("Cancelled.\n");
    }
    return result;
}
