/*
 * msg.c - messages from the core code to the user.
 */
#include "msg.h"

#include <stdarg.h>
#include <stdio.h>

static void console_handler(enum msg_kind kind, const char *text)
{
    if (kind == MSG_ERROR) {
        fprintf(stderr, "error: %s\n", text);
    } else {
        printf("%s\n", text);
        fflush(stdout);
    }
}

static msg_handler g_handler = console_handler;

void msg_set_handler(msg_handler handler)
{
    g_handler = handler ? handler : console_handler;
}

static void send(enum msg_kind kind, const char *fmt, va_list args)
{
    char text[1024];
    vsnprintf(text, sizeof(text), fmt, args);
    g_handler(kind, text);
}

void msg_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    send(MSG_INFO, fmt, args);
    va_end(args);
}

void msg_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    send(MSG_ERROR, fmt, args);
    va_end(args);
}
