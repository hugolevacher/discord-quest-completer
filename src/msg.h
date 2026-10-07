/*
 * msg.h - messages from the core code (search, spawning, downloads) to the user.
 *
 * The core never prints directly: it calls msg_info / msg_error, and whichever
 * interface is running decides where they go. The default handler prints to
 * the console; the window interface installs its own.
 */
#ifndef MSG_H
#define MSG_H

enum msg_kind {
    MSG_INFO,
    MSG_ERROR,
};

/* Receives one message: a full sentence, no trailing newline, no "error:" prefix. */
typedef void (*msg_handler)(enum msg_kind kind, const char *text);

/* Route messages to handler; NULL restores the console default. */
void msg_set_handler(msg_handler handler);

void msg_info(const char *fmt, ...);
void msg_error(const char *fmt, ...);

#endif /* MSG_H */
