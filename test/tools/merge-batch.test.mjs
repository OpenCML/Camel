// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * `scripts/merge-batch.mjs` over a throwaway remote with an integration branch and five lanes:
 * two that merge and build, one that could be fast-forwarded, one whose change conflicts with what integration gained after the lane
 * branched, and one that merges cleanly and breaks the (stub) build. The build is a stub command
 * that exits 1 when the file BROKEN exists, so the in-batch search has something to tell apart:
 * the broken lane sits between the two good ones, so dropping the wrong lane, or dropping
 * everything after the first bad prefix, shows up as a different merged set.
 *
 * Read from the result: the lanes that went in and those skipped with their reason, the first
 * parent of every merge (the integration tip it landed on), the trailer on every merge, the
 * remote untouched without --push and advanced to the tool's tip with it, and the build count
 * (the base, the full batch, two prefix probes, the rebuilt result); and, over a base that already
 * carries the breaking file, exit 4 with nothing dropped and nothing pushed.
 *
 * The reading of the touched plans runs over a second remote with a stub selector and a stub plan
 * run: a red that the newest promotion table lists (in either of its row shapes) lets the batch
 * through and leaves `camel-read/<sha>` with `new=0`; a red that it does not list, or that only an
 * older table lists, is printed with its plan, exits 5, pushes nothing and leaves no record. A batch
 * that changes no path the build reads is not read (the first remote's lanes).
 *
 * Exit: 0 when every check holds, 1 when one does not.
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const tool = path.resolve(here, '..', '..', 'scripts', 'merge-batch.mjs');

const checks = [];
function check(name, holds, detail) {
    checks.push({ name, holds: Boolean(holds), detail: detail ?? '' });
}

function git(dir, args) {
    return execFileSync('git', ['-C', dir, ...args], { encoding: 'utf8' }).replace(/\n+$/, '');
}

function write(dir, rel, text) {
    const full = path.join(dir, rel);
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, text, 'utf8');
}

function commit(dir, note) {
    git(dir, ['add', '-A']);
    git(dir, ['commit', '--quiet', '-m', note]);
    return git(dir, ['rev-parse', 'HEAD']);
}

function identity(dir) {
    git(dir, ['config', 'user.name', 'merge-batch test']);
    git(dir, ['config', 'user.email', 'merge-batch@example.invalid']);
    git(dir, ['config', 'core.autocrlf', 'false']);
    git(dir, ['config', 'commit.gpgsign', 'false']);
}

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'merge-batch-'));
const remote = path.join(root, 'remote.git');
const maker = path.join(root, 'maker');
const seat = path.join(root, 'seat');
const buildStub = path.join(root, 'stub-build.mjs');
const buildLog = path.join(root, 'builds.log');

// The stub build: red when BROKEN is in the tree, and it records every call.
fs.writeFileSync(
    buildStub,
    `import fs from 'node:fs';\n` +
        `fs.appendFileSync(${JSON.stringify(buildLog)}, 'build\\n');\n` +
        `process.exit(fs.existsSync('BROKEN') ? 1 : 0);\n`,
);

execFileSync('git', ['init', '--quiet', '--bare', '--initial-branch=integration', remote]);
execFileSync('git', ['init', '--quiet', '--initial-branch=integration', maker]);
identity(maker);
git(maker, ['remote', 'add', 'origin', remote]);
write(maker, 'shared.txt', 'line one\nline two\n');
const root0 = commit(maker, 'the integration tip lanes branched from');

function lane(name, files, note) {
    git(maker, ['checkout', '--quiet', '-b', name, root0]);
    for (const [rel, text] of Object.entries(files)) write(maker, rel, text);
    const sha = commit(maker, note);
    git(maker, ['push', '--quiet', 'origin', name]);
    return sha;
}

lane('lane/clean-a', { 'a.txt': 'a\n' }, 'add a');
lane('lane/conflict', { 'shared.txt': 'line one\nline two, lane edit\n' }, 'edit line two on the lane');
lane('lane/breaks', { 'BROKEN': 'x\n', 'b.txt': 'b\n' }, 'add b and break the build');
lane('lane/clean-b', { 'c.txt': 'c\n' }, 'add c');

// Integration moves on after the lanes branched: the same line the conflicting lane edited.
git(maker, ['checkout', '--quiet', 'integration']);
write(maker, 'shared.txt', 'line one\nline two, integration edit\n');
const tipBefore = commit(maker, 'integration edits line two');
git(maker, ['push', '--quiet', 'origin', 'integration']);

// A lane branched from the current tip could be fast-forwarded; it must still arrive as a merge.
git(maker, ['checkout', '--quiet', '-b', 'lane/ff', tipBefore]);
write(maker, 'd.txt', 'd\n');
commit(maker, 'add d on the current tip');
git(maker, ['push', '--quiet', 'origin', 'lane/ff']);

execFileSync('git', ['clone', '--quiet', '-c', 'core.autocrlf=false', remote, seat]);
identity(seat);
for (const name of ['lane/clean-a', 'lane/conflict', 'lane/breaks', 'lane/clean-b', 'lane/ff']) {
    git(seat, ['fetch', '--quiet', 'origin', `${name}:${name}`]);
}

const lanes = [
    { branch: 'lane/clean-a', reading: 'adds a.txt; plan x green' },
    { branch: 'lane/conflict', reading: 'edits shared.txt' },
    { branch: 'lane/breaks', reading: 'adds b.txt' },
    { branch: 'lane/clean-b', reading: 'adds c.txt; plan y green' },
    { branch: 'lane/ff', reading: 'adds d.txt' },
];
const lanesFile = path.join(root, 'lanes.json');
fs.writeFileSync(lanesFile, JSON.stringify(lanes));

function run(extra) {
    fs.rmSync(buildLog, { force: true });
    const result = spawnSync(
        process.execPath,
        [tool, '--seat', seat, '--lanes', lanesFile, '--build', `node "${buildStub}"`, ...extra],
        { encoding: 'utf8' },
    );
    const out = `${result.stdout ?? ''}${result.stderr ?? ''}`;
    const line = out.split('\n').find((one) => one.startsWith('MERGE_BATCH_RESULT '));
    return {
        status: result.status ?? -1,
        out,
        result: line ? JSON.parse(line.slice('MERGE_BATCH_RESULT '.length)) : null,
        builds: fs.existsSync(buildLog) ? fs.readFileSync(buildLog, 'utf8').split('\n').filter(Boolean).length : 0,
    };
}

// --- without --push: the build and the search, and nothing leaves the seat -----
const first = run([]);
const names = (list) => list.map((one) => one.branch).join(',');
check('the batch finishes with exit 0', first.status === 0 && first.result, first.out);
if (first.result) {
    check(
        'the clean lanes are merged and the build-breaking one is not, in lane order',
        names(first.result.merged) === 'lane/clean-a,lane/clean-b,lane/ff',
        names(first.result.merged),
    );
    const conflict = first.result.skipped.find((one) => one.branch === 'lane/conflict');
    check(
        'the conflicting lane is skipped for a textual conflict naming the file',
        conflict && conflict.reason.includes('textual conflict') && conflict.reason.includes('shared.txt'),
        JSON.stringify(first.result.skipped),
    );
    const breaker = first.result.skipped.find((one) => one.branch === 'lane/breaks');
    check(
        'the build-breaking lane is skipped for the build, not for a conflict',
        breaker && breaker.reason.includes('build'),
        JSON.stringify(first.result.skipped),
    );
    check(
        'the build ran for the base, the full batch, two prefix probes and the rebuilt result (5 builds)',
        first.builds === 5 && first.result.builds === 5,
        `${first.builds} builds`,
    );

    const chain = git(seat, ['rev-list', '--first-parent', '--reverse', `${tipBefore}..${first.result.tip}`]).split('\n');
    check('the result is three merge commits above the tip', chain.length === 3, chain.join(','));
    let parentOk = true;
    let expectedFirst = tipBefore;
    for (const sha of chain) {
        const parents = git(seat, ['rev-list', '--parents', '-n', '1', sha]).split(' ').slice(1);
        if (parents.length !== 2 || parents[0] !== expectedFirst) parentOk = false;
        expectedFirst = sha;
    }
    check('every merge has the integration tip it landed on as its first parent', parentOk, chain.join(','));
    const readings = chain.map((sha) => git(seat, ['log', '-n', '1', '--format=%(trailers:key=Lane-Reading,valueonly)', sha]).trim());
    check(
        'every merge carries the lane reading as a Lane-Reading trailer',
        readings[0] === 'adds a.txt; plan x green' && readings[1] === 'adds c.txt; plan y green' && readings[2] === 'adds d.txt',
        readings.join(' | '),
    );
    const body = git(seat, ['log', '-n', '1', '--format=%B', chain[0]]);
    check('the merge message lists the lane commit titles', body.includes('add a'), body);
    check('the tree has the clean lanes and not the broken one', fs.existsSync(path.join(seat, 'c.txt')) && !fs.existsSync(path.join(seat, 'BROKEN')), '');
}
check(
    'without --push the remote integration is where it was',
    git(remote, ['rev-parse', 'integration']) === tipBefore,
    git(remote, ['rev-parse', 'integration']),
);

// --- with --push: the remote moves to the tool's tip ---------------------------
// The seat carries the real pre-push hook, so a batch of three merges is pushed through the shape
// and trailer rules the repository enforces, not past them.
const hooksDir = path.resolve(seat, git(seat, ['rev-parse', '--git-path', 'hooks']));
fs.mkdirSync(hooksDir, { recursive: true });
fs.copyFileSync(path.resolve(here, '..', '..', 'scripts', 'hooks', 'pre-push'), path.join(hooksDir, 'pre-push'));
// --- with --push (continued) ---------------------------
const second = run(['--push']);
check(
    'with --push the remote integration is the tool\'s tip',
    second.status === 0 && second.result && git(remote, ['rev-parse', 'integration']) === second.result.tip,
    second.out,
);

// --- a build that did not run is not a verdict on a lane ----------------------
fs.writeFileSync(path.join(root, 'infra.lanes.json'), JSON.stringify([{ branch: 'lane/breaks', reading: 'adds b.txt' }]));
const infra = spawnSync(
    process.execPath,
    [tool, '--seat', seat, '--lanes', path.join(root, 'infra.lanes.json'), '--build', 'node -e "process.exit(4)"', '--base', 'origin/integration'],
    { encoding: 'utf8' },
);
check(
    'a build that exits with something other than the red code stops the tool (2) and drops no lane',
    infra.status === 2 && !`${infra.stdout}`.includes('dropping'),
    `${infra.stdout}${infra.stderr}`,
);

// --- a build that hangs is ended at its limit and is no verdict on a lane --------
// The stub records its own pid and then sleeps for ten minutes; the tool has to end it (the whole
// tree, the shell that started it included) and report that the build gave no verdict.
const hangStub = path.join(root, 'stub-hang.mjs');
const hangPid = path.join(root, 'hang.pid');
fs.writeFileSync(
    hangStub,
    `import fs from 'node:fs';\n` +
        `fs.writeFileSync(${JSON.stringify(hangPid)}, String(process.pid));\n` +
        `setTimeout(() => {}, 600000);\n`,
);
const remoteAtHang = git(remote, ['rev-parse', 'integration']);
const hung = spawnSync(
    process.execPath,
    [tool, '--seat', seat, '--lanes', lanesFile, '--build', `node "${hangStub}"`, '--build-timeout', '3', '--push', '--base', 'origin/integration'],
    { encoding: 'utf8', timeout: 60000 },
);
const hungOut = `${hung.stdout}${hung.stderr}`;
check(
    'a build that hangs past --build-timeout stops the tool with exit 2 and says it gave no verdict',
    hung.status === 2 && hungOut.includes('did not run to a verdict') && hungOut.includes('ran past its limit'),
    hungOut,
);
check(
    'a hung build drops no lane and pushes nothing',
    !hungOut.includes('dropping') && git(remote, ['rev-parse', 'integration']) === remoteAtHang && !hungOut.includes('MERGE_BATCH_RESULT'),
    hungOut,
);
const hangingPid = fs.existsSync(hangPid) ? Number(fs.readFileSync(hangPid, 'utf8')) : 0;
const stillThere = (() => {
    if (!hangingPid) return true;
    try {
        process.kill(hangingPid, 0);
        return true;
    } catch {
        return false;
    }
})();
check('the hung build\'s process is gone when the tool returns', hangingPid > 0 && !stillThere, `pid ${hangingPid}`);

// --- an integration tip that does not build is no lane's fault ----------------
// A base that already carries BROKEN makes every prefix of the batch red; the search must not
// blame the first lane, it must say the tip itself is red and stop.
git(maker, ['checkout', '--quiet', '-b', 'red-base', tipBefore]);
write(maker, 'BROKEN', 'x');
commit(maker, 'a tip that does not build');
git(maker, ['push', '--quiet', 'origin', 'red-base']);
const remoteBefore = git(remote, ['rev-parse', 'integration']);
const redBase = run(['--push', '--base', 'origin/red-base']);
check(
    'a base that does not build stops the tool with its own exit code (4)',
    redBase.status === 4,
    redBase.out,
);
check(
    'a red base drops no lane and says the integration tip itself does not build',
    !redBase.out.includes('dropping') && redBase.out.includes('integration tip itself does not build'),
    redBase.out,
);
check(
    'a red base pushes nothing',
    git(remote, ['rev-parse', 'integration']) === remoteBefore && redBase.result === null,
    redBase.out,
);
check('a red base costs the batch build and one build of the base (2 builds)', redBase.builds === 2, `${redBase.builds} builds`);

// --- the reading of the touched plans on the built tip ----------------------------
// A second remote whose tip carries a stub plan selector (scripts/test.js), two red tables (the
// newest by promotion number is the baseline, one of its rows in the hand-written shape) and a lane
// that adds a file under src/, which the build reads. The build is green; the plan run is a stub that
// writes a summary.json from a scenario file, so what the tool does with the reds is the variable.
const readRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'merge-batch-read-'));
const readRemote = path.join(readRoot, 'remote.git');
const readMaker = path.join(readRoot, 'maker');
const readSeat = path.join(readRoot, 'seat');
const selectLog = path.join(readRoot, 'select.log');
const readLog = path.join(readRoot, 'read.log');
const scenario = path.join(readRoot, 'scenario.json');
const readStub = path.join(readRoot, 'stub-read.mjs');
fs.writeFileSync(
    readStub,
    `import fs from 'node:fs';\n` +
        `import path from 'node:path';\n` +
        `const [seat, plans] = process.argv.slice(2);\n` +
        `fs.appendFileSync(${JSON.stringify(readLog)}, plans + '\\n');\n` +
        `const rows = JSON.parse(fs.readFileSync(${JSON.stringify(scenario)}, 'utf8'));\n` +
        `const out = path.join(seat, 'test', 'results', 'fetched');\n` +
        `fs.mkdirSync(out, { recursive: true });\n` +
        `fs.writeFileSync(path.join(out, 'summary.json'), JSON.stringify({ test_timings: rows }));\n` +
        `process.exit(rows.some((r) => r.status !== 'pass') ? 1 : 0);\n`,
);
execFileSync('git', ['init', '--quiet', '--bare', '--initial-branch=integration', readRemote]);
execFileSync('git', ['init', '--quiet', '--initial-branch=integration', readMaker]);
identity(readMaker);
git(readMaker, ['remote', 'add', 'origin', readRemote]);
write(
    readMaker,
    'scripts/test.js',
    `import fs from 'node:fs';\n` +
        `fs.appendFileSync(${JSON.stringify(selectLog)}, process.argv.slice(2).join(' ') + ' base=' + process.env.CAMEL_TEST_BASE + '\\n');\n` +
        `console.log('[INFO] --changed: 1 changed file(s) select 2 plan(s)');\n` +
        `console.log(process.cwd() + '/test/plans/smoke');\n` +
        `console.log(process.cwd() + '/test/plans/feat/compile');\n`,
);
write(readMaker, '.gitignore', 'test/results/\n');
write(readMaker, 'internal/reports/2026-01-01-promotion-2-reds.tsv', '# older\nplan_a\tonly_in_older\n');
write(
    readMaker,
    'internal/reports/2026-01-02-promotion-3-reds.tsv',
    '# newest\nplan_a\tknown_red\nlegacy_red\ttest/plans/feat/compile.plan.toml\tfast\t\n',
);
write(readMaker, 'internal/reports/2026-01-02-promotion-3-reds-extra.tsv', 'plan_a\tnot_a_table\n');
const readBase = commit(readMaker, 'the tip with a selector and red tables');
git(readMaker, ['push', '--quiet', 'origin', 'integration']);
git(readMaker, ['checkout', '--quiet', '-b', 'lane/code', readBase]);
write(readMaker, 'src/unit.cpp', 'int unit() { return 1; }\n');
commit(readMaker, 'add a unit the build reads');
git(readMaker, ['push', '--quiet', 'origin', 'lane/code']);
execFileSync('git', ['clone', '--quiet', '-c', 'core.autocrlf=false', readRemote, readSeat]);
identity(readSeat);
git(readSeat, ['fetch', '--quiet', 'origin', 'lane/code:lane/code']);

function runRead(rows, extra = []) {
    fs.writeFileSync(scenario, JSON.stringify(rows));
    for (const file of [readLog, selectLog]) fs.rmSync(file, { force: true });
    const result = spawnSync(
        process.execPath,
        [
            tool, '--seat', readSeat, '--lane', 'lane/code=adds a unit; plan smoke green',
            '--build', 'node -e "process.exit(0)"',
            '--read', `node "${readStub}" "{seat}" {plans}`,
            '--base', 'origin/integration', ...extra,
        ],
        { encoding: 'utf8', timeout: 120000 },
    );
    const out = `${result.stdout ?? ''}${result.stderr ?? ''}`;
    return { status: result.status ?? -1, out };
}

const row = (plan, test, status, plan_path = 'test/plans/feat/compile.plan.toml') => ({ plan, test, status, plan_path });
const common = path.resolve(readSeat, git(readSeat, ['rev-parse', '--git-common-dir']));
const remoteTip = () => git(readRemote, ['rev-parse', 'integration']);

// The reds are all in the newest table (one under each of its row shapes): the batch is pushed.
const known = runRead(
    [row('plan_a', 'known_red', 'fail'), row('plan_b', 'legacy_red', 'fail'), row('plan_a', 'fine', 'pass'), row('plan_a', 'skipped', 'skip')],
    ['--push'],
);
const knownTip = known.out.split('\n').find((one) => one.startsWith('  tip '))?.trim().split(/\s+/)[1];
check('reds that are all in the newest promotion table do not stop the batch: exit 0 and pushed', known.status === 0 && knownTip && remoteTip() === knownTip, known.out);
check(
    'the plans are the selector\'s, relative to the seat, and the selector is asked against the integration tip the batch started from',
    fs.readFileSync(readLog, 'utf8').trim() === 'test/plans/smoke,test/plans/feat/compile' &&
        fs.readFileSync(selectLog, 'utf8').trim() === `--changed --list base=${readBase}`,
    `${fs.existsSync(readLog) ? fs.readFileSync(readLog, 'utf8') : ''}${fs.existsSync(selectLog) ? fs.readFileSync(selectLog, 'utf8') : ''}`,
);
const record = knownTip && fs.existsSync(path.join(common, 'camel-read', knownTip)) ? fs.readFileSync(path.join(common, 'camel-read', knownTip), 'utf8') : '';
check(
    'the reading record is written for the tip: plans, red count, the newest table as baseline, new=0',
    record.includes('plans=test/plans/smoke,test/plans/feat/compile\n') &&
        record.includes('reds=2\n') &&
        record.includes('baseline=2026-01-02-promotion-3-reds.tsv\n') &&
        record.includes('new=0\n'),
    record,
);

// One red the newest table does not list, and one that only the older table lists: both are new.
// The tool pushes nothing, writes no record, prints each with its plan, and exits 5.
const heldBefore = remoteTip();
fs.rmSync(path.join(common, 'camel-read'), { recursive: true, force: true });
const held = runRead(
    [row('plan_a', 'known_red', 'fail'), row('plan_a', 'brand_new', 'fail'), row('plan_a', 'only_in_older', 'fail'), row('plan_a', 'fine', 'pass')],
    ['--base', readBase, '--no-fetch'],
);
check('a red the newest table does not list stops the tool with its own exit code (5)', held.status === 5, held.out);
check(
    'every new red is printed with its plan, one per line, and a known red is not',
    held.out.includes('new red  plan_a  brand_new\n') && held.out.includes('new red  plan_a  only_in_older\n') && !held.out.includes('new red  plan_a  known_red'),
    held.out,
);
check(
    'a batch with new reds pushes nothing and leaves no reading record',
    remoteTip() === heldBefore && !fs.existsSync(path.join(common, 'camel-read')) && /pushed\s+no \(new reds\)/.test(held.out),
    held.out,
);

fs.rmSync(readRoot, { recursive: true, force: true });

if (!process.env.KEEP_TMP) fs.rmSync(root, { recursive: true, force: true });

const failed = checks.filter((one) => !one.holds);
for (const one of failed) {
    console.log(`FAIL  ${one.name}`);
    console.log(`      ${one.detail.split('\n').filter(Boolean).slice(0, 8).join('\n      ')}`);
}
console.log(`merge-batch: ${checks.length} checks, ${failed.length} failed`);
process.exit(failed.length === 0 ? 0 : 1);
