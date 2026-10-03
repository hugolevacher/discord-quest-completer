/*
 * config.h - project-wide constants.
 */
#ifndef CONFIG_H
#define CONFIG_H

#define APP_NAME "Game process spawner"

/*
 * Every spawned exe and its folder structure is created under this folder,
 * next to the spawner exe (auto-created if missing), so cleanup is just
 * "delete this folder" -- which is what the delete command does.
 */
#define SPAWN_DIR "spawned_games"

/* Argument that makes a spawned copy run as a dummy "game" process. */
#define CHILD_FLAG "--child"

/* Longest line accepted at the prompt, including the newline. */
#define INPUT_MAX 512

#endif /* CONFIG_H */
