# Installing UE4SS and Byteboard

The mod needs **RE-UE4SS experimental 3.0.1-1183-gcdbc0b66 or newer**. Older builds, including the stable
3.0.1 release, lack `LoopInGameThreadWithDelay`; the mod then logs that the build is too old and does
nothing. UE4SS installed by mod managers (AstroModLoader Classic, the Vortex extension) is an older
3.0.1 build; Byteboard has not been tested with it.

## Install

1. Download the experimental build from the
   [RE-UE4SS releases](https://github.com/UE4SS-RE/RE-UE4SS/releases) page (the `UE4SS_v3.0.1-*.zip`
   asset, not the `zDEV` one).
2. Extract it into `<game>\Astro\Binaries\Win64`, next to `Astro-Win64-Shipping.exe`. You get `dwmapi.dll`
   and a `ue4ss\` folder. If another tool already uses a `dwmapi.dll` proxy there, resolve that first.
3. In `ue4ss\UE4SS-settings.ini` set:
   ```ini
   [EngineVersionOverride]
   MajorVersion = 4
   MinorVersion = 27
   ```
4. Copy the `Byteboard` folder from the release archive into `<game>\Astro\Binaries\Win64\ue4ss\Mods\`,
   so that `ue4ss\Mods\Byteboard\Byteboard.dll` exists. Its `enabled.txt` turns it on; no `mods.txt`
   entry is needed.
5. Optional: UE4SS enables its console mods by default (`ConsoleEnablerMod`, `CheatManagerEnablerMod` in
   `ue4ss\Mods\mods.txt`). Set them to `0` if you do not want the in-game console.
6. Start the game. `ue4ss\UE4SS.log` should contain `[Byteboard] loaded`. On the first start, and after
   each game update, the mod reads the item data from your game files into `Byteboard\data` (a few
   seconds; `Byteboard\out\overlay.log` shows the result).

Back up `%LOCALAPPDATA%\Astro\Saved\SaveGames` before the first run with any mod.

To run the mod from a clone of the repository instead, see [DEVELOPMENT.md](DEVELOPMENT.md).

## Uninstall

To remove only Byteboard, close the game and delete `ue4ss\Mods\Byteboard`. Items researched with the
overlay stay researched in your save, as if you had used the game's catalog.

To remove UE4SS as well, delete what it added to `<game>\Astro\Binaries\Win64`:

```powershell
$w = '<game>\Astro\Binaries\Win64'
Remove-Item "$w\dwmapi.dll" -Confirm:$false
Remove-Item "$w\ue4ss" -Recurse -Confirm:$false
```
