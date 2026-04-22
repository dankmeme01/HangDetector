# Hang Detector

This mod aims to detect a big and relatively common issue - closure hangs. These are most often caused by mods doing <cr>unsafe</c> things in static destructors, causing the game to lock up and freeze forever when closing it. Such issues can be very difficult to debug, and they aren't even always obvious - they may simply leave the game running forever in background after it seems like it closed.

## How it works

Hang detector bundles a small program called <cy>watchdog</c>, that is launched alongside Geometry Dash. Using IPC, this program observes the state of the game, and most of the time sits idle. When it detects that the game has hung and no longer responds to requests, it begins to investigate.

Watchdog gives the game a 2.5 second grace period. This should be enough for most sane static destructors to run. If the game still hasn't closed after that time passes, the game is killed and certain debug information is collected; rather than letting it potentially run forever.

## Credits

[Prevter](https://github.com/Prevter/) - for making the logo!
