// Plumbing shared by the extraction steps: the context that carries results from step to step, reading a
// previous step's output when a step runs on its own, writing outputs, and running steps with readable errors.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {FormatError} from './binreader.mjs';
import {openPak} from './pak.mjs';
import {PAK, displayPath} from './paths.mjs';

// A problem the user can fix (pak not found, a missing input file); printed without a stack trace.
export class ToolError extends Error {}

function openGamePak() {
  if (!fs.existsSync(PAK)) {
    throw new ToolError(
      `ASTRONEER pak not found: ${PAK}\n`
      + 'Set ASTRONEER_PAK to the full path of Astro/Content/Paks/pakchunk0-WindowsNoEditor.pak in your game folder.');
  }
  return openPak(PAK);
}

// Steps store their results on the context (itemTypes, catalog, icons, items) for the steps after them, so
// a full run opens the pak once and never reads back its own files. A step run on its own gets an empty
// context and reads its inputs from data/.
function createContext() {
  let pak = null;
  return {
    openPak() {
      pak ??= openGamePak();
      return pak;
    },
    get pak() {
      return this.openPak();
    },
    close() {
      pak?.close();
      pak = null;
    },
  };
}

// A JSON file an earlier step wrote.
export function readInput(file, producerStep) {
  const rerun = `run "node tools/extract.mjs" (or "node tools/steps/${producerStep}.mjs")`;
  if (!fs.existsSync(file)) throw new ToolError(`${displayPath(file)} is missing: ${rerun} first.`);
  return readJson(file, `${rerun} again`);
}

// remedy tells the user what to do when the file is not valid JSON.
export function readJson(file, remedy) {
  const text = fs.readFileSync(file, 'utf8');
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new ToolError(`${displayPath(file)} is not valid JSON (${error.message}): ${remedy}.`);
  }
}

export function writeOutput(file, content) {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, content);
}

// Runs step modules ({name, run(context)}) in order. With openPakFirst the pak is opened and checked before any
// step runs. Returns false, after printing the error and setting a non-zero exit code, if a step fails.
export function runSteps(steps, {openPakFirst = false} = {}) {
  const context = createContext();
  let current = 'extract';
  try {
    if (openPakFirst) context.openPak();
    for (const step of steps) {
      current = step.name;
      step.run(context);
    }
    return true;
  } catch (error) {
    const expected = error instanceof ToolError || error instanceof FormatError;
    console.error(`${current}: ${expected ? error.message : error.stack}`);
    process.exitCode = 1;
    return false;
  } finally {
    context.close();
  }
}

// True when the module with this import.meta.url is the script node was started with.
export function isMainModule(moduleUrl) {
  const script = process.argv[1];
  if (!script) return false;
  try {
    return fs.realpathSync(script) === fs.realpathSync(fileURLToPath(moduleUrl));
  } catch {
    return false;
  }
}
