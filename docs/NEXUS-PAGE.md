# Nexus Mods page

Published at https://www.nexusmods.com/astroneer/mods/244 (0.1.0, 2026-11-07). The fields below are what the page
holds; keep them in sync when the page changes. The description is BBCode, the format
the Nexus editor takes.

| Field | Value |
|---|---|
| Name | Byteboard - Research Catalog and Printer Recipes |
| Category | User Interface |
| Author | Skycheeck |
| Language | Any (the field takes one language; the interface follows the game's language, 10 languages) |
| Version | 0.1.0 |
| Game version | Tested on ASTRONEER 1.42.3 only |
| Tags | Quality of Life; User Interface. Generative AI usage: Mod Content AI-Generated and Media AI-Generated (adds the AI-Generated Content and AI Media tags). Nexus has no UE4SS tag |
| Requirements (off-site) | RE-UE4SS experimental 3.0.1-1183 or newer, https://github.com/UE4SS-RE/RE-UE4SS/releases |
| Permissions | MIT licence; source at https://github.com/Skycheeck/astroneer-byteboard |
| Main file | `Byteboard-0.1.0.zip` from the GitHub release (built by GitHub Actions); mod manager download off, since the archive goes into `ue4ss\Mods` by hand |

Summary (max 350 characters):

> A searchable research catalog and printer recipe list drawn inside the game: search, purpose groups,
> one-click research, pick a printer recipe from a list and print. UE4SS Lua mod plus a Dear ImGui overlay.
> Follows the game's language (10 languages). Written by an AI model.

Before uploading:

- Images, in this order: `docs/screenshots/catalog.webp`, `printer.webp`, `backpack.webp` (taken on 1.42.3, without
  other overlays). If the form refuses WebP, convert them to PNG first.
- Do not attach UE4SS, `dwmapi.dll` or any file from the game; the archive from the release has none.

## Description

```bbcode
[size=5][b]Byteboard[/b][/size]
A better research and printer screen for ASTRONEER, drawn inside the game.

[b][color=#ff9900]The code was written by an AI model (Claude by Anthropic) and has not been fully reviewed by a human. It was tested in the game only in part. Use it at your own risk and back up your saves first.[/color][/b]

[size=4][b]Features[/b][/size]
[list]
[*][b]Research catalog[/b]: opens and closes together with the backpack research catalog. Search by item or resource name ("quartz" shows everything printed from Quartz). Filters by purpose, printer and status. The item card shows the recipe, the subgroup across all printers and a Research button.
[*][b]Printer list[/b]: appears when you open a printer. Items you can print come first. Click a researched item to select its recipe on the printer, then Print.
[*][b]Backpack (Q)[/b]: a compact list of the backpack printer's recipes.
[*][b]Every printer item[/b], read from your own game files: rails, expansion items and mission rewards such as the VTOL and the hoverboard. A reward shows which mission gives it.
[/list]
The interface follows the game's language: English, Russian, German, Spanish, French, Italian, Dutch, Polish, Brazilian Portuguese and Turkish. Item names, descriptions and mission titles come from your own game files; the other interface texts are AI translations, corrections are welcome on GitHub.

[size=4][b]Requirements[/b][/size]
[list]
[*]Windows, DirectX 11, single-player. On a multiplayer host the mod can act on another player's printer.
[*]Tested on ASTRONEER 1.42.3 only: install, data extraction, catalog, research, recipe selection and printing, the English and Russian interface. Other game versions have not been tested; an update that changes the game's files or functions can break the mod.
[*][url=https://github.com/UE4SS-RE/RE-UE4SS/releases]RE-UE4SS[/url] experimental 3.0.1-1183 or newer (the UE4SS_v3.0.1-*.zip asset). The UE4SS that mod managers install is older and has not been tested with this mod.
[/list]

[size=4][b]Installation[/b][/size]
[list=1]
[*]Install UE4SS: extract it next to Astro-Win64-Shipping.exe in [i]<game>\Astro\Binaries\Win64[/i], and in [i]ue4ss\UE4SS-settings.ini[/i] set MajorVersion = 4 and MinorVersion = 27 under [i][EngineVersionOverride][/i].
[*]Copy the [i]Byteboard[/i] folder from this archive into [i]<game>\Astro\Binaries\Win64\ue4ss\Mods\[/i].
[*]Start the game. On the first start, and after each game update, the mod reads item names, recipes and icons from your own game files (a few seconds). The archive contains no game files.
[/list]

[size=4][b]Safety[/b][/size]
[list]
[*]Research spends bytes, so it works only while the "Research allowed" checkbox is ticked.
[*]The mod does not write to saves and does not call the game's cheat functions. Creative-only items cannot be researched.
[/list]

[size=4][b]Uninstall[/b][/size]
Delete [i]ue4ss\Mods\Byteboard[/i]. Items you researched stay researched.

[size=4][b]Problems[/b][/size]
Logs: [i]Byteboard\out\overlay.log[/i], [i]Byteboard\out\events.log[/i] and [i]ue4ss\UE4SS.log[/i]. Report issues on [url=https://github.com/Skycheeck/astroneer-byteboard/issues]GitHub[/url] with these logs.

[size=4][b]Source and licence[/b][/size]
MIT. Source code, build instructions and technical documents: [url=https://github.com/Skycheeck/astroneer-byteboard]github.com/Skycheeck/astroneer-byteboard[/url]. ASTRONEER is a trademark of System Era Softworks; this mod is unofficial.

[line]

[size=5][b]Byteboard (по-русски)[/b][/size]
Удобный экран исследований и печати для ASTRONEER прямо в игре.

[b][color=#ff9900]Код написан нейросетью (Claude от Anthropic) и полностью человеком не проверялся. В игре он проверен лишь частично. Используйте на свой риск и сначала сделайте копию сохранений.[/color][/b]

[size=4][b]Возможности[/b][/size]
[list]
[*][b]Каталог исследований[/b] открывается и закрывается вместе с каталогом исследований рюкзака. Поиск по названию предмета или ресурса («кварц» покажет всё, что из него печатается). Фильтры по назначению, принтеру и статусу. В карточке предмета: рецепт, вся его подгруппа со всех принтеров и кнопка изучения.
[*][b]Список принтера[/b] появляется, когда вы открываете принтер. Сначала то, что уже можно печатать. Клик по изученному предмету выбирает его рецепт на принтере, затем нажмите «Печатать».
[*][b]Рюкзак (Q)[/b]: компактный список рецептов принтера рюкзака.
[*][b]Все предметы принтеров[/b] из ваших файлов игры: рельсы, предметы дополнений и награды за задания, например СВВП и ховерборд. У награды указано, какое задание её даёт.
[/list]
Язык интерфейса — как в игре: английский, русский, немецкий, испанский, французский, итальянский, нидерландский, польский, бразильский португальский или турецкий. Названия и описания предметов и названия заданий берутся из ваших файлов игры; остальные тексты интерфейса переведены нейросетью, исправления принимаются на GitHub.

[size=4][b]Требования[/b][/size]
[list]
[*]Windows, DirectX 11, одиночная игра. На хосте сетевой игры мод может сработать на принтере другого игрока.
[*]Проверено только на ASTRONEER 1.42.3: установка, чтение данных игры, каталог, изучение, выбор рецепта и печать, интерфейс на английском и русском. Другие версии игры не проверялись; обновление, которое меняет файлы или функции игры, может сломать мод.
[*][url=https://github.com/UE4SS-RE/RE-UE4SS/releases]RE-UE4SS[/url] experimental 3.0.1-1183 или новее (файл UE4SS_v3.0.1-*.zip). UE4SS, который ставят менеджеры модов, старее; с этим модом он не проверялся.
[/list]

[size=4][b]Установка[/b][/size]
[list=1]
[*]Установите UE4SS: распакуйте его рядом с Astro-Win64-Shipping.exe в [i]<папка игры>\Astro\Binaries\Win64[/i], а в [i]ue4ss\UE4SS-settings.ini[/i] в разделе [i][EngineVersionOverride][/i] укажите MajorVersion = 4 и MinorVersion = 27.
[*]Скопируйте папку [i]Byteboard[/i] из архива в [i]<папка игры>\Astro\Binaries\Win64\ue4ss\Mods\[/i].
[*]Запустите игру. При первом запуске и после каждого обновления игры мод читает названия предметов, рецепты и иконки из ваших файлов игры (несколько секунд). В архиве нет файлов игры.
[/list]

[size=4][b]Безопасность[/b][/size]
[list]
[*]Изучение тратит байты, поэтому работает, только пока отмечена галочка «Изучение разрешено».
[*]Мод не пишет в сохранения и не вызывает читерские функции игры. Предметы только для творческого режима изучить нельзя.
[/list]

[size=4][b]Удаление[/b][/size]
Удалите папку [i]ue4ss\Mods\Byteboard[/i]. Изученные предметы останутся изученными.

[size=4][b]Проблемы[/b][/size]
Логи: [i]Byteboard\out\overlay.log[/i], [i]Byteboard\out\events.log[/i] и [i]ue4ss\UE4SS.log[/i]. О проблемах пишите на [url=https://github.com/Skycheeck/astroneer-byteboard/issues]GitHub[/url] и прикладывайте эти логи.

[size=4][b]Исходники и лицензия[/b][/size]
MIT. Исходный код, инструкция по сборке и технические документы: [url=https://github.com/Skycheeck/astroneer-byteboard]github.com/Skycheeck/astroneer-byteboard[/url]. ASTRONEER — товарный знак System Era Softworks; мод неофициальный.
```

## File description

```bbcode
Unpack the Byteboard folder into <game>\Astro\Binaries\Win64\ue4ss\Mods\. Needs RE-UE4SS experimental 3.0.1-1183 or newer.
```
