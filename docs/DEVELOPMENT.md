# Development

## Repository layout

| Path | What |
|---|---|
| `mod/Byteboard` | the mod folder exactly as it ships: `enabled.txt`, `README.txt`, `Scripts/` (Lua). The build copies `Byteboard.dll` here; the game creates `in/`, `out/`, `data/` (all ignored by git) |
| `mod/mods.txt` | for UE4SS when it loads `mod/` through `+ModsFolderPaths` |
| `overlay/` | the DLL: `src/` (overlay), `extract/` (game data extraction), `preview/`, Dear ImGui and zlib submodules |
| `tools/` | the Node reference extractor and `package.mjs` |

## Build

```bat
git submodule update --init
overlay\build.cmd
```

It configures CMake with the newest Visual Studio that has the C++ x64 tools and builds Release into
`overlay\bin`: `Byteboard.dll` (the game DLL), `ByteboardPreview.dll`, `preview.exe` and
`byteboard-extract.exe`, then copies `Byteboard.dll` into `mod\Byteboard`. The game keeps the loaded DLL
locked, so close the game before you build; a new build takes effect at the next game start.

## Play from the repository

Install UE4SS ([INSTALL-UE4SS.md](INSTALL-UE4SS.md)) and, instead of copying the mod into `ue4ss\Mods`,
add the repository's mods folder (absolute path, forward slashes) under `[Overrides]` in
`ue4ss\UE4SS-settings.ini`:

```ini
+ModsFolderPaths = C:/path/to/astroneer-byteboard/mod
```

Do not keep a copy in `ue4ss\Mods\Byteboard` at the same time.

## Game data

In the game the DLL extracts the data itself (`overlay/src/gamedata.cpp`, `overlay/extract/`). Outside the
game, for `preview.exe`, write it into `mod\Byteboard\data` with either extractor:

```powershell
overlay\bin\byteboard-extract.exe '<game>\Astro\Content\Paks\pakchunk0-WindowsNoEditor.pak' mod\Byteboard\data
$env:ASTRONEER_PAK = '<game>\Astro\Content\Paks\pakchunk0-WindowsNoEditor.pak'; node tools/extract.mjs
```

Both must produce identical files; after any change to either extractor, run both and compare the three
files byte for byte. A single Node step runs with `node tools/steps/<step>.mjs`. The groups and the rules
for which items are research entries are in `tools/lib/taxonomy.mjs` and its C++ port in `overlay/extract`.

## Preview without the game

```bash
overlay/bin/preview.exe out.bmp "" 1600 900                          # catalog, nothing selected
overlay/bin/preview.exe out.bmp Solar_Small 1600 900                 # catalog with an item selected
overlay/bin/preview.exe out.bmp printer:ItemCategory_SmFab 1600 900  # a printer list
overlay/bin/preview.exe --unload-test                                # the DLL survives FreeLibrary
```

`preview.exe` opens a D3D11 window with the game's window class, loads `ByteboardPreview.dll` and saves one
frame. It reads the files in `mod/Byteboard`, so it shows the last state the mod wrote. Run it from Git
Bash: Windows PowerShell 5.1 drops an empty `""` argument.

## Release

```bash
node tools/package.mjs        # dist/Byteboard-<version>.zip from overlay/bin/Byteboard.dll
```

The version is in `overlay/CMakeLists.txt` (`project(... VERSION x.y.z)`) and in
`mod/Byteboard/Scripts/main.lua` (`VERSION`); the script refuses to pack when they differ. The GitHub
workflow builds and packs on pushes to `main` and on pull requests; a tag `vX.Y.Z` also creates a draft
release with the archive.

## Checks before a commit

- `overlay\build.cmd` builds without warnings in `overlay/src` and `overlay/extract`.
- `byteboard-extract.exe` and `node tools/extract.mjs` give identical files.
- `preview.exe --unload-test` passes; previews of the catalog, an item and each printer look right.
- `dumpbin /exports overlay\bin\Byteboard.dll` shows no exports.
- Every `.lua` file parses (for example with the `luaparse` npm package, Lua 5.3 mode) and uses only UE4SS
  globals and Lua built-ins; every game-object access is preceded by `valid()`.

## In-game checklist

The mod can only be tested in the game. After a change:

1. `ue4ss\UE4SS.log` shows `[Byteboard] version x.y.z, overlay loaded` and `[Byteboard] loaded`;
   `out/state.json` updates every second; `out/overlay.log` shows the data extraction on the first start.
2. Opening and closing the backpack catalog shows and hides the catalog window.
3. Opening a printer panel and the backpack (Q) shows the printer list; closing hides it.
4. Selecting a recipe moves the printer to it; printing starts.
5. Research is refused while «Изучение разрешено» is off, and works when it is on.
6. `out/events.log` has no errors.

## Logs

All in the mod folder unless noted.

| File | Written by |
|---|---|
| `out/events.log` | mod: panels, signals, errors |
| `out/commands.log` | mod: every command and its result |
| `out/overlay.log` | overlay: start, data extraction, hooks, faults |
| `<game>\Astro\Binaries\Win64\ue4ss\UE4SS.log` | UE4SS |
