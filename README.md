# Byteboard

**English** · [Русский](README.ru.md)

A better research and printer screen for ASTRONEER, drawn inside the game. The research catalog gets search,
purpose groups and filters, and research takes one click. Printers get a recipe list with recipe selection
and a Print button. The byte balance and the researched items are read live from the game.

It is a UE4SS Lua mod plus a Dear ImGui DirectX 11 overlay. The interface is in Russian. Unofficial; no game
files are included.

> [!WARNING]
> **The code was written by an AI model (Claude by Anthropic) and has not been fully reviewed by a human.**
> It was tested in the game only in part. Use it at your own risk and back up your saves first.

## Compatibility

- ASTRONEER 1.42.3 (Unreal Engine 4.27), Windows, DirectX 11.
- Borderless or windowed mode is recommended; exclusive fullscreen has not been tested.
- [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) experimental 3.0.1-1183-gcdbc0b66 or newer.
- Single-player only. On a multiplayer host the mod can take another player's printer panel, catalog or
  backpack for yours; recipe selection and the Print button can then act on that player's printer. On
  someone else's server it has not been tested.

## Features

- **Research catalog**: opens and closes together with the backpack research catalog, or by hand with F8.
  Search matches item names in Russian and in English, and resources by their Russian names: "кварц"
  (Quartz) shows everything printed from it. Filters by purpose, printer and status. The item card shows the
  description, the recipe, every item of its subgroup across all printers (cheapest first) and a Research
  button.
- **Printer list**: appears when you open a printer panel. Items you can already print come first, items not
  yet researched below. Clicking a researched item selects its recipe on the printer; clicking one not yet
  researched opens it in the catalog. At the top: the current recipe, the ingredients at hand and a Print
  button.
- **Backpack (Q)**: a narrow list of the backpack printer's recipes.
- Window positions and sizes are saved in `ue4ss/out/overlay.ini`; to restore the defaults, delete the file
  while the game is closed.

## Requirements

- [Node.js](https://nodejs.org/) 18 or newer: extracts the data from the game.
- Visual Studio 2022 or newer, or its Build Tools, with the "Desktop development with C++" workload (MSVC
  x64) and its "C++ CMake tools for Windows" component: builds the overlay.
- Git.

## Install

1. Clone the repository together with Dear ImGui:
   ```bash
   git clone --recurse-submodules https://github.com/Skycheeck/astroneer-byteboard.git
   ```
2. Install UE4SS and point it at the clone's mods folder: [docs/INSTALL-UE4SS.md](docs/INSTALL-UE4SS.md).
3. Extract the data from your own copy of the game. The repository does not contain it: a script reads it
   from the game's pak file in a few seconds. By default it reads the pak from the default Steam folder
   (`C:Program Files (x86)SteamsteamappscommonASTRONEER`); for any other location, including another
   Steam library, set `ASTRONEER_PAK`:
   ```powershell
   $env:ASTRONEER_PAK = '<game folder>\Astro\Content\Paks\pakchunk0-WindowsNoEditor.pak'
   node tools/extract.mjs
   ```
   Run the extraction again after a game update.
4. Build the overlay: `overlay\build.cmd`.
5. Start the game.

## Safety

- Research spends bytes, so it runs only while the «Изучение разрешено» (research allowed) checkbox in the
  catalog window is ticked (the checkbox creates the file `ue4ss/in/allow-unlock`). The mod asks the game to
  research an item with the same call the game's own catalog uses.
- Recipe selection and printing do not depend on the checkbox: they do what the printer's own buttons do.
- The mod does not write to saves and does not call the game's cheat functions. Items that exist only in
  Creative Mode (Aero, for example) cannot be researched.
- Before the first start with mods, make a copy of `%LOCALAPPDATA%\Astro\Saved\SaveGames`.

## Uninstall

Remove the `+ModsFolderPaths` line from the UE4SS settings or remove UE4SS completely:
[docs/INSTALL-UE4SS.md](docs/INSTALL-UE4SS.md#uninstall). Researched items stay in the save.

## Troubleshooting

- `ue4ss/out/events.log`: mod events; `ue4ss/out/commands.log`: commands and their results;
  `ue4ss/out/overlay.log`: the overlay. `UE4SS.log` is in the game's `ue4ss` folder.
- If the overlay hits an error while drawing a frame, it turns itself off until the game restarts and logs
  this in `overlay.log`; the game keeps running.
- The mod loads a copy of the DLL (`ue4ss/out/ResearchOverlay-active.dll`), so you can rebuild the overlay
  while the game runs; the new build loads after a game restart.

## How it works

The mod and the overlay never talk to each other directly: the mod writes the game state to
`ue4ss/out/state.json` once a second, and panel open/close signals and command results to other files in
`ue4ss/out/` as they happen; the overlay writes commands to `ue4ss/in/cmd.txt`. Details:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): components, threads, safety rules;
- [docs/PROTOCOL.md](docs/PROTOCOL.md): the formats of all shared files;
- [docs/GAME-API.md](docs/GAME-API.md): which game functions are used;
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): building, previews without the game, checks.

## License

[MIT](LICENSE). Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

ASTRONEER is a trademark of System Era Softworks. This project is unofficial and not affiliated with System
Era Softworks. The files `tools/extract.mjs` creates (`data/`, `overlay/catalog.tsv`, `overlay/taxonomy.tsv`,
`overlay/icons.rgba`) contain game data: do not publish them.
