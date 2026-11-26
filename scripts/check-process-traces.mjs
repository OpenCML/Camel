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
//        node scripts/check-process-traces.mjs --root <dir> --baseline <file> [--lower]
//   exit 0 when clean or nothing above the baseline, 1 with a list or a baseline that rose
//   exit 2 when the tree was not named, the baseline named was not read, or `--list` met `--baseline`
//   `--list` prints the same list and exits 0: it lists, and judges nothing
//
// `--root` is required and has no default: the directory a process was started in is not
// necessarily the tree the reader meant, so the tree is named here by the caller.
//
// A tree that inherited traces is red for a debt, and a red that lists all of them on every run
// names none of them as this change's: a check that refuses everything teaches the one bypass it
// cannot see, `--no-verify`. So `--baseline <file>` names that debt written down per file, and the
// run judges only what is above it, file by file -- a total hides a file that rose while another
// fell. The file is in the tree so a change to it is a change in a diff, and it may only go down:
// the working copy is compared with `HEAD:` of the same path, and an entry that grew refuses the
// run. That comparison is the mechanical half; "the baseline only goes down" kept by convention is
// kept until a commit would rather not. `--lower` writes the current counts down and refuses to
// write when anything rose, and a file reading below its count is allowed and named, because the
// slack closes when the lane that cleaned it writes the lower number down.
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
const baselineAt = args.indexOf('--baseline');
const baselineArg = baselineAt >= 0 ? args[baselineAt + 1] : '';
if (baselineAt >= 0 && (!baselineArg || baselineArg.startsWith('--'))) {
  console.log('check-process-traces: --baseline <file> was given with no file behind it.');
  console.log('A run at the threshold of zero takes no baseline at all; say which debt it is otherwise.');
  process.exit(2);
}
const lower = args.includes('--lower');
if (lower && !baselineArg) {
  console.log('check-process-traces: --lower writes the baseline it was told about, and none was named.');
  process.exit(2);
}
if (listOnly && baselineArg) {
  console.log('check-process-traces: --list judges nothing, so it takes no baseline; drop one of the two.');
  process.exit(2);
}

// The baseline's path is read against the tree it describes, so that the file and the tree it counts
// travel together: a path written into a hook is one checkout's absolute path, and the next tree to
// push would read the debt of a different one.
const baselineFile = baselineArg ? path.resolve(root, baselineArg) : '';
const baselineRel = baselineFile ? path.relative(root, baselineFile).split(path.sep).join('/') : '';
const baseline = new Map();
// A baseline that is not there yet is the first landing, and the one run that may stand where there
// is nothing to lower: the debt it writes is the debt the tree already carries, and the alternative
// -- starting from zero and refusing every push until it is at zero -- is the state this file ends.
let baselineLanded = false;
if (baselineFile) {
  let text = '';
  try {
    text = fs.readFileSync(baselineFile, 'utf8');
  } catch (error) {
    // A baseline that cannot be read is not an empty one: starting from zero would call every
    // inherited trace an addition and refuse every push, which is the state this file exists to end.
    // `--lower` is the one run that may stand where there is no file yet, because writing the first
    // one is how a baseline begins.
    if (error.code !== 'ENOENT' || !lower) {
      console.log(`check-process-traces: the baseline ${baselineRel || baselineArg} was not read: ${error.message}`);
      console.log('A run at the threshold of zero names no --baseline; a missing debt file is not that run.');
      process.exit(2);
    }
  }
  // A file that was read -- even an empty one -- is a copy that the run may only lower. Only the
  // path that had nothing at all is a first landing that writes the debt as it stands.
  baselineLanded = text.trim().length > 0;
  if (text.trim()) {
    let doc;
    try {
      doc = JSON.parse(text);
    } catch (error) {
      console.log(`check-process-traces: ${baselineRel} is not JSON: ${error.message}`);
      process.exit(2);
    }
    for (const [file, count] of Object.entries(doc ?? {})) {
      if (!Number.isInteger(count) || count < 0) {
        console.log(`check-process-traces: ${baselineRel} gives '${file}' ${JSON.stringify(count)}, and a count is a whole number.`);
        process.exit(2);
      }
      baseline.set(file, count);
    }
  }
  // "The baseline only goes down" is the whole of the ratchet, and a sentence in a commit message
  // does not keep it: the number a lane would rather raise is the one it reads next. So the copy in
  // the tree is compared with the copy in `HEAD`, and an entry that grew -- or one added for a file
  // `HEAD` did not list -- refuses the run before anything is scanned.
  if (baselineRel && !baselineRel.startsWith('..')) {
    let committedText = null;
    try {
      committedText = execFileSync('git', ['-C', root, 'show', `HEAD:${baselineRel}`], {
        encoding: 'utf8',
        maxBuffer: 1 << 26,
        stdio: ['ignore', 'pipe', 'ignore'],
      });
    } catch (error) {
      committedText = null;
    }
    if (committedText === null) {
      console.log(`check-process-traces: ${baselineRel} has no committed copy under HEAD; it is read as it stands.`);
    } else {
      // A copy is committed, so this tree has a debt written down even if the working copy is gone:
      // `--lower` may not stand in for it and write a higher number back.
      baselineLanded = true;
      let committed = {};
      try {
        committed = JSON.parse(committedText) ?? {};
      } catch (error) {
        committed = null;
      }
      if (committed === null) {
        console.log(`check-process-traces: the committed ${baselineRel} is not JSON, so nothing says it only went down.`);
      } else {
        const raised = [];
        for (const [file, count] of baseline) {
          const was = Number.isInteger(committed[file]) ? committed[file] : 0;
          if (count > was) raised.push(`${file}: ${was} in HEAD, ${count} in the working copy`);
        }
        if (raised.length) {
          console.log(`check-process-traces: the baseline was raised for ${raised.length} file(s), and a baseline only goes down:`);
          for (const line of raised) console.log('  ' + line);
          console.log('A trace that is new is fixed in the comment it is written in, not in the number that counts it.');
          process.exit(1);
        }
      }
    }
  }
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
// The same lines counted per file: a baseline of a total cannot say which file rose, and a total
// that held still while one file rose and another fell reads as a tree that did not change.
const perFile = new Map();
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
    const rel = path.relative(root, file);
    // The baseline is committed, so its keys are read on whatever host checks the tree out next: a
    // path written with one platform's separator names a file that the other platform says is gone,
    // and a baseline of paths that do not exist is a baseline of zeros.
    const key = rel.split(path.sep).join('/');
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
      if (pattern.test(text)) {
        perFile.set(key, (perFile.get(key) ?? 0) + 1);
        hits.push({ file: key, text: `${rel}:${k + 1}: ${line.trim().slice(0, 160)}` });
      }
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

// What the baseline adds to the verdict: the same scan, judged against the debt instead of against
// nothing. A file above its count is a trace this change wrote; a file below it is a cleanup the
// lane has made and not yet written down, which is allowed and named -- the slack closes when that
// lane runs `--lower`, and a push that wrote the tracked file itself would be a surprise.
if (baselineFile) {
  const added = [];
  const dropped = [];
  for (const file of [...new Set([...perFile.keys(), ...baseline.keys()])].sort()) {
    const now = perFile.get(file) ?? 0;
    const was = baseline.get(file) ?? 0;
    if (now > was) added.push([file, was, now]);
    else if (now < was) dropped.push([file, was, now]);
  }
  const addedTotal = added.reduce((sum, [, was, now]) => sum + (now - was), 0);
  const droppedTotal = dropped.reduce((sum, [, was, now]) => sum + (was - now), 0);

  if (lower) {
    if (addedTotal && baselineLanded) {
      console.log(
        `process traces in comments: not written -- ${addedTotal} trace(s) beyond the baseline in ${added.length} file(s):`,
      );
      for (const [file, was, now] of added) console.log(`  ${file}: baseline ${was}, now ${now} (+${now - was})`);
      console.log('--lower writes the debt down; it does not take a raise. Fix the comments above, then lower.');
      process.exit(1);
    }
    const written = {};
    for (const file of [...perFile.keys()].sort()) {
      const count = perFile.get(file);
      if (count) written[file] = count;
    }
    fs.writeFileSync(baselineFile, JSON.stringify(written, null, 2) + '\n', 'utf8');
    const total = Object.values(written).reduce((sum, n) => sum + n, 0);
    const wasTotal = [...baseline.values()].reduce((sum, n) => sum + n, 0);
    const first = baselineLanded ? '' : ' -- the first landing, which writes the debt the tree already carries';
    console.log(
      `check-process-traces: baseline written (${baselineRel}: ${total} trace(s) in ${Object.keys(written).length} file(s), ` +
        `was ${wasTotal} in ${baseline.size})${first}`,
    );
    if (droppedTotal) console.log(`  the tree reads ${droppedTotal} trace(s) below the copy it replaces, in ${dropped.length} file(s)`);
    process.exit(0);
  }

  if (added.length) {
    console.log(
      `process traces in comments: ${addedTotal} beyond the baseline in ${added.length} file(s) ` +
        `(${baselineRel}; scanned ${filesRead} files, ${commentLines} comment lines)`,
    );
    for (const [file, was, now] of added) {
      console.log(`  ${file}: baseline ${was}, now ${now} (+${now - was})`);
      for (const hit of hits) if (hit.file === file) console.log('    ' + hit.text);
    }
    console.log('A comment says what the code does and why; drop the dated ruling, the handover path, the role,');
    console.log('the host, the refactor step, the lane name, the work item number and the commit hash.');
    console.log('The history is the commit message.');
    process.exit(1);
  }

  console.log(
    `process traces in comments: none beyond the baseline (${baselineRel}: ${[...baseline.values()].reduce((sum, n) => sum + n, 0)} trace(s) in ${baseline.size} file(s); ` +
      `scanned ${filesRead} files, ${commentLines} comment lines)`,
  );
  if (dropped.length) {
    console.log(`  ${dropped.length} file(s) read below it by ${droppedTotal} trace(s); --lower writes that down:`);
    for (const [file, was, now] of dropped) console.log(`  ${file}: baseline ${was}, now ${now} (-${was - now})`);
  }
  process.exit(0);
}

if (hits.length) {
  if (listOnly) {
    // The same lines a refusal prints, and the line that says which of the two this is: a reader who
    // pipes this into a gate has to meet the word "nothing judged" and not mistake a listing for a
    // pass.
    console.log(
      `process traces in comments: ${hits.length} listed, nothing judged (--list; scanned ${filesRead} files, ${commentLines} comment lines)`,
    );
    for (const hit of hits) console.log('  ' + hit.text);
    process.exit(0);
  }
  console.log(`process traces in comments: ${hits.length} (scanned ${filesRead} files, ${commentLines} comment lines)`);
  for (const hit of hits) console.log('  ' + hit.text);
  console.log('A comment says what the code does and why; drop the dated ruling, the handover path, the role,');
  console.log('the host, the refactor step, the lane name, the work item number and the commit hash.');
  console.log('The history is the commit message.');
  process.exit(1);
}
console.log(listOnly ? `${reading} (--list, nothing judged)` : reading);