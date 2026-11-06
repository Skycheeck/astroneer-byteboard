// Step 1: parse every item type blueprint in the pak.
// Input: the pak, Astro/Content/Items/ItemTypes/**/*.uasset + .uexp (UE 4.27 cooked packages, lib/uasset.mjs).
// Output: data/itemtypes.json and context.itemTypes: {"<path under /Game/Items/ItemTypes/>": {cls, sup, props}}
//   cls    the item type's class path, e.g. "/Game/Items/ItemTypes/Components/Generator_Small.Generator_Small_C"
//   sup    its parent class path ("/Script/Astro.ItemType" or another item type's class); null if not a blueprint
//   props  the class default object's tagged properties (value encoding in lib/uasset.mjs), with the
//          ItemCatalogData subobject's properties under CatalogData
// An item type that cannot be read stops the step, so a parse error never turns into a shorter catalog.
import {ToolError, isMainModule, readInput, runSteps, writeOutput} from '../lib/context.mjs';
import {FILES, ITEMTYPES_PACKAGE_DIR, displayPath, packageToPakPath, pakPathToPackage} from '../lib/paths.mjs';
import {parseAsset} from '../lib/uasset.mjs';

export const name = 'dump-itemtypes';

const MAX_WARNINGS_SHOWN = 20;

export function run(context) {
  const pak = context.pak;
  const directory = packageToPakPath(ITEMTYPES_PACKAGE_DIR);
  const itemTypes = {};
  const failures = [];
  const warnings = [];
  for (const file of pak.files.keys()) {
    if (!file.startsWith(directory) || !file.endsWith('.uasset')) continue;
    const base = file.slice(0, -'.uasset'.length);
    const key = base.slice(directory.length);
    try {
      const asset = parseAsset(pak.read(file), pak.read(base + '.uexp'), pakPathToPackage(file));
      itemTypes[key] = toItemType(asset);
      for (const warning of asset.warnings) warnings.push(`${key}: ${warning}`);
    } catch (error) {
      failures.push(`${key}: ${error.message}`);
    }
  }

  if (warnings.length) {
    console.warn(`${warnings.length} properties could not be read and are left out:`);
    for (const warning of warnings.slice(0, MAX_WARNINGS_SHOWN)) console.warn(`  ${warning}`);
    if (warnings.length > MAX_WARNINGS_SHOWN) console.warn(`  … and ${warnings.length - MAX_WARNINGS_SHOWN} more`);
  }
  if (failures.length) {
    throw new ToolError(`${failures.length} item types could not be read:\n  ${failures.join('\n  ')}`);
  }

  writeOutput(FILES.itemTypes, JSON.stringify(itemTypes, null, 1));
  context.itemTypes = itemTypes;
  console.log(`${displayPath(FILES.itemTypes)}: ${Object.keys(itemTypes).length} item types`);
}

function toItemType(asset) {
  const defaults = asset.exports.find(e => e.name.startsWith('Default__')) ?? asset.exports[0];
  const catalogData = asset.exports.find(e => e.className === 'ItemCatalogData');
  const blueprintClass = asset.exports.find(e => e.className === 'BlueprintGeneratedClass');
  for (const required of [defaults, catalogData]) {
    if (required && required.propertiesEnd === null) throw new Error(`properties of ${required.name} are unreadable`);
  }
  const props = {...defaults.props};
  if (catalogData) props.CatalogData = catalogData.props;
  return {
    cls: defaults.classPath,
    sup: blueprintClass ? blueprintClass.superPath : null,
    props,
  };
}

// The steps after this one read data/itemtypes.json when run on their own.
export function load(context) {
  return context.itemTypes ?? readInput(FILES.itemTypes, name);
}

if (isMainModule(import.meta.url)) runSteps([{name, run}]);
