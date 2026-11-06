// Regenerates every game-derived file from your own copy of the game; rerun it after a game patch.
//   dump-itemtypes  → data/itemtypes.json
//   build-catalog   → data/catalog.json
//   build-icons     → data/icons.json
//   build-items     → data/items.json
//   export-overlay  → mod/Byteboard/data/{catalog.tsv,taxonomy.tsv,icons.rgba}
// The steps run in this process and share one open pak; each can also run alone (node tools/steps/<step>.mjs),
// reading the earlier steps' files from data/. The pak path comes from ASTRONEER_PAK (default in lib/paths.mjs).
import {runSteps} from './lib/context.mjs';
import * as buildCatalog from './steps/build-catalog.mjs';
import * as buildIcons from './steps/build-icons.mjs';
import * as buildItems from './steps/build-items.mjs';
import * as dumpItemTypes from './steps/dump-itemtypes.mjs';
import * as exportOverlay from './steps/export-overlay.mjs';

const STEPS = [dumpItemTypes, buildCatalog, buildIcons, buildItems, exportOverlay];

runSteps(STEPS, {openPakFirst: true});
