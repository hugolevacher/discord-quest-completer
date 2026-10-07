/*
 * commands.h - built-in commands of the interactive prompt.
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>

/* What the prompt loop should do after a command runs. */
typedef enum {
    CMD_CONTINUE,
    CMD_EXIT
} cmd_result;

/*
 * If input's first word names a built-in command (short or long form, any
 * case), run it with the rest of the line as its argument, store what the loop
 * should do next in *result and return true. Otherwise return false: input is
 * not a command.
 */
bool command_run(const char *input, cmd_result *result);

/* Print a one-line notice once the startup update check finds a newer release. */
void command_update_notice(void);

#endif /* COMMANDS_H */
