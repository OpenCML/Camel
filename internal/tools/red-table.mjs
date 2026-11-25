#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Regenerate the "what is red on develop" table from records only.
//
//   node internal/tools/red-table.mjs                # writes internal/reports/<today>-develop-reds.cn.md
//   node internal/tools/red-table.mjs --print        # table to stdout, writes nothing
//   node internal/tools/red-table.mjs --develop <sha> --out <file>
//
// It reads records only: it builds nothing and runs no test.
//   * .git/camel-readings/<develop12>-<plans>-<hash>-v1.jsonl(+.where) -- the merge gate's per-case
//     verdicts; each file is measured on one develop commit (`.where.develop`);
//   * .git/camel-passed/<sha>.json -- the gate's own record of each merge: lane, plan set, cases moved;
//   * test/results/log/<run>/run.json + fail/<plan>/<case>/{meta.json,stderr.txt} -- every recorded
//     test run: the commit it ran on, whether the tree was dirty, which plans it selected, and per
//     failing case the runner's failure lines and the program's stderr;
//   * internal/backlog.md -- so a red with an owner points at that row instead of being described twice.
//
// A row is a (plan, case) whose **most recent** record says fail. Deliberate reds -- the harness's own
// failure fixtures and every `known_fail` mark (the `xfail/` store) -- are subtracted and counted
// separately: a page that mixes them with real reds cannot be read as an answer.
//
// `是`/`否` in the fourth-from-last column are only given by a record measured on the very commit the
// page names. Everything else is `未知`, and the header says how much of the tree the records cover.

import { readFileSync, readdirSync, statSync, existsSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname, relative, basename, sep } from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { writeRedTable, compareRedTables, formatComparison, provenanceFrom } from './red-table-context.mjs';

const SCRIPT = fileURLToPath(import.meta.url);
const REPO = join(dirname(SCRIPT), '..', '..');

const argv = process.argv.slice(2);
const argOf = (name, dflt = null) => {
  const i = argv.indexOf(name);
  return i >= 0 && argv[i + 1] ? argv[i + 1] : dflt;
};
const PRINT = argv.includes('--print');
const T0 = process.hrtime.bigint();

// ---------------------------------------------------------------- comparing two tables
// `--diff` is answered before any record is read: the comparison is about two files, and a reader who
// asked for it must not pay 10 s of record loading, nor have a page written under them as a side
// effect. The refusal (exit 2) is the point when the two tables were taken under different conditions
// (C-1124-183/185): "these are not the same instrument" is not a diff with a hundred new rows.
{
  const i = argv.indexOf('--diff');
  if (i >= 0) {
    const a = argv[i + 1];
    const b = argv[i + 2];
    if (!a || !b || a.startsWith('--') || b.startsWith('--')) {
      process.stderr.write('red-table: --diff needs two files: --diff <a.tsv> <b.tsv>\n');
      process.exit(2);
    }
    let cases = null;
    const casesFile = argOf('--cases');
    if (casesFile) {
      // The inventory is what turns "red in A, absent from B" into `removed` instead of `fixed`:
      // without it this module refuses to claim a case was deleted. One `plan/case` per line, a
      // `#` line is a comment, and a TSV's `plan<TAB>case` is accepted too.
      try {
        cases = readFileSync(casesFile, 'utf8').split('\n')
          .map((l) => l.replace(/\r$/, ''))
          .filter((l) => l.trim() && !l.startsWith('#'))
          .map((l) => (l.includes('\t') ? l.split('\t').slice(0, 2).join('/') : l.trim()));
      } catch (e) {
        process.stderr.write(`red-table: --cases could not be read: ${e?.message ?? e}\n`);
        process.exit(2);
      }
    }
    let result;
    try {
      result = compareRedTables(a, b, {
        allowUnknownLoad: argv.includes('--allow-unknown-load'),
        allowUnknownJobs: argv.includes('--allow-unknown-jobs'),
        allowDifferentTier: argv.includes('--allow-different-tier'),
        cases,
      });
    } catch (e) {
      // A file that cannot be read is "nothing was compared", which is exit 2 and not a diff with
      // zero differences: the same distinction the gate makes between a red and a run that did not
      // start.
      process.stderr.write(`red-table: --diff could not read a table: ${e?.message ?? e}\n`);
      process.exit(2);
    }
    process.stdout.write(formatComparison(result));
    process.exit(result.code);
  }
}

const git = (...args) => execFileSync('git', ['-C', REPO, ...args], { encoding: 'utf8' }).trim();
const rel = (p) => p.split(sep).join('/');
// the records live in the main checkout, so an absolute path in them is not relative to this worktree;
// cut it at the first top-level directory of the tree instead of trusting `relative()`.
const repoRel = (p) => {
  const s = String(p ?? '').replace(/\\/g, '/');
  const m = s.match(/(?:^|\/)(test|internal|docs|src|scripts|lib|editors|modules|stdlib)\/.*$/);
  return m ? m[0].replace(/^\//, '') : rel(relative(REPO, String(p ?? '')));
};
const readJson = (p) => { try { return JSON.parse(readFileSync(p, 'utf8')); } catch { return null; } };
const clip = (s, n) => (s.length > n ? s.slice(0, n - 1) + '…' : s);

// ---------------------------------------------------------------- what the page is anchored on
const develop = argOf('--develop') || git('rev-parse', 'origin/develop');
const developShort = develop.slice(0, 12);
const developSubject = git('log', '-1', '--format=%s', develop);
const developDate = git('log', '-1', '--format=%cI', develop);
const generatedAt = new Date();
const commonDir = (() => {
  const p = git('rev-parse', '--git-common-dir');
  return /^[A-Za-z]:/.test(p) || p.startsWith('/') ? p : join(REPO, p);
})();

const listFiles = (root, want, out = [], depth = 0) => {
  if (!existsSync(root) || depth > 12) return out;
  for (const n of readdirSync(root)) {
    const p = join(root, n);
    let st; try { st = statSync(p); } catch { continue; }
    if (st.isDirectory()) listFiles(p, want, out, depth + 1);
    else if (want(n)) out.push(p);
  }
  return out;
};

// ---------------------------------------------------------------- the plan tree (the denominator)
const planFiles = listFiles(join(REPO, 'test', 'plans'), (n) => n.endsWith('.plan.toml'))
  .map((p) => rel(relative(REPO, p))).sort();
// A plan's identity is the `id = "..."` its file declares (run-tests.mjs:2957 falls back to the file
// stem); the readings and the run logs both key cases by that id, so the map has to be built from it.
const planById = new Map();
const planStem = new Map();
for (const p of planFiles) {
  const stem = basename(p).replace(/\.plan\.toml$/, '');
  planStem.set(stem, [...(planStem.get(stem) ?? []), p]);
  let id = stem;
  try {
    const head = readFileSync(join(REPO, p), 'utf8').split('\n', 40);
    for (const line of head) { const m = line.match(/^\s*id\s*=\s*"([^"]+)"/); if (m) { id = m[1]; break; } }
  } catch { /* keep the stem */ }
  planById.set(id, p);
}
const targetPlans = (target) => {
  const p = join(REPO, target);
  if (!existsSync(p)) return [];
  if (statSync(p).isDirectory()) return listFiles(p, (n) => n.endsWith('.plan.toml')).map((f) => rel(relative(REPO, f)));
  return [target];
};

// ---------------------------------------------------------------- store 1: the gate's readings
// per case name: { verdict, at, develop, record }  (latest wins)
const readingState = new Map();
const readingFiles = [];
const tipAll = new Set();       // every case the record on the anchor commit names
const tipReds = new Set();      // ...of those, the red ones
const tipTargets = new Set();
const coveredByReadings = new Set();
let tipAt = null;
const readingsDir = join(commonDir, 'camel-readings');
if (existsSync(readingsDir)) {
  for (const f of readdirSync(readingsDir)) {
    if (!f.endsWith('.jsonl')) continue;
    const where = readJson(join(readingsDir, f + '.where'));
    if (!where) continue;
    const record = f.replace(/-v1\.jsonl$/, '');
    readingFiles.push({ record, develop: where.develop, at: where.measured_at, cases: where.cases ?? 0 });
    for (const t of where.plans_list ?? []) { coveredByReadings.add(t); if (where.develop === develop) tipTargets.add(t); }
    const lines = readFileSync(join(readingsDir, f), 'utf8').split('\n');
    for (const line of lines) {
      if (!line.trim()) continue;
      const o = JSON.parse(line);
      if (where.develop === develop) { tipAll.add(o.name); if (o.verdict !== 'pass') tipReds.add(o.name); if (!tipAt || where.measured_at > tipAt) tipAt = where.measured_at; }
      const prev = readingState.get(o.name);
      if (!prev || prev.at <= where.measured_at) {
        readingState.set(o.name, {
          name: o.name, verdict: o.verdict, reading: o.reading ?? null,
          at: where.measured_at, develop: where.develop, record, source: 'reading',
        });
      }
    }
  }
}

// ---------------------------------------------------------------- store 2: the gate's passed records
const passedFiles = [];
const passedDir = join(commonDir, 'camel-passed');
if (existsSync(passedDir)) {
  for (const f of readdirSync(passedDir)) {
    if (!f.endsWith('.json')) continue;
    const o = readJson(join(passedDir, f));
    if (o) passedFiles.push(o);
  }
}

// ---------------------------------------------------------------- store 3: the recorded runs
const logRoots = [];
{
  const main = git('worktree', 'list', '--porcelain').split('\n').find((l) => l.startsWith('worktree '));
  for (const root of [REPO, main ? main.slice('worktree '.length) : null]) {
    if (!root) continue;
    const p = join(root, 'test', 'results', 'log');
    if (existsSync(p) && !logRoots.includes(p)) logRoots.push(p);
  }
}

const DIAG = [
  /^\s*Guarantee\s*:/i,
  /\berror\b\s*:/i,
  /Camel Invariant Violation/i,
  /\b(Unresolved|NoMatching|No matching|Cannot|not supported|unimplemented|not implemented)\b/,
  /killed by (a )?signal|Segmentation|SIGSEGV|SIGABRT|stack overflow/i,
];
function diagnosticOf(meta, caseDir) {
  const lines = [];
  for (const p of [join(caseDir, 'stderr.txt'), join(caseDir, 'stdout.txt')]) {
    if (!existsSync(p)) continue;
    let text = ''; try { text = readFileSync(p, 'utf8'); } catch { continue; }
    for (const l of text.split('\n')) if (l.trim()) lines.push(l.trim());
  }
  for (const re of DIAG) { const hit = lines.find((l) => re.test(l)); if (hit) return clip(hit, 210); }
  const failures = [...(meta?.failures ?? [])];
  if (meta?.error_message) failures.push(meta.error_message);
  const informative = failures.find((l) => l && !/^expected exit \d+, got exit \d+$/.test(String(l).trim()));
  if (informative) return clip(String(informative).split('\n')[0], 210);
  if (lines.length) return clip(lines[0], 210);
  return '';
}

const runs = [];              // { id, at, rev, dirty, stems:Set, failed:Set("plan case"), why:Map(name->text) }
const xfailLatest = new Map();
let failCaseCount = 0;
for (const root of logRoots) {
  for (const id of readdirSync(root)) {
    if (!statSync(join(root, id)).isDirectory()) continue;
    const runJson = join(root, id, 'run.json');
    if (!existsSync(runJson)) continue;
    const run = readJson(runJson);
    if (!run) continue;
    const stems = new Set();
    for (const p of run.plans ?? []) stems.add(basename(p).replace(/\.plan\.toml$/, ''));
    const failed = new Set();
    const why = new Map();
    const planPathOfRun = new Map();   // plan id -> the plan file the run actually executed
    const dir = join(root, id, 'fail');
    let n = 0;
    if (existsSync(dir)) {
      for (const plan of readdirSync(dir)) {
        const pd = join(dir, plan);
        if (!statSync(pd).isDirectory()) continue;
        stems.add(plan);
        for (const kase of readdirSync(pd)) {
          const kd = join(pd, kase);
          if (!statSync(kd).isDirectory()) continue;
          const name = `${plan}/${kase}`;
          failed.add(name);
          n++;
          const meta = readJson(join(kd, 'meta.json'));
          why.set(name, diagnosticOf(meta, kd));
          if (meta?.plan_path) planPathOfRun.set(plan, repoRel(meta.plan_path));
          if (meta?.signal || meta?.timed_out) why.set(name + '\u0000s', meta?.signal ? 'signal' : 'timeout');
        }
      }
    }
    failCaseCount += n;
    // the xfail store: known_fail marks
    const xdir = join(root, id, 'xfail');
    if (existsSync(xdir)) {
      for (const plan of readdirSync(xdir)) {
        const pd = join(xdir, plan);
        if (!statSync(pd).isDirectory()) continue;
        for (const kase of readdirSync(pd)) {
          const kd = join(pd, kase);
          if (!statSync(kd).isDirectory()) continue;
          const meta = readJson(join(kd, 'meta.json'));
          const rec = { plan, case: kase, planPath: meta?.plan_path ? repoRel(meta.plan_path) : null, why: meta?.failures?.[0] ?? '', record: id, at: run.started_at ?? '', rev: run.revision ?? '' };
          const prev = xfailLatest.get(`${plan} ${kase}`);
          if (!prev || (prev.at ?? '') <= (rec.at ?? '')) xfailLatest.set(`${plan} ${kase}`, rec);
        }
      }
    }
    runs.push({ id, at: run.started_at ?? '', rev: run.revision ?? '', dirty: !!run.dirty, stems, failed, why, planPathOfRun, tier: run.tier ?? '', jobs: Number.isInteger(run.jobs) ? run.jobs : null });
  }
}
runs.sort((a, b) => (b.at ?? '').localeCompare(a.at ?? ''));

// ---------------------------------------------------------------- the latest record per case
// candidate names: everything any record ever called red
const candidates = new Set([...readingState].filter(([, v]) => v.verdict !== 'pass').map(([k]) => k));
for (const r of runs) for (const n of r.failed) candidates.add(n);

// For a candidate, the newest evidence is either a reading that names it, or the newest run that
// selected its plan (that run judged it: it is in `failed/` or it is not).
const stateOf = (name) => {
  const planId = name.split('/')[0];
  const reading = readingState.get(name) ?? null;
  let newestRun = null;
  for (const r of runs) {                     // runs are newest first
    if (!r.stems.has(planId)) continue;
    newestRun = r;
    break;
  }
  const rVerdict = newestRun ? (newestRun.failed.has(name) ? 'fail' : 'pass') : null;
  const c = [];
  if (reading) c.push({ at: reading.at, kind: 'reading', verdict: reading.verdict, rec: reading });
  if (newestRun) c.push({ at: newestRun.at, kind: 'run', verdict: rVerdict, rec: newestRun });
  c.sort((a, b) => (a.at ?? '').localeCompare(b.at ?? ''));
  const latest = c[c.length - 1] ?? null;
  return { latest, reading, newestRun };
};

// ---------------------------------------------------------------- rows
const FIXTURE_PLAN = /^(test\/tools\/selftest\/|test\/plans\/debug\/|selftest\.|probe\.)/;
// a plan that lives outside test/plans (the harness's own selftest plans) is a fixture whatever its
// filename says: those cases are written to fail so that the runner's own failure paths are exercised
const isFixture = (plan, planId, name) =>
  FIXTURE_PLAN.test(plan ?? '') || FIXTURE_PLAN.test(planId ?? '') || FIXTURE_PLAN.test(name ?? '') ||
  (!planFiles.includes(plan ?? '') && /^(selftest|probe)\./.test(planId ?? ''));
const planPathOf = (planId) => planById.get(planId) ?? (planStem.get(planId)?.[0] && planStem.get(planId).length === 1 ? planStem.get(planId)[0] : null);

// the newest diagnostic any run holds for a case, whatever verdict that run gave it: the gate's
// readings store only `expected exit 0, got exit 1`, so this is the only place the refusal survives
const diagIndex = new Map();   // name -> { at, text }
for (const r of runs) {        // newest first, so the first hit is the newest
  for (const [name, text] of r.why) {
    if (name.includes('\u0000') || !text) continue;
    if (!diagIndex.has(name)) diagIndex.set(name, { at: r.at, text, record: r.id });
  }
}

// the plan file a plan id actually points at, from the runs that recorded it (the harness's own
// selftest plans live outside test/plans, so the id alone cannot be resolved there)
const planPathByName = new Map();
for (const r of runs) for (const [k, v] of r.planPathOfRun) if (!planPathByName.has(k)) planPathByName.set(k, v);

// The readings keep only `expected exit 0, got exit 1`; a lane that measured a plan itself can leave
// the refusal原文 behind as data under internal/reports/data/, and this joins it in as a last resort.
const errData = new Map();     // "plan/case" -> { text, from }
const dataDir = join(REPO, 'internal', 'reports', 'data');
let errDataFiles = 0;
if (existsSync(dataDir)) {
  for (const f of readdirSync(dataDir)) {
    if (!f.endsWith('.tsv')) continue;
    errDataFiles++;
    for (const line of readFileSync(join(dataDir, f), 'utf8').split('\n')) {
      if (!line.trim() || line.startsWith('#') || line.startsWith('plan\t')) continue;
      const [plan, kase, ...rest] = line.split('\t');
      const text = rest.join('\t').trim();
      if (!plan || !kase || !text) continue;
      if (!errData.has(`${plan}/${kase}`)) errData.set(`${plan}/${kase}`, { text, from: f });
    }
  }
}

const rows = [];
const fixtures = [];
for (const name of candidates) {
  const [planId, ...rest] = name.split('/');
  const kase = rest.join('/');
  const { latest, reading, newestRun } = stateOf(name);
  if (!latest || latest.verdict !== 'fail') continue;    // a later record says it is green
  const plan = planPathOf(planId) ?? planId;
  let why = '';
  if (latest.kind === 'reading') {
    why = reading?.reading && !/^expected exit \d+, got exit \d+$/.test(reading.reading) ? reading.reading : '';
  } else {
    why = newestRun.why.get(name) ?? '';
  }
  if (!why) why = diagIndex.get(name)?.text ?? '';
  if (!why) why = errData.get(name)?.text ?? '';
  if (!why) why = readingState.get(name)?.reading ?? '';
  const rec = {
    name, plan, planId, case: kase, why,
    record: latest.kind === 'reading' ? latest.rec.record : latest.rec.id,
    source: latest.kind,
    observedAt: latest.at,
    rev: latest.kind === 'reading' ? latest.rec.develop : latest.rec.rev,
    dirty: latest.kind === 'run' ? latest.rec.dirty : false,
    signal: newestRun?.why.get(name + '\u0000s') === 'signal',
    timedOut: newestRun?.why.get(name + '\u0000s') === 'timeout',
  };
  if (isFixture(plan, planId, name) || isFixture(planPathByName.get(planId), planId, name)) { fixtures.push({ ...rec, mark: 'harness fixture that fails on purpose' }); continue; }
  rows.push(rec);
}

// known_fail marks, subtracted and listed once
for (const [key, rec] of xfailLatest) {
  fixtures.push({
    name: key.replace(' ', '/'), plan: rec.planPath ?? rec.plan, case: rec.case, why: rec.why,
    record: rec.record, observedAt: rec.at, rev: rec.rev, dirty: false,
    mark: 'known_fail（`xfail/`）',
  });
}

// ---------------------------------------------------------------- classification
const BACKLOG = new Map();
{
  const p = join(REPO, 'internal', 'backlog.md');
  if (existsSync(p)) {
    for (const line of readFileSync(p, 'utf8').split('\n')) {
      const m = line.match(/^\|\s*((?:C|D)-\d{4}-\d+[^|\s]*)\s*\|/);
      if (m) BACKLOG.set(m[1], line);
    }
  }
}
const DEBT = [
  { id: 'C-1123-64', re: /Unresolved reference: 'to_array'|random\.cml:114/, what: '`to_array` was withdrawn by `D-1123-49`' },
  { id: 'C-1123-56', re: /\(Range\)|has no elements to take|does not match the function's declared return type 'int\[\]'|no candidate of 'len' applies to \(<> \(Range\)/, what: 'the `Range` line (`D-1123-61`/`D-1123-90` 的债)' },
  { id: 'C-1123-1', re: /no derivative rule for operator ':op\/access_key'/, what: 'no derivative rule for `:op/access_key`' },
  { id: 'C-1123-63', re: /transpose_loop_escape|no-cotangent-space/, what: 'a loop-state port has no cotangent space' },
  { id: 'C-1123-18', re: /exemplars|no such directory/, what: '`exemplars/` moved and ten cases still look for it' },
  { id: 'C-1123-33', re: /Expected '\]' before ':'|range\(3, -1, -1\)/, what: 'a backward slice cannot name the position before the first' },
  { id: 'C-1124-56', re: /graph_space|grad_in_a_loop/, what: 'nvm graph space segfaults at the 1 MB threshold' },
];
const ENV = [
  { re: /onnxruntime|onnx runtime/i, what: 'onnxruntime missing' },
  { re: /no (CUDA|cuda) device|CUDNN|libcurand|no gpu|GPU (is )?(not|unavailable)|tiles.*skip/i, what: 'no GPU on this host' },
  { re: /GetProcessTimes|cpu reading|no cpu reading|processor time/i, what: 'no CPU reading on this host' },
  { re: /below the 4x margin|the bound is the work's own time/i, what: 'timing margin on a shared host' },
  { re: /ModuleNotFoundError|No module named|ENOENT.*(python|pip)/i, what: 'a dependency is not installed on the host' },
  { re: /timed out|timeout after|hit the case timeout/i, what: 'hit the case timeout' },
];
function classify(row) {
  const text = `${row.case} ${row.why}`;
  for (const d of DEBT) if (d.re.test(text)) return { cls: '债', id: d.id, basis: d.what };
  for (const e of ENV) if (e.re.test(text)) return { cls: '环境缺件', basis: e.what };
  if (row.timedOut) return { cls: '环境缺件', basis: 'hit the case timeout' };
  if (!row.why) return { cls: '说不清', tag: '没有失败方式', basis: 'the record holds only an exit code; nothing names the refusal' };
  if (row.signal || /killed by (a )?signal|SIGSEGV|SIGABRT|stack overflow/i.test(row.why)) return { cls: '真缺陷', basis: 'the program died on a signal' };
  if (/Guarantee|Invariant Violation|error:|Unresolved|NoMatching|No matching|Cannot|not supported|unimplemented|not implemented/i.test(row.why)) {
    return { cls: '真缺陷', basis: 'a diagnostic or an invariant names what refused' };
  }
  if (/^expected exit \d+, got exit \d+$/.test(row.why.trim())) return { cls: '说不清', tag: '只有退出码', basis: 'the record holds only an exit code; nothing names the refusal' };
  // the record names the expectation that was not met, even when it names no refusal
  if (/does not (contain|start with|match|end with)|differs from|expected (?!exit)/i.test(row.why)) {
    return { cls: '真缺陷', basis: 'an assertion names the expectation the run did not meet' };
  }
  return { cls: '说不清', tag: '不点名拒绝', basis: 'the one line the record holds names no refusal' };
}

// ---------------------------------------------------------------- the anchor
// The readings are written **before** a merge, on that merge's first parent, so the tip of develop
// has no reading of its own until the next merge reads it. The page therefore anchors on the newest
// develop commit that a record was actually measured on, and prints how far that is from the tip --
// otherwise the `还成立吗` column would be dead by construction.
const readingsByDevelop = new Map();   // develop sha -> newest measured_at
for (const r of readingFiles) {
  const prev = readingsByDevelop.get(r.develop);
  if (!prev || prev.at <= r.at) readingsByDevelop.set(r.develop, { at: r.at, cases: r.cases, record: r.record });
}
const isAncestor = (sha) => {
  try { execFileSync('git', ['-C', REPO, 'merge-base', '--is-ancestor', sha, develop]); return true; } catch { return false; }
};
let anchor = null;
for (const [sha, info] of readingsByDevelop) if (isAncestor(sha) && (!anchor || anchor.at < info.at)) anchor = { sha, ...info };
if (!anchor) anchor = { sha: develop, at: null, cases: 0, record: null };
const anchorDistance = anchor.sha === develop ? 0 : Number(git('rev-list', '--count', `${anchor.sha}..${develop}`));
const anchorSubject = anchor.sha === develop ? developSubject : git('log', '-1', '--format=%s', anchor.sha);

// every case the reading on the anchor commit names, and which of them are red there
const anchorAll = new Set();
const anchorReds = new Set();
const anchorPlanIds = new Set();
{
  for (const r of readingFiles) {
    if (r.develop !== anchor.sha) continue;
    const file = join(readingsDir, r.record + '-v1.jsonl');
    if (!existsSync(file)) continue;
    for (const line of readFileSync(file, 'utf8').split('\n')) {
      if (!line.trim()) continue;
      const o = JSON.parse(line);
      anchorAll.add(o.name);
      anchorPlanIds.add(o.name.split('/')[0]);
      if (o.verdict !== 'pass') anchorReds.add(o.name);
    }
  }
}
const anchorCovered = anchorAll.size > 0;
function stillRed(row) {
  if (!anchorCovered) return { ans: '未知', basis: 'no record was measured on any develop commit' };
  if (!anchorPlanIds.has(row.planId)) {
    return { ans: '未知', basis: `the reading on the anchor commit covers ${anchorPlanIds.size} plan(s) and not this one` };
  }
  if (anchorAll.has(row.name)) return anchorReds.has(row.name)
    ? { ans: '是', basis: `the reading on the anchor commit has it red (${(anchor.at ?? '').slice(0, 16)})` }
    : { ans: '否', basis: 'the reading on the anchor commit has it, and not red' };
  return { ans: '未知', basis: 'the plan is covered on the anchor commit but this case is not in that reading' };
}

const all = rows.map((r) => ({ ...r, cls: classify(r), ans: stillRed(r) }));

// the run store on its own, so the page can be compared with the previous stocktake's arithmetic
const runCandidateNames = new Set();
for (const r of runs) for (const n of r.failed) runCandidateNames.add(n);
const runFixtureCount = [...runCandidateNames].filter((n) => {
  const id = n.split('/')[0];
  return isFixture(planPathOf(id) ?? id, id, n);
}).length;
const runCandidateCount = runCandidateNames.size;
const runRowCount = all.filter((r) => runCandidateNames.has(r.name)).length;

// ---------------------------------------------------------------- counts
const coveredPlans = new Set();
for (const r of all) if (planFiles.includes(r.plan)) coveredPlans.add(r.plan);
for (const t of coveredByReadings) for (const p of targetPlans(t)) if (planFiles.includes(p)) coveredPlans.add(p);
for (const r of runs) for (const s of r.stems) { const p = planPathOf(s); if (p && planFiles.includes(p)) coveredPlans.add(p); }
for (const f of fixtures) if (planFiles.includes(f.plan)) coveredPlans.add(f.plan);
const neverCovered = planFiles.filter((p) => !coveredPlans.has(p));

const tally = (xs, f) => xs.reduce((m, x) => { const k = f(x); m[k] = (m[k] ?? 0) + 1; return m; }, {});
const byClass = tally(all, (r) => r.cls.cls);
const byTag = tally(all, (r) => r.cls.tag ?? '(无)');
const byAns = tally(all, (r) => r.ans.ans);
const byDebt = tally(all.filter((r) => r.cls.cls === '债'), (r) => r.cls.id);
const planRows = new Map();
for (const r of all) planRows.set(r.plan, (planRows.get(r.plan) ?? 0) + 1);
const newestObs = all.reduce((a, r) => ((r.observedAt ?? '') > a ? r.observedAt : a), '');
const oldestObs = all.reduce((a, r) => ((r.observedAt ?? '') < a ? r.observedAt : a), '9999');

// ---------------------------------------------------------------- page
const esc = (s) => String(s ?? '').replace(/\|/g, '\\|').replace(/\r?\n/g, ' ');
const stamp = (t) => (t ? String(t).replace('T', ' ').slice(0, 16) : '—');
const sorted = [...all].sort((a, b) => (a.plan ?? '').localeCompare(b.plan ?? '') || String(a.case).localeCompare(String(b.case)));

// ---------------------------------------------------------------- the table as a diffable file
// A page is read by a person; a TSV is read by the next comparison. A comparison can only refuse what
// the file states, so the file states it: the develop tip it was taken at, the anchor its "still red"
// column rests on, at what `jobs`, and at what `load` (1m/5m/15m). `load` cannot be read from any
// record -- `run.json` keeps `jobs` but no host load -- so it is either passed by whoever ran the
// layer (`--load`, or `--load-from <saved log>`) or it stays `unknown`, and an `unknown` refuses a
// comparison instead of passing silently (red-table-context.mjs).
const TSV = argOf('--tsv');
if (TSV) {
  const runJobs = new Map(runs.map((r) => [r.id, r.jobs]));
  const contributing = [...new Set(all.map((r) => r.record).filter(Boolean).map((id) => runJobs.get(id)).filter((v) => Number.isInteger(v)))].sort((a, b) => a - b);
  const statedJobs = argOf('--jobs');
  let jobs = statedJobs && /^\d+$/.test(statedJobs) ? Number(statedJobs) : 'unknown';
  let jobsSource = statedJobs ? 'cli --jobs' : 'unknown';
  if (!statedJobs && contributing.length === 1) { jobs = contributing[0]; jobsSource = 'run.json of the rows\' newest records'; }
  if (!statedJobs && contributing.length > 1) { jobs = `mixed ${contributing.join(',')}`; jobsSource = 'run.json of the rows\' newest records'; }
  const loadFromLog = argOf('--load-from');
  let load = argOf('--load');
  let loadSource = load ? 'cli --load' : 'unknown';
  if (!load && loadFromLog) {
    let text = '';
    try { text = readFileSync(loadFromLog, 'utf8'); } catch { /* reported as unknown below */ }
    const m = [...text.matchAll(/^\[load\]\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+\(1m\/5m\/15m\)/gm)].pop();
    if (m) { load = `${m[1]},${m[2]},${m[3]}`; loadSource = `log ${rel(relative(REPO, loadFromLog))}`; }
  }
  writeRedTable(TSV, provenanceFrom({
    tip: develop, anchor: anchor.sha, jobs, jobsSource, load, loadSource,
    tier: argOf('--tier'), generatedAt: generatedAt.toISOString(),
    source: argOf('--source') || `${readingFiles.length} readings, ${runs.length} runs, ${all.length} rows`,
  }), sorted.map((r) => ({ ...r, cls: r.cls?.cls ?? '' })));
  process.stdout.write(
    `[red-table] wrote ${TSV} rows=${sorted.length} tip=${developShort} anchor=${anchor.sha.slice(0, 12)} ` +
    `jobs=${jobs} (${jobsSource}) load=${load ?? 'unknown'} (${loadSource})\n`
  );
  process.exit(0);
}

const outPath = argOf('--out') || join(REPO, 'internal', 'reports', `${generatedAt.toISOString().slice(0, 10)}-develop-reds.cn.md`);
const LINES = [];
const L = (s = '') => LINES.push(s);
const pct = (n, d) => `${Math.round((n / d) * 100)}%`;

L(`# \`develop\` 上的红：按记录数出来的 ${all.length} 条（${developShort}）`);
L();
L(`**这张表覆盖了 ${coveredPlans.size} 个计划（\`test/plans\` 全树 ${planFiles.length} 个），还有 ${neverCovered.length} 个计划在任何记录里都没有一条读数 —— 那 ${neverCovered.length} 个计划是「未知」，不是「绿」。**`);
L();
L(`**「未知」不是缺陷，假装已知才是。** 本页没有跑测试、没有构建：每一行都只是某条记录**说过一次红**，而「现在还红吗」这一列 ${byAns['是'] ?? 0} 条能答「是」、${byAns['否'] ?? 0} 条能答「否」、${byAns['未知'] ?? 0} 条只能答「未知」。`);
L();
L('| 项 | 值 |');
L('|---|---|');
L(`| 生成时刻 | ${generatedAt.toISOString().replace('T', ' ').slice(0, 19)} UTC |`);
L(`| 读的是哪个 \`develop\` 提交（尖端） | \`${develop}\`（${developDate.replace('T', ' ').slice(0, 19)} ${esc(developSubject)}） |`);
L(`| 表锚在哪个 \`develop\` 提交 | \`${anchor.sha}\`${anchor.sha === develop ? '（就是尖端）' : `（develop 上最新一个**有读数**的提交，落后尖端 ${anchorDistance} 个提交）`} |`);
L(`| 锚点的读数时间与标题 | ${stamp(anchor.at)} ${anchorSubject ? esc(anchorSubject) : ''} |`);
L(`| 记录的最新 / 最老观测 | ${stamp(newestObs)} / ${stamp(oldestObs === '9999' ? '' : oldestObs)} |`);
L(`| 读到的记录 | 闸门读数 ${readingFiles.length} 份（${new Set(readingFiles.map((r) => r.develop)).size} 个 develop 提交）、闸门放行记录 ${passedFiles.length} 份、跑样本 ${runs.length} 次（${failCaseCount} 条失败用例记录） |`);
L(`| 跑样本的位置 | ${logRoots.length ? logRoots.map((p) => `\`${rel(relative(REPO, p))}\``).join('、') : '没有 `test/results/log`'} |`);
L();
L(`**为什么锚点不是尖端**：闸门在**合并之前**测那个合并的第一父，所以 \`develop\` 的尖端要等下一次合并才会被读到 —— 最新一个有读数的提交是 \`${anchor.sha.slice(0, 12)}\`，落后尖端 ${anchorDistance} 个提交。这中间每一笔都可能已经修好某条红、也可能带进来新的，所以「是」/「否」说的是**锚点那个提交**，不是尖端。仪器的年龄不等于被观察者的年龄（\`C-1124-69\`）。`);
L();
L('## 一、口径');
L();
L('**一行＝一条红**：某个「计划/用例」**最近一次**记录判它 `fail`。同一个「计划/用例」只占一行；后来的记录说它绿，它就不在表里（记录既记红也记绿，判绿的方式是「那次跑样本选中了这个计划而它不在 `fail/` 下」—— 跳过在记录里与绿不可分，这是记录的粗处，见第七节）。');
L();
L('**减掉故意红的**：底层的夹具与 `known_fail` 标记单独列在第四节，不进表。上一轮盘点的减法（466 条记录里的红 − 35 条故意夹具 = 431 条真红）在这里换成同一件事：本页从记录里拿到 ' + `${candidates.size}` + ` 个「计划/用例」，减去故意红的 ${fixtures.length} 条，剩 ${all.length} 条。`);
L();
L('**七列**：');
L();
L('- `失败方式(一行原文)`：那一行原文。闸门读数只存 `expected exit 0, got exit 1` 这一句、不存 stderr，所以这一列取**最近一条带诊断的记录**（stderr 的 `Guarantee :` / `error:` 一行，或 runner 自己的失败行）；`记录 id` 列仍是**最近一次**观测。两者不是同一条时，原因就是这个。');
L('- `在 develop 的这个提交上还成立吗`：`是`/`否` 只由**恰好测在这个提交上**的记录给出（闸门每次合并前会在它那一步的 develop 提交上测它选中的计划）；测在别的提交上的一律 `未知`。');
L('- `归类`：说的是**这条红是什么**，与「今天还在不在」是两件事。`真缺陷`＝有诊断或不变式点名了拒绝，且没有任何 backlog 行认领它；`债`＝有 backlog 行，于是指到那个 id、不重复描述；`环境缺件`＝缺卡/缺库/缺读数/时间余量；`夹具或探针`＝故意红（第四节）；`说不清`＝记录里只有一句退出码，或者那一行不点名任何拒绝。');
L('- `记录 id` 后面的 `†`：那是一次跑在**脏树**上的样本（`run.json.dirty = true`），它证明的是那棵树，严格说不算 `develop`。');
L();
L('## 二、数');
L();
L(`- 行数 **${all.length}**，涉及计划 **${planRows.size}** 个；`);
L(`- 这 ${all.length} 行的最近一次记录：**闸门读数** ${all.filter((r) => r.source === 'reading').length} 行、**跑样本** ${all.filter((r) => r.source === 'run').length} 行；`);
L(`- 只看跑样本那一套（上一轮盘点用的就是它）：候选 **${runCandidateCount}** 个名字 —— 与上一轮的 466 同一个数；按本页的夹具判据认出 **${runFixtureCount}** 条故意红的，剩 **${runCandidateCount - runFixtureCount}**（上一轮报 466 − 35 = 431：差的 11 条是夹具判据不同，不是红变多了）。这 ${runCandidateCount - runFixtureCount} 条里今天仍落在表里的只有 **${runRowCount}** 条，其余后来都有记录说它绿了；`);
L(`- 归类：${Object.entries(byClass).sort((a, b) => b[1] - a[1]).map(([k, v]) => `**${k}** ${v}`).join(' / ')}；`);
L(`- 还成立吗：${['是', '否', '未知'].map((k) => `\`${k}\` ${byAns[k] ?? 0}（${pct(byAns[k] ?? 0, all.length)}）`).join(' / ')}；`);
L(`- 记录覆盖的计划 **${coveredPlans.size} / ${planFiles.length}**（${pct(coveredPlans.size, planFiles.length)}），**${neverCovered.length} 个计划一条读数都没有**；`);
L(`- 故意红的夹具 **${fixtures.length}** 条（第四节，不进上表）；其中**验 runner 用的故意失败** ${fixtures.filter((f) => f.mark.startsWith('harness')).length} 条、**known_fail 标记**（有 owner 的红）${fixtures.filter((f) => f.mark.startsWith('known_fail')).length} 条；`);
L(`- 闸门那份锚点读数测的是 ${anchorPlanIds.size} 个计划、${anchorAll.size} 条用例（这就是「是」/「否」能覆盖的全部）；`);
L();
if (Object.keys(byDebt).length) {
  L(`债指到的 id：${Object.entries(byDebt).sort((a, b) => b[1] - a[1]).map(([id, n]) => `\`${id}\`（${n} 条）`).join('、')}。表里只写 id，不重复描述 —— 那些红的形状在 backlog 那几行里。`);
  L();
  const missing = ['C-1124-57', 'C-1124-67'].filter((id) => !BACKLOG.has(id));
  if (missing.length) {
    L(`派工里点名的「\`C-1124-57\`…\`C-1124-67\` 那 11 条常年红」在这份 \`internal/backlog.md\` 里**还没有行**：\`D-1124-3\` 的 ④ 说「拆出的 11 条 backlog 行 \`C-1124-57\`…\`67\`」已交，而同一行的备注写着「这批行还没写进本表」，编号却已经用掉了 \`C-1124-68\`/\`C-1124-69\`。所以表里指不到它们 —— 指到的是实际存在的行（上面那几个 id）。`);
    L();
  }
  L('**一条判读（2026-11-24，手写，不由本脚本重算）**：`opt.splice_place` 那 29 条红里有 12 条压在 `src/stdlib/perf.cml:37` 的 `(0 .. repeat)->reduce<accumulate, []>` 上。它**不**是 `C-1123-64` 那条 `to_array` 债的另一种表现：`reduce` 解析得到，只是 `Range` 上的那个重载（`prelude.cml:1112`）不收 reducer 与初值；`to_array` 则根本没解析到，而它就在 `prelude.cml:1068` 里（`0b2fd35bbc`，11-23 18:10 起就在）。一条是**查找**，一条是**候选选择** —— 两个机制、两种修法，把 `to_array` 修好不会让 `perf.cml:37` 变绿，反之亦然。证据与缺的那条读数见 backlog `C-1124-70`。');
  L();
}
L('### 红最多的十个计划');
L();
L('| 计划 | 红数 | 归类主要是什么 | 最近一次观测 |');
L('|---|---|---|---|');
{
  const top = [...planRows.entries()].sort((a, b) => b[1] - a[1]).slice(0, 10);
  for (const [plan, n] of top) {
    const rs = all.filter((r) => r.plan === plan);
    const cls = tally(rs, (r) => r.cls.cls);
    const main = Object.entries(cls).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${v}`).join('、');
    const at = rs.reduce((a, r) => ((r.observedAt ?? '') > a ? r.observedAt : a), '');
    L(`| ${esc(plan)} | ${n} | ${main} | ${stamp(at)} |`);
  }
}
L();
L('### 最新几份闸门读数（`是`/`否` 的全部依据）');
L();
L('「是」/「否」只能来自**恰好测在一个 develop 提交上**的读数。下面是最新的八份 —— 一眼就能看出表锚在哪、尖端落后多少：');
L();
L('| 测的 develop 提交 | 读数时间 | 用例数 | 红数 | 计划数 | 记录 id |');
L('|---|---|---|---|---|---|');
{
  const recent = [...readingsByDevelop.entries()].sort((a, b) => b[1].at.localeCompare(a[1].at)).slice(0, 8);
  for (const [sha, info] of recent) {
    const file = join(readingsDir, info.record + '-v1.jsonl');
    let cases = 0, reds = 0;
    const plans = new Set();
    if (existsSync(file)) {
      for (const line of readFileSync(file, 'utf8').split('\n')) {
        if (!line.trim()) continue;
        const o = JSON.parse(line);
        cases++; plans.add(o.name.split('/')[0]);
        if (o.verdict !== 'pass') reds++;
      }
    }
    L(`| \`${sha.slice(0, 12)}\`${sha === develop ? '（尖端）' : ''}${sha === anchor.sha ? '（锚点）' : ''} | ${stamp(info.at)} | ${cases} | ${reds} | ${plans.size} | \`${info.record}\` |`);
  }
}
L();
L('## 三、表');
L();
L('| 计划 | 用例 | 失败方式(一行原文) | 最近一次观测到的运行记录 id | 观测时间 | 在 `develop` 的这个提交上还成立吗 | 归类 |');
L('|---|---|---|---|---|---|---|');
for (const r of sorted) {
  L(`| ${esc(r.plan)} | ${esc(r.case)} | ${esc(r.why || '—')} | \`${esc(r.record)}\`${r.dirty ? '†' : ''} | ${stamp(r.observedAt)} | ${r.ans.ans} | ${r.cls.cls}${r.cls.tag ? `（${r.cls.tag}）` : ''}${r.cls.id ? `（${r.cls.id}）` : ''} |`);
}
L();
L(`## 四、故意红的夹具（${fixtures.length} 条，不进上表）`);
L();
L('判据两条：① 计划的路径在 `test/tools/selftest/` 或 `test/plans/debug/` 之下 —— 那两组用例**就是要失败**，用来验 runner 自己；② 计划把它标成 `known_fail`，它因此落在记录的 `xfail/` 下 —— 那种红是被点名、有 owner 的红，不是没人知道的红。');
L();
L('| 计划 | 用例 | 失败方式 | 记录 id | 观测时间 | 标记 |');
L('|---|---|---|---|---|---|');
for (const f of fixtures.sort((a, b) => String(a.plan).localeCompare(String(b.plan)) || String(a.case).localeCompare(String(b.case)))) {
  L(`| ${esc(f.plan)} | ${esc(f.case)} | ${esc(clip(f.why || '—', 170))} | \`${esc(f.record)}\` | ${stamp(f.observedAt)} | ${f.mark} |`);
}
L();
L(`## 五、记录没覆盖到的 ${neverCovered.length} 个计划 —— 它们是「未知」，不是「绿」`);
L();
L('这 ' + neverCovered.length + ' 个计划在闸门读数与跑样本里**一条读数都没有**。冻结得久、没人碰过的计划就停在这里：它们里面现在有没有红，本页答不了，要答只能去跑，而跑全量是禁止的。**下一轮要缩这个数，只能让合并位把它们逐个测一遍，或者让每条车道顺手测它没碰过的计划。**');
L();
for (let i = 0; i < neverCovered.length; i += 4) L(neverCovered.slice(i, i + 4).map((p) => `\`${p}\``).join(' · '));
L();
L('## 六、生成这张表的东西');
L();
L('```');
L('node internal/tools/red-table.mjs            # 重写本页：只读记录，不跑测试、不构建');
L('node internal/tools/red-table.mjs --print    # 打印到 stdout，不写文件');
L('node internal/tools/red-table.mjs --develop <sha> --out <file>');
L('```');
L();
L('`--develop` 缺省取 `origin/develop`；`--out` 缺省是 `internal/reports/<当天日期>-develop-reds.cn.md`。跑起来多久、谁在什么时刻跑，见 `internal/tools/red-table.md`。');
L();
L('## 七、这份口径自己缺什么');
L();
L(`1. **闸门读数不存失败原文，这是本页最大的一处缺。** ${all.length} 行里有 ${byClass['说不清'] ?? 0} 行归不进前四类，其中 ${byTag['只有退出码'] ?? 0} 行的记录只写着 \`expected exit 0, got exit 1\` 那一句（另 ${byTag['不点名拒绝'] ?? 0} 行那一行不点名任何拒绝）。闸门写进 \`.git/camel-readings\` 的每行只有 \`name / verdict / reading\`，\`reading\` 就是那一句退出码。**改法很小**：让闸门把失败用例的 stderr 首行一起写进 \`reading\`（或另给一个字段），下一版这张表的「归类」就能从 ${pct((all.length - (byClass['说不清'] ?? 0)), all.length)} 变成绝大部分。`);
L('2. **跳过在记录里与绿不可分。** 一次跑样本选中了某个计划，而某个用例不在它的 `fail/` 下，本页读作「那次它绿了」—— 但它也可能被那层 tier 滤掉、或者 `skip`。所以「最近一次记录说它绿」的准确形状是「最近一次记录没有说它红」。');
L('3. **脏树的样本算进来了**（记录 id 后面的 `†`）。它们证明的是那棵树，严格说不算 `develop`；只看干净树的读数要另起一份。');
L('4. **`是`/`否` 只覆盖锚点那一步选中的计划**（这次是 1 个计划、21 条用例）。闸门只测「这次合并碰到的计划」，而那正是这张表要暴露的东西：一个很久没被任何合并碰到的计划，它的红可以一直红下去而没人知道。**要让 `是`/`否` 有覆盖面，唯一不违铁律的办法是让合并位在合并后顺手把那棵树上「没被选中的计划」轮着测一遍**，或者让每条车道交工时顺手测一个它没碰过的计划。');
L('5. **`失败方式` 与 `记录 id` 可能不同源**：前者取最近一条带诊断的记录，后者取最近一次观测。这是有意的 —— 闸门不存原文，若强行同源，那一列会全是退码。');

const page = LINES.join('\n') + '\n';
if (PRINT) process.stdout.write(page);
else { mkdirSync(dirname(outPath), { recursive: true }); writeFileSync(outPath, page, 'utf8'); process.stdout.write(`wrote ${rel(relative(REPO, outPath))}\n`); }
const ms = Number(process.hrtime.bigint() - T0) / 1e6;
process.stdout.write(`rows=${all.length} plans=${planRows.size} covered=${coveredPlans.size}/${planFiles.length} never=${neverCovered.length} fixtures=${fixtures.length} classes=${JSON.stringify(byClass)} still=${JSON.stringify(byAns)} ms=${ms.toFixed(0)}\n`);
