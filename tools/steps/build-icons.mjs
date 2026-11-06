// Step 3: extract the catalog's icons as 64×64 RGBA images.
// Input: context.catalog or data/catalog.json; the icon textures from the pak (lib/texture.mjs, lib/dxt.mjs).
// Output: data/icons.json, {"<icon name>": "data:image/png;base64,..."} in first-use order, and context.icons,
// a Map from icon name to {pixels, dataUri}. An icon that cannot be decoded is reported and left out; its items
// show a letter instead. When no icon at all can be decoded the step fails and leaves data/icons.json as it was.
import {ICON_SIZE} from '../lib/atlas.mjs';
import {FormatError} from '../lib/binreader.mjs';
import {ToolError, isMainModule, readInput, runSteps, writeOutput} from '../lib/context.mjs';
import {decodeToRgba, resizeBox} from '../lib/dxt.mjs';
import {FILES, displayPath} from '../lib/paths.mjs';
import {encodePng, toDataUri} from '../lib/png.mjs';
import {readTexture2D} from '../lib/texture.mjs';
import * as buildCatalog from './build-catalog.mjs';

export const name = 'build-icons';

export function run(context) {
  const catalog = buildCatalog.load(context);
  const iconPaths = collectIconPaths(catalog);
  // Opened here, outside the per-icon error handling, so a missing or unreadable pak fails the step.
  const pak = context.pak;
  const icons = new Map();
  const failures = [];
  for (const [iconName, objectPath] of iconPaths) {
    try {
      const pixels = readIcon(pak, objectPath);
      icons.set(iconName, {pixels, dataUri: toDataUri(encodePng(pixels, ICON_SIZE, ICON_SIZE))});
    } catch (error) {
      if (!(error instanceof FormatError)) throw error;
      failures.push(`${iconName}: ${error.message}`);
    }
  }
  if (iconPaths.size > 0 && icons.size === 0) {
    throw new ToolError(`none of the ${iconPaths.size} icons could be decoded:\n  ${failures.join('\n  ')}`);
  }

  const uris = Object.fromEntries([...icons].map(([iconName, icon]) => [iconName, icon.dataUri]));
  writeOutput(FILES.icons, JSON.stringify(uris));
  context.icons = icons;
  console.log(`${displayPath(FILES.icons)}: ${icons.size} of ${iconPaths.size} icons`);
  if (failures.length) console.warn(`icons that could not be decoded:\n  ${failures.join('\n  ')}`);
}

// The texture's top mip as ICON_SIZE×ICON_SIZE RGBA8 pixels.
function readIcon(pak, objectPath) {
  const texture = readTexture2D(pak, objectPath);
  const pixels = decodeToRgba(texture);
  if (texture.width === ICON_SIZE && texture.height === ICON_SIZE) return pixels;
  return resizeBox(pixels, texture.width, texture.height, ICON_SIZE, ICON_SIZE);
}

// Icon name → texture object path, in first-use order. The pages and the overlay key icons by name, so two
// different textures with the same name would be ambiguous.
function collectIconPaths(catalog) {
  const paths = new Map();
  for (const item of catalog) {
    if (!item.iconPath) continue;
    const known = paths.get(item.icon);
    if (known && known !== item.iconPath) {
      throw new ToolError(`two icon textures are named ${item.icon}: ${known} and ${item.iconPath}`);
    }
    paths.set(item.icon, item.iconPath);
  }
  return paths;
}

// The steps after this one read data/icons.json, {"<icon name>": PNG data URI}, when run on their own; in a
// full run the same object is built from context.icons.
export function load(context) {
  if (context.icons) return Object.fromEntries([...context.icons].map(([iconName, icon]) => [iconName, icon.dataUri]));
  return readInput(FILES.icons, name);
}

if (isMainModule(import.meta.url)) runSteps([{name, run}]);
