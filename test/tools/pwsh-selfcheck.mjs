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
 * otherwise `pwsh` from `PATH`, then on Windows `powershell` (Windows PowerShell 5.1). One resolution
 * for both modes, so the probe and the run cannot disagree about which binary they mean.
 */
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')
const USAGE = `usage: node test/tools/pwsh-selfcheck.mjs --probe
       node test/tools/pwsh-selfcheck.mjs <self-check.ps1> [args...]`

// The interpreters tried, in order. On Windows, Windows PowerShell 5.1 is the fallback: the tools
// these self-checks test are run on Windows workstations, which often have no pwsh, and a self-check
// that only runs where pwsh happens to be installed is one that never runs.
function interpreters() {
    const named = process.env.CAMEL_PWSH
    if (named && named.trim()) return [named.trim()]
    return process.platform === 'win32' ? ['pwsh', 'powershell'] : ['pwsh']
}

// Windows PowerShell refuses to run a script file under the default execution policy of many
// machines; the policy is not what these self-checks are about.
function scriptArgs(script) {
    const policy = process.platform === 'win32' ? ['-ExecutionPolicy', 'Bypass'] : []
    return ['-NoProfile', ...policy, '-File', script]
}

/** Which interpreter answers, or why none does. Never returns "no answer" as "fine". */
function probe() {
    const env = `${process.platform}${process.env.CAMEL_PWSH ? ', CAMEL_PWSH' : ''}`
    const reasons = []
    for (const exe of interpreters()) {
        const run = spawnSync(exe, ['-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()'], {
            encoding: 'utf8',
        })
        if (run.error?.code === 'ENOENT') {
            reasons.push(`no '${exe}' on this host (${env})`)
            continue
        }
        if (run.error) {
            reasons.push(`'${exe}' could not be started (${env}): ${run.error.message}`)
            continue
        }
        const text = `${run.stdout || ''}${run.stderr || ''}`.trim()
        if (run.status !== 0) {
            reasons.push(`'${exe}' exited ${run.status} when asked its version (${env}): ${text || 'no output'}`)
            continue
        }
        return { ok: true, exe, version: text.split(/\r?\n/)[0] || 'unknown version' }
    }
    return { ok: false, reason: reasons.join('; ') }
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
// `--probe-bash`: a PowerShell and a bash that reads Windows paths, for the self-checks that do all
// of their measuring in bash. On Windows the bash is the one `internal/tools/git-bash.ps1` finds, so
// the probe and the self-check cannot disagree about which bash they mean.
if (argv[0] === '--probe-bash') {
    const found = probe()
    if (!found.ok) {
        console.error(found.reason)
        process.exit(1)
    }
    const helper = path.join(REPO_ROOT, 'internal', 'tools', 'git-bash.ps1')
    const ask =
        process.platform === 'win32'
            ? spawnSync(found.exe, ['-NoProfile', '-Command', `. '${helper}'; $b = Get-GitBash; if ($b) { $b; exit 0 } else { exit 1 }`], { encoding: 'utf8' })
            : spawnSync('bash', ['-c', 'command -v bash'], { encoding: 'utf8' })
    const where = `${ask.stdout || ''}`.trim()
    if (ask.status === 0 && where) {
        console.log(`bash found: ${where}`)
        process.exit(0)
    }
    console.error(process.platform === 'win32' ? 'no Git for Windows bash on this host' : 'no bash on this host')
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
const child = spawnSync(found.exe, [...scriptArgs(script), ...argv.slice(1)], {
    encoding: 'utf8',
    stdio: ['inherit', 'pipe', 'pipe'],
    maxBuffer: 64 * 1024 * 1024,
})
if (child.error) {
    console.error(`'${found.exe} -File ${script}' could not be started: ${child.error.message}`)
    process.exit(127)
}
process.stdout.write(child.stdout || '')
process.stderr.write(child.stderr || '')
// A child that died on a signal has no exit code, and a signal is not a pass either.
if (child.status === null) {
    console.error(`'${found.exe} -File ${script}' ended on signal ${child.signal}`)
    process.exit(126)
}
// A self-check whose every check was skipped here measured nothing, and its "0 checks, 0 failed"
// must not read as green. The count is the last one its summary line printed.
const counts = [...`${child.stdout || ''}`.matchAll(/\b(\d+) check/g)]
if (child.status === 0 && counts.length > 0 && Number(counts[counts.length - 1][1]) === 0) {
    console.error(`${path.basename(script)} ran no check on this host: everything it measures was skipped, which is not a pass`)
    process.exit(3)
}
process.exit(child.status)