// The one source for groups and printers (the overlay gets them through overlay/taxonomy.tsv) and for the rules
// that decide which catalog items are research entries.

// Functional groups that cut across printers, in display order: [key, label].
export const GROUPS = [
  ['power', 'Энергия'],
  ['oxygen', 'Кислород'],
  ['prod', 'Производство'],
  ['storage', 'Хранение'],
  ['base', 'Платформы и база'],
  ['auto', 'Автоматизация'],
  ['transport', 'Транспорт'],
  ['terrain', 'Терраформинг'],
  ['explore', 'Разведка и снаряжение'],
  ['fun', 'Отдых и декор'],
];

// Printers in display order. key is the catalog's printer value, category the game's ItemCategory asset name,
// short the lower-case word used inside sentences.
export const PRINTERS = [
  {key: 'Backpack', category: 'ItemCategory_Backpack', label: 'Рюкзак', short: 'рюкзак'},
  {key: 'SmFab', category: 'ItemCategory_SmFab', label: 'Малый принтер', short: 'малый'},
  {key: 'MedFab', category: 'ItemCategory_MedFab', label: 'Средний принтер', short: 'средний'},
  {key: 'LgFab', category: 'ItemCategory_LgFab', label: 'Большой принтер', short: 'большой'},
];

// [pattern, group key, subgroup label]. Rules are ordered: the first rule matching the English name wins, then
// the first rule matching the asset id.
const RULES = [
  [/\b(Ring|Hoop|Arch|Canopy)\b/i, 'auto', 'Сенсорные кольца'],
  [/Sensor|Repeat|Proximity|Delay|Count|Button/i, 'auto', 'Датчики и логика'],
  [/Horn|Hologram|Figurine/i, 'fun', 'Отдых и декор'],
  [/Seat|Crane|Winch/i, 'transport', 'Навесное'],
  [/Tapper|Arm|Extractor|Hopper/i, 'auto', 'Механизмы'],
  [/Solar/i, 'power', 'Солнечные панели'],
  [/Wind/i, 'power', 'Ветротурбины'],
  [/Generator$|RTG/i, 'power', 'Генераторы'],
  [/Battery|Power Cells/i, 'power', 'Батареи'],
  [/Splitter|Extenders|Switch/i, 'power', 'Проводка'],
  [/Oxygenator|Oxygen Filters|Oxygen Tank/i, 'oxygen', 'Кислород'],
  [/Tethers/i, 'oxygen', 'Тросы'],
  [/Printer$/i, 'prod', 'Принтеры'],
  [/Print Filter/i, 'prod', 'Фильтры печати'],
  [/Smelt|Chemistry|Centrifuge|Condenser/i, 'prod', 'Переработка'],
  [/Shredder/i, 'prod', 'Шреддеры'],
  [/Trade|EXO Request|Packager/i, 'prod', 'Торговля и упаковка'],
  [/Research/i, 'prod', 'Исследования'],
  [/Canister/i, 'storage', 'Контейнеры'],
  [/Storage|Silo/i, 'storage', 'Склады'],
  [/Shelter/i, 'base', 'Убежища'],
  [/Landing/i, 'base', 'Посадка'],
  [/Platform/i, 'base', 'Платформы'],
  [/Shuttle/i, 'transport', 'Корабли'],
  [/Thruster/i, 'transport', 'Двигатели'],
  [/Rover|Buggy|Tractor|Trailer/i, 'transport', 'Машины'],
  [/Drill Strength|Paver/i, 'terrain', 'Буры для транспорта'],
  [/Drill|Augment|Boost|Alignment|Brush|Inhibitor|Terrain Analyzer|Leveling/i, 'terrain', 'Модули терраформера'],
  [/Jump|Jetpack|Drone|Aero/i, 'explore', 'Передвижение'],
  [/Beacon|Scanner|Seeker|Probe/i, 'explore', 'Поиск'],
  [/Light|Glowstick|Flare/i, 'explore', 'Свет'],
  [/Dynamite/i, 'explore', 'Взрывчатка'],
  [/Camera|Firework|Sphere|Display/i, 'fun', 'Отдых и декор'],
];

// {group, subgroup} for a catalog item ({en, id}); both null when no rule matches.
export function classify(item) {
  for (const text of [item.en, item.id]) {
    for (const [pattern, group, subgroup] of RULES) {
      if (pattern.test(text)) return {group, subgroup};
    }
  }
  return {group: null, subgroup: null};
}

// Catalog items that are not research entries of their own: the starter habitat, wrecked and event copies,
// creative plinths, and LiquidCanister_Fixed_T3_IT, a hidden copy of LiquidCanister_T3_IT with the same name,
// cost and printer.
export const EXCLUDE = /Starter|SemiWrecked|_GW_|CanisterPlinth|^LiquidCanister_Fixed_T3_IT$/;

// Items that can never be researched carry a huge price instead (ExtraLargeResourceCanister_IT costs
// 1 000 000 000 bytes); anything from this cost up is not a research entry.
export const NOT_RESEARCHABLE_COST = 1e8;

// Creative-only items the game hides in code rather than with bIsCreativeModeOnly; they are left out of the
// catalog. The Lua mod refuses to unlock the same ids (its CREATIVE_ONLY list).
export const CREATIVE_ONLY = new Set([
  'CreativeDroneIT',  // Aero, the camera drone
]);
