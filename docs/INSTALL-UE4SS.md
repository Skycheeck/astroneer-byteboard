# Installing UE4SS for Byteboard

The mod needs **RE-UE4SS experimental 3.0.1-1183-gcdbc0b66 or newer**. Older builds, including the stable
3.0.1 release, lack `LoopInGameThreadWithDelay`; the mod then logs that the build is too old and does
nothing.

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
   and add the mods folder of your clone (absolute path, forward slashes) under `[Overrides]`:
   ```ini
   +ModsFolderPaths = C:/path/to/astroneer-byteboard/ue4ss/mod
   ```
   The mod finds its files relative to its own location, so it must stay inside the clone.
4. Optional: UE4SS enables its console mods by default (`ConsoleEnablerMod`, `CheatManagerEnablerMod` in
   `ue4ss\Mods\mods.txt`). Set them to `0` if you do not want the in-game console.
5. Start the game. `ue4ss\UE4SS.log` should contain `[ResearchBridge] loaded`.

Back up `%LOCALAPPDATA%\Astro\Saved\SaveGames` before the first run with any mod.

## Uninstall

Close the game, then delete what UE4SS added to `<game>\Astro\Binaries\Win64`:

```powershell
$w = '<game>\Astro\Binaries\Win64'
Remove-Item "$w\dwmapi.dll" -Confirm:$false
Remove-Item "$w\ue4ss" -Recurse -Confirm:$false
```

To keep UE4SS but disable only this mod, remove the `+ModsFolderPaths` line or set
`ResearchBridge : 0` in `ue4ss/mod/mods.txt`. Items researched with the overlay stay researched in your
save, as if you had used the game's catalog.
