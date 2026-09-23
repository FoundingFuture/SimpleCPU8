// Extracts the demo programs of the browser version into examples/<name>/<name>.asm.
//
// The browser project keeps every demo as a TypeScript template string:
// the short ones in the DEMOS table of packages/ui/src/main.ts, the long
// ones as *-src.ts modules that main.ts imports. This script evaluates the
// modules and copies the resolved text out, so the assembly here is byte
// for byte what the picker loads.
//
//   /opt/node22/bin/node --experimental-strip-types examples/extract-demos.mjs [ref-dir]
//
// ref-dir is the browser project's root. It defaults to ../SimpleCPU beside
// this repository. Node 22 runs the .ts modules once their .js import
// suffixes are rewritten, so the needed modules are copied to
// build-scratch/demo-src first. No demo names an external asset through
// .file, .image or .sample: the MIDI, the mazes, the sprites and the samples
// were generated into db lines by the reference's scripts.

import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");
const ref = resolve(process.argv[2] ?? join(root, "..", "SimpleCPU"));
const uiSrc = join(ref, "packages", "ui", "src");
if (!existsSync(join(uiSrc, "main.ts"))) {
  console.error(`no browser project at ${ref}: expected packages/ui/src/main.ts`);
  process.exit(1);
}

// Copy the demo modules to scratch with .js imports pointing at .ts files.
const scratch = join(root, "build-scratch", "demo-src");
mkdirSync(scratch, { recursive: true });
const modules = readdirSync(uiSrc).filter((f) => /^(.*-src|pacman-.*)\.ts$/.test(f));
for (const f of modules) {
  const text = readFileSync(join(uiSrc, f), "utf8").replace(/from "(\.\/[\w-]+)\.js"/g, 'from "$1.ts"');
  writeFileSync(join(scratch, f), text);
}
const exported = {};
for (const f of modules) {
  Object.assign(exported, await import(pathToFileURL(join(scratch, f)).href));
}

// The DEMOS table names the demos. A value is a template literal in
// main.ts, an alias of another name, or an export of a module.
const mainTs = readFileSync(join(uiSrc, "main.ts"), "utf8");
const block = /const DEMOS: Record<string, string> = \{([\s\S]*?)\n\};/.exec(mainTs);
if (!block) throw new Error("main.ts no longer has a DEMOS table");

function resolveSource(name, depth = 0) {
  if (depth > 4) throw new Error(`demo source ${name} aliases in a circle`);
  const literal = new RegExp(`^const ${name} = \`([\\s\\S]*?)\`;$`, "m").exec(mainTs);
  if (literal) {
    if (literal[1].includes("\\") || literal[1].includes("${")) {
      throw new Error(`${name} needs evaluation, copy it as a module instead`);
    }
    return literal[1];
  }
  const alias = new RegExp(`^const ${name} = (\\w+);$`, "m").exec(mainTs);
  if (alias) return resolveSource(alias[1], depth + 1);
  if (typeof exported[name] === "string") return exported[name];
  throw new Error(`demo source ${name} is neither in main.ts nor exported by a module`);
}

let count = 0;
for (const entry of block[1].matchAll(/^\s*(\w+):\s*(\w+),/gm)) {
  const [, key, name] = entry;
  const dir = join(here, key);
  mkdirSync(dir, { recursive: true });
  let text = resolveSource(name);
  if (!text.endsWith("\n")) text += "\n";
  writeFileSync(join(dir, `${key}.asm`), text);
  count++;
}
console.log(`extracted ${count} demos into ${here}`);
