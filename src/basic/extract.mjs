// Extract the interpreter's C sources from the browser project's TypeScript
// strings. One time tool, kept so the extraction can be repeated after an
// upstream change.
//
//   node --experimental-strip-types extract.mjs <ref>/packages/ui/src/basic [outdir]
//
// Each source module exports one string constant. The files are written
// with the string's exact value, so a line number the compiler reports
// matches the browser project's.

import { pathToFileURL } from "node:url";
import { writeFileSync } from "node:fs";
import { join } from "node:path";

const src = process.argv[2];
const out = process.argv[3] ?? ".";
if (!src) {
  console.error("usage: extract.mjs <basic source dir> [outdir]");
  process.exit(2);
}

const files = [
  ["main.c", "main-c.ts", "MAIN_C"],
  ["term.c", "term-c.ts", "TERM_C"],
  ["lex.c", "lex-c.ts", "LEX_C"],
  ["expr.c", "expr-c.ts", "EXPR_C"],
  ["strings.c", "strings-c.ts", "STRINGS_C"],
  ["edit.c", "edit-c.ts", "EDIT_C"],
  ["run.c", "run-c.ts", "RUN_C"],
  ["basic.h", "basic-h.ts", "BASIC_H"],
];

for (const [name, module, symbol] of files) {
  const m = await import(pathToFileURL(join(src, module)).href);
  writeFileSync(join(out, name), m[symbol]);
  console.log(`${name}: ${m[symbol].length} bytes`);
}
