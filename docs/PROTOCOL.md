# File protocol

The Lua mod and the in-game overlay never talk to each other directly. They share files under
`ue4ss/` in the repository. This page is the single source of truth for those files.
Paths are relative to the repository root.

| File | Writer | Readers | Purpose |
|---|---|---|---|
| `ue4ss/out/state.json` | mod, every 1 s | overlay | balance, unlocked items, open printer |
| `ue4ss/out/results.json` | mod, per command | overlay | results of the last 8 commands |
| `ue4ss/out/catalog.txt` | mod, on change | overlay | backpack research catalog opened or closed |
| `ue4ss/out/printer.txt` | mod, on change | overlay | printer panel or backpack printer opened or closed |
| `ue4ss/in/cmd.txt` | overlay | mod, every 100 ms | one pending command |
| `ue4ss/in/allow-unlock` | overlay checkbox | mod, overlay | research (spending bytes) is allowed while it exists |
| `ue4ss/out/events.log`, `ue4ss/out/commands.log` | mod | people | diagnostics; rotated at load when larger than 1 MB |
| `ue4ss/out/overlay.log`, `ue4ss/out/overlay.ini` | overlay | people, overlay | diagnostics; window layout |

All text files are UTF-8 without BOM. The mod writes every `out/` file by writing `<name>.tmp`,
deleting `<name>` and renaming the temporary file. A reader can therefore briefly find a file
missing and must keep its previous value for a moment instead of treating that as a lost
connection. Readers open the files with full sharing (`FILE_SHARE_READ | FILE_SHARE_WRITE |
FILE_SHARE_DELETE` on Windows) so they never block the mod's delete.

## Timings

| Constant | Value | Used by |
|---|---|---|
| state period | 1 s | mod |
| fast loop period (commands, signals, select steps) | 100 ms | mod |
| state is stale after | 5 s (file age) | overlay |
| command expires after | 8 s (`unixSec` in the command); shorter than the unlock/print timeout, so a command the overlay gave up on never runs | mod |
| unlock / print pending timeout | 10 s | overlay |
| select pending timeout | 30 s | overlay |
| select job limit | 20 s | mod |

## `state.json`

One JSON object. Keys may appear in any order; readers look keys up by name.

| Key | Type | Meaning |
|---|---|---|
| `t` | integer | Unix time of the write |
| `ok` | boolean | `true` when in a world and `balance` and `unlocked` are both valid |
| `error` | string | why `ok` is false (for example `not in a world yet`) |
| `balance` | number | research bytes, as the game's catalog shows them |
| `perMinute` | number | bytes per minute |
| `unlocked` | array of strings | class paths of every unlocked item type, for example `/Game/Items/ItemTypes/Components/Generator_Small.Generator_Small_C` |
| `unlockedError` | string | set when the unlocked list could not be read completely (then `ok` is false) |
| `printerCurrent` | string | class path of the world actor class of the recipe shown on the open printer (`PrinterComponent:GetCurrentItem`, an item's `PickupActor`), for example `/Game/Components_Small/Splitter/Splitter_3Way.Splitter_3Way_C`; missing when no printer is open |
| `printerHave` | string | comma-separated counts, one per ingredient of that recipe, in recipe order; `?` for a count that could not be read |
| `printerCanPrint` | boolean | `PrinterComponent:GetCanPrint` |
| `gameState`, `playerState` | object | raw research fields, diagnostics only |

To compare classes, readers use the **object name**: the part after the last `.`, with a trailing
`_C` removed (`Splitter_3Way_C` → `Splitter_3Way`). An item matches `printerCurrent` when that
name equals the object name of the item's `pickup` class or of the item's own class.

## `results.json`

A JSON array, newest first, at most 8 entries:

```json
[{"id":"0313669500170000","op":"print","arg":"/Game/Items/ItemTypes/Components/StorageRack_Medium.StorageRack_Medium_C","ok":true,"code":"ok","t":1793827306}]
```

| Key | Type | Meaning |
|---|---|---|
| `id` | string | the command id |
| `op` | string | `unlock`, `select`, `print` or the unknown op as received |
| `arg` | string | the command argument, may be empty |
| `ok` | boolean | the command did what it was asked |
| `code` | string | machine-readable outcome, see below; readers switch on this, never on `error` |
| `error` | string | human-readable English detail for logs; missing when `ok` |
| `t` | integer | Unix time of the result |

Codes:

| Code | Ops | Meaning |
|---|---|---|
| `ok` | all | done (unlock: request sent to the game; select: the printer shows the recipe; print: printing started) |
| `bad_command` | all | the line could not be parsed or the argument is not an item class path |
| `unknown_op` | — | op is not unlock, select or print |
| `expired` | all | the command was older than 8 s when the mod read it |
| `not_in_world` | all | no player controller yet |
| `disabled` | unlock | `in/allow-unlock` does not exist |
| `creative_only` | unlock | the item exists only in Creative Mode |
| `already_unlocked` | unlock | |
| `class_not_loaded` | unlock, select, print | the item class is not loaded in the game |
| `no_printer` | select, print | no printer panel or backpack printer is open |
| `busy` | print | a select job is still stepping the printer (a new select instead ends the running one with `superseded`) |
| `not_found` | select | the recipe is not on this printer |
| `printer_closed` | select | the printer panel closed during the job |
| `printer_changed` | select | another printer was opened during the job |
| `superseded` | select | a newer select replaced this one |
| `timeout` | select | the job hit the 20 s limit or the step limit |
| `no_response` | select | the printer stopped reacting to steps |
| `wrong_recipe` | print | the printer shows another recipe than `arg` |
| `cannot_print` | print | `GetCanPrint` is false (missing resources or power) |
| `call_failed` | all | a game call raised an error; `error` has the message |

A `select` writes exactly one result, when the job ends.

## `catalog.txt`, `printer.txt`

One line: `<seq> open` or `<seq> closed` for the catalog, `<seq> open <ItemCategory_...>` or
`<seq> closed` for printers. `<seq>` is an integer that changes with every write; readers act when
it changes and treat the first value they see as a baseline. Categories: `ItemCategory_Backpack`,
`ItemCategory_SmFab`, `ItemCategory_MedFab`, `ItemCategory_LgFab`.

The mod writes `closed` when the panel closes and also when the open printer or backpack
disappears (world unload, return to the main menu, printer picked up).

## `cmd.txt`

One line: `<id> <unixSec> <op> [<arg>]`

- `<id>`: 1 to 32 characters from `[0-9A-Za-z_-]`, unique per writer.
- `<unixSec>`: Unix time when the command was written. The mod refuses commands older than 8 s
  with `expired`, so a command left behind never runs in a later session.
- `<op>`: `unlock`, `select` or `print`.
- `<arg>`: an item type class path, `/Game/Items/ItemTypes/....<Name>_C`. Required for `unlock`
  and `select`; optional for `print`, where it names the recipe the user expects on the printer.

The overlay never overwrites a waiting command: it writes a temporary file and moves it to
`cmd.txt` with `MoveFileExW` without `MOVEFILE_REPLACE_EXISTING`, which fails while a command is
still waiting. The mod deletes `cmd.txt` before running the command and skips it if the
delete fails. At load the mod deletes any `cmd.txt` left from a previous session.

## `allow-unlock`

An empty marker file. Only `unlock` checks it; `select` and `print` do what the printer's own
buttons do and are not gated. The overlay checkbox «Изучение разрешено» creates and deletes it.

## Generated data shared by the tools and the overlay

`tools/extract.mjs` writes these from the user's own game files. They are not in git.

- `overlay/catalog.tsv`: UTF-8, tab-separated, one item per line. The first line starts with `#`
  and lists the column names; readers skip lines that start with `#`. Columns:
  `id, classPath, nameRu, nameEn, group, subgroup, printer, cost, recipe, icon, description, pickup`.
  `group` is a group key from `taxonomy.tsv`; `printer` is a printer key (`Backpack`, `SmFab`,
  `MedFab`, `LgFab`); `recipe` is `<count>× <name>` joined by `, `; `icon` is an index into
  `icons.rgba`; `pickup` is the class path of the item's world actor class.
- `overlay/taxonomy.tsv`: UTF-8, tab-separated. Lines `group	<key>	<label>` in display order,
  then lines `printer	<key>	<ItemCategory_...>	<label>	<short label>` in display order.
- `overlay/icons.rgba`: `u32 width, u32 height` (little-endian), then RGBA8 pixels; 64×64 cells,
  16 per row.
- `data/catalog.json`: every catalog item from the game files, with `classPath` and `pickup`.
- `data/items.json`: `{ items, groups, printers, icons }`, the input of `overlay/catalog.tsv`,
  `taxonomy.tsv` and `icons.rgba`. `items` is the researchable list, `printers` is
  `[{key, category, label, short}]`, `groups` is `[[key, label], ...]`, `icons` maps icon names
  to PNG data URIs.
