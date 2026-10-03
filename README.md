# Discord Quest Completer

A tiny C utility for Windows. Some Discord quests ask you to **play a game**, and
to verify them Discord only checks whether a **process whose executable is named
after that game is running** — it doesn't check that the real game is installed
or actually being played.

This tool exploits that: it asks you for a name, copies itself to `<name>.exe`,
and launches that copy as a long-running process. Discord then sees a process
with the game's executable name running, and the "play the game" step completes.

## Usage

1. Run `spawner.exe`.
2. When prompted, type the game's executable name **without** `.exe`
   (e.g. for a quest about *League of Legends*, type `LeagueClient`).
3. A new console window opens running `<name>.exe`. Leave it open while Discord
   checks the quest.
4. Close that window when you're done. The `<name>.exe` file stays on disk;
   delete it yourself if you don't want to keep it.

> Finding the right name: Discord looks for the same executable name the real
> game registers. Check the quest details or the game's install folder for the
> exact `.exe` name, then type that (minus the `.exe`).

## Note

Using this to complete quests you didn't actually earn may violate Discord's
Terms of Service and could put your account at risk. Use it at your own
discretion.
