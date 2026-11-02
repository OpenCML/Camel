#!/usr/bin/env node
/**
 * Three ways a run ends without an exit code, kept apart in the words that report it.
 *
 * `exitCode` is `null` when there is no exit code: the timeout killed the child, a signal took it
 * down, or it never started. All three used to print `got null`, and that was read as a timeout for
 * two rounds while the process was dying of SIGSEGV in 0.03 s -- a crash diagnosed as a hang, in
 * both directions. So the runner names them:
 *
 *   expected exit 0, got exit 1
 *   expected exit 0, got signal SIGSEGV (rc=139, 0.03 s)     -- 128 + the signal number
 *   expected exit 0, got timeout after 100 ms                -- only a real timeout says this
 *
 * and the `[SUMMARY]` line separates the last two too: `timed out: <n>` for the bound, `killed by a
 * signal: <n>` for the signal. `null` is not an exit code; it is the absence of one.
 *
 * Red when: a signal kill prints `null`, a signal kill calls itself a timeout, a timeout is reported
 * as anything but a timeout, the summary stops telling the two apart, or the rc of a signal is not
 * the shell's 128 + n.
 *
 * Run: node exit_reporting.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'exit-reporting-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// A child killed by a signal, and a child the bound takes down. Both are real processes: the point
// is what the runner says about a process the kernel ended, not what it says about a number.
const CRASH = path.join(SCRATCH, 'crash.mjs')
fs.writeFileSync(CRASH, 'process.kill(process.pid, "SIGSEGV")\n')
const HANG = path.join(SCRATCH, 'hang.mjs')
fs.writeFileSync(HANG, 'while (true) {}\n')

function writePlan(name, casePath, timeoutMs) {
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    fs.writeFileSync(
        planPath,
        [
            `id = "probe.${name}"`,
            'tier = "fast"',
            '',
            '[[tests]]',
            `name = "probe.${name}.entry"`,
            'kind = "behavior"',
            'tool = "node"',
            `case = "${casePath.replace(/\\/g, '/')}"`,
            'args = ["${case}"]',
            'expect_exit = 0',
            `timeout_ms = ${timeoutMs}`,
            '',
        ].join('\n'),
    )
    return planPath
}

function run(planPath) {
    const result = spawnSync(process.execPath, [RUNNER, planPath, '--jobs', '1', '--tier', 'fast'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH, CAMEL_TEST_SHOW_OUTPUT: '' },
    })
    return { status: result.status, output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}

// ---- a signal ---------------------------------------------------------------------------------
{
    const out = run(writePlan('crash', CRASH, 30000))
    const line = (out.output.match(/expected exit 0, got [^\n]*/) || [''])[0]
    check('a signal kill is reported as a signal', /got signal SIGSEGV/.test(line), line || out.output)
    check('the rc is the shell\'s 128 + 11', /rc=139/.test(line), line)
    check('the signal line carries the time it took', /SIGSEGV \(rc=139, [\d.]+ m?s\)/.test(line), line)
    check('a signal kill never says null', !/null/.test(line), line)
    check('a signal kill does not call itself a timeout', !/timeout/i.test(line), line)
    check('the run is red', out.status === 1, `status ${out.status}`)
    check('the summary counts it as a signal', /killed by a signal: 1/.test(out.output), out.output)
    check('the summary does not count it as a timeout', !/timed out:/.test(out.output), out.output)
}

// ---- a timeout --------------------------------------------------------------------------------
{
    const out = run(writePlan('hang', HANG, 100))
    const line = (out.output.match(/expected exit 0, got [^\n]*/) || [''])[0]
    check('a timeout is reported as a timeout', /got timeout after 100 ms/.test(line), line || out.output)
    check('a timeout never says null', !/null/.test(line), line)
    check('a timeout is not called a signal', !/signal/i.test(line), line)
    check('the run is red', out.status === 1, `status ${out.status}`)
    check('the summary counts it as a timeout', /timed out: 1/.test(out.output), out.output)
    check('the summary does not count it as a signal', !/killed by a signal:/.test(out.output), out.output)
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)