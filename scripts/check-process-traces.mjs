#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Comments say what the code does and why; they do not record how the project got there.
// This check refuses the traces that are unambiguous: a path into the handover pages, a dated
// ruling, a role name, a host name or a local path, a calendar date, the step a refactor had
// reached ("refactor stage 2.3", "stage 3.1", "step 2"), a lane branch name or lane code, a work
// item number (`D-<year>-<n>`, `C-<year>-<n>`), a commit hash, "this lane" / "待审" standing in for who
// decided what, and a narration of what the code used to be. It reads comment text only (after
// `//`, `///`, `#` in CMake/TOML/shell/PowerShell, and inside `/* */`), so a date or a path that is
// data in a string literal is not its business. A test plan's `name`, `desc` and `known_fail_*`
// fields are prose a person reads when they pick the case up -- the name is what the other entries
// cite -- so those count as comments: they are scanned too.
//
// The step a refactor had reached is the trace this check exists for: `refactor stage 2.3` records
// a schedule, and a schedule expires while the code does not, so the page it pointed at is gone six
// months later and the comment reads like a constraint that is not one. What a comment may say is
// what the code does and why; when the code is what a later step of a plan would have made it, the
// comment says that, not the step.
//
// The trees read are `src/`, `scripts/`, `internal/tools/` and `test/`. The first version read
// `src/` only, and while it reported green the same day's merges left work item numbers in plans,
// lane codes in `known_fail_owner`, and a dated ruling in sixty `desc` fields:
// a check that reads one tree cannot say anything about the others, and a green light from it was
// read as "the tree is clean".
//
// The reading is printed with the verdict, and a run that read nothing is red: "0 traces" and "0
// files scanned" print the same otherwise, and a checker whose pattern stopped matching anything
// would then report green forever. The reading also names the directories it read.
//
// A pattern always catches some of the same word in another domain, and the check has met this
// twice: `/*` in a shell comment is a glob (`out/*/`), not the start of a block, and `lane-1` or
// `<lane>-<time>` in a documented path is a placeholder for some lane, not the name of one. Both
// were fixed by giving the word a semantic condition -- a block only opens in C-like languages, a
// lane code holds at least one letter -- and neither by an allowlist, because an allowlist of
// one-word exceptions grows until nobody reads it while a condition is stated where the pattern is.
// When a hit turns out to be a different domain, ask which condition distinguishes it, and put that
// condition in the pattern.
//
// A tree that already carries traces has to be read before it can be worked through, so `--list`
// prints the same report and exits 0: that flag asks for the reading, not for the verdict. It is a
// flag on this script rather than a second script because a listing and a refusal must name the
// same lines -- two readers of one pattern are two patterns the day one of them is edited -- and it
// says "nothing judged" on its own line, because output that reads like a check and exits 0 is the
// gate that passes everything. A run that read nothing stays red in both modes: an empty list is
// not evidence of an empty tree.
//
// Usage: node scripts/check-process-traces.mjs --root <dir> [--list]
//   exit 0 when clean, 1 with a list, 2 when the tree to read was not named
//   `--list` prints the same list and exits 0: it lists, and judges nothing
//
// `--root` is required and has no default: the directory a process was started in is not
// necessarily the tree the reader meant, so the tree is named here by the caller.
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
// A listing is not a verdict, so the flag is read before anything is scanned and never changes what
// is scanned.
const listOnly = args.includes('--list');
const rootAt = args.indexOf('--root');
const root = rootAt >= 0 ? args[rootAt + 1] : '';
if (!root || root.startsWith('--')) {
  console.log(
    'check-process-traces: --root <dir> is required. The directory a tool was started in is not ' +
      'necessarily the tree to read, so the tree is named by the caller and never inherited.',
  );
  process.exit(2);
}
const dirs = ['src', 'scripts', 'internal/tools', 'test'];
const exts = new Set([
  '.cpp', '.h', '.hpp', '.cc', '.c', '.inc', '.def', '.cml', '.cmake',
  // The tool trees that grew traces while only `src/` was read.
  '.mjs', '.js', '.ps1', '.sh', '.toml',
]);
// A test plan's prose fields: what a person reads when deciding whether the case is the one they
// need, so the same rule applies to them as to a comment. A test's `name` belongs to that set for a
// stronger reason: it is what other entries cite (`compare_to = "writings.extract"`), so a trace in
// a name outlives the step it recorded -- renaming it breaks the references and keeping it publishes
// the step -- while the name is also the one string a reader quotes when they say which case is red.
const proseKeys = ['name', 'desc', 'known_fail_owner', 'known_fail_reason', 'note', 'title'];

const pattern = new RegExp(
  [
    'internal/(questions|plans|reports|reviews|archive)/|internal/(inbox|lanes|backlog)\\.md',
    '\\bruling of 20\\d\\d',
    "\\bauthor'?s (ruling|answer|call|spec)",
    '\\(author[,)]',
    '\\bdecision-maker\\b',
    '负责人|决策者|协调者',
    '88zjwei',
    '/home/zjwei',
    // The work root is not a trace by itself: the tools define it, and they name it `$HOME/camel-work`,
    // `~/camel-work`, `.../camel-work`, so a comment that says where a temp directory goes is
    // describing the layout the code implements. What is a trace is one host's directory, which is
    // written with a concrete home in front: a manifest row reads `<home>/<user>/camel-work/tmp/...`
    // and those rows were written on one machine. The condition is the prefix, not the word.
    '(?<=/home/[a-z0-9_-]+/)camel-work',
    '\\b20(25|26)-[01]\\d-[0-3]\\d\\b',
    // The step a refactor had reached, in the spellings it has been written in.
    '\\brefactor\\s+(?:stage|plan|step)\\b',
    '\\bstage\\s+\\d+(?:\\.\\d+)?\\b',
    '\\bstep\\s+\\d+\\.\\d+\\b',
    '\\bthe ruling\\b',
    // A lane branch name, and a commit named in a comment.
    '\\blane/[a-z0-9][a-z0-9-]*',
    '\\blane-[a-z0-9]*[a-z][a-z0-9]*(?:-[a-z0-9]+)*\\b',
    '\\bcommit\\s+[0-9a-f]{6,40}\\b',
    '`(?=[0-9a-f]*[a-f])[0-9a-f]{7,40}`',
    // Who decided it, and which work item it was: neither belongs in the code. The left side is
    // "not a letter or a digit" rather than a word boundary because a test's name is written as one
    // token: a name that ends in an underscore and then a work item number has no boundary between
    // the two, and a name is where such a number is most likely to be glued on.
    '(?<![A-Za-z0-9])[DC]-\\d{4}-\\d+(?!\\d)',
    '待审',
    '\\bthis lane\\b',
    '\\biron rule \\d+\\b',
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
    if (lang === 'hash' && c === '#') { text += line.slice(i + 1); break; }
    if (c === '/' && line[i + 1] === '/') { text += line.slice(i + 2); break; }
    // `/* */` is C's. In a shell or PowerShell file a `/*` is a glob (`out/*/`) or a path, and
    // opening a block on it never closes: everything after it reads as a comment, which turns code
    // into hits and makes the check refuse lines nobody wrote as prose.
    if (lang !== 'hash' && c === '/' && line[i + 1] === '*') { inBlock = true; i += 2; continue; }
    i++;
  }
  return [text, inBlock];
}

// A TOML line's prose: its `#` comment, and the value of a `desc` / `known_fail_*` style key. The
// value is read as text, not as data: it is the sentence a reader takes the case by.
function tomlProse(line, inBlockString) {
  const stripped = line.replace(/\s+$/, '');
  const trimmed = stripped.trimStart();
  if (inBlockString) {
    const end = stripped.indexOf('"""');
    return [end < 0 ? stripped : stripped.slice(0, end), end < 0];
  }
  const open = trimmed.match(new RegExp(`^(?:${proseKeys.join('|')})\\s*=\\s*(.*)$`));
  if (open) {
    const value = open[1];
    if (value.startsWith('"""')) {
      const rest = value.slice(3);
      const end = rest.indexOf('"""');
      return [end < 0 ? rest : rest.slice(0, end), end < 0];
    }
    const quoted = value.match(/^"((?:[^"\\]|\\.)*)"/);
    return [quoted ? quoted[1] : value, false];
  }
  const hash = stripped.indexOf('#');
  if (hash >= 0) {
    // A `#` inside a string is data, not a comment: only take it when the quotes before it balance.
    const before = stripped.slice(0, hash);
    const quotes = (before.match(/"/g) || []).length;
    if (quotes % 2 === 0) return [stripped.slice(hash + 1), false];
  }
  return ['', false];
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
    const ext = path.extname(file);
    const toml = ext === '.toml';
    const lang = toml || ext === '.cmake' || ext === '.ps1' || ext === '.sh' || file.endsWith('CMakeLists.txt')
      ? 'hash'
      : 'c';
    let inBlock = false;
    let inTomlString = false;
    lines.forEach((line, k) => {
      let text;
      if (toml) {
        const [prose, still] = tomlProse(line, inTomlString);
        inTomlString = still;
        text = prose;
        // A `#` comment on the same line as a prose key is still a comment, and may carry a trace.
        const [comment] = commentText(line, false, 'hash');
        if (comment.trim()) text += ' ' + comment;
      } else {
        const [prose, still] = commentText(line, inBlock, lang);
        inBlock = still;
        text = prose;
      }
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
  if (listOnly) {
    // The same lines a refusal prints, and the line that says which of the two this is: a reader who
    // pipes this into a gate has to meet the word "nothing judged" and not mistake a listing for a
    // pass.
    console.log(
      `process traces in comments: ${hits.length} listed, nothing judged (--list; scanned ${filesRead} files, ${commentLines} comment lines)`,
    );
    for (const h of hits) console.log('  ' + h);
    process.exit(0);
  }
  console.log(`process traces in comments: ${hits.length} (scanned ${filesRead} files, ${commentLines} comment lines)`);
  for (const h of hits) console.log('  ' + h);
  console.log('A comment says what the code does and why; drop the dated ruling, the handover path, the role,');
  console.log('the host, the refactor step, the lane name, the work item number and the commit hash.');
  console.log('The history is the commit message.');
  process.exit(1);
}
console.log(listOnly ? `${reading} (--list, nothing judged)` : reading);