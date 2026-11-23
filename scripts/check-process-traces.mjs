#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Comments say what the code does and why; they do not record how the project got there.
// This check refuses the traces that are unambiguous: a path into the handover pages, a dated
// ruling, a role name, a host name or a local path, a calendar date, the step a refactor had
// reached ("refactor stage 2.3", "stage 3.1", "step 2"), a lane branch name, a commit hash, and a
// narration of what the code used to be. It reads comment text only (after `//`, `///`, `#` in
// CMake, and inside `/* */`), so a date or a path that is data in a string literal is not its
// business.
//
// The step a refactor had reached is the trace this check exists for: `refactor stage 2.3` records
// a schedule, and a schedule expires while the code does not, so the page it pointed at is gone six
// months later and the comment reads like a constraint that is not one. What a comment may say is
// what the code does and why; when the code is what a later step of a plan would have made it, the
// comment says that, not the step.
//
// The reading is printed with the verdict, and a run that read nothing is red: "0 traces" and "0
// files scanned" print the same otherwise, and a checker whose pattern stopped matching anything
// would then report green forever. The reading also names the directory it read: this check reads
// `src/` and nothing else, so a traced comment in `test/`, `examples/` or `showcase/` is outside it.
//
// Usage: node scripts/check-process-traces.mjs [--root <dir>]   exit 0 when clean, 1 with a list
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const root = args.includes('--root') ? args[args.indexOf('--root') + 1] : process.cwd();
const dirs = ['src'];
const exts = new Set(['.cpp', '.h', '.hpp', '.cc', '.c', '.inc', '.def', '.cml', '.cmake']);

const pattern = new RegExp(
  [
    'internal/(questions|plans|reports|reviews|archive)/|internal/(inbox|lanes|backlog)\.md',
    '\\bruling of 20\\d\\d',
    "\\bauthor'?s (ruling|answer|call|spec)",
    '\\(author[,)]',
    '\\bdecision-maker\\b',
    '负责人|决策者|协调者',
    '88zjwei',
    '/home/zjwei',
    'camel-work',
    '\\b20(25|26)-[01]\\d-[0-3]\\d\\b',
    // The step a refactor had reached, in the spellings it has been written in.
    '\\brefactor\\s+(?:stage|plan|step)\\b',
    '\\bstage\\s+\\d+(?:\\.\\d+)?\\b',
    '\\bstep\\s+\\d+\\.\\d+\\b',
    '\\bthe ruling\\b',
    // A lane branch name, and a commit named in a comment.
    '\\blane/[a-z0-9][a-z0-9-]*',
    '\\bcommit\\s+[0-9a-f]{6,40}\\b',
    '`(?=[0-9a-f]*[a-f])[0-9a-f]{7,40}`',
    // What the code used to be, in the words that narration uses.
    '曾经|原本|暂时|当时',
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
let filesRead = 0;
let commentLines = 0;
const unreadable = [];
for (const d of dirs) {
  for (const file of walk(path.join(root, d))) {
    let lines;
    try {
      lines = fs.readFileSync(file, 'utf8').split('\n');
    } catch (error) {
      unreadable.push(`${path.relative(root, file)}: ${error.message}`);
      continue;
    }
    filesRead += 1;
    const lang = file.endsWith('.cmake') || file.endsWith('CMakeLists.txt') ? 'cmake' : 'c';
    let inBlock = false;
    lines.forEach((line, k) => {
      const [text, still] = commentText(line, inBlock, lang);
      inBlock = still;
      if (!text.trim()) return;
      commentLines += 1;
      if (pattern.test(text)) hits.push(`${path.relative(root, file)}:${k + 1}: ${line.trim().slice(0, 160)}`);
    });
  }
}

// A file that could not be read is a trace that was not looked for.
if (unreadable.length) {
  console.log(`process traces in comments: not read -- ${unreadable.length} file(s) failed to read`);
  for (const u of unreadable) console.log('  ' + u);
  process.exit(1);
}

// The same for a run that read nothing: it proves nothing, so it does not pass.
if (filesRead === 0 || commentLines === 0) {
  console.log(
    `process traces in comments: not read -- ${filesRead} file(s) and ${commentLines} comment line(s) under ${dirs.join(', ')}`,
  );
  console.log('Nothing was scanned, so nothing was checked. Look at --root and the extension list.');
  process.exit(1);
}

const reading = `scanned ${filesRead} files, ${commentLines} comment lines, no trace`;
if (hits.length) {
  console.log(`process traces in comments: ${hits.length} (scanned ${filesRead} files, ${commentLines} comment lines)`);
  for (const h of hits) console.log('  ' + h);
  console.log('A comment says what the code does and why; drop the dated ruling, the handover path, the role,');
  console.log('the host, the refactor step, the lane name and the commit hash. The history is the commit message.');
  process.exit(1);
}
console.log(reading);
