// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What the pre-push hook asks of a push to `integration`: every merge commit in the pushed range
 * carries a `Lane-Reading:` trailer (`Lane-Reading: docs-only` for a documentation-only merge),
 * and a change to a path the build reads arrives built -- a commit at or above it, before the tip,
 * is recorded under `<git common dir>/camel-built/`, and so is a reading of the plans the batch
 * touched with no red the newest promotion does not list (`camel-read/<sha>`, a line `new=0`).
 * A record that says `new=` with anything else refuses the push; a range that changes no build
 * input needs neither record.
 *
 * The real `scripts/hooks/pre-push` is run over a throwaway repository. Three pushes: two merges
 * that both carry the trailer (allowed, one of them `docs-only`), the same two with the trailer
 * missing from the second (refused, naming that merge and not the first), and a single-parent
 * commit on top (refused by the shape rule). The trailer is read from the commit's own message,
 * so a sentence that merely mentions a reading does not count. Then the build rule: a merge that adds
 * a file under `src/` is refused while nothing records a build, allowed once its commit is recorded,
 * and still allowed with a documentation merge on top of it that was not built itself. The reading
 * rule is the same walk: refused with a build record and no reading, refused with a reading that
 * has new reds, allowed with `new=0`, and a documentation-only range is allowed with neither.
 *
 * Exit: 0 when every check holds, 1 when one does not.
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const hookScript = path.resolve(here, '..', '..', 'scripts', 'hooks', 'pre-push');
const integration = 'refs/heads/integration';
const url = 'https://example.invalid/Camel.git';

const checks = [];
function check(name, holds, detail) {
    checks.push({ name, holds: Boolean(holds), detail: detail ?? '' });
}

function git(dir, args) {
    return execFileSync('git', ['-C', dir, ...args], { encoding: 'utf8' }).replace(/\n+$/, '');
}

function commit(dir, note) {
    git(dir, ['add', '-A']);
    git(dir, ['commit', '--quiet', '--allow-empty', '-m', note]);
    return git(dir, ['rev-parse', 'HEAD']);
}

function merge(dir, branch, message) {
    git(dir, ['merge', '--quiet', '--no-ff', '-m', message, branch]);
    return git(dir, ['rev-parse', 'HEAD']);
}

function hookShell() {
    const onPath = spawnSync('sh', ['-c', 'echo ok'], { encoding: 'utf8' });
    if (onPath.status === 0) return { sh: 'sh', root: '' };
    const execPath = execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim();
    const root = path.dirname(path.dirname(path.dirname(execPath)));
    const candidates = [path.join(root, 'usr', 'bin', 'sh.exe'), path.join(root, 'bin', 'sh.exe'), path.join(root, 'usr', 'bin', 'sh')];
    const sh = candidates.find((one) => fs.existsSync(one));
    return sh ? { sh, root } : { reason: `no sh beside git (looked in ${candidates.join(', ')})` };
}

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

function pushIntegration(dir, shell, localSha, remoteSha) {
    const run = spawnSync(shell.sh, ['-e', hookScript, 'origin', url], {
        cwd: dir,
        env: hookEnv(shell.root),
        input: `${integration} ${localSha} ${integration} ${remoteSha}\n`,
        encoding: 'utf8',
    });
    return { status: run.status ?? -1, out: `${run.stdout ?? ''}${run.stderr ?? ''}` };
}

const shell = hookShell();
if (shell.reason) {
    console.log(`FAIL  the hook is run over an integration push\n      skipped: ${shell.reason}`);
    console.log('integration-readings: 1 checks, 1 failed');
    process.exit(1);
}

// A tip, and two lanes that each add a file; the merges are made by hand so the message is the
// variable under test.
function build(secondMessage) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'integration-readings-'));
    git(dir, ['init', '--quiet', '--initial-branch=integration']);
    for (const [key, value] of [['user.name', 'readings test'], ['user.email', 'readings@example.invalid'], ['core.autocrlf', 'false'], ['commit.gpgsign', 'false']]) {
        git(dir, ['config', key, value]);
    }
    fs.writeFileSync(path.join(dir, 'base.txt'), 'base\n');
    const tip = commit(dir, 'the tip');
    for (const lane of ['one', 'two']) {
        git(dir, ['checkout', '--quiet', '-b', `lane/${lane}`, tip]);
        fs.writeFileSync(path.join(dir, `${lane}.txt`), `${lane}\n`);
        commit(dir, `lane ${lane}`);
    }
    git(dir, ['checkout', '--quiet', 'integration']);
    const first = merge(dir, 'lane/one', 'Merge lane/one\n\n- lane one\n\nLane-Reading: plan x green');
    const second = merge(dir, 'lane/two', secondMessage);
    return { dir, tip, first, second };
}

const allPresent = build('Merge lane/two\n\n- lane two\n\nLane-Reading: docs-only');
const allowed = pushIntegration(allPresent.dir, shell, allPresent.second, allPresent.tip);
check('two merges that both carry a Lane-Reading trailer (one docs-only) are allowed', allowed.status === 0, allowed.out);

const missing = build('Merge lane/two\n\n- lane two\n\nthe reading is: plan x green');
const refused = pushIntegration(missing.dir, shell, missing.second, missing.tip);
check(
    'a merge without the trailer is refused, and the refusal names that merge and not the one that has it',
    refused.status === 1 && refused.out.includes(missing.second) && !refused.out.includes(missing.first),
    refused.out,
);

// The shape rule is still in force beside the trailer: a lane commit that is not a merge is refused
// as a lane commit, and the trailer check does not have to say anything about it.
fs.writeFileSync(path.join(allPresent.dir, 'page.md'), 'a page\n');
const page = commit(allPresent.dir, 'a lane commit');
const straight = pushIntegration(allPresent.dir, shell, page, allPresent.tip);
check('a commit that is not a merge is refused as a lane commit that went straight in', straight.status === 1 && straight.out.includes('went straight in'), straight.out);

// The build rule. The same two merges, the second bringing a file the build reads.
const code = build('Merge lane/two\n\n- lane two\n\nLane-Reading: plan x green');
fs.mkdirSync(path.join(code.dir, 'src'), { recursive: true });
git(code.dir, ['checkout', '--quiet', '-b', 'lane/code', code.second]);
fs.writeFileSync(path.join(code.dir, 'src', 'unit.cpp'), 'int unit() { return 1; }\n');
commit(code.dir, 'lane code');
git(code.dir, ['checkout', '--quiet', 'integration']);
const codeMerge = merge(code.dir, 'lane/code', 'Merge lane/code\n\n- a unit\n\nLane-Reading: plan x green');
const unbuilt = pushIntegration(code.dir, shell, codeMerge, code.tip);
check(
    'a merge that changes a path the build reads is refused while no build of it is recorded, and the refusal names it',
    unbuilt.status === 1 && unbuilt.out.includes(codeMerge) && unbuilt.out.includes('src/unit.cpp'),
    unbuilt.out,
);
const builtDir = path.join(path.resolve(code.dir, git(code.dir, ['rev-parse', '--git-common-dir'])), 'camel-built');
fs.mkdirSync(builtDir, { recursive: true });
fs.writeFileSync(path.join(builtDir, codeMerge), 'Release\n');
const buildOnly = pushIntegration(code.dir, shell, codeMerge, code.tip);
check(
    'a built merge that changes a build input is still refused without a reading, and the refusal says how to produce one',
    buildOnly.status === 1 && buildOnly.out.includes(codeMerge) && buildOnly.out.includes('no new red') && buildOnly.out.includes('merge-batch') && !buildOnly.out.includes('no commit from there to the tip was built'),
    buildOnly.out,
);
const readDir = path.join(path.resolve(code.dir, git(code.dir, ['rev-parse', '--git-common-dir'])), 'camel-read');
fs.mkdirSync(readDir, { recursive: true });
fs.writeFileSync(path.join(readDir, codeMerge), 'plans=test/plans/smoke\nreds=4\nbaseline=2026-01-01-promotion-9-reds.tsv\nnew=2\n');
const redRecord = pushIntegration(code.dir, shell, codeMerge, code.tip);
check(
    'a reading record with new reds is refused, and the refusal names the commit and the count',
    redRecord.status === 1 && redRecord.out.includes(codeMerge) && redRecord.out.includes('new=2'),
    redRecord.out,
);
fs.writeFileSync(path.join(readDir, codeMerge), 'plans=test/plans/smoke\nreds=4\nbaseline=2026-01-01-promotion-9-reds.tsv\nnew=0\n');
const built = pushIntegration(code.dir, shell, codeMerge, code.tip);
check('the same push is allowed once the build and a reading with new=0 of that merge are recorded', built.status === 0, built.out);
git(code.dir, ['checkout', '--quiet', '-b', 'lane/page', codeMerge]);
fs.writeFileSync(path.join(code.dir, 'page.md'), 'a page\n');
commit(code.dir, 'lane page');
git(code.dir, ['checkout', '--quiet', 'integration']);
const pageMerge = merge(code.dir, 'lane/page', 'Merge lane/page\n\n- a page\n\nLane-Reading: docs-only');
const onTop = pushIntegration(code.dir, shell, pageMerge, code.tip);
check('a documentation merge on top of a built and read merge needs neither of its own', onTop.status === 0, onTop.out);

// Lane merges that change a build input and carry no record at all: neither a build nor a reading.
const bare = build('Merge lane/two\n\n- lane two\n\nLane-Reading: plan x green');
git(bare.dir, ['checkout', '--quiet', '-b', 'lane/more-code', bare.second]);
fs.mkdirSync(path.join(bare.dir, 'src'), { recursive: true });
fs.writeFileSync(path.join(bare.dir, 'src', 'more.cpp'), 'int more() { return 2; }\n');
commit(bare.dir, 'lane more code');
git(bare.dir, ['checkout', '--quiet', 'integration']);
const bareMerge = merge(bare.dir, 'lane/more-code', 'Merge lane/more-code\n\n- more code\n\nLane-Reading: plan x green');
const bareBuiltDir = path.join(path.resolve(bare.dir, git(bare.dir, ['rev-parse', '--git-common-dir'])), 'camel-built');
fs.mkdirSync(bareBuiltDir, { recursive: true });
const noReading = pushIntegration(bare.dir, shell, bareMerge, bare.tip);
check('lane merges that change a build input with no record of either kind are refused', noReading.status === 1 && noReading.out.includes(bareMerge) && noReading.out.includes('src/more.cpp'), noReading.out);
fs.writeFileSync(path.join(bareBuiltDir, bareMerge), 'Release\n');
const builtNoReading = pushIntegration(bare.dir, shell, bareMerge, bare.tip);
check('the build record alone does not stand for the reading', builtNoReading.status === 1 && builtNoReading.out.includes('no new red'), builtNoReading.out);

// A reading record on the tip stands for the merges under it, as the build record does: the lane
// merges beneath it are accepted without records of their own.
fs.writeFileSync(path.join(bare.dir, 'page.md'), 'a page\n');
git(bare.dir, ['checkout', '--quiet', '-b', 'lane/more-page', bareMerge]);
commit(bare.dir, 'lane more page');
git(bare.dir, ['checkout', '--quiet', 'integration']);
const pageOnBare = merge(bare.dir, 'lane/more-page', 'Merge lane/more-page\n\n- a page\n\nLane-Reading: docs-only');
const bareReadDir = path.join(path.resolve(bare.dir, git(bare.dir, ['rev-parse', '--git-common-dir'])), 'camel-read');
fs.mkdirSync(bareReadDir, { recursive: true });
fs.writeFileSync(path.join(bareReadDir, pageOnBare), 'plans=test/plans/smoke\nreds=0\nbaseline=x\nnew=0\n');
const readAtTip = pushIntegration(bare.dir, shell, pageOnBare, bare.tip);
check('a reading with new=0 at the tip stands for the build-input merges under it', readAtTip.status === 0, readAtTip.out);

// A range that changes documents only needs neither record.
const docs = build('Merge lane/two\n\n- lane two\n\nLane-Reading: docs-only');
const docsOnly = pushIntegration(docs.dir, shell, docs.second, docs.tip);
check('a range that changes no build input is allowed with no record of either kind', docsOnly.status === 0, docsOnly.out);

for (const one of [allPresent, missing, code, bare, docs]) fs.rmSync(one.dir, { recursive: true, force: true });

const failed = checks.filter((one) => !one.holds);
for (const one of failed) {
    console.log(`FAIL  ${one.name}`);
    console.log(`      ${one.detail.split('\n').filter(Boolean).slice(0, 6).join('\n      ')}`);
}
console.log(`integration-readings: ${checks.length} checks, ${failed.length} failed`);
process.exit(failed.length === 0 ? 0 : 1);
