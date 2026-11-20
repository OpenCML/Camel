#!/usr/bin/env node
// Comments say what the code does and why; they do not record how the project got there.
// This check refuses the traces that are unambiguous: a path into the handover pages, a dated
// ruling, a role name, a host name or a local path, and a calendar date written into a comment.
// It reads comment text only (after `//`, `///`, `#` in CMake, and inside `/* */`), so a date or a
// path that is data in a string literal is not its business.
//
// Usage: node scripts/check-process-traces.mjs [--root <dir>]   exit 0 when clean, 1 with a list
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const root = args.includes('--root') ? args[args.indexOf('--root') + 1] : process.cwd();
const dirs = ['src', 'include', 'modules', 'stdlib', 'tools'];
const exts = new Set(['.cpp', '.h', '.hpp', '.cc', '.c', '.inc', '.def', '.cml', '.cmake']);

const pattern = new RegExp(
  [
    'docs/handover',
    '\\bruling of 20\\d\\d',
    "\\bauthor'?s (ruling|answer|call|spec)",
    '\\(author[,)]',
    '\\bdecision-maker\\b',
    '负责人|决策者|协调者',
    '88zjwei',
    '/home/zjwei',
    'camel-work',
    '\\b20(25|26)-[01]\\d-[0-3]\\d\\b',
  ].join('|'),
);

function commentText(line, state, lang) {
  // Returns [text inside comments on this line, still-in-block-comment].
  let text = '';
  let i = 0;
  let inBlock = state;
  let inString = null;
  while (i < line.length) {
    if (inBlock) {
      const end = line.indexOf('*/', i);
      if (end < 0) { text += line.slice(i); return [text, true]; }
      text += line.slice(i, end); i = end + 2; inBlock = false; continue;
    }
    const c = line[i];
    if (inString) {
      if (c === '\\') { i += 2; continue; }
      if (c === inString) inString = null;
      i++; continue;
    }
    if (c === '"' || c === "'") { inString = c; i++; continue; }
    if (lang === 'cmake' && c === '#') { text += line.slice(i + 1); break; }
    if (c === '/' && line[i + 1] === '/') { text += line.slice(i + 2); break; }
    if (c === '/' && line[i + 1] === '*') { inBlock = true; i += 2; continue; }
    i++;
  }
  return [text, inBlock];
}

function* walk(dir) {
  if (!fs.existsSync(dir)) return;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) yield* walk(p);
    else if (exts.has(path.extname(entry.name)) || entry.name === 'CMakeLists.txt') yield p;
  }
}

const hits = [];
for (const d of dirs) {
  for (const file of walk(path.join(root, d))) {
    const lang = file.endsWith('.cmake') || file.endsWith('CMakeLists.txt') ? 'cmake' : 'c';
    const lines = fs.readFileSync(file, 'utf8').split('\n');
    let inBlock = false;
    lines.forEach((line, k) => {
      const [text, still] = commentText(line, inBlock, lang);
      inBlock = still;
      if (text && pattern.test(text)) hits.push(`${path.relative(root, file)}:${k + 1}: ${line.trim().slice(0, 160)}`);
    });
  }
}

if (hits.length) {
  console.log(`process traces in comments: ${hits.length}`);
  for (const h of hits) console.log('  ' + h);
  console.log('A comment says what the code does and why; drop the dated ruling, the handover path, the role or the host.');
  process.exit(1);
}
console.log('process traces in comments: 0');
