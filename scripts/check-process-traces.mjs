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
// A pattern always catches some of the same word in another domain, and the check has met this
// twice: `/*` in a shell comment is a glob (`out/*/`), not the start of a block, and `lane-1` or
// `<lane>-<time>` in a documented path is a placeholder for some lane, not the name of one. Both
// were fixed by giving the word a semantic condition -- a block only opens in C-like languages, a
// lane code holds at least one letter -- and neither by an allowlist, because an allowlist of
// one-word exceptions grows until nobody reads it while a condition is stated where the pattern is.
// When a hit turns out to be a different domain, ask which condition distinguishes it, and put that
// condition in the pattern.
//
// Two questions, and only one of them is a verdict:
//
//   --added-from <rev> [--head <rev>]   what does this push *add*? The lines the diff from `<rev>`
//                                       to `--head` (default `HEAD`) adds, scanned with the same
//                                       pattern in the same trees. This is the only judging mode,
//                                       and it is the one the pre-push hook runs.
//
//   --root <dir>                        the whole tree as it stands: every trace in it, listed.
//                                       A report for the work of taking the inherited debt down.
//                                       It decides no push, because a tree inherits the traces
//                                       other changes wrote and the change being pushed is not
//                                       answerable for them.
//
// A diff is the truth about a push, and a per-file count is not: a fixed count of traces per file
// was a second table to keep in step with the first, so every push that added a trace anywhere
// refused every later push until the number was lowered by hand -- and the files it named were not
// the ones the refused author had touched. The diff answers the same question about the same lines
// without a second copy of the tree's state to keep true.
//
// A whole-tree run that read nothing is red, and so is a diff whose base cannot be read: "0 traces"
// and "0 files scanned" print the same otherwise, and a checker whose pattern stopped matching, or
// a base commit this repository does not have, would then read as a pass forever.
//
// Usage: node scripts/check-process-traces.mjs --root <dir>
//        node scripts/check-process-traces.mjs --root <dir> --added-from <rev> [--head <rev>]
//   exit 0 when the lines added carry no trace (or when the whole-tree report found none)
//   exit 1 with the lines the verdict is about
//   exit 2 when the tree was not named, or when neither question was asked
//   exit 3 when the diff's base or head could not be read, so nothing was judged
//
// `--root` is required and has no default: the directory a process was started in is not
// necessarily the tree the reader meant, so the tree is named here by the caller.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const args = process.argv.slice(2);
const rootAt = args.indexOf('--root');
const root = rootAt >= 0 ? args[rootAt + 1] : '';
if (!root || root.startsWith('--')) {
  console.log(
    'check-process-traces: --root <dir> is required. The directory a tool was started in is not ' +
      'necessarily the tree to read, so the tree is named by the caller and never inherited.',
  );
  process.exit(2);
}
const addedFromAt = args.indexOf('--added-from');
const addedFrom = addedFromAt >= 0 ? args[addedFromAt + 1] : '';
if (addedFromAt >= 0 && (!addedFrom || addedFrom.startsWith('--'))) {
  console.log('check-process-traces: --added-from <rev> was given with no revision behind it.');
  process.exit(2);
}
const headAt = args.indexOf('--head');
const head = headAt >= 0 ? args[headAt + 1] : 'HEAD';
if (headAt >= 0 && (!head || head.startsWith('--'))) {
  console.log('check-process-traces: --head <rev> was given with no revision behind it.');
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

/**
 * Every line of one file that carries comment or prose text, as `{ index, text }` with `index`
 * counted from 0. One reader for both questions: a whole-tree report and a diff verdict that read
 * the same lines with two loops would be two patterns the day one of them is edited.
 */
function proseLines(file, text) {
  const ext = path.extname(file);
  const toml = ext === '.toml';
  const lang = toml || ext === '.cmake' || ext === '.ps1' || ext === '.sh' || file.endsWith('CMakeLists.txt')
    ? 'hash'
    : 'c';
  const out = [];
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
    if (prose.trim()) out.push({ index: k, text: prose });
  });
  return out;
}

function* walk(dir) {
  if (!fs.existsSync(dir)) return;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) yield* walk(p);
    else if (exts.has(path.extname(entry.name)) || entry.name === 'CMakeLists.txt') yield p;
  }
}

function report(lines, reading, judged, trailing) {
  console.log(
    judged
      ? `process traces in comments: ${lines.length} (${reading})`
      : `process traces in comments: ${lines.length} listed, nothing judged (${reading})`,
  );
  for (const line of lines) console.log('  ' + line);
  if (trailing) console.log(trailing);
  process.exit(judged ? 1 : 0);
}

const advice =
  'A comment says what the code does and why; drop the dated ruling, the handover path, the role,\n' +
  'the host, the refactor step, the lane name, the work item number and the commit hash.\n' +
  'The history is the commit message.';

// --- the push's own lines ----------------------------------------------------
if (addedFrom) {
  const git = (argv, maxBuffer = 1 << 26) =>
    execFileSync('git', ['-C', root, ...argv], { encoding: 'utf8', maxBuffer, stdio: ['ignore', 'pipe', 'pipe'] });

  let base;
  let tip;
  // `<rev>` is taken as the caller spelled it (`HEAD~3`, `<sha>^`, `<sha>`), so the commit check is a
  // second question and not an appending of `^{commit}` to whatever arrived: `<sha>^` plus `^{commit}`
  // is a different revision.
  const commit = (rev) => {
    const sha = git(['rev-parse', '--verify', '--quiet', rev]).trim();
    git(['cat-file', '-e', `${sha}^{commit}`]);
    return sha;
  };
  try {
    base = commit(addedFrom);
    tip = commit(head);
  } catch (error) {
    console.log(`check-process-traces: the diff ${addedFrom}..${head} could not be read: ${String(error.stderr || error.message).trim().split('\n')[0]}`);
    console.log('A base this repository does not have means nothing was judged: fetch it and push again.');
    process.exit(3);
  }

  let diffText;
  try {
    diffText = git(['diff', '--unified=0', '--no-color', '--no-ext-diff', '--diff-filter=ACMR', base, tip, '--', ...dirs], 1 << 28);
  } catch (error) {
    console.log(`check-process-traces: the diff ${base.slice(0, 12)}..${tip.slice(0, 12)} could not be read: ${String(error.stderr || error.message).trim().split('\n')[0]}`);
    process.exit(3);
  }

  // The added line numbers per file, from the hunks: `@@ -a,b +c,d @@` says the new side starts at
  // line `c` and holds `d` lines, and `d` of them are additions (unified 0 has no context).
  const added = new Map();
  let current = null;
  for (const raw of diffText.split('\n')) {
    if (raw.startsWith('+++ ')) {
      const target = raw.slice(4).trim();
      current = target === '/dev/null' ? null : target.replace(/^b\//, '');
      if (current && !added.has(current)) added.set(current, new Set());
      continue;
    }
    if (raw.startsWith('@@')) {
      if (!current) continue;
      const plus = raw.match(/\+(\d+)(?:,(\d+))?/);
      if (!plus) continue;
      const start = Number(plus[1]);
      const count = plus[2] === undefined ? 1 : Number(plus[2]);
      for (let n = start; n < start + count; n++) added.get(current).add(n - 1);
    }
  }

  const named = [];
  let addedLines = 0;
  let commentLines = 0;
  for (const [rel, lines] of [...added.entries()].sort()) {
    if (lines.size === 0) continue;
    addedLines += lines.size;
    let text;
    try {
      text = git(['show', `${tip}:${rel}`]);
    } catch (error) {
      console.log(`check-process-traces: ${rel} could not be read at ${tip.slice(0, 12)}: ${String(error.stderr || error.message).trim().split('\n')[0]}`);
      process.exit(3);
    }
    for (const line of proseLines(rel, text)) {
      if (!lines.has(line.index)) continue;
      commentLines += 1;
      if (pattern.test(line.text)) named.push(`${rel}:${line.index + 1}: ${text.split('\n')[line.index].trim().slice(0, 160)}`);
    }
  }

  const reading = `${base.slice(0, 12)}..${tip.slice(0, 12)} adds ${addedLines} line(s) in ${added.size} file(s), ${commentLines} of them comment`;
  if (named.length === 0) {
    console.log(
      `process traces in comments: the lines this push adds carry none (${reading}); ` +
        'the traces the tree inherited are not this push\'s.',
    );
    process.exit(0);
  }
  report(named, reading, true, advice);
}

// --- the whole tree as it stands ---------------------------------------------
const hits = [];
let filesRead = 0;
let commentLines = 0;
const unreadable = [];
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
    // A path written with one platform's separator names a file the other platform says is gone, and
    // a report of paths that do not exist is a report of nothing.
    const rel = path.relative(root, file).split(path.sep).join('/');
    for (const line of proseLines(file, text)) {
      commentLines += 1;
      if (pattern.test(line.text)) {
        hits.push(`${path.relative(root, file)}:${line.index + 1}: ${text.split('\n')[line.index].trim().slice(0, 160)}`);
      }
    }
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

const reading = `scanned ${filesRead} files, ${commentLines} comment lines`;
if (hits.length === 0) {
  console.log(`${reading}, no trace`);
  process.exit(0);
}
// The whole tree is a report and not a verdict: a tree inherits the traces other changes wrote, so
// refusing a push over this list refuses the wrong author. `--added-from` is the judging mode.
console.log(
  `process traces in comments: ${hits.length} in the tree (${reading}); ` +
    'this is the report for taking the inherited debt down, and it refuses no push',
);
for (const hit of hits) console.log('  ' + hit);
process.exit(0);