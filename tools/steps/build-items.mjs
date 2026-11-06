// Step 4: the research entries the overlay shows, with their groups and icons.
// Input: context.catalog or data/catalog.json; context.icons or data/icons.json.
// Output: data/items.json and context.items, {items, groups, printers, icons}:
//   items     the research entries (see toEntry), in catalog order
//   groups    [[key, label], ...] in display order (lib/taxonomy.mjs)
//   printers  [{key, category, label, short}, ...] in display order (lib/taxonomy.mjs)
//   icons     {"<icon name>": PNG data URI} for the icons the entries use, in first-use order
// The research entries are the catalog items below NOT_RESEARCHABLE_COST that EXCLUDE does not name.
import {isMainModule, readInput, runSteps, writeOutput} from '../lib/context.mjs';
import {FILES, displayPath} from '../lib/paths.mjs';
import {EXCLUDE, GROUPS, NOT_RESEARCHABLE_COST, PRINTERS, classify} from '../lib/taxonomy.mjs';
import * as buildCatalog from './build-catalog.mjs';
import * as buildIcons from './build-icons.mjs';

export const name = 'build-items';

export function run(context) {
  const catalog = buildCatalog.load(context);
  const iconUris = buildIcons.load(context);
  const researchable = catalog.filter(item => item.cost < NOT_RESEARCHABLE_COST && !EXCLUDE.test(item.id));
  warnAboutLookAlikes(researchable);

  const unclassified = [];
  const items = researchable.map(item => {
    const {group, subgroup} = classify(item);
    if (!group) unclassified.push(item.id);
    return toEntry(item, group, subgroup);
  });

  const icons = {};
  for (const item of items) {
    if (item.icon && iconUris[item.icon]) icons[item.icon] = iconUris[item.icon];
  }

  const result = {items, groups: GROUPS, printers: PRINTERS, icons};
  writeOutput(FILES.items, JSON.stringify(result, null, 1));
  context.items = result;
  console.log(`${displayPath(FILES.items)}: ${items.length} items, ${Object.keys(icons).length} icons`);
  if (unclassified.length) {
    console.warn(`not in any group (add a rule in tools/lib/taxonomy.mjs): ${unclassified.join(', ')}`);
  }
}

function toEntry(item, group, subgroup) {
  return {
    id: item.id,                        // asset name, e.g. "Generator_Small"
    classPath: item.classPath,          // the item type's class, the argument of the mod's commands
    pickup: item.pickup,                // the world actor class, which printers report as their recipe
    nameRu: item.ru,
    nameEn: item.en,
    description: (item.descRu || '').replace(/\r/g, '').trim(),
    printer: item.printer,              // printer key (lib/taxonomy.mjs PRINTERS)
    cost: item.cost,                    // research cost in bytes
    group,                              // group key (lib/taxonomy.mjs GROUPS)
    subgroup,                           // subgroup label
    recipe: item.recipe.map(ingredient => ({count: ingredient.n, ru: ingredient.ru, en: ingredient.en})),
    icon: item.icon,                    // icon name, a key of icons
  };
}

// Two entries with the same name, cost and printer are most likely one item listed twice; EXCLUDE decides.
function warnAboutLookAlikes(entries) {
  const firstBySignature = new Map();
  for (const item of entries) {
    const signature = `${item.ru}|${item.cost}|${item.printer}`;
    const first = firstBySignature.get(signature);
    if (first) {
      console.warn(`${first} and ${item.id} share name, cost and printer; `
        + 'if one is a duplicate, add it to EXCLUDE in tools/lib/taxonomy.mjs');
    } else {
      firstBySignature.set(signature, item.id);
    }
  }
}

// The steps after this one read data/items.json when run on their own.
export function load(context) {
  return context.items ?? readInput(FILES.items, name);
}

if (isMainModule(import.meta.url)) runSteps([{name, run}]);
