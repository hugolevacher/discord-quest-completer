/*
 * config.h - project-wide constants.
 */
#ifndef CONFIG_H
#define CONFIG_H

#define APP_NAME "Discord Quest Completer"

/* This build's version; compared with the latest GitHub release tag (vX.Y.Z). */
#ifndef APP_VERSION
#define APP_VERSION "1.2.0"
#endif

/* Where the update check looks for the latest release. */
#define UPDATE_HOST L"api.github.com"
#define UPDATE_PATH L"/repos/hugolevacher/discord-quest-completer/releases/latest"
#define UPDATE_CHECK_FILE "latest_release.json"

/*
 * Everything the tool saves (the games list, recent games, the spawned games)
 * lives in this folder next to the exe ("Discord Quest Completer data"), so the
 * exe's own folder stays tidy. It is created when first needed.
 */
#define DATA_DIR APP_NAME " data"

/*
 * Every spawned exe and its folder structure is created under this folder,
 * inside DATA_DIR, so cleanup is just "delete this folder" -- which is what
 * the delete command does.
 */
#define SPAWN_DIR "spawned_games"

/* Force the window or the prompt, whichever way the exe was started. */
#define GUI_FLAG "--gui"
#define CLI_FLAG "--cli"

/* Argument that makes a spawned copy run as a dummy "game" process. */
#define CHILD_FLAG "--child"

/* Follows CHILD_FLAG: the number of seconds after which the copy exits. */
#define CHILD_TIMER_FLAG "--for"

/* Longest spawn path (relative to SPAWN_DIR) the list command reports. */
#define GAME_PROC_PATH_MAX 260

/* Longest line accepted at the prompt, including the newline. */
#define INPUT_MAX 512

/* Discord's public list of the games it can detect by process. */
#define GAMES_HOST L"discord.com"
#define GAMES_PATH L"/api/v9/applications/detectable"
#define USER_AGENT L"discord-quest-completer"

/* The list is cached in DATA_DIR and re-downloaded when older than this. */
#define GAMES_CACHE_FILE "detectable_games.json"
#define GAMES_CACHE_MAX_AGE_SECONDS (24 * 60 * 60)

/* Games spawned before, remembered in DATA_DIR for the recent command. */
#define RECENT_FILE "recent_games.txt"
#define RECENT_MAX 10

/* Most games the find command prints for one search. */
#define FIND_MAX_RESULTS 10

#endif /* CONFIG_H */
