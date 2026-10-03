/*
 * menu.h - arrow-key selection menu for the console.
 */
#ifndef MENU_H
#define MENU_H

#include <stddef.h>

/* Write the text of item i into buf (at most cap bytes, NUL included). */
typedef void (*menu_label_fn)(size_t i, char *buf, size_t cap, void *ctx);

/*
 * Show title and n items; the user moves with Up/Down (Home/End) and picks
 * with Enter. Returns the chosen index, or -1 if cancelled with Esc. Returns
 * -1 without showing anything when n is 0 or input isn't an interactive
 * console (e.g. piped).
 */
int menu_pick(const char *title, size_t n, menu_label_fn label, void *ctx);

#endif /* MENU_H */
