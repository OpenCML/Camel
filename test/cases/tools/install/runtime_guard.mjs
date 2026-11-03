// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What an install says when the C++ runtime it finds is too old to load it.
//
// The versions a binary requires from libstdc++.so.6 are ELF version requirements and the loader
// resolves them before `main`, so the sentence cannot come from inside the program: it comes from
// `bin/camel`, the launcher (`scripts/cxx-runtime-guard.mjs`), which runs before that process
// exists. This case holds both ends of it:
//
//   * the ordinary install starts -- `--version` prints the stamp, exit 0 -- through the launcher,
//     which is what every caller of `bin/camel` now goes through;
//   * an install whose `libs/libstdc++.so.6` is the distribution's older copy ends with exit 1 and
//     two lines that name the version required, the copy that provides too little, and where to put
//     a newer one -- and *not* with the loader's own pile of `version `GLIBCXX_x.y.z' not found`.
//
// The second half needs a too-old runtime, and invents none: it uses the distribution's own
// libstdc++.so.6, and skips (printing why) on a host whose copy is new enough, or when the install
// has no launcher (Windows, macOS, an install built before the launcher existed).
//
//   node test/cases/tools/install/runtime_guard.mjs [--camel <install>/bin/camel]

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import { takeCamel } from '../../../tools/camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..')

function usage(message) {
    process.stderr.write(`runtime_guard: ${message}\n`)
    process.exit(2)
}

const { camel } = takeCamel(process.argv.slice(2), REPO_ROOT, usage)

let failures = 0
function check(ok, what) {
    if (ok) {
        process.stdout.write(`PASS ${what}\n`)
    } else {
        failures += 1
        process.stdout.write(`FAIL ${what}\n`)
    }
}

/** Run a command and keep its three readings instead of throwing on a non-zero status. */
function run(exe, args, options = {}) {
    try {
        const stdout = execFileSync(exe, args, {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'pipe'],
            ...options
        })
        return { status: 0, stdout, stderr: '' }
    } catch (error) {
        return {
            status: typeof error.status === 'number' ? error.status : -1,
            stdout: error.stdout ?? '',
            stderr: error.stderr ?? ''
        }
    }
}

/** The highest GLIBCXX_3.4.N a file's dynamic strings name, or null. */
function highestGlibcxx(file) {
    let text
    try {
        text = fs.readFileSync(file).toString('latin1')
    } catch {
        return null
    }
    let top = null
    for (const match of text.matchAll(/GLIBCXX_3\.4\.(\d+)/g)) {
        const minor = Number(match[1])
        if (top === null || minor > top) top = minor
    }
    return top === null ? null : `GLIBCXX_3.4.${top}`
}

// The distribution's copy: what the loader falls back to when an install carries none, and what an
// install that was copied without its `libs/` finds. The paths are the ones a distribution uses for
// its own multiarch and multi-lib layouts, and nothing here writes to any of them.
function distributionRuntime() {
    const candidates = [
        '/lib/x86_64-linux-gnu/libstdc++.so.6',
        '/usr/lib/x86_64-linux-gnu/libstdc++.so.6',
        '/lib64/libstdc++.so.6',
        '/usr/lib64/libstdc++.so.6',
        '/lib/aarch64-linux-gnu/libstdc++.so.6',
        '/usr/lib/aarch64-linux-gnu/libstdc++.so.6'
    ]
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) return candidate
    }
    return null
}

// 1. The ordinary install, through the launcher.
const version = run(camel, ['--version'])
check(version.status === 0, `${camel} --version exits 0 (it exited ${version.status})`)
check(
    /Camel v\d/.test(version.stdout),
    `${camel} --version prints the version stamp (it printed ${JSON.stringify(version.stdout.split('\n')[0] ?? '')})`
)

if (process.platform !== 'linux') {
    process.stdout.write(`PASS the too-old-runtime half does not apply to ${process.platform}\n`)
    process.exit(failures === 0 ? 0 : 1)
}

const installRoot = path.dirname(path.dirname(camel))
const realBinary = path.join(installRoot, 'bin', `${path.basename(camel)}.real`)
if (!fs.existsSync(realBinary)) {
    process.stdout.write(
        `PASS ${camel} is not a launcher (no ${path.basename(realBinary)} beside it), so there is no sentence to measure\n`
    )
    process.exit(failures === 0 ? 0 : 1)
}

const bundledRuntime = path.join(installRoot, 'libs', 'libstdc++.so.6')
const needed = highestGlibcxx(bundledRuntime)
const distribution = distributionRuntime()
const provided = distribution === null ? null : highestGlibcxx(distribution)
if (distribution === null || needed === null || provided === null) {
    process.stdout.write('PASS no distribution libstdc++.so.6 to compare, so there is no too-old install to build\n')
    process.exit(failures === 0 ? 0 : 1)
}
const neededMinor = Number(needed.split('.').pop())
const providedMinor = Number(provided.split('.').pop())
if (providedMinor >= neededMinor) {
    process.stdout.write(
        `PASS this host's ${distribution} is ${provided}, not older than the install's ${needed}, so no too-old install can be built here\n`
    )
    process.exit(failures === 0 ? 0 : 1)
}

// 2. An install whose runtime is too old: the four files the start needs, in a directory of their
// own, with the distribution's copy in `libs/`. Copied rather than linked: `$ORIGIN` is read from
// the path the loader opens, and a link would resolve the runtime from the install it points at.
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-install-guard-'))
try {
    for (const dir of ['bin', 'libs']) fs.mkdirSync(path.join(work, dir), { recursive: true })
    fs.copyFileSync(camel, path.join(work, 'bin', 'camel'))
    fs.copyFileSync(realBinary, path.join(work, 'bin', path.basename(realBinary)))
    for (const name of fs.readdirSync(path.join(installRoot, 'libs'))) {
        if (!name.endsWith('.so')) continue
        fs.copyFileSync(path.join(installRoot, 'libs', name), path.join(work, 'libs', name))
    }
    fs.copyFileSync(distribution, path.join(work, 'libs', 'libstdc++.so.6'))
    fs.chmodSync(path.join(work, 'bin', 'camel'), 0o755)

    const refused = run(path.join(work, 'bin', 'camel'), ['--version'], {
        env: { ...process.env, LD_LIBRARY_PATH: '' }
    })
    check(refused.status === 1, `a too-old runtime exits 1 (it exited ${refused.status})`)
    check(
        refused.stdout === '',
        `nothing runs (standard output was ${JSON.stringify(refused.stdout)})`
    )
    check(
        new RegExp(`needs the C\\+\\+ runtime ${needed}, and \\S+ has at most ${provided}`).test(refused.stderr),
        `the sentence names the version required and the copy that is too old: ${JSON.stringify(refused.stderr.split('\n')[0] ?? '')}`
    )
    check(
        /put a libstdc\+\+\.so\.6 that has GLIBCXX_3\.4\.\d+ .* into \S+libs, and run this again\./.test(
            refused.stderr
        ),
        'and says where to put a newer one'
    )
    check(
        !/\(required by/.test(refused.stderr),
        `the loader's own pile is not what the user reads (it read ${JSON.stringify(refused.stderr.split('\n').length)} lines)`
    )
} finally {
    fs.rmSync(work, { recursive: true, force: true })
}

process.exit(failures === 0 ? 0 : 1)