// Builds the release archive dist/Byteboard-<version>.zip from a built overlay:
//   Byteboard/                     unpacks into <game>/Astro/Binaries/Win64/ue4ss/Mods/
//     Byteboard.dll                overlay/bin/Byteboard.dll (run overlay\build.cmd first)
//     <tracked files of mod/Byteboard>   enabled.txt, README.txt, Scripts/**
//     LICENSE.txt, THIRD_PARTY_NOTICES.md
// Only files git tracks are packed, so the local in/, out/ and data/ folders (game data, logs) never ship.
// The version comes from overlay/CMakeLists.txt and must match VERSION in Scripts/main.lua; with
// `--tag vX.Y.Z` (the CI release job) it must match the tag as well.
//   node tools/package.mjs [--tag vX.Y.Z]
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {ROOT} from './lib/paths.mjs';

const MOD = 'mod/Byteboard';
const DLL = 'overlay/bin/Byteboard.dll';
const RUNTIME_FOLDERS = ['in/', 'out/', 'data/'];

function fail(message) {
  console.error(`package: ${message}`);
  process.exit(1);
}

function readVersions() {
  const cmake = fs.readFileSync(path.join(ROOT, 'overlay/CMakeLists.txt'), 'utf8');
  const lua = fs.readFileSync(path.join(ROOT, MOD, 'Scripts/main.lua'), 'utf8');
  const dllVersion = cmake.match(/project\(Byteboard VERSION (\d+\.\d+\.\d+)/)?.[1];
  const luaVersion = lua.match(/^local VERSION = "(\d+\.\d+\.\d+)"/m)?.[1];
  if (!dllVersion || !luaVersion) fail('cannot read the version from overlay/CMakeLists.txt or Scripts/main.lua');
  if (dllVersion !== luaVersion) fail(`version mismatch: CMakeLists.txt ${dllVersion}, main.lua ${luaVersion}`);
  return dllVersion;
}

function trackedModFiles() {
  const out = execFileSync('git', ['ls-files', '-z', '--', MOD], {cwd: ROOT, encoding: 'utf8'});
  const files = out.split('\0').filter(Boolean).map(file => file.slice(MOD.length + 1));
  const runtime = files.filter(file => RUNTIME_FOLDERS.some(folder => file.startsWith(folder)));
  if (runtime.length) fail(`runtime files are tracked and would ship: ${runtime.join(', ')}`);
  if (!files.includes('Scripts/main.lua')) fail(`${MOD}/Scripts/main.lua is not tracked`);
  return files;
}

// A minimal ZIP writer (deflate, no zip64): the archive has a few dozen small files.
const CRC_TABLE = Array.from({length: 256}, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function dosDateTime(date) {
  const time = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
  const day = ((date.getFullYear() - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
  return {time, day};
}

function zip(entries, date) {
  const {time, day} = dosDateTime(date);
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const {name, data} of entries) {
    const nameBytes = Buffer.from(name, 'utf8');
    const packed = zlib.deflateRawSync(data, {level: 9});
    const crc = crc32(data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);       // version needed
    local.writeUInt16LE(0x0800, 6);   // UTF-8 names
    local.writeUInt16LE(8, 8);        // deflate
    local.writeUInt16LE(time, 10);
    local.writeUInt16LE(day, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(packed.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBytes.length, 26);
    locals.push(local, nameBytes, packed);

    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);     // made by: MS-DOS/FAT
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(0x0800, 8);
    central.writeUInt16LE(8, 10);
    central.writeUInt16LE(time, 12);
    central.writeUInt16LE(day, 14);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(packed.length, 20);
    central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(nameBytes.length, 28);
    central.writeUInt32LE(offset, 42);
    centrals.push(central, nameBytes);
    offset += local.length + nameBytes.length + packed.length;
  }
  const centralSize = centrals.reduce((sum, part) => sum + part.length, 0);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(centralSize, 12);
  end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, ...centrals, end]);
}

function main() {
  const tagIndex = process.argv.indexOf('--tag');
  const tag = tagIndex > 0 ? process.argv[tagIndex + 1] : null;
  const version = readVersions();
  if (tag && tag !== `v${version}`) fail(`tag ${tag} does not match the version ${version}`);

  const dllPath = path.join(ROOT, DLL);
  if (!fs.existsSync(dllPath)) fail(`${DLL} is missing: run overlay\\build.cmd first`);
  // The DLL logs "Byteboard <version> started" (overlay/src/dllmain.cpp); a DLL without that string
  // was built from other sources or for another version.
  const dll = fs.readFileSync(dllPath);
  if (!dll.includes(Buffer.from(`Byteboard ${version} started`, 'ascii'))) {
    fail(`${DLL} was not built from version ${version} of this source: run overlay\\build.cmd again`);
  }

  const entries = trackedModFiles().map(file => ({
    name: `Byteboard/${file}`,
    data: fs.readFileSync(path.join(ROOT, MOD, file)),
  }));
  entries.push({name: 'Byteboard/Byteboard.dll', data: dll});
  entries.push({name: 'Byteboard/LICENSE.txt', data: fs.readFileSync(path.join(ROOT, 'LICENSE'))});
  entries.push({name: 'Byteboard/THIRD_PARTY_NOTICES.md', data: fs.readFileSync(path.join(ROOT, 'THIRD_PARTY_NOTICES.md'))});
  entries.sort((a, b) => a.name.localeCompare(b.name));

  const epoch = Number(process.env.SOURCE_DATE_EPOCH);
  const archive = zip(entries, Number.isFinite(epoch) && epoch > 0 ? new Date(epoch * 1000) : new Date());
  const target = path.join(ROOT, 'dist', `Byteboard-${version}.zip`);
  fs.mkdirSync(path.dirname(target), {recursive: true});
  fs.writeFileSync(target, archive);
  console.log(`dist/${path.basename(target)}: ${entries.length} files, ${archive.length} bytes`);
  for (const {name, data} of entries) console.log(`  ${name} (${data.length})`);
}

main();
