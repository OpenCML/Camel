#!/usr/bin/env node
/**
 * The build stamp records the configuration as well as the sources, and `-NoBuild` refuses an
 * install whose option set moved.
 *
 * The hole this pins: `out/<tag>/build-info.json` used to carry only a SHA-256 over the source
 * content, and `.camel-opts.json` — where the CMake option values live — is git-ignored, so no
 * fingerprint covered it. A build with `ENABLE_FASTVM_JIT=OFF` and a build with it `ON` stamped the
 * *same* fingerprint, and `camel-remote.ps1 -NoBuild` ("reuse the existing install") would run the
 * no-JIT binary for a JIT tree and report a reading that looked normal.
 *
 * The case builds a fake checkout under `${test_tmp}` — one CMake option, one source file, a
 * `cmake/options.json` and a `.gitignore` that lists `.camel-opts.json` — stamps it with
 * `scripts/build-info.js stamp` (the same file a build writes), then holds:
 *
 *   1. check with the same options: exit 0 and no output at all (a correct run gains no noise);
 *   2. check after `.camel-opts.json` flips the option: exit 4, and the refusal names the option
 *      that moved (`ENABLE_FASTVM_JIT: built ON, a rebuild would use OFF`) and says the sources are
 *      the same bytes — the one sentence the old stamp could not produce;
 *   3. check after flipping it back: exit 0 again, so refusing is not a one-way door;
 *   4. `-AllowStale`: the same refusal, then "continuing anyway" and exit 0;
 *   5. a stamp with `buildOptions` deleted: exit 4, "records no build options";
 *   6. a stamp with `schema` 2: exit 4, "different form of this guard", said to be untrustworthy
 *      rather than matched — the old stamp cannot prove which options produced the install;
 *   7. a fresh stamp with the other option value: exit 0, which is the reuse `-NoBuild` must keep;
 *   8. a Debug install: `--tag debug` with no `--config` passes (that is `-Tier debug -NoBuild`,
 *      which verifies out/debug without building), `--config Debug` passes, and `--config Release`
 *      is refused — same sources, same options, another configuration.
 *
 * No compiler runs: the case pins the guard's reading, not the artifact.
 *
 * Usage: node options_change.mjs            (uses CAMEL_TEST_TMPDIR, else a fresh temp directory)
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const BUILD_INFO = path.join(REPO_ROOT, 'scripts', 'build-info.js')

const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-stamp-'))
const ROOT = path.join(SCRATCH, 'repo')
const STAMP = path.join(ROOT, 'out', 'latest', 'build-info.json')

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}`, ...(ok || detail === undefined ? [] : [`\n     ${detail}`]))
    if (!ok) failed = true
}

function write(rel, text) {
    const file = path.join(ROOT, rel)
    fs.mkdirSync(path.dirname(file), { recursive: true })
    fs.writeFileSync(file, text)
}

function git(args) {
    return spawnSync('git', ['-C', ROOT, ...args], { encoding: 'utf8', windowsHide: true })
}

// The guard's environment is part of what it compares (`CAMEL_TARGET_ARCH`, `CAMEL_SKIP_PYTHON`),
// so every run below starts from the same empty ones instead of whatever the gate exported.
const ENV = { ...process.env, CAMEL_TARGET_ARCH: '', CAMEL_SKIP_PYTHON: '' }

function stampInfo(args) {
    const run = spawnSync(process.execPath, [BUILD_INFO, ...args, '--root', ROOT], {
        encoding: 'utf8',
        windowsHide: true,
        env: ENV
    })
    return { status: run.status, output: `${run.stdout || ''}${run.stderr || ''}` }
}

function checkTree(extra = []) {
    return checkTag('latest', extra)
}

function checkTag(tag, extra = []) {
    const run = spawnSync(process.execPath, [BUILD_INFO, 'check', '--tag', tag, '--root', ROOT, ...extra], {
        encoding: 'utf8',
        windowsHide: true,
        env: ENV
    })
    return { status: run.status, output: `${run.stdout || ''}${run.stderr || ''}` }
}

function readStamp() {
    return JSON.parse(fs.readFileSync(STAMP, 'utf-8'))
}

function writeStamp(stamp) {
    fs.writeFileSync(STAMP, JSON.stringify(stamp, null, 2) + '\n')
}

// --- the fake checkout ------------------------------------------------------
fs.rmSync(ROOT, { recursive: true, force: true })
write(
    'cmake/options.json',
    JSON.stringify(
        {
            options: [
                { name: 'ENABLE_FASTVM_JIT', description: 'Enable JIT compilation', default: 'ON', type: 'platform' },
                { name: 'ENABLE_FASTVM_COMPUTED_GOTO', description: 'Computed goto', default: 'ON', type: 'bool' }
            ]
        },
        null,
        4
    ) + '\n'
)
write('src/main.cpp', '// the one source file this fake checkout builds\n')
write('CMakeLists.txt', 'project(case CXX)\n')
write('.gitignore', '.camel-opts.json\nout/\n')

const init = git(['init', '-q'])
check('git init in the fake checkout', init.status === 0, init.stderr || init.error)
git(['config', 'user.email', 'case@example.invalid'])
git(['config', 'user.name', 'camel test case'])
git(['add', '-A'])
const commit = git(['commit', '-q', '-m', 'the fake checkout'])
check('git commit in the fake checkout', commit.status === 0, commit.stderr || commit.error)

// --- 1. the stamp, with the option at its default ---------------------------
const stampRun = stampInfo(['stamp', '--tag', 'latest', '--config', 'Release'])
check('stamp exits 0', stampRun.status === 0, stampRun.output)
check('the stamp log names the option fingerprint', /options [0-9a-f]{12}/.test(stampRun.output), stampRun.output)

let stamp = readStamp()
check('the stamp records a schema this build writes', stamp.schema === 3, `schema ${stamp.schema}`)
check(
    'the stamp records the build options and their hash',
    stamp.buildOptions && /^[0-9a-f]{64}$/.test(stamp.buildOptions.hash || ''),
    JSON.stringify(stamp.buildOptions)
)
check(
    'the recorded option set is the one a build would pass',
    stamp.buildOptions.cmake.ENABLE_FASTVM_JIT === 'ON' && stamp.buildOptions.cmake.ENABLE_FASTVM_COMPUTED_GOTO === 'ON',
    JSON.stringify(stamp.buildOptions.cmake)
)
check(
    'the stamp records the platform and the compiler selection',
    typeof stamp.buildOptions.platform === 'string' && stamp.buildOptions.platform.includes('/') &&
        typeof stamp.buildOptions.compilerFlags === 'string' && stamp.buildOptions.compilerFlags.includes('clang'),
    JSON.stringify({ platform: stamp.buildOptions.platform, compilerFlags: stamp.buildOptions.compilerFlags })
)
const sourceHash = stamp.fingerprint.hash
const optionsHash = stamp.buildOptions.hash

const clean = checkTree()
check('check with the same options exits 0', clean.status === 0, clean.output)
check('check with the same options prints nothing', clean.output.trim() === '', clean.output)

// --- 2. an option moves, the sources do not ---------------------------------
write('.camel-opts.json', '{ "ENABLE_FASTVM_JIT": "OFF" }\n')
const moved = checkTree()
check('check after the option moved exits 4', moved.status === 4, `${moved.status}: ${moved.output}`)
check(
    'the refusal names the option that moved',
    moved.output.includes('ENABLE_FASTVM_JIT: built ON, a rebuild would use OFF'),
    moved.output
)
check(
    'the refusal says the sources are the same bytes',
    moved.output.includes('Same source bytes, other options'),
    moved.output
)
check(
    'the refusal does not claim the sources moved',
    !moved.output.includes('built from different sources'),
    moved.output
)
check(
    'the refusal prints both option fingerprints',
    moved.output.split(optionsHash.slice(0, 12)).length - 1 === 1,
    moved.output
)
check(
    'the sources did not move: the stamp still carries the same source hash',
    readStamp().fingerprint.hash === sourceHash,
    `${readStamp().fingerprint.hash} vs ${sourceHash}`
)

// --- 3. flipping it back is accepted again ----------------------------------
fs.rmSync(path.join(ROOT, '.camel-opts.json'), { force: true })
const back = checkTree()
check('check after the option went back exits 0', back.status === 0, `${back.status}: ${back.output}`)

// --- 4. -AllowStale is loud and continues -----------------------------------
write('.camel-opts.json', '{ "ENABLE_FASTVM_JIT": "OFF" }\n')
const stale = checkTree(['--allow-stale'])
check('check --allow-stale exits 0', stale.status === 0, `${stale.status}: ${stale.output}`)
check(
    'check --allow-stale still prints the refusal, then continues anyway',
    stale.output.includes('ENABLE_FASTVM_JIT: built ON, a rebuild would use OFF') &&
        stale.output.includes('-AllowStale was given: continuing anyway') &&
        stale.output.includes('does not describe this tree'),
    stale.output
)
fs.rmSync(path.join(ROOT, '.camel-opts.json'), { force: true })

// --- 5. a stamp with no option record is untrustworthy ----------------------
const withoutOptions = readStamp()
delete withoutOptions.buildOptions
writeStamp(withoutOptions)
const noOptions = checkTree()
check('a stamp with no buildOptions is refused (exit 4)', noOptions.status === 4, `${noOptions.status}: ${noOptions.output}`)
check(
    'the refusal says the options were never recorded',
    noOptions.output.includes('records no build options') && noOptions.output.includes('two different installs'),
    noOptions.output
)

// --- 6. an older stamp is refused, not matched ------------------------------
const oldSchema = readStamp()
oldSchema.schema = 2
writeStamp(oldSchema)
const oldForm = checkTree()
check('a schema-2 stamp is refused (exit 4)', oldForm.status === 4, `${oldForm.status}: ${oldForm.output}`)
check(
    'the refusal says an old stamp is untrustworthy rather than matching',
    oldForm.output.includes('different form of this guard') &&
        oldForm.output.includes('cannot prove which options produced the install'),
    oldForm.output
)

// --- 7. a rebuild with the other options is reusable again ------------------
write('.camel-opts.json', '{ "ENABLE_FASTVM_JIT": "OFF" }\n')
const restamp = stampInfo(['stamp', '--tag', 'latest'])
check('stamping the other option set exits 0', restamp.status === 0, restamp.output)
stamp = readStamp()
check(
    'the second stamp records the other value',
    stamp.buildOptions.cmake.ENABLE_FASTVM_JIT === 'OFF' && stamp.buildOptions.hash !== optionsHash,
    JSON.stringify(stamp.buildOptions.cmake)
)
check(
    'the two builds have the same source fingerprint and different option hashes',
    stamp.fingerprint.hash === sourceHash,
    `${stamp.fingerprint.hash} vs ${sourceHash}`
)
const reuse = checkTree()
check('check with the rebuilt options exits 0 (the reuse -NoBuild must keep)', reuse.status === 0, reuse.output)

// --- 8. the configuration is compared only where the caller names one -------
// `camel-remote.ps1 -Tier debug -NoBuild` verifies out/debug without building and passes no
// `--config`; that run must keep working. When a configuration is named, an install collected as
// the other one is refused.
const debugStampRun = stampInfo(['stamp', '--tag', 'debug', '--config', 'Debug'])
check('stamping a debug install exits 0', debugStampRun.status === 0, debugStampRun.output)
const debugStamp = JSON.parse(fs.readFileSync(path.join(ROOT, 'out', 'debug', 'build-info.json'), 'utf-8'))
check('the debug stamp records its configuration', debugStamp.config === 'Debug', debugStamp.config)
check(
    'both stamps carry the same source and option hashes: only the configuration differs',
    debugStamp.fingerprint.hash === stamp.fingerprint.hash && debugStamp.buildOptions.hash === stamp.buildOptions.hash,
    `${debugStamp.fingerprint.hash} / ${debugStamp.buildOptions.hash}`
)
const bareDebug = checkTag('debug')
check(
    'check --tag debug with no --config passes, as -Tier debug -NoBuild does',
    bareDebug.status === 0 && bareDebug.output.trim() === '',
    `${bareDebug.status}: ${bareDebug.output}`
)
const asDebug = checkTag('debug', ['--config', 'Debug'])
check('check --config Debug on a Debug install exits 0', asDebug.status === 0, `${asDebug.status}: ${asDebug.output}`)
const asRelease = checkTag('debug', ['--config', 'Release'])
check('check --config Release on a Debug install is refused (exit 4)', asRelease.status === 4, `${asRelease.status}: ${asRelease.output}`)
check(
    'the refusal names both configurations',
    asRelease.output.includes('collected as a Debug build') && asRelease.output.includes('about the Release build'),
    asRelease.output
)
check(
    'the refusal says the sources and the options are unchanged',
    asRelease.output.includes('Same source bytes, another configuration') &&
        !asRelease.output.includes('built from different sources'),
    asRelease.output
)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)