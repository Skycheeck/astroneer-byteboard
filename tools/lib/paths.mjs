// Where the tools read and write, and how game object paths map to files in the pak.
// Every path is resolved from this file's location, so the tools work from any working directory.
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const DATA = path.join(ROOT, 'data');
export const OVERLAY = path.join(ROOT, 'overlay');

// The game's main pak: ASTRONEER_PAK, or the default Steam install location.
export const PAK = process.env.ASTRONEER_PAK
  || 'C:/Program Files (x86)/Steam/steamapps/common/ASTRONEER/Astro/Content/Paks/pakchunk0-WindowsNoEditor.pak';

export const FILES = {
  itemTypes: path.join(DATA, 'itemtypes.json'),
  catalog: path.join(DATA, 'catalog.json'),
  icons: path.join(DATA, 'icons.json'),
  items: path.join(DATA, 'items.json'),
  overlayCatalog: path.join(OVERLAY, 'catalog.tsv'),
  overlayTaxonomy: path.join(OVERLAY, 'taxonomy.tsv'),
  overlayIcons: path.join(OVERLAY, 'icons.rgba'),
};

// "data/catalog.json" for messages.
export function displayPath(file) {
  return path.relative(ROOT, file).split(path.sep).join('/');
}

// Game content: the package "/Game/X/Y" is the file "Astro/Content/X/Y.uasset" (+ ".uexp") in the pak.
const GAME_ROOT = '/Game/';
const CONTENT_DIR = 'Astro/Content/';
export const ITEMTYPES_PACKAGE_DIR = '/Game/Items/ItemTypes/';

export function packageToPakPath(packagePath) {
  if (!packagePath.startsWith(GAME_ROOT)) throw new Error(`not a /Game/ package: ${packagePath}`);
  return CONTENT_DIR + packagePath.slice(GAME_ROOT.length);
}

export function pakPathToPackage(pakPath) {
  if (!pakPath.startsWith(CONTENT_DIR)) throw new Error(`not under ${CONTENT_DIR}: ${pakPath}`);
  return GAME_ROOT + pakPath.slice(CONTENT_DIR.length).replace(/\.[^./]+$/, '');
}

// "/Game/A/B.B_C" → "/Game/A/B"
export function packageOf(objectPath) {
  const dot = objectPath.indexOf('.');
  return dot < 0 ? objectPath : objectPath.slice(0, dot);
}

// The name docs/PROTOCOL.md compares classes by: the part after the last ".", without a trailing "_C".
// "/Game/Components_Small/Splitter/Splitter_3Way.Splitter_3Way_C" → "Splitter_3Way"
export function objectName(objectPath) {
  return objectPath.slice(objectPath.lastIndexOf('.') + 1).replace(/_C$/, '');
}
