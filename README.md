# CoC DCotE — Sprint Mod

A small **ASI plugin** that adds a **sprint** (run) key to *Call of Cthulhu: Dark Corners of the Earth* (2006) — the game has no run-on-demand, so this lets you move faster on a hotkey, either **hold** or **toggle**.

It works **without touching the game `.exe`** (the Steam build is SteamStub-protected — patching the exe breaks it). Instead the plugin locates the player movement-speed values **in memory at runtime** and scales them while sprint is active.

## Install

1. You need an **ASI loader** in the game's `Engine` folder. The easiest one ships with [ThirteenAG's WidescreenFix](https://thirteenag.github.io/wfp) for this game (`dinput8.dll` = Ultimate ASI Loader).
2. Drop **`Sprint.asi`** and **`Sprint.ini`** into:
   `...\steamapps\common\Call of Cthulhu\Engine\scripts\`
3. Launch the game. Press your sprint key.

## Configuration — `Sprint.ini`

```ini
[Sprint]
Key=20          ; Virtual-Key code (decimal). 20=CapsLock, 160=Left Shift, 162=Right Ctrl, 9=Tab
Toggle=1        ; 1 = press to toggle on/off, 0 = hold-to-sprint
Multiplier=1.7  ; speed multiplier while sprinting
ManualWalkVA=0  ; 0 = auto-find the speed in memory. Set an absolute address only if auto-find fails.
```

Changes apply on game restart (the ini is read at load).

## How it works

The game parses an embedded XML config into a struct of floats:
`Crawl=180, Walk=380, Run=560, Jump=590, Climb=25`.

On startup the plugin scans committed memory for the **Walk (380.0)** value whose neighbours match the rest of that signature (`Run/Jump/Climb`), which uniquely identifies the live speed struct. While sprint is active it writes `Walk * Multiplier` (and Run) there; on release/toggle-off it restores the originals. The exe has **no ASLR** (fixed ImageBase `0x400000`), so the located address is stable.

A log is written to `Sprint.log` next to the plugin (handy for debugging the auto-find).

> ⚠️ **Turn sprint OFF during scripted sequences** (chases, the hotel escape, stealth scripts). The engine ties some scripts to movement timing, and over-speeding can desync triggers.

## Build

Requires MSVC (Build Tools / Visual Studio), **32-bit** (the game is x86):

```bat
build.bat
```

`build.bat` calls `vcvars32.bat` then:
```
cl /LD /O2 /MT Sprint.cpp /Fe:Sprint.asi user32.lib
```
Adjust the Visual Studio path inside `build.bat` to match your install.

## Credits & license

Made for the community as part of a full DCotE setup guide.
ASI loading via [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
Released under the [MIT License](LICENSE).
