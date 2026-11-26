// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A push to `develop` publishes a *tip*, and the reading that covers a tip is a promotion record:
 * the whole layer measured on it, the comparison with the previous promotion case by case, and what
 * its timeouts said once they were run alone -- a tip is read against the layer, not against its
 * parent.
 *
 * So the check is measured here in two parts: `promotion-record.mjs` over records that hold and
 * records that do not, and then the real `scripts/hooks/pre-push` over a push of
 * `refs/heads/develop`, because the wiring (the hook calling the script, and the words it refuses
 * in) is the half that a reader cannot see in either file alone.
 *
 * The record cannot live inside the commit it names: a commit cannot contain its own sha. That is
 * why every fixture puts the tip one commit below the record, which is also the shape the real
 * promotion of this tree has -- the page and its record land on `integration` after the tip they
 * name was measured.
 *
 * Why a throwaway repository: the script reads the promotion records committed in the tree it is
 * run in, and this tree's own record is the promotion's evidence rather than a fixture. Nothing is
 * pushed and no remote is contacted.
 *
 * Exit: 0 when every check holds, 1 when one does not, 2 when a shell or git could not be read --
 * a host that cannot run the hook cannot say the hook is fine.
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const treeRoot = path.resolve(here, '..', '..');
const recordScript = path.join(treeRoot, 'scripts', 'promotion-record.mjs');
const hookScript = path.join(treeRoot, 'scripts', 'hooks', 'pre-push');
const zeros = '0'.repeat(40);
const develop = 'refs/heads/develop';
const url = 'https://example.invalid/Camel.git';
const absentTip = '1'.repeat(40);
const promotionFile = 'internal/reports/2026-11-26-promotion-9.record.json';

const checks = [];
function check(name, holds, detail) {
    checks.push({ name, holds: Boolean(holds), detail: detail ?? '' });
}

function git(dir, args) {
    return execFileSync('git', ['-C', dir, ...args], { encoding: 'utf8' }).replace(/\n+$/, '');
}

function commit(dir, note) {
    git(dir, ['add', '-A']);
    git(dir, [
        '-c', 'user.name=promotion-record test',
        '-c', 'user.email=promotion-record@example.invalid',
        'commit', '--quiet', '--allow-empty', '-m', note,
    ]);
    return git(dir, ['rev-parse', 'HEAD']);
}

// The record the promotion actually writes, with the fields this check reads.
function promotionRecord(tip, overrides = {}) {
    return JSON.stringify(
        {
            kind: 'promotion',
            promotion: 9,
            tip,
            tier: 'fast',
            scope: 'test/plans (the whole plan tree)',
            readings: { total: 100, pass: 90, fail: 9, skip: 1 },
            comparison: { against: '2026-11-25-promotion-8-reds.tsv', new_reds: 0, vanished_reds: 0 },
            timeouts_rerun: { count: 2, run_alone: true, green: ['a.b', 'c.d'], still_red_not_timeout: [] },
            ...overrides,
        },
        null,
        2,
    );
}

function write(dir, rel, text) {
    const full = path.join(dir, rel);
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, text, 'utf8');
    return full;
}

// A repository whose scripts are this tree's scripts: the hook resolves `$top/scripts/promotion-record.mjs`
// and the script under test is the shipped one, copied so that the fixture's own tree carries it.
function buildFixture(record) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'promotion-record-'));
    write(dir, 'scripts/promotion-record.mjs', fs.readFileSync(recordScript, 'utf8'));
    write(dir, 'scripts/hooks/pre-push', fs.readFileSync(hookScript, 'utf8'));
    write(dir, 'internal/tools/camel-gate.local.json', JSON.stringify({ mergeWorktree: dir }, null, 2));
    write(dir, 'src/clean.c', '// The program reads nothing and answers zero.\nint main(void) { return 0; }\n');
    git(dir, ['init', '--quiet', '--initial-branch=main']);
    // A fixture that rewrites line endings would put a file in the tree that this file did not write,
    // and the hook reads its scripts from the working copy.
    git(dir, ['config', 'core.autocrlf', 'false']);
    git(dir, ['config', 'core.safecrlf', 'false']);
    const tip = commit(dir, 'the tip a promotion names');
    if (record !== null) write(dir, promotionFile, record(tip));
    const above = record === null ? tip : commit(dir, 'the promotion record');
    return { dir, tip, above };
}

function verifyPromotion(dir, sha) {
    const run = spawnSync(process.execPath, [path.join(dir, 'scripts', 'promotion-record.mjs'), sha], {
        cwd: dir,
        encoding: 'utf8',
    });
    return { status: run.status ?? -1, out: `${run.stdout ?? ''}${run.stderr ?? ''}` };
}

// --- the shell git would start a hook with -----------------------------------
// On Windows that is the one Git for Windows installs beside git and not the one on PATH; on a host
// whose git keeps its shell in /bin the same step lands on /. `sh` on PATH is tried first and is the
// answer everywhere git does not have to be asked.
function hookShell() {
    const onPath = spawnSync('sh', ['-c', 'echo ok'], { encoding: 'utf8' });
    if (onPath.status === 0) return { sh: 'sh', root: '' };
    let execPath = '';
    try {
        execPath = execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim();
    } catch (error) {
        return { reason: `git could not be asked for its exec path: ${error.message}` };
    }
    const root = path.dirname(path.dirname(path.dirname(execPath)));
    const candidates = [
        path.join(root, 'usr', 'bin', 'sh.exe'),
        path.join(root, 'bin', 'sh.exe'),
        path.join(root, 'usr', 'bin', 'sh'),
        path.join(root, 'bin', 'sh'),
    ];
    const sh = candidates.find((one) => fs.existsSync(one));
    return sh ? { sh, root } : { reason: `no sh beside git (looked in ${candidates.join(', ')})` };
}

// The hook's body runs `cat`, `tr`, `sed` and `git`, which on Windows are binaries in git's own
// usr/bin, so the environment git gives a hook is rebuilt with node's directory first (the hook's
// `node` has to be this one).
function hookEnv(root) {
    const env = { ...process.env };
    for (const key of Object.keys(env)) {
        if (key.toUpperCase() === 'PATH') delete env[key];
    }
    const dirs = [path.dirname(process.execPath)];
    for (const one of [path.join(root, 'usr', 'bin'), path.join(root, 'mingw64', 'bin')]) {
        if (root && fs.existsSync(one)) dirs.push(one);
    }
    dirs.push(process.env.PATH ?? process.env.Path ?? '');
    env.PATH = dirs.filter(Boolean).join(path.delimiter);
    return env;
}

function pushDevelop(dir, sha, shell) {
    const line = `${develop} ${sha} ${develop} ${zeros}\n`;
    const run = spawnSync(shell.sh, ['-e', 'scripts/hooks/pre-push', 'origin', url, develop, sha], {
        cwd: dir,
        env: hookEnv(shell.root),
        input: line,
        encoding: 'utf8',
    });
    return { status: run.status ?? -1, out: `${run.stdout ?? ''}${run.stderr ?? ''}` };
}

// --- the script, over records that hold and records that do not ----------------
const good = buildFixture(promotionRecord);
const held = verifyPromotion(good.dir, good.tip);
check(
    'a tip whose record sits one commit above it is accepted',
    held.status === 0 && held.out.includes('has a promotion record') && held.out.includes('promotion 9'),
    held.out,
);

const absent = verifyPromotion(good.dir, good.above);
check(
    'a tip with no record naming it is refused, in the words that name the missing reading',
    absent.status === 1 && absent.out.includes('缺晋级记录'),
    absent.out,
);

// The record below names a tip this checkout does not have: the guard is `--is-ancestor`, and it is
// exit 2 because a checkout that cannot see a commit has answered nothing about it.
const stranger = verifyPromotion(good.dir, absentTip);
check(
    'a tip this checkout does not carry is undecided (2), not a red',
    stranger.status === 2 && stranger.out.includes('not on this checkout'),
    stranger.out,
);

function refusedBy(mutate, expects, name) {
    const fixture = buildFixture((tip) => promotionRecord(tip, mutate));
    const run = verifyPromotion(fixture.dir, fixture.tip);
    check(name, run.status === 1 && expects.every((word) => run.out.includes(word)), run.out);
}

refusedBy({ comparison: { new_reds: 0, vanished_reds: 0 } }, ['comparison.against'], 'a record that was compared with nothing is refused');
refusedBy({ comparison: { against: 'promotion-8', new_reds: 2, vanished_reds: 0 } }, ['new reds'], 'a record with new reds is not a basis to publish');
refusedBy(
    { timeouts_rerun: { count: 2, run_alone: true, green: ['a.b'], still_red_not_timeout: [] } },
    ['account for'],
    'a record that accounts for only some of its timeouts is refused',
);
refusedBy({ readings: { total: 100, pass: 90, fail: 9 } }, ['readings.total/pass/fail/skip'], 'a record with missing layer cells is refused');
refusedBy({ tier: '' }, ['which layer'], 'a record that does not say which layer it measured is refused');

// --- the hook, over a real develop push --------------------------------------
const shell = hookShell();
if (shell.reason) {
    check('the hook is run over a develop push', false, `skipped: ${shell.reason}`);
} else {
    const allowedFixture = buildFixture((tip) => promotionRecord(tip));
    const allowed = pushDevelop(allowedFixture.dir, allowedFixture.tip, shell);
    check(
        'the hook allows a develop push whose tip has a promotion record',
        allowed.status === 0 && allowed.out.includes('allowed'),
        allowed.out,
    );

    const bareFixture = buildFixture(null);
    const refused = pushDevelop(bareFixture.dir, bareFixture.tip, shell);
    check(
        'the hook refuses a develop push whose tip has none, naming the promotion record',
        refused.status === 1 && refused.out.includes('缺晋级记录'),
        refused.out,
    );
}

const failed = checks.filter((one) => !one.holds);
for (const one of failed) {
    console.log(`FAIL  ${one.name}`);
    console.log(`      ${one.detail.split('\n').filter(Boolean).slice(0, 6).join('\n      ')}`);
}
console.log(`promotion-record: ${checks.length} checks, ${failed.length} failed`);
process.exit(failed.length === 0 ? 0 : 1);