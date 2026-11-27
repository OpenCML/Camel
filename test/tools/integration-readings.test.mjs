// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What the pre-push hook asks of a push to `integration`: every merge commit in the pushed range
 * carries a `Lane-Reading:` trailer (`Lane-Reading: docs-only` for a documentation-only merge),
 * and a change to a path the build reads arrives built -- a commit at or above it, before the tip,
 * is recorded under `<git common dir>/camel-built/`.
 *
 * The real `scripts/hooks/pre-push` is run over a throwaway repository. Three pushes: two merges
 * that both carry the trailer (allowed, one of them `docs-only`), the same two with the trailer
 * missing from the second (refused, naming that merge and not the first), and a single-parent
 * commit on top (refused by the shape rule). The trailer is read from the commit's own message,
 * so a sentence that merely mentions a reading does not count. Then the build rule: a merge that adds
 * a file under `src/` is refused while nothing records a build, allowed once its commit is recorded,
 * and still allowed with a documentation merge on top of it that was not built itself.
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
const builtDir = path.join(git(code.dir, ['rev-parse', '--path-format=absolute', '--git-common-dir']), 'camel-built');
fs.mkdirSync(builtDir, { recursive: true });
fs.writeFileSync(path.join(builtDir, codeMerge), 'Release\n');
const built = pushIntegration(code.dir, shell, codeMerge, code.tip);
check('the same push is allowed once the build of that merge is recorded', built.status === 0, built.out);
git(code.dir, ['checkout', '--quiet', '-b', 'lane/page', codeMerge]);
fs.writeFileSync(path.join(code.dir, 'page.md'), 'a page\n');
commit(code.dir, 'lane page');
git(code.dir, ['checkout', '--quiet', 'integration']);
const pageMerge = merge(code.dir, 'lane/page', 'Merge lane/page\n\n- a page\n\nLane-Reading: docs-only');
const onTop = pushIntegration(code.dir, shell, pageMerge, code.tip);
check('a documentation merge on top of a built merge needs no build of its own', onTop.status === 0, onTop.out);

for (const one of [allPresent, missing, code]) fs.rmSync(one.dir, { recursive: true, force: true });

const failed = checks.filter((one) => !one.holds);
for (const one of failed) {
    console.log(`FAIL  ${one.name}`);
    console.log(`      ${one.detail.split('\n').filter(Boolean).slice(0, 6).join('\n      ')}`);
}
console.log(`integration-readings: ${checks.length} checks, ${failed.length} failed`);
process.exit(failed.length === 0 ? 0 : 1);
