# Discord Quest Completer

Completes Discord's "play a game" and "stream a game" quests without the game.
Discord only checks that a process with the game's exe path is running, so this
tool copies itself to that path (for example `_retail_/wow.exe`) and runs the
copy as a small window.

Windows only. Download `spawner.exe` from the
[latest release](https://github.com/hugolevacher/discord-quest-completer/releases/latest);
no install needed.

## Quick start

1. Double-click `spawner.exe`.
2. Search for the game your quest is about, e.g. `world of warcraft`.
3. Pick an exe and click **Start game**. Optionally pick when it should close.
4. The game's window opens minimized in the taskbar. Leave it running until
   the quest completes.

To **stream** a game, restore its window from the taskbar and pick it in
Discord's Go Live.

If a game shows "Discord can't detect this game", it has no exe in Discord's
list, so this tool can't complete its quest.

## Command line

Run `spawner.exe` from a terminal for a prompt with the same features, or give
a command directly: `spawner.exe spawn _retail_/wow.exe 15m`. `--gui` and
`--cli` force the window or the prompt.

### Commands

| Command | What it does |
|---|---|
| `f` / `find <name> [time]` | Search Discord's game list and pick an exe to spawn |
| `s` / `spawn <path> [time]` | Spawn an exe path directly, e.g. `spawn _retail_/wow.exe` |
| `rc` / `recent [time]` | Spawn a game you used before |
| `l` / `list` | Show running games and how long they've run |
| `x` / `stop [all]` | Stop one running game, or all of them |
| `d` / `delete` | Delete spawned games (offers to stop running ones) |
| `r` / `refresh` | Re-download Discord's game list |
| `u` / `update` | Check for a newer version |
| `c` / `clear` | Clear the screen |
| `h` / `help` | Show the commands |
| `q` / `quit` | Exit (games keep running) |

- **Time:** add `90s`, `15m`, `2h` or `1h30m` and the game closes itself when
  it's up, e.g. `find minecraft 15m`.
- Games Discord detects by their launch arguments (Minecraft, Team Fortress 2...)
  are started with those arguments.

## Files

Everything the tool saves goes in a `Discord Quest Completer data` folder next
to `spawner.exe`: the game list (refreshed daily), your recent games, and the
spawned games. Delete the folder to remove it all.

## Note

Completing quests this way may violate Discord's Terms of Service. Use at your
own risk.
