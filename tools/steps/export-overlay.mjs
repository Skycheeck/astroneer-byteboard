// Step 5: the files the in-game overlay reads, in the formats docs/PROTOCOL.md defines.
// Input: context.items or data/items.json (items, groups, printers, icons); the icon pixels from context.icons,
// or decoded from the PNGs in data/items.json when the step runs on its own.
// Output:
//   overlay/catalog.tsv   a '#' header line, then one item per line: id, classPath, nameRu, nameEn, group (key),
//                         subgroup, printer (key), cost, recipe ("<count>× <name>" joined by ", "), icon (index
//                         into icons.rgba, -1 for none), description, pickup (class path)
//   overlay/taxonomy.tsv  "group <key> <label>" lines, then "printer <key> <category> <label> <short>" lines,
//                         each in display order
//   overlay/icons.rgba    the icon atlas (layout in lib/atlas.mjs), in the order of data/items.json's icons
import {ICON_SIZE, buildAtlas} from '../lib/atlas.mjs';
import {ToolError, isMainModule, runSteps, writeOutput} from '../lib/context.mjs';
import {FILES, displayPath} from '../lib/paths.mjs';
import {decodePng, fromDataUri} from '../lib/png.mjs';
import * as buildItems from './build-items.mjs';

export const name = 'export-overlay';

const CATALOG_COLUMNS = [
  'id', 'classPath', 'nameRu', 'nameEn', 'group', 'subgroup', 'printer', 'cost', 'recipe', 'icon', 'description',
  'pickup',
];

export function run(context) {
  const data = buildItems.load(context);
  const iconNames = Object.keys(data.icons);

  writeOutput(FILES.overlayCatalog, catalogTsv(data.items, iconNames));
  writeOutput(FILES.overlayTaxonomy, taxonomyTsv(data.groups, data.printers));
  const {atlas, width, height} = buildAtlas(iconNames.length, index => iconPixels(context, data, iconNames[index]));
  writeOutput(FILES.overlayIcons, atlas);

  console.log(`${displayPath(FILES.overlayCatalog)}: ${data.items.length} items; `
    + `${displayPath(FILES.overlayTaxonomy)}: ${data.groups.length} groups, ${data.printers.length} printers; `
    + `${displayPath(FILES.overlayIcons)}: ${iconNames.length} icons, ${width}x${height}`);
}

function catalogTsv(items, iconNames) {
  const iconIndex = new Map(iconNames.map((iconName, index) => [iconName, index]));
  const lines = ['#' + CATALOG_COLUMNS.join('\t')];
  for (const item of items) {
    const recipe = item.recipe.map(({count, ru}) => `${count}× ${ru}`).join(', ');
    const row = [
      item.id,
      item.classPath,
      item.nameRu,
      item.nameEn,
      item.group,
      item.subgroup,
      item.printer,
      item.cost,
      recipe,
      iconIndex.has(item.icon) ? iconIndex.get(item.icon) : -1,
      item.description,
      item.pickup,
    ];
    lines.push(row.map(tsvCell).join('\t'));
  }
  return lines.join('\n') + '\n';
}

function taxonomyTsv(groups, printers) {
  const lines = [];
  for (const [key, label] of groups) lines.push(['group', key, label].map(tsvCell).join('\t'));
  for (const printer of printers) {
    lines.push(['printer', printer.key, printer.category, printer.label, printer.short].map(tsvCell).join('\t'));
  }
  return lines.join('\n') + '\n';
}

// Tabs and line breaks would split a cell or a line, so they become spaces.
function tsvCell(value) {
  return String(value ?? '').replace(/[\t\r\n]+/g, ' ').trim();
}

function iconPixels(context, data, iconName) {
  const pixels = context.icons?.get(iconName)?.pixels;
  if (pixels) return pixels;
  const png = decodePng(fromDataUri(data.icons[iconName]));
  if (png.width !== ICON_SIZE || png.height !== ICON_SIZE) {
    throw new ToolError(`icon ${iconName} in ${displayPath(FILES.items)} is ${png.width}x${png.height}, `
      + `not ${ICON_SIZE}x${ICON_SIZE}; rerun "node tools/extract.mjs"`);
  }
  return png.pixels;
}

if (isMainModule(import.meta.url)) runSteps([{name, run}]);
