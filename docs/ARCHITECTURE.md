# Architecture

Byteboard is one mod folder (`mod/Byteboard` here, `ue4ss/Mods/Byteboard` in the game) with two parts
that never call each other: a Lua mod and an overlay DLL. They share files (`docs/PROTOCOL.md`). The
DLL also extracts the data it shows from the game's own pak; `tools/` is the Node reference of that
extraction for development.

```
game pak --> Byteboard.dll extract/ (first start, game update) --> data/catalog.tsv, taxonomy.tsv, icons.rgba
                                                                          |  read once at start
                                                                          v
+---------------------------+       out/*.json, *.txt        +---------------------------+
| Lua mod (Scripts/)        | -----------------------------> | Byteboard.dll (Dear ImGui)|
| reads the game and runs   |                                | draws over the game frame |
| commands on game thread   | <----------------------------- | and writes commands       |
+---------------------------+          in/cmd.txt            +---------------------------+
```

The release archive (`tools/package.mjs`, built by `.github/workflows/build.yml`) is the tracked files of
`mod/Byteboard` plus `Byteboard.dll`, the licence and the notices. It contains no game files.

## Lua mod: `mod/Byteboard/Scripts`

`main.lua` loads the modules in `rb/` with a small `loadfile` loader (not `require`, which is not reliable for
mods loaded from an extra mods folder), checks the UE4SS version, loads `Byteboard.dll` from the mod folder
before it writes any file (the DLL creates `in/`, `out/`, `data/`), then registers the hooks and starts the
loops. Modules have no side
effects when loaded.

| Module | Responsibility |
|---|---|
| `rb/uobj.lua` | the only access path to game objects: `valid()`, safe lookups, the local player controller |
| `rb/protocol.lua` | files: paths, JSON, atomic writes with retry, logs, signals, results ring, reading `cmd.txt` |
| `rb/state.lua` | builds `state.json`: balance, unlocked items, the open printer's recipe |
| `rb/panels.lua` | which catalog, printer or backpack is open; writes `catalog.txt` and `printer.txt` |
| `rb/select.lua` | the select job that steps a printer to a recipe |
| `rb/commands.lua` | parses and runs `unlock`, `select`, `print`; one result per command |
| `rb/loop.lua` | game-thread loops and circuit breakers |

Rules every module keeps (each one cost a game crash before):

- `valid()` before any method call or property read on a game object; a call on a null or stale object
  crashes inside UE4SS, and `pcall` cannot catch it.
- No game object is kept between ticks; only object paths are stored.
- Only named properties are read.
- All Lua runs on the game thread (`LoopInGameThreadWithDelay`; hooks already run there). Every loop and
  hook body runs in `pcall` behind a circuit breaker that turns it off after repeated failures.
- No `Authority*` or exec cheat functions are called.

## Overlay: `overlay/`

One DLL, built by CMake with Dear ImGui and zlib as static libraries. All code is in `namespace rd`; each header
says which threads use it.

| Module | Responsibility | Threads |
|---|---|---|
| `dllmain.cpp` | `DllMain` (pins the module, creates `in/`, `out/`, `data/`), `MainThread`: paths, game data, catalog, hooks, file polling | loader, poll |
| `paths` | the mod folder: the DLL's own folder in the game, `<repo>/mod/Byteboard` for the preview DLL | once, then read-only |
| `gamedata` | runs the extraction into `data/new` when `data/source.txt` does not match the pak and this DLL file, then moves the three files into `data/`; extracts once more when the data does not load | once |
| `extract/` | reads the pak (v11, zlib), cooked UE 4.27 packages, string tables, `.locres` and DXT textures; writes the three data files; never throws, bounds-checks every read | once |
| `catalog` | loads `catalog.tsv` and `taxonomy.tsv` | once, then read-only |
| `json` | a small JSON reader for the mod's files | any |
| `bridge` | the overlay side of the protocol: state snapshots, signals, commands, allow-unlock | poll writes, render reads |
| `hooks` | Present (vtable), window procedure (subclass), SetCursorPos/ClipCursor (import table) | game + render |
| `render` | ImGui setup, input, the view state machine, drawing into the back buffer, fault guard | render |
| `ui_state` | view state, filters, selection, pending commands and their messages | render |
| `ui_style`, `ui_widgets` | colours, fonts, icon atlas, shared drawing pieces | render |
| `ui_catalog.cpp`, `ui_printer.cpp` | the two windows | render |
| `preview_capture.cpp` | only in `ByteboardPreview.dll`: the preview export and BMP capture | render |

Three threads touch the overlay: the game thread runs the window procedure and the cursor hooks, the
render thread runs `Present` (all ImGui and D3D work), and the overlay's own thread extracts the data, then
polls the files.
They share only atomics, mutex-guarded queues and immutable state snapshots.

Safety measures:

- The original functions are stored before any hook is installed.
- The DLL pins itself, so a UE4SS hot reload cannot unload code that hooks still point to.
- Every frame runs inside a structured-exception guard (built with `/EHa`). A fault turns the overlay off
  for the rest of the session and releases the cursor; the game keeps running.
- The render target of the back buffer is created and released within each frame.
- No IME calls and no ImGui keyboard navigation: keys meant for the game never press overlay buttons.
  Keyboard input reaches ImGui only while a text field is active; button and key releases always reach
  the game.
- One overlay per game process (named mutex).

## Tools: `tools/`

Plain Node.js ESM, no npm dependencies, for development only: players never need Node. `extract.mjs` is the
reference the DLL's extraction is checked against (the outputs must be identical byte for byte). It runs the
steps in one process with one open pak;
each step can also run alone (`node tools/steps/<step>.mjs`) and then reads the earlier steps' files from
`data/`.

| Step | Output |
|---|---|
| `steps/dump-itemtypes.mjs` | `data/itemtypes.json`: every item type blueprint in the pak |
| `steps/build-catalog.mjs` | `data/catalog.json`: catalog items with names, costs, recipes, classes |
| `steps/build-icons.mjs` | `data/icons.json`: 64×64 icons |
| `steps/build-items.mjs` | `data/items.json`: the research entries with groups (`lib/taxonomy.mjs`) |
| `steps/export-overlay.mjs` | `mod/Byteboard/data/`: `catalog.tsv`, `taxonomy.tsv`, `icons.rgba` |

`lib/` holds the format readers (pak v11, cooked UE 4.27 packages, `.locres`, string tables, textures,
DXT, PNG) and the taxonomy: purpose groups, printers and which items are research entries.

`package.mjs` builds the release archive (see above).
