# Development

## Build

```bat
overlay\build.cmd
```

It configures CMake with the newest Visual Studio that has the C++ x64 tools and builds Release into
`overlay\bin`: `ResearchOverlay.dll` (the game DLL), `ResearchOverlayPreview.dll` and `preview.exe`. The build
needs only the Dear ImGui submodule; the game data is read at run time.

The mod copies `overlay\bin\ResearchOverlay.dll` to `ue4ss\out\ResearchOverlay-active.dll` and loads the copy,
so you can rebuild while the game runs. The new build takes effect after a game restart (the DLL pins
itself and cannot be reloaded in a running game).

## Regenerate game data

```powershell
$env:ASTRONEER_PAK = '<game>\Astro\Content\Paks\pakchunk0-WindowsNoEditor.pak'
node tools/extract.mjs
```

Rerun it after a game patch. A single step runs with `node tools/steps/<step>.mjs`. The groups and the
rules for which items are research entries are in `tools/lib/taxonomy.mjs`.

## Preview without the game

```bash
overlay/bin/preview.exe out.bmp "" 1600 900                          # catalog, nothing selected
overlay/bin/preview.exe out.bmp Solar_Small 1600 900                 # catalog with an item selected
overlay/bin/preview.exe out.bmp printer:ItemCategory_SmFab 1600 900  # a printer list
overlay/bin/preview.exe --unload-test                                # the DLL survives FreeLibrary
```

`preview.exe` opens a D3D11 window with the game's window class, loads `ResearchOverlayPreview.dll` and
saves one frame. It reads the same `ue4ss/out` files as the game overlay, so it shows the last state the
mod wrote. Run it from Git Bash: Windows PowerShell 5.1 drops an empty `""` argument.

## Checks before a commit

- `overlay\build.cmd` builds without warnings in `overlay/src`.
- `preview.exe --unload-test` passes; previews of the catalog, an item and each printer look right.
- `dumpbin /exports overlay\bin\ResearchOverlay.dll` shows no exports.
- Every `.lua` file parses (for example with the `luaparse` npm package, Lua 5.3 mode) and uses only UE4SS
  globals and Lua built-ins; every game-object access is preceded by `valid()`.
- `node tools/extract.mjs` runs without errors.

## In-game checklist

The mod can only be tested in the game. After a change:

1. `ue4ss\UE4SS.log` shows `[ResearchBridge] loaded`; `ue4ss/out/state.json` updates every second.
2. Opening and closing the backpack catalog shows and hides the catalog window.
3. Opening a printer panel and the backpack (Q) shows the printer list; closing hides it.
4. Selecting a recipe moves the printer to it; printing starts.
5. Research is refused while «Изучение разрешено» is off, and works when it is on.
6. `ue4ss/out/events.log` has no errors.

## Logs

| File | Written by |
|---|---|
| `ue4ss/out/events.log` | mod: panels, signals, errors |
| `ue4ss/out/commands.log` | mod: every command and its result |
| `ue4ss/out/overlay.log` | overlay: start, hooks, faults |
| `<game>\Astro\Binaries\Win64\ue4ss\UE4SS.log` | UE4SS |
