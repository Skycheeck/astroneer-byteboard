// Step 2: build the research catalog from the item types and the game's texts.
// Input: context.itemTypes or data/itemtypes.json; from the pak, the en and ru Game.locres (lib/locres.mjs) and
// the string tables the item texts point to (lib/stringtable.mjs).
// Output: data/catalog.json and context.catalog: every item type that has catalog data, a printer category and
// is not creative-only, sorted by printer, then catalog position. Fields of an item:
//   id, path        asset name and path under /Game/Items/ItemTypes/
//   classPath       the item type's class, the argument of the mod's unlock/select/print commands
//   pickup          class path of the world actor the item prints as (PickupActor), or null
//   en, ru          display name; subEn, subRu: tooltip subtitle; descRu, descEn: description
//   printer         printer key (Backpack, SmFab, MedFab, LgFab) or another category name (Exploration, ...)
//   seq, varSeq     position in the printer's catalog and among the item's variants
//   variant, base   variant type (EItemVariantType value) and the id of the item it is a variant of
//   isBase, hidden  base item flag and bHiddenUntilUnlocked
//   cost            research cost in bytes
//   recipe          [{item, n, en, ru}]: ingredient object name, count and display names
//   icon, iconPath  icon texture name and object path
// subEn, descEn, isBase and hidden are not read by the page or the overlay; they are kept so the file describes
// each item completely.
import {isMainModule, readInput, runSteps, writeOutput} from '../lib/context.mjs';
import {parseLocres} from '../lib/locres.mjs';
import {FILES, displayPath, objectName} from '../lib/paths.mjs';
import {readStringTable} from '../lib/stringtable.mjs';
import {CREATIVE_ONLY} from '../lib/taxonomy.mjs';
import {refPath} from '../lib/uasset.mjs';
import * as dumpItemTypes from './dump-itemtypes.mjs';

export const name = 'build-catalog';

const LOCRES_FILES = {
  en: 'Astro/Content/Localization/Game/en/Game.locres',
  ru: 'Astro/Content/Localization/Game/ru/Game.locres',
};

// UnlockCost is left out of the asset when it equals the native ItemType default, which is 1000
// (never written explicitly; the wiki lists 1000 for the Tapper and the Alignment/Inhibitor mods).
const DEFAULT_UNLOCK_COST = 1000;

export function run(context) {
  const itemTypes = dumpItemTypes.load(context);
  const types = createTypeResolver(itemTypes);
  const texts = createLocalizer(context.pak);
  const items = [];
  for (const key of Object.keys(itemTypes)) {
    const item = toCatalogEntry(key, types, texts);
    if (item) items.push(item);
  }
  items.sort(byPrinterThenPosition);
  texts.reportMisses();

  writeOutput(FILES.catalog, JSON.stringify(items, null, 1));
  context.catalog = items;
  console.log(`${displayPath(FILES.catalog)}: ${items.length} items`);
}

function toCatalogEntry(key, types, texts) {
  const itemType = types.get(key);
  // Only item types that define their own catalog data are catalog entries; their children are not.
  if (!itemType.props.CatalogData) return null;
  const {props, catalogData} = types.resolve(key);
  const id = idOf(key);
  if (catalogData.bIsCreativeModeOnly || CREATIVE_ONLY.has(id)) return null;
  const category = categoryOf(key, types);
  if (!category) return null;

  const baseClass = refPath(catalogData.BaseItemType);
  const baseKey = baseClass ? types.keyOfClass(baseClass) : undefined;
  const iconPath = refPath(props.WidgetIcon);
  return {
    id,
    path: key,
    classPath: itemType.cls,
    pickup: refPath(props.PickupActor),
    en: texts.localize(props.Name, 'en'),
    ru: texts.localize(props.Name, 'ru'),
    subEn: texts.localize(props.TooltipSubtitle, 'en'),
    subRu: texts.localize(props.TooltipSubtitle, 'ru'),
    descRu: texts.localize(props.Description, 'ru'),
    descEn: texts.localize(props.Description, 'en'),
    printer: printerOf(category),
    seq: catalogData.CategorySequenceNumber || 0,
    varSeq: catalogData.VariationSequenceNumber || 0,
    variant: catalogData.VariantType?.split('::')[1] || null,
    base: baseKey ? idOf(baseKey) : null,
    isBase: catalogData.bIsBaseItem !== false && !catalogData.BaseItemType,
    cost: catalogData.UnlockCost ?? DEFAULT_UNLOCK_COST,
    hidden: Boolean(catalogData.bHiddenUntilUnlocked),
    recipe: recipeOf(props, types, texts),
    icon: iconPath ? objectName(iconPath) : null,
    iconPath,
  };
}

function idOf(key) {
  return key.split('/').pop();
}

// The effective properties of an item type: its parents' properties overridden by its own, and the same for
// CatalogData, as the game's class defaults inherit them.
function createTypeResolver(itemTypes) {
  const keyByClass = new Map(Object.entries(itemTypes).map(([key, type]) => [type.cls, key]));
  const resolved = new Map();
  const inProgress = new Set();

  function resolve(key) {
    if (resolved.has(key)) return resolved.get(key);
    if (inProgress.has(key)) throw new Error(`class inheritance loops through ${key}`);
    inProgress.add(key);
    const type = itemTypes[key];
    const parentKey = type.sup ? keyByClass.get(type.sup) : undefined;
    const parent = parentKey ? resolve(parentKey) : {props: {}, catalogData: {}};
    const ownCatalogData = isPlainObject(type.props.CatalogData) ? type.props.CatalogData : {};
    const result = {
      props: {...parent.props, ...type.props},
      catalogData: {...parent.catalogData, ...ownCatalogData},
    };
    inProgress.delete(key);
    resolved.set(key, result);
    return result;
  }

  return {
    get: key => itemTypes[key],
    keyOfClass: classPath => keyByClass.get(classPath),
    resolve,
  };
}

function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

// The printer category reference; a variant without its own category takes its base item's.
function categoryOf(key, types, visited = new Set()) {
  const {catalogData} = types.resolve(key);
  if (catalogData.Category) return catalogData.Category;
  if (!catalogData.BaseItemType || visited.has(key)) return null;
  visited.add(key);
  const baseClass = refPath(catalogData.BaseItemType);
  const baseKey = baseClass ? types.keyOfClass(baseClass) : undefined;
  return baseKey ? categoryOf(baseKey, types, visited) : null;
}

// "@/Game/Items/ItemCategories/ItemCategory_SmFab.ItemCategory_SmFab" → "SmFab"
function printerOf(category) {
  const path = refPath(category);
  return path ? objectName(path).replace(/^ItemCategory_/, '') : String(category);
}

function recipeOf(props, types, texts) {
  const ingredients = props.ConstructionRecipe?.Ingredients || [];
  return ingredients.map(ingredient => {
    const classPath = refPath(ingredient.ItemType);
    const item = classPath ? objectName(classPath) : '';
    const key = classPath ? types.keyOfClass(classPath) : undefined;
    const displayName = key ? types.resolve(key).props.Name : undefined;
    return {
      item,
      n: ingredient.Count || 1,
      en: texts.localize(displayName, 'en') || item,
      ru: texts.localize(displayName, 'ru') || item,
    };
  });
}

function byPrinterThenPosition(a, b) {
  if (a.printer !== b.printer) return a.printer > b.printer ? 1 : -1;
  return a.seq - b.seq || a.varSeq - b.varSeq;
}

// Text lookup as the game does it: a string table entry is the .locres string under the table's namespace and
// the entry key, falling back to the table's source string; a base text is the .locres string under its own
// namespace and key, falling back to its source string. Texts that fall back are counted and reported.
function createLocalizer(pak) {
  const locres = {
    en: parseLocres(pak.read(LOCRES_FILES.en)),
    ru: parseLocres(pak.read(LOCRES_FILES.ru)),
  };
  const tables = new Map();
  const untranslated = {en: new Set(), ru: new Set()};
  const unresolved = new Set();

  function stringTable(tablePath) {
    if (!tables.has(tablePath)) {
      try {
        tables.set(tablePath, readStringTable(pak, tablePath));
      } catch (error) {
        console.warn(`string table ${tablePath} could not be read: ${error.message}`);
        tables.set(tablePath, null);
      }
    }
    return tables.get(tablePath);
  }

  function localize(text, language) {
    if (text === undefined || text === null) return null;
    if (typeof text === 'string') return text;
    const strings = locres[language];
    if (text.table !== undefined) {
      const table = stringTable(text.table);
      const localized = table ? strings[table.namespace]?.[text.key] : undefined;
      if (localized) return localized;
      const source = table?.entries[text.key];
      if (source) {
        untranslated[language].add(`${text.table} ${text.key}`);
        return source;
      }
      unresolved.add(`${text.table} ${text.key}`);
      return text.key;
    }
    if (text.key !== undefined) {
      const localized = strings[text.ns ?? '']?.[text.key];
      if (localized) return localized;
      if (text.src) untranslated[language].add(`${text.ns ?? ''} ${text.key}`);
      return text.src;
    }
    return null;
  }

  function reportMisses() {
    for (const [language, keys] of Object.entries(untranslated)) {
      if (keys.size) console.warn(`${keys.size} texts have no ${language} translation and use the source string`);
    }
    if (unresolved.size) {
      console.warn(`${unresolved.size} string table entries were not found; the key is shown instead:`);
      for (const entry of unresolved) console.warn(`  ${entry}`);
    }
  }

  return {localize, reportMisses};
}

// The steps after this one read data/catalog.json when run on their own.
export function load(context) {
  return context.catalog ?? readInput(FILES.catalog, name);
}

if (isMainModule(import.meta.url)) runSteps([{name, run}]);
