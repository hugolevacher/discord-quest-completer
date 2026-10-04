# Discord Quest Completer

A small C tool for Windows. Some Discord quests ask you to **play a game**, and
to verify them Discord checks whether a **process with that game's executable
path is running**. It doesn't check that the real game is installed or actually
being played.

This tool takes advantage of that. It copies itself to the path Discord looks
for (for example `_retail_/wow.exe`) and runs that copy as a long-running
process, so Discord sees the game as running and the "play the game" step
completes.

## Usage

Run `spawner.exe`. It opens a prompt where you can spawn as many games as you
like in one session.

### Find a game and spawn it

1. Type `find` followed by the game's name, e.g. `find world of warcraft`.
   You can also search by exe name, e.g. `find wow`.
2. The matching games are listed with the exe paths Discord looks for.
3. Pick a path with the **Up/Down** arrow keys and press **Enter** to spawn it
   (**Esc** cancels).
4. A new window opens running the game's exe. Leave it open while Discord checks
   the quest, and close it when you're done.

The first search downloads Discord's official list of detectable games
(about 12 MB) and saves it next to the exe. It is downloaded again
automatically once it's more than a day old.

> If a game shows **"none - Discord can't detect this game by its process"**,
> Discord has no exe registered for it, so this tool can't complete its quest.

### Spawn a path directly

If you already know the path, use `spawn` (or `s`):

```
> spawn _retail_/wow.exe
> s RobloxPlayerBeta
```

Use `/` or `\`, and the `.exe` is optional. Folders in the path are created
for you, because Discord matches the **end of the full path** (the folder and
the exe name), not just the file name.

### Commands

| Command | What it does |
|---|---|
| `h` / `help` | Show the commands |
| `s` / `spawn <path>` | Spawn a game by its exe path |
| `f` / `find <name>` | Search Discord's game list, then pick a result to spawn |
| `r` / `refresh` | Download Discord's game list again now |
| `d` / `delete` | Delete every spawned game (skips any that are still running) |
| `c` / `clear` | Clear the screen |
| `q` / `quit` | Exit (spawned game windows keep running) |

### Cleaning up

Everything the tool spawns goes into a `spawned_games` folder next to
`spawner.exe`. Close the game windows, then run `d` to delete it, or delete the
folder yourself. If a game is still running, `d` skips it, deletes the rest and
tells you.

## Note

Using this to complete quests you didn't actually earn may violate Discord's
Terms of Service and could put your account at risk. Use it at your own
discretion.
