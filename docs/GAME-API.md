# Game API notes

What the mod uses from ASTRONEER 1.42.3 (UE 4.27), found through UE4SS reflection and by testing in the
game. Names are reflection names; all native classes live under `/Script/Astro`.

## Research

- **`UResearchSystem`**: every useful function is static, so the mod calls it on
  `/Script/Astro.Default__ResearchSystem`.
  - `float GetCurrentResearchPointBalance(APlayerController*)`: the balance the game's catalog shows.
  - `float GetCurrentResearchPointsPerMinute(APlayerController*)`
  - `bool IsItemTypeUnlocked(APlayerController*, UClass* ItemType)`
- **`AAstroGameState`** holds the shared research pool of a normal game: `CurrentResearchPointBalance`,
  `KnownItemTypes`, `UnlockedItemTypes` (`TArray<UClass*>`). **`AAstroPlayerState`** has the same fields per
  player for per-player research modes; there is one per known player, so the local one is reached through
  the controller's `PlayerState`, not with `FindFirstOf`.
- **`AAstroPlayerController:ServerRequestUnlockItem(UClass* ItemTypeToUnlock, bool bAllowPlayFabBinding)`**: the
  call the game's own catalog makes to research an item (server RPC). It takes the item type **class**, for
  example `/Game/Items/ItemTypes/Components/Generator_Small.Generator_Small_C`. The game's catalog passes
  `false`. The mod hooks it only to log what the game itself sends.
- The controller also has exec cheats (`GrantAllUnlocks`, `ResetAllUnlocks`) and `Authority*` functions on
  the research system. The mod never calls them.

## Item types

- Every catalog item is a Blueprint class under `/Game/Items/ItemTypes/` whose class default object carries
  `CatalogData` (`ItemCatalogData`): `Category` (`ItemCategory_Backpack`, `_SmFab`, `_MedFab`, `_LgFab`, ...),
  `UnlockCost`, `bIsCreativeModeOnly`, `BaseItemType` and variant fields. Fields equal to the parent's are
  not serialised.
- `UnlockCost` is left out of the asset when it equals the native default, **1000**. That value never appears
  explicitly, and the community wiki lists 1000 for items such as the Tapper.
- `PickupActor` is the world actor class an item prints as. Printers report their current recipe by this
  class, not by the item type.
- **Aero** (`CreativeDroneIT`, the camera drone) is creative-only, but its asset has no
  `bIsCreativeModeOnly`: the game hides it in code. `ServerRequestUnlockItem` would research it in a normal
  save, so both the tools and the mod exclude it.

## Printers

`UPrinterComponent` (on every printer actor and on the backpack's printer):

| Function | Use |
|---|---|
| `GetCurrentItem()` | the recipe on display, as its `PickupActor` class |
| `GetCurrentVisibleRowBlueprintIndex()`, `GetCurrentVisibleColumnBlueprintIndex()` | position in the recipe grid |
| `GetTotalVisibleBlueprintRowsCount()`, `GetTotalVisibleBlueprintColumnCount()` | grid size (the column count depends on the row) |
| `IncrementBlueprint(true)`, `DecrementBlueprint(true)` | next/previous variant in the row |
| `IncrementBlueprintRow(true)`, `DecrementBlueprintRow(true)` | next/previous row; both directions wrap |
| `GetCanPrint()` | the printer's own "can print" state (resources and power) |
| `StartPrinting()` | what the print button does |
| `GetAvailableIngredientCounts()` | counts of each ingredient **within reach** of the printer, not the ones inserted |

A step lands a few frames after the call, so the mod waits for the position to change before the next step.

## Panels

- `/Script/Astro.ControlPanel:CrackedChanged(cracked)` fires when a control panel opens or closes. It also
  fires for backpack panels, so the mod only accepts panels that belong to an actor with a printer component
  and an `ItemCategory_` printer category.
- `BackpackRail_C.IsRaised` is true while the backpack (Q) is open.
- `BackpackCatalog_C.ExtendTop` is false while the backpack research catalog is open.

## Crash lessons

Each of these crashed the game before the mod was changed:

- Calling a method on a null or stale object crashes inside UE4SS, and `pcall` cannot catch it. Even
  `GetFName` on a null object property crashes, so `IsValid` comes first, always.
- Walking all properties of the player pawn crashed; the mod reads only named properties.
- `LoopAsync` runs Lua on UE4SS's async thread in the same Lua state the game-thread callbacks use; the
  resulting registry corruption crashed inside UE4SS's Lua (`lua_rawgeti` on a corrupted table). All mod code
  now runs on the game thread through `LoopInGameThreadWithDelay`.
- A UE4SS hot reload (Ctrl+R) frees the overlay DLL while its hooks are installed; the DLL pins itself.
- Windows text input (IME) calls from the render thread crashed in `inputhost.dll`; the overlay makes none.
