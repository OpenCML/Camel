// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The push hook and the checker it calls share one contract, and the hook owns half of it: the
 * checker reads a tree only when the caller names one, so the hook has to name this tree.
 *
 * The failure this file measures is the hook starting the checker without that name: the checker
 * answers that the tree was not named and exits 2, and every push to the remote is refused before
 * anything is read. A check that reads the hook's text cannot see it -- the line and the required
 * word are both present for a grep, and the pairing between them is what was missing. So the hook
 * is run, and the arguments the checker is actually started with are written down.
 *
 * Why a throwaway repository instead of this tree: the checker read over this tree reads thousands
 * of files and judges them by a rule the tree is still being brought up to, so a run of it here
 * would be slow and red for reasons this file does not measure. The fixture's `src/` and `scripts/`
 * are clean, so a non-zero exit can only be the argument contract. Nothing is pushed and no remote
 * is contacted: the hook is started by hand, in a directory that is a repository and nothing else.
 *
 * The remote name handed to the hook is `origin` and not a made-up one. The checker call sits behind
 * the hook's `origin` test, so any other name skips the call and every assertion below would hold
 * over an invocation that never happened -- the vacuous reading is the one a reader reaches for
 * first. The hook's other half, the integration shape lock, is a no-op stand-in here: that is
 * another file's subject, and its own text carries a work item number and a date, which the checker
 * this fixture runs would refuse and turn into a red this file does not mean.
 *
 * The file read here is `scripts/hooks/pre-push`, which is where the push rules live; `.husky/pre-push`
 * hands every push to it and holds no rule of its own. A test that read the dispatcher would be
 * reading a file that cannot fail the way this one does.
 *
 * The push it sends is two real commits against each other, not a pair of zeros: a ref with no tip on
 * the remote has no range, so the hook says it has nothing to compare and passes without starting the
 * checker -- a green over an invocation that never happened. Two commits give it a range, and the
 * checker's stand-in records the invocation.
 *
 * `--hook <file>` reads another hook instead of that one. It is how the negative control is run
 * without editing the hook: a copy with the required name removed must turn this file red. The plan
 * entry passes no flag, so what a run measures by default is the file `.husky/pre-push` hands to.
 *
 * Exit: 0 when every check holds, 1 when one does not, 2 when the hook or the shell git ships could
 * not be read -- a host that cannot run this cannot say the hook is fine.
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const treeRoot = path.resolve(here, '..', '..');
const defaultHook = path.join(treeRoot, 'scripts', 'hooks', 'pre-push');
const checkerFile = path.join(treeRoot, 'scripts', 'check-process-traces.mjs');
const checkerName = 'check-process-traces.mjs';
const branch = 'refs/heads/a-branch-this-fixture-never-sends';
const url = 'https://example.invalid/Camel.git';
const notNamed = '--root <dir> is required';

// --- the interpreter git itself would use ------------------------------------------------------
// A hook is started by `sh`. On Windows that is the one Git for Windows installs beside git and not
// the one on the machine's PATH; asking git for its exec path and stepping up out of
// mingw64/libexec/git-core finds the same interpreter without a guess about one install, and on a
// host whose git keeps its shell in /bin the same step lands on /.
function gitShell() {
    let execPath = '';
    try {
        execPath = execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim();
    } catch (error) {
        return { reason: `git could not be asked for its exec path: ${error.message}` };
    }
    const root = path.dirname(path.dirname(path.dirname(execPath)));
    if (!path.isAbsolute(root)) {
        return { reason: `git's exec path is not absolute (${execPath}), so no shell sits beside it` };
    }
    const candidates = [
        path.join(root, 'usr', 'bin', 'sh.exe'),
        path.join(root, 'bin', 'sh.exe'),
        path.join(root, 'usr', 'bin', 'sh'),
        path.join(root, 'bin', 'sh'),
    ];
    const sh = candidates.find((one) => fs.existsSync(one));
    if (!sh) {
        return { reason: `no sh beside git (looked in ${candidates.join(', ')})` };
    }
    return { sh, root };
}

// The hook's body runs `cat`, `tr`, `sed` and `git`, which on Windows are binaries in git's own
// usr/bin: a shell started from a node process has only the machine's PATH. This rebuilds the
// environment git gives a hook, with node's own directory kept first so the hook's `node` is the
// interpreter running this check. On Windows the PATH variable's name is not fixed in case, and two
// spellings of it in one environment is a coin toss, so every spelling is dropped before one is set.
function hookEnv(root) {
    const env = { ...process.env };
    for (const key of Object.keys(env)) {
        if (key.toUpperCase() === 'PATH') delete env[key];
    }
    const dirs = [path.dirname(process.execPath)];
    for (const one of [path.join(root, 'usr', 'bin'), path.join(root, 'mingw64', 'bin')]) {
        if (fs.existsSync(one)) dirs.push(one);
    }
    dirs.push(process.env.PATH ?? process.env.Path ?? '');
    env.PATH = dirs.filter(Boolean).join(path.delimiter);
    return env;
}

// The stand-in for the checker. It writes down the arguments it was started with and then runs the
// real checker on them, so one run gives both readings: what the hook passed, and whether the
// checker accepted it. The record is appended to, not overwritten, because a hook that starts more
// than one node script must not be read as the one invocation a reader hopes for.
function standIn(recordPath, realChecker) {
    return [
        '// A stand-in for the checker: it writes down its arguments and then runs the real one.',
        "import fs from 'node:fs';",
        "import { spawnSync } from 'node:child_process';",
        '',
        `const RECORD = ${JSON.stringify(recordPath)};`,
        `const CHECKER = ${JSON.stringify(realChecker)};`,
        '',
        'const argv = process.argv.slice(1);',
        'const child = spawnSync(process.execPath, [CHECKER, ...process.argv.slice(2)], {',
        "    encoding: 'utf8',",
        '});',
        'fs.appendFileSync(',
        '    RECORD,',
        '    JSON.stringify({',
        '        argv,',
        '        cwd: process.cwd(),',
        '        status: child.status ?? -1,',
        "        stdout: child.stdout ?? '',",
        "        stderr: child.stderr ?? '',",
        "    }) + '\\n',",
        ');',
        "process.stdout.write(child.stdout ?? '');",
        "process.stderr.write(child.stderr ?? '');",
        'process.exit(child.status ?? 1);',
        '',
    ].join('\n');
}

function write(dir, rel, text) {
    const full = path.join(dir, rel);
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, text, 'utf8');
    return full;
}

// A repository and nothing else: the hook resolves its tree through `git rev-parse --show-toplevel`
// exactly as it does on a real push, so the value it passes has to be found the same way and not
// inherited from a directory this file happened to be started in.
function buildFixture(hookSource) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hook-contract-'));
    const recordPath = path.join(dir, 'record.jsonl');
    // The hook under test, as it stands: a stand-in for it would measure the stand-in, which is how
    // this fixture spent a while passing over a hook that never started the checker.
    write(dir, 'scripts/hooks/pre-push', hookSource);
    write(dir, 'src/clean.c', '// The program reads nothing and answers zero.\nint main(void) { return 0; }\n');
    write(dir, 'checker-real/check-process-traces.mjs', fs.readFileSync(checkerFile, 'utf8'));
    write(
        dir,
        'scripts/check-process-traces.mjs',
        standIn(recordPath, path.join(dir, 'checker-real', 'check-process-traces.mjs')),
    );
    execFileSync('git', ['init', '--quiet', '--initial-branch=main', dir], { encoding: 'utf8' });
    const git = (...args) => execFileSync('git', args, { cwd: dir, encoding: 'utf8' }).trim();
    git('config', 'user.email', 'fixture@example.invalid');
    git('config', 'user.name', 'a fixture');
    git('add', '--all');
    git('commit', '--quiet', '-m', 'The first commit of a branch that is about to be pushed.');
    write(dir, 'src/second.c', '// A second file, so the push has two commits and a range to judge.\nint second(void) { return 1; }\n');
    git('add', '--all');
    git('commit', '--quiet', '-m', 'The second commit: the one this push sends.');
    return { dir, recordPath, base: git('rev-parse', 'HEAD~1'), tip: git('rev-parse', 'HEAD') };
}

// One path, two spellings: a hook's `$top` comes out of git with forward slashes and the drive in
// either case, and a path this file resolved itself carries backslashes. They name one directory or
// they name two, and realpath settles a short name or a junction between them.
function asKey(one, base) {
    let text = String(one ?? '').replace(/^\/([A-Za-z])\//, (_, drive) => `${drive.toUpperCase()}:/`);
    let resolved = path.resolve(base, text);
    try {
        resolved = fs.realpathSync.native(resolved);
    } catch (error) {
        // An unreadable path is not the same tree as a readable one; the string comparison below
        // still decides, and the caller prints both spellings.
    }
    return resolved.replace(/\\/g, '/').replace(/\/+$/, '').toLowerCase();
}

function measure(hookSource, shell) {
    const { dir, recordPath, base, tip } = buildFixture(hookSource);
    const readings = { dir, recordPath, entries: [] };
    let removed = false;
    let removeError = '';
    try {
        readings.toplevel = execFileSync('git', ['rev-parse', '--show-toplevel'], {
            cwd: dir,
            encoding: 'utf8',
        }).trim();
        // husky's dispatcher starts the hook as `sh -e`, and the line it prints when the hook fails
        // is that dispatcher's; the same flag is used here, or a hook whose first command fails
        // would run on and this file would measure a state a real push never reaches.
        const refLine = `${branch} ${tip} ${branch} ${base}\n`;
        try {
            readings.hookOutput = execFileSync(
                shell.sh,
                ['-e', 'scripts/hooks/pre-push', 'origin', url, branch, tip, branch, base],
                { cwd: dir, env: hookEnv(shell.root), input: refLine, encoding: 'utf8' },
            );
            readings.hookStatus = 0;
        } catch (error) {
            readings.hookStatus = typeof error.status === 'number' ? error.status : -1;
            readings.hookOutput = `${error.stdout ?? ''}${error.stderr ?? ''}`;
        }
        if (fs.existsSync(recordPath)) {
            readings.entries = fs
                .readFileSync(recordPath, 'utf8')
                .split('\n')
                .filter((line) => line.trim())
                .map((line) => JSON.parse(line));
        }
    } finally {
        try {
            fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 });
        } catch (error) {
            removeError = error.message;
        }
        readings.removed = !fs.existsSync(dir);
        readings.removeError = removeError;
    }
    return readings;
}

// --- arguments ---------------------------------------------------------------------------------
const argv = process.argv.slice(2);
let hookPath = defaultHook;
for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--hook' && argv[i + 1]) {
        hookPath = path.resolve(argv[i + 1]);
        i += 1;
    } else {
        console.log(`hook-checker-contract: cannot read this argument: '${argv[i]}'`);
        console.log('usage: node test/tools/hook-checker-contract.test.mjs [--hook <file>]');
        process.exit(2);
    }
}

const results = [];
function check(name, ok, detail = '') {
    results.push(ok);
    console.log(ok ? `  PASS  ${name}` : `  FAIL  ${name}`);
    if (!ok && detail) console.log(`        ${detail}`);
}

function cannotRead(reason) {
    console.log(`hook-checker-contract: not read -- ${reason}`);
    console.log('hook-checker-contract: 0 checks, 0 failed');
    process.exit(2);
}

// --- the two files this measures, and the interpreter ------------------------------------------
console.log(`hook-checker-contract: reading ${hookPath}`);
if (!fs.existsSync(hookPath)) cannotRead(`${hookPath} is not there`);
if (!fs.existsSync(checkerFile)) cannotRead(`the checker is not there (${checkerFile})`);
const shell = gitShell();
if (shell.reason) cannotRead(shell.reason);

let readings = null;
try {
    readings = measure(fs.readFileSync(hookPath, 'utf8'), shell);
} catch (error) {
    cannotRead(`the fixture could not be read: ${error.message}`);
}
const calls = readings.entries.filter((entry) => String(entry.argv?.[0] ?? '').endsWith(checkerName));
for (const entry of readings.entries) {
    console.log(`  call  node ${entry.argv.join(' ')}  (exit ${entry.status})`);
}

// The value the checker would read as its tree, by the checker's own rule: the word after `--root`,
// and not empty, and not the next flag. A `--root` with nothing behind it is a half-made edit that
// the checker refuses, and a reader of this file must meet it as "no tree named", not as a value
// that happens to resolve.
function treeNamed(entry) {
    const args = Array.isArray(entry.argv) ? entry.argv : [];
    const at = args.indexOf('--root');
    const value = at >= 0 ? args[at + 1] : undefined;
    return typeof value === 'string' && value !== '' && !value.startsWith('--') ? value : undefined;
}
const names = calls.map(treeNamed);

check(
    'the hook reaches the checker',
    calls.length > 0,
    `no call to ${checkerName} was recorded (${readings.entries.length} node script(s) started)`,
);
check(
    'the checker is told which tree to read',
    calls.length > 0 && calls.every((entry) => treeNamed(entry) !== undefined),
    `the arguments were: ${calls.map((entry) => entry.argv.join(' ')).join(' | ') || '(none)'}`,
);
check(
    'and the tree it is told is the one the hook runs in',
    calls.length > 0 &&
        calls.every((entry) => {
            const value = treeNamed(entry);
            return (
                value !== undefined &&
                asKey(value, entry.cwd ?? readings.dir) === asKey(readings.toplevel, readings.dir)
            );
        }),
    `--root names ${names.join(', ') || '(nothing)'}; the tree root is ${readings.toplevel}`,
);
check(
    `the checker accepted the invocation (no "${notNamed}")`,
    calls.length > 0 && calls.every((entry) => entry.status === 0) && !readings.hookOutput.includes(notNamed),
    `the checker exited ${calls.map((entry) => entry.status).join(', ') || '(never started)'}` +
        `: ${`${readings.hookOutput}`.trim()}`,
);
check(
    'the checker read the tree it was told about',
    calls.length > 0 &&
        calls.every(
            (entry) => Number((String(entry.stdout ?? '').match(/scanned (\d+) files/) ?? [])[1]) > 0,
        ),
    `the checker's reading: ` +
        `${calls.map((entry) => `${entry.stdout ?? ''}${entry.stderr ?? ''}`).join(' ')}`.trim(),
);
check(
    'a push from this tree would go through',
    readings.hookStatus === 0,
    `the hook exited ${readings.hookStatus}: ${`${readings.hookOutput}`.trim()}`,
);
check(
    'the fixture left nothing behind',
    readings.removed,
    `the fixture is still there (${readings.dir})${readings.removeError ? `: ${readings.removeError}` : ''}`,
);

const failed = results.filter((ok) => !ok).length;
console.log('');
console.log(`hook-checker-contract: ${results.length} checks, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
