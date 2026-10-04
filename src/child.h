/*
 * child.h - what a spawned copy runs: a window standing in for the game.
 */
#ifndef CHILD_H
#define CHILD_H

/*
 * Entry point for a spawned copy (started with CHILD_FLAG, without a console).
 * Opens the game's window and runs until it is closed or its timer
 * (CHILD_TIMER_FLAG) runs out. Returns the process exit code.
 */
int child_main(int argc, char **argv);

#endif /* CHILD_H */
