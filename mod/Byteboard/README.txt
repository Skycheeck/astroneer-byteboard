Byteboard - a research catalog and printer recipe list for ASTRONEER, drawn inside the game.
https://github.com/Skycheeck/astroneer-byteboard

The code was written by an AI model (Claude by Anthropic) and has not been fully reviewed by a human.
It was tested in the game only in part. Use it at your own risk and back up your saves first:
%LOCALAPPDATA%\Astro\Saved\SaveGames

REQUIREMENTS
- ASTRONEER on Windows, DirectX 11, single-player. Tested on ASTRONEER 1.42.3 only.
- RE-UE4SS experimental 3.0.1-1183 or newer: https://github.com/UE4SS-RE/RE-UE4SS/releases
  (the UE4SS_v3.0.1-*.zip asset). Extract it next to Astro-Win64-Shipping.exe in
  <game>\Astro\Binaries\Win64, and in ue4ss\UE4SS-settings.ini set
  [EngineVersionOverride] MajorVersion = 4 and MinorVersion = 27.

INSTALL
1. Copy this Byteboard folder into <game>\Astro\Binaries\Win64\ue4ss\Mods\
   (so that ue4ss\Mods\Byteboard\Byteboard.dll exists).
2. Start the game. On the first start, and after each game update, the mod reads the item
   names, recipes and icons from your own game files into Byteboard\data (a few seconds).

USE
- The research catalog opens and closes with the backpack research catalog.
- Opening a printer shows its recipe list: click a researched item to select its recipe, then Print.
- Research spends bytes, so it only works while the "Research allowed"
  checkbox in the catalog window is ticked.
- The interface follows the game's language: English, Russian, German, Spanish, French, Italian, Dutch, Polish, Brazilian Portuguese and Turkish.
  Item names and descriptions come from your game; the interface texts in lang\ are translations
  (corrections welcome).

UNINSTALL
Delete ue4ss\Mods\Byteboard. Items you researched stay researched in your save.

TROUBLESHOOTING
Logs: Byteboard\out\events.log (mod), Byteboard\out\overlay.log (overlay and data extraction),
ue4ss\UE4SS.log (UE4SS). Without a Byteboard\out folder the DLL did not load: see UE4SS.log.
Report problems on GitHub with these logs.
