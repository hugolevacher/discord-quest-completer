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

/* Follows CHILD_FLAG: the number of seconds after which the copy exits. */
#define CHILD_TIMER_FLAG "--for"

/* Longest line accepted at the prompt, including the newline. */
#define INPUT_MAX 512

/* Discord's public list of the games it can detect by process. */
#define GAMES_HOST L"discord.com"
#define GAMES_PATH L"/api/v9/applications/detectable"
#define USER_AGENT L"discord-quest-completer/1.0"

/* The list is cached next to the exe and re-downloaded when older than this. */
#define GAMES_CACHE_FILE "detectable_games.json"
#define GAMES_CACHE_MAX_AGE_SECONDS (24 * 60 * 60)

/* Most games the find command prints for one search. */
#define FIND_MAX_RESULTS 10

#endif /* CONFIG_H */
