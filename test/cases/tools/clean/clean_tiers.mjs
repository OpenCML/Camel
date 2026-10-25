#!/usr/bin/env node
/**
 *  `npm run clean` removes exactly the four tiers it owns and nothing else.
 *
 * The case builds a checkout under `${test_tmp}` that looks like a used worktree: a build tree, a
 * `cmake-build-*` directory, three `out/<tag>` installs, two test run directories, the results
 *  log, source-tree pollution the guard reports, and a source declaration. Two files must
 * survive because they are source, not build output:
 *
 *   - `stdlib/keep.cml` — a declaration file (`stdlib/*.cml`);
 *   - `stdlib/keep.cmo` — a file that looks exactly like the pollution the source tier deletes,
 *     but it is in the fake checkout's git index (`git add`), so the tracked-path rule keeps it.
 *
 * It also plants the tracked fixture `test/cases/modules/pass_not_found/unloadable.cmo`, a file
 * that is tracked in the real checkout and must never be removed, and `tmp/keep.txt`, a file
 * outside the `run-*` scratch. Then it runs, in order:
 *
 *   1. each tier flag alone with `--dry-run`, and checks the exact `would remove` set;
 *   2. the default (no tier flag) and `--all`, which select the same four tiers;
 *   3. `node scripts/check-source-artifacts.mjs --clean --dry-run --root <fake>`, which must
 *      delegate to the same implementation and list the same source artifacts;
 *   4. the default for real, and checks the exact paths that went and the exact paths that stayed;
 *   5. the fake checkout's `.git` still exists, and no surviving file was touched.
 *
 * The tracked `stdlib/keep.cmo` makes both the dry run and the real run exit 1: the cleaner leaves
 * it in place and says so, which is the rule the case pins. Everything else was still removed.
 *
 * Usage: node clean_tiers.mjs            (uses CAMEL_TEST_TMPDIR, else a fresh temp directory)
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CLEAN = path.join(REPO_ROOT, 'scripts', 'clean.js')
const CHECK = path.join(REPO_ROOT, 'scripts', 'check-source-artifacts.mjs')

const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-clean-'))
const ROOT = path.join(SCRATCH, 'repo')

const BUILD = ['build', 'cmake-build-debug']
const OUT = ['out/debug', 'out/latest', 'out/nightly']
const TMP = ['test/results/log', 'tmp/test/run-100', 'tmp/test/run-200']
const SOURCE = ['modules/foo/libfoo.so', 'stdlib/polluted.cmo']
const ALL = [...BUILD, ...OUT, ...TMP, ...SOURCE]
const TRACKED = 'stdlib/keep.cmo'
const SURVIVORS = [
    '.git',
    'tmp/keep.txt',
    'stdlib/keep.cml',
    'stdlib/keep.cmo',
    'src/keep.cpp',
    'test/cases/modules/pass_not_found/unloadable.cmo'
]

let failures = 0
function check(ok, message) {
    if (!ok) {
        failures++
        console.log(`FAIL ${message}`)
    }
}

function checkSet(label, actual, expected) {
    const got = [...actual].sort()
    const want = [...expected].sort()
    check(
        got.length === want.length && got.every((value, i) => value === want[i]),
        `${label}: got [${got.join(', ')}], expected [${want.join(', ')}]`
    )
}

function write(rel, text = 'planted by clean_tiers.mjs\n') {
    const file = path.join(ROOT, rel)
    fs.mkdirSync(path.dirname(file), { recursive: true })
    fs.writeFileSync(file, text)
}

function exists(rel) {
    return fs.existsSync(path.join(ROOT, rel))
}

function run(script, args) {
    const result = spawnSync(process.execPath, [script, ...args], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        windowsHide: true
    })
    if (result.error) {
        throw result.error
    }
    return result
}

const stripAnsi = (text) => text.replace(/\x1b\[[0-9;]*m/g, '')

function report(stdout) {
    const removed = new Set()
    const skipped = new Set()
    for (const line of stripAnsi(stdout).split(/\r?\n/)) {
        let found = line.match(/^(?:would remove|removed) (.+)$/)
        if (found) {
            removed.add(found[1])
            continue
        }
        found = line.match(/^clean: skip (.+?): /)
        if (found) {
            skipped.add(found[1])
        }
    }
    return { removed, skipped }
}

function clean(args) {
    const result = run(CLEAN, ['--root', ROOT, ...args])
    return { ...result, ...report(result.stdout) }
}

function checkSurvivors(label) {
    for (const rel of SURVIVORS) {
        check(exists(rel), `${label}: ${rel} must survive`)
    }
}

// --- the fake checkout ------------------------------------------------------
fs.rmSync(ROOT, { recursive: true, force: true })
write('build/Release/camel')
write('build/Debug/camel')
write('cmake-build-debug/CMakeCache.txt')
write('out/latest/bin/camel')
write('out/debug/bin/camel')
write('out/nightly/build-info.json')
write('tmp/test/run-100/feat.tools/one/scratch.txt')
write('tmp/test/run-200/feat.tools/two/scratch.txt')
write('tmp/keep.txt')
write('test/results/log/20260101_1/summary.json')
write('test/cases/modules/pass_not_found/unloadable.cmo', 'the tracked fixture\n')
write('stdlib/keep.cml', '// a declaration file: source, never clean output\nexport func keep(): int\n')
write('stdlib/keep.cmo', 'tracked build output: the cleaner must not delete it\n')
write('stdlib/polluted.cmo', 'build output left in the source tree\n')
write('modules/foo/libfoo.so', 'linker output left in a source directory\n')
write('src/keep.cpp', '// source outside every tier\n')

const gitInit = spawnSync('git', ['init', '-q', ROOT], { encoding: 'utf8', windowsHide: true })
check(gitInit.status === 0, `git init in the fake checkout failed: ${gitInit.stderr || gitInit.error}`)
const gitAdd = spawnSync('git', ['-C', ROOT, 'add', '-f', TRACKED], { encoding: 'utf8', windowsHide: true })
check(gitAdd.status === 0, `git add ${TRACKED} failed: ${gitAdd.stderr || gitAdd.error}`)

// --- each tier alone, dry ---------------------------------------------------
for (const [flag, expected] of [
    ['--build', BUILD],
    ['--out', OUT],
    ['--tmp', TMP]
]) {
    const result = clean([flag, '--dry-run'])
    check(result.status === 0, `${flag} --dry-run must exit 0, got ${result.status}`)
    checkSet(`${flag} --dry-run`, result.removed, expected)
    check(result.skipped.size === 0, `${flag} --dry-run must not skip anything`)
}
{
    const result = clean(['--source', '--dry-run'])
    check(result.status === 1, `--source --dry-run must exit 1 for the tracked file, got ${result.status}`)
    checkSet('--source --dry-run', result.removed, SOURCE)
    checkSet('--source --dry-run skipped', result.skipped, [TRACKED])
}

// --- the default and --all select the same tiers ----------------------------
{
    const byDefault = clean(['--dry-run'])
    check(byDefault.status === 1, `the default --dry-run must exit 1 for the tracked file, got ${byDefault.status}`)
    checkSet('default --dry-run', byDefault.removed, ALL)
    checkSet('default --dry-run skipped', byDefault.skipped, [TRACKED])

    const all = clean(['--all', '--dry-run'])
    check(all.status === 1, `--all --dry-run must exit 1 for the tracked file, got ${all.status}`)
    checkSet('--all --dry-run', all.removed, ALL)
    checkSet('--all --dry-run skipped', all.skipped, [TRACKED])
}

// Nothing has been deleted yet.
check(exists('build/Release/camel'), 'the dry runs must not delete build/')
check(exists('out/latest/bin/camel'), 'the dry runs must not delete out/latest')
check(exists('tmp/test/run-100/feat.tools/one/scratch.txt'), 'the dry runs must not delete a run directory')
check(exists('stdlib/polluted.cmo'), 'the dry runs must not delete source-tree pollution')
checkSurvivors('after the dry runs')

// --- the guard's --clean delegates to the same delete -----------------------
{
    const result = run(CHECK, ['--clean', '--dry-run', '--root', ROOT])
    const parsed = report(result.stdout)
    check(result.status === 1, `the guard --clean --dry-run must exit 1 for the tracked file, got ${result.status}`)
    checkSet('guard --clean --dry-run', parsed.removed, SOURCE)
    checkSet('guard --clean --dry-run skipped', parsed.skipped, [TRACKED])
    check(exists('stdlib/polluted.cmo'), 'the guard --clean --dry-run must not delete anything')
}

// --- for real ---------------------------------------------------------------
{
    const result = clean([])
    check(result.status === 1, `the real run must exit 1 for the tracked file, got ${result.status}`)
    checkSet('real run', result.removed, ALL)
    checkSet('real run skipped', result.skipped, [TRACKED])
}
for (const rel of BUILD.concat(OUT, TMP, SOURCE)) {
    check(!exists(rel), `${rel} must be removed`)
}
check(exists('out'), 'out/ itself is left as an empty directory')
check(fs.readdirSync(path.join(ROOT, 'out')).length === 0, 'out/ must be empty after the clean')
checkSurvivors('after the real run')

if (failures > 0) {
    console.log(`FAIL clean.tiers: ${failures} check(s) failed`)
    process.exit(1)
}
console.log(
    'PASS clean.tiers: the four tiers, the default/--all set, the guard delegation, ' +
        'and the tracked-file, fixture and .git rules'
)