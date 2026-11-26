#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The one bridge between a plan entry and a self-check written in PowerShell.
 *
 * Nineteen of this tooling's own tests are `internal/tools/*.test.ps1`. A plan entry cannot start one
 * of them by naming the script, in either of the two ways an entry has:
 *
 *   * `tool = "pwsh"` resolves through `toolExecutable` in the runner (`test/tools/run-tests.mjs`),
 *     which answers `<install>/bin/<tool>` for every tool that is neither `node` nor `python`. The
 *     entry would therefore look for `out/latest/bin/pwsh` -- a file no build produces -- and fail
 *     before the script ran. A tool the install does not ship has no spelling in that table;
 *   * `case` must be a file the runner can execute, and a `.ps1` is not one: it is a document to
 *     `node` and to `camel` alike.
 *
 * So an entry names this file as its `case` and this file starts `pwsh`. There are two modes because
 * there are two questions, and they must not answer for each other:
 *
 *   `--probe`        "is there a pwsh on this host?" Exit 0 when there is, 1 when there is not, with
 *                    the measurement on the line. An entry names this as its `skip_unless`, so a host
 *                    without one **skips** the entry and prints the reason. A skip is not a pass and
 *                    the summary counts them separately (`docs/test-plans.md`, "skip_unless"), which
 *                    is the whole point: the gate host is Linux and ships no PowerShell, so what this
 *                    must never do is report the entry as green.
 *   `<script> [...]` run that script and exit with its own exit code. The output is inherited, not
 *                    copied, so an entry's `expect_stdout_contains` reads the summary the script
 *                    printed rather than a count kept here.
 *
 * A pwsh that disappears between the probe and the run is **127 with a sentence, never 0**: an
 * unread self-check is not a green one.
 *
 * The interpreter is `$CAMEL_PWSH` when that is set (a host whose PowerShell is not on `PATH`), and
 * `pwsh` from `PATH` otherwise. One resolution for both modes, so the probe and the run cannot
 * disagree about which binary they mean.
 */
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')
const USAGE = `usage: node test/tools/pwsh-selfcheck.mjs --probe
       node test/tools/pwsh-selfcheck.mjs <self-check.ps1> [args...]`

function interpreter() {
    const named = process.env.CAMEL_PWSH
    return named && named.trim() ? named.trim() : 'pwsh'
}

/** Where pwsh is, or why there is none. Never returns "no answer" as "fine". */
function probe() {
    const exe = interpreter()
    const env = `${process.platform}${process.env.CAMEL_PWSH ? ', CAMEL_PWSH' : ''}`
    const run = spawnSync(exe, ['-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()'], {
        encoding: 'utf8',
    })
    if (run.error?.code === 'ENOENT') {
        return { ok: false, reason: `no '${exe}' on this host (${env})` }
    }
    if (run.error) {
        return { ok: false, reason: `'${exe}' could not be started (${env}): ${run.error.message}` }
    }
    const text = `${run.stdout || ''}${run.stderr || ''}`.trim()
    if (run.status !== 0) {
        return { ok: false, reason: `'${exe}' exited ${run.status} when asked its version (${env}): ${text || 'no output'}` }
    }
    return { ok: true, exe, version: text.split(/\r?\n/)[0] || 'unknown version' }
}

const argv = process.argv.slice(2)
if (argv.length === 0 || argv[0] === '--help' || argv[0] === '-h') {
    console.error(USAGE)
    process.exit(2)
}
if (argv[0] === '--probe') {
    const found = probe()
    if (found.ok) {
        console.log(`pwsh found: ${found.exe} ${found.version}`)
        process.exit(0)
    }
    // The reason travels as the skip's own text: `precondition not met (...): <this line>`.
    console.error(found.reason)
    process.exit(1)
}

const script = path.isAbsolute(argv[0]) ? argv[0] : path.resolve(REPO_ROOT, argv[0])
if (!fs.existsSync(script)) {
    console.error(`no such self-check: ${script}`)
    process.exit(2)
}
const found = probe()
if (!found.ok) {
    console.error(`the run reached a host its own probe said has a pwsh, and it does not: ${found.reason}`)
    process.exit(127)
}
const child = spawnSync(found.exe, ['-NoProfile', '-File', script, ...argv.slice(1)], { stdio: 'inherit' })
if (child.error) {
    console.error(`'${found.exe} -File ${script}' could not be started: ${child.error.message}`)
    process.exit(127)
}
// A child that died on a signal has no exit code, and a signal is not a pass either.
if (child.status === null) {
    console.error(`'${found.exe} -File ${script}' ended on signal ${child.signal}`)
    process.exit(126)
}
process.exit(child.status)