// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The wheel tags a release is allowed to upload, as a static check of the release gate.
//
// The rule itself lives in `scripts/check_wheel_tags.py --ruling`, because that script is what the
// build job already runs and one definition is the point: the two forms of the 2026-10-25 decision
// (`cp310-abi3-<platform>` with the interop extension, `py3-none-<platform>` without it; see
// docs/handover/README.cn.md item 7 and review index C28) are asserted there, and this file only
// finds the wheels, finds an interpreter for them and passes the exit code through.
//
// The gate does not build, so there is usually nothing to check: `dist/pypi/` holds wheels only
// after `npm run pypi:rollup`, which is a release step of its own. With no wheel in it this prints
// a SKIPPED banner naming that command and exits zero -- the same accounting `rule-proofs.mjs`
// prints for a missing solver: nothing was checked, and the banner says so instead of the layer
// reporting a pass. Wheels are found when:
//
//   * they are named on the command line (`node scripts/check-wheel-tags.mjs <wheel>...`), or
//   * `dist/pypi/*.whl` exists and nothing is named.
//
// An interpreter is `$CAMEL_PYPI_PYTHON`, then `python3`, then `python`. Wheels without one is a
// failure, not a skip: a wheel house cannot exist on a machine that has no Python 3 at all.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const WHEEL_HOUSE = path.join(ROOT, 'dist', 'pypi')

/** A path as it is printed: relative to the tree, with `/` on every platform. */
function shown(target) {
    return (path.relative(ROOT, target) || '.').split(path.sep).join('/')
}

function wheelsOf(targets) {
    if (targets.length > 0) {
        return targets.map((target) => path.resolve(target))
    }
    if (!fs.existsSync(WHEEL_HOUSE)) {
        return []
    }
    return fs
        .readdirSync(WHEEL_HOUSE)
        .filter((name) => name.endsWith('.whl'))
        .sort()
        .map((name) => path.join(WHEEL_HOUSE, name))
}

function findPython() {
    const named = (process.env.CAMEL_PYPI_PYTHON || '').trim()
    const candidates = named ? [named, 'python3', 'python'] : ['python3', 'python']
    for (const candidate of candidates) {
        // No `shell`: the probe is `python -c ...`, and a shell would split it at the semicolon.
        // A real interpreter (`python.exe`, `/usr/bin/python3`) is found through PATH without one;
        // a console-script `.cmd` wrapper would not be, and the failure message below says so.
        const probe = spawnSync(candidate, ['-c', 'import sys; print(sys.version_info[0])'], {
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'ignore']
        })
        if (probe.status === 0 && probe.stdout.trim() === '3') {
            return candidate
        }
    }
    return null
}

function main() {
    const targets = process.argv.slice(2)
    const wheels = wheelsOf(targets)
    const named = targets.length > 0
    if (wheels.length === 0) {
        console.log(
            'check-wheel-tags: SKIPPED: no wheel to read, so no tag was checked (this is not a pass).\n' +
                `  looked in ${shown(WHEEL_HOUSE)}/ for *.whl\n` +
                '  a wheel is built by `npm run pypi:rollup` (release step; it builds the tree), and\n' +
                '  this check runs over whatever that left in dist/pypi/'
        )
        process.exit(0)
    }

    const python = findPython()
    if (!python) {
        console.error(
            'check-wheel-tags: FAIL: wheels were found, but no Python 3 interpreter is on this ' +
                'machine ($CAMEL_PYPI_PYTHON, python3, python). The rule is ' +
                'scripts/check_wheel_tags.py; run it with the Python that built the wheels.'
        )
        process.exit(1)
    }

    const checker = path.join(ROOT, 'scripts', 'check_wheel_tags.py')
    const run = spawnSync(python, [checker, '--ruling', ...wheels], {
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'pipe']
    })
    if (run.stdout) process.stdout.write(run.stdout)
    if (run.stderr) process.stderr.write(run.stderr)
    if (run.status !== 0) {
        console.error(
            `check-wheel-tags: FAIL: the ${wheels.length} wheel(s) checked ` +
                `(${named ? 'named on the command line' : `from ${shown(WHEEL_HOUSE)}/`}) do not ` +
                'follow the two forms of the 2026-10-25 ruling: cp310-abi3 with the interop ' +
                'extension, py3-none without it.'
        )
        process.exit(run.status ?? 1)
    }
    console.log(`check-wheel-tags: ok (${wheels.length} wheel(s), checker exit 0)`)
    return 0
}

process.exit(main() ?? 0)