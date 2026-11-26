#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Comments say what the code does and why; they do not record how the project got there.
// This check refuses the traces that are unambiguous: a path into the handover pages, a dated
// ruling, a role name, a host name or a local path, a calendar date of any year, the step a refactor
// had reached (a stage or step number), a lane branch name or lane code, a work item number
// (`D-<year>-<n>`, `C-<year>-<n>`), a commit hash (in backticks, after the word that names a commit,
// or bare when it resolves as a commit in the repository), a phrase that stands in for who decided
// what, and a narration of what the code used to be. It reads comment text only
// (after `//`, `///`, `#` in CMake/TOML/shell/PowerShell, and inside `/* */`), so a date or a path
// that is data in a string literal is not its business. A test plan's `name`, `desc` and
// `known_fail_*` fields are prose a person reads when they pick the case up -- the name is what the
// other entries cite -- so those count as comments: they are scanned too.
//
// The step a refactor had reached is the trace this check exists for: `refactor stage 2.3` records
// a schedule, and a schedule expires while the code does not, so the page it pointed at is gone six
// months later and the comment reads like a constraint that is not one. What a comment may say is
// what the code does and why; when the code is what a later step of a plan would have made it, the
// comment says that, not the step.
//
// A bare hash is matched only when it is seven or more hex digits holding at least one digit and one
// letter AND `git cat-file` resolves it as a commit of the repository under `--root`. Hex that does
// not name a commit (a mask, a checksum, a colour) is not a trace, and code is never read here: a
// constant written in code is not a comment.
//
// The trees read are `src/`, `scripts/`, `internal/tools/` and `test/`. A check that reads one tree
// cannot say anything about the others, and a green light from it is read as "the tree is clean".
//
// The reading is printed with the verdict, and a whole-tree run that read nothing is red: "0 traces"
// and "0 files scanned" print the same otherwise, and a checker whose pattern stopped matching
// anything would then report green forever. The reading also names the directories it read.
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
// One question, asked of two things. A change is judged on the lines it adds: `--range <a>..<b>`
// scans the files as they stand at `<b>` and keeps the hits that fall on a line the diff from `<a>`
// to `<b>` added, so a push is refused for the traces it writes and not for the ones the tree
// already held. Without `--range` the whole working tree is read. A tree that already carries traces
// has to be read before it can be worked through, so `--list` prints the same report and exits 0: it
// asks for the reading and not for the verdict. It is a flag on this script rather than a second
// script because a listing and a refusal must name the same lines -- two readers of one pattern are
// two patterns the day one of them is edited -- and it says "nothing judged" on its own line,
// because output that reads like a check and exits 0 is the gate that passes everything.
//
// Usage: node scripts/check-process-traces.mjs --root <dir> [--range <a>..<b>] [--list]
//   exit 0 when clean (or listing), 1 with a list of the traces found
//   exit 2 when the tree was not named or the range could not be read: an unreadable range is not
//   an empty one
//
// `--root` is required and has no default: the directory a process was started in is not
// necessarily the tree the reader meant, so the tree is named here by the caller. A range is
// read in the repository that holds `--root`.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

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
const rangeAt = args.indexOf('--range');
const rangeArg = rangeAt >= 0 ? args[rangeAt + 1] : '';
let rangeBase = '';
let rangeHead = '';
if (rangeAt >= 0) {
  const parts = (rangeArg ?? '').split('..');
  if (parts.length !== 2 || !parts[0] || !parts[1] || parts[1].startsWith('.')) {
    console.log(`check-process-traces: --range wants <a>..<b>, and got '${rangeArg ?? ''}'.`);
    process.exit(2);
  }
  [rangeBase, rangeHead] = parts;
}
const ranged = rangeAt >= 0;

function git(...gitArgs) {
  return execFileSync('git', ['-C', root, '-c', 'core.quotepath=false', ...gitArgs], {
    encoding: 'utf8',
    maxBuffer: 1 << 28,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
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
    '\\b20\\d\\d-[01]\\d-[0-3]\\d\\b',
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
// Lines whose comment text holds a token that may be a bare commit hash; they are judged once the
// repository has said which tokens name a commit.
const candidates = [];
let filesRead = 0;
let commentLines = 0;
const unreadable = [];

// Seven or more hex digits with at least one digit and one letter, standing alone: a word made of
// the letters a-f ("defaced") and a plain number are not hashes, and `0x1f2e3d4` is a constant.
const hashToken = /(?<![0-9A-Za-z_])(?=[0-9a-f]*\d)(?=[0-9a-f]*[a-f])[0-9a-f]{7,40}(?![0-9A-Za-z_])/g;

// Reads one file's text. `only` is the set of 1-based line numbers whose comments are judged, or
// null for all of them. The comment state (an open block, a TOML string) is advanced on every line,
// judged or not, because a line that was not added can still open the block an added line sits in.
function scanText(key, rel, text, only) {
  const ext = path.extname(key);
  const toml = ext === '.toml';
  const lang = toml || ext === '.cmake' || ext === '.ps1' || ext === '.sh' || key.endsWith('CMakeLists.txt')
    ? 'hash'
    : 'c';
  let inBlock = false;
  let inTomlString = false;
  text.split('\n').forEach((line, k) => {
    let prose;
    if (toml) {
      const [value, still] = tomlProse(line, inTomlString);
      inTomlString = still;
      prose = value;
      // A `#` comment on the same line as a prose key is still a comment, and may carry a trace.
      const [comment] = commentText(line, false, 'hash');
      if (comment.trim()) prose += ' ' + comment;
    } else {
      const [value, still] = commentText(line, inBlock, lang);
      inBlock = still;
      prose = value;
    }
    if (!prose.trim()) return;
    if (only && !only.has(k + 1)) return;
    commentLines += 1;
    const entry = { file: key, n: k + 1, text: `${rel}:${k + 1}: ${line.trim().slice(0, 160)}` };
    if (pattern.test(prose)) {
      hits.push(entry);
    } else {
      const tokens = prose.match(hashToken);
      if (tokens) candidates.push({ entry, tokens });
    }
  });
}

function isScanned(name) {
  return exts.has(path.extname(name)) || name === 'CMakeLists.txt' || name.endsWith('/CMakeLists.txt');
}

if (!ranged) {
  for (const d of dirs) {
    for (const file of walk(path.join(root, d))) {
      let text;
      try {
        text = fs.readFileSync(file, 'utf8');
      } catch (error) {
        unreadable.push(`${path.relative(root, file)}: ${error.message}`);
        continue;
      }
      filesRead += 1;
      const rel = path.relative(root, file);
      scanText(rel.split(path.sep).join('/'), rel, text, null);
    }
  }
} else {
  // The lines the diff from the base to the head adds, per file. A header's `+++` line is only read
  // between `diff --git` and the first hunk: inside a hunk a line that begins with `++` is content.
  let diff;
  try {
    // The empty tree is the base of a history that has no earlier commit; git knows its name but
    // only reads it once the object exists, and `mktree` writes that one object and nothing else.
    if (rangeBase === '4b825dc642cb6eb9a060e54bf8d69288fbee4904') execFileSync('git', ['-C', root, 'mktree'], { input: '', stdio: ['pipe', 'ignore', 'ignore'] });
    diff = git('diff', '--no-color', '--no-ext-diff', '--no-renames', '-U0', '--diff-filter=AM', rangeBase, rangeHead, '--', ...dirs);
  } catch (error) {
    const why = `${error.stderr ?? error.message}`.trim().split('\n')[0];
    console.log(`check-process-traces: the range ${rangeArg} could not be read: ${why}`);
    console.log('An unreadable range is not an empty one, so nothing is passed on it.');
    process.exit(2);
  }
  const added = new Map();
  let current = null;
  let inHeader = false;
  for (const line of diff.split('\n')) {
    if (line.startsWith('diff --git ')) {
      inHeader = true;
      current = null;
    } else if (inHeader && line.startsWith('+++ ')) {
      const name = line.slice(4).replace(/\t.*$/, '');
      current = name === '/dev/null' ? null : name.replace(/^b\//, '');
      if (current && !added.has(current)) added.set(current, new Set());
    } else if (line.startsWith('@@ ')) {
      inHeader = false;
      const hunk = line.match(/^@@ -\S+ \+(\d+)(?:,(\d+))? @@/);
      if (hunk && current) {
        const from = Number(hunk[1]);
        const count = hunk[2] === undefined ? 1 : Number(hunk[2]);
        for (let n = from; n < from + count; n++) added.get(current).add(n);
      }
    }
  }
  for (const [key, lineSet] of [...added].sort((a, b) => (a[0] < b[0] ? -1 : 1))) {
    if (!isScanned(key) || lineSet.size === 0) continue;
    let text;
    try {
      text = git('show', `${rangeHead}:${key}`);
    } catch (error) {
      unreadable.push(`${key}: ${`${error.stderr ?? error.message}`.trim().split('\n')[0]}`);
      continue;
    }
    filesRead += 1;
    scanText(key, key.split('/').join(path.sep), text, lineSet);
  }
}

// Which candidate tokens are commits of this repository. `cat-file --batch-check` answers one line
// per input line, in order: `<oid> commit <size>` when the name resolves, `<name> missing` or
// `<name> ambiguous` when it does not.
function commitsAmong(tokens) {
  const names = [...new Set(tokens)];
  const resolved = new Set();
  if (names.length === 0) return resolved;
  let out = '';
  try {
    out = execFileSync('git', ['-C', root, 'cat-file', '--batch-check'], {
      encoding: 'utf8',
      input: names.map((name) => `${name}^{commit}`).join('\n') + '\n',
      maxBuffer: 1 << 26,
      stdio: ['pipe', 'pipe', 'ignore'],
    });
  } catch (error) {
    return resolved;
  }
  out.split('\n').forEach((answer, index) => {
    if (/^[0-9a-f]{40,64} commit \d+$/.test(answer)) resolved.add(names[index]);
  });
  return resolved;
}
const commits = commitsAmong(candidates.flatMap((one) => one.tokens));
for (const { entry, tokens } of candidates) {
  if (tokens.some((token) => commits.has(token))) hits.push(entry);
}
hits.sort((a, b) => (a.file === b.file ? a.n - b.n : a.file < b.file ? -1 : 1));

// A file that could not be read is a trace that was not looked for.
if (unreadable.length) {
  console.log(`process traces in comments: not read -- ${unreadable.length} file(s) failed to read`);
  for (const u of unreadable) console.log('  ' + u);
  process.exit(1);
}

// The same for a whole-tree run that read nothing: it proves nothing, so it does not pass. A range
// that added nothing in these trees is a different thing -- there was nothing to read.
if (!ranged && (filesRead === 0 || commentLines === 0)) {
  console.log(
    `process traces in comments: not read -- ${filesRead} file(s) and ${commentLines} comment line(s) under ${dirs.join(', ')}`,
  );
  console.log('Nothing was scanned, so nothing was checked. Look at --root and the extension list.');
  process.exit(1);
}

const scope = ranged ? ` in the lines added by ${rangeArg}` : '';
if (hits.length) {
  const counts = `scanned ${filesRead} files, ${commentLines} comment lines`;
  if (listOnly) {
    // The same lines a refusal prints, and the line that says which of the two this is: a reader who
    // pipes this into a gate has to meet the word "nothing judged" and not mistake a listing for a
    // pass.
    console.log(`process traces in comments: ${hits.length} listed${scope}, nothing judged (--list; ${counts})`);
    for (const hit of hits) console.log('  ' + hit.text);
    process.exit(0);
  }
  console.log(`process traces in comments: ${hits.length}${scope} (${counts})`);
  for (const hit of hits) console.log('  ' + hit.text);
  console.log('A comment says what the code does and why; drop the dated ruling, the handover path, the role,');
  console.log('the host, the refactor step, the lane name, the work item number and the commit hash.');
  console.log('The history is the commit message.');
  process.exit(1);
}
const reading = `scanned ${filesRead} files, ${commentLines} comment lines, no trace${scope}`;
console.log(listOnly ? `${reading} (--list, nothing judged)` : reading);
