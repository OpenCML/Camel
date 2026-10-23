// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Run one command and report the CPU time (user + system) the kernel charged to it and to every
// process it waited for. This is the reading a plan's `cpu_budget_ms` is judged against: the
// process's own CPU time, not the wall clock, by the author's ruling of 2026-10-23 on the shared
// host (docs/handover/reviews/questions-2026-10-23.md, "declaration-cost-gate" (a)):
//
//   "改量 CPU 时间，不量墙钟。共享主机上的墙钟随负载漂移 ... 进程的 CPU 时间（用户态 + 内核态）基本
//    不随负载变"
//
// Why this is a process of its own instead of the runner measuring its child directly: the only
// reading Node can make of a child's CPU is the delta of `cutime` + `cstime` in /proc/self/stat
// (fields 16 and 17), the CPU of every child this process has reaped -- the reading
// test/tools/startup-cost.mjs, scripts/perf-gate.mjs and test/tools/para-speedup.mjs make of the
// one child they each run. The plan runner reaps every test's child from one process, so its own
// delta would charge one test's CPU to another. This wrapper reaps exactly one command, so its
// delta is that test's CPU alone: the reading stays exact while the whole layer runs in parallel.
//
// The kernel folds a reaped child's CPU into its parent's `cutime`/`cstime`, and the fold is
// recursive, so one reading covers the whole tree: for a `tool = "python"` test it is the driver
// process and every `camel` process the driver waited for.
//
//   node test/tools/child-cpu.mjs -- <command> [args...]
//
// The reading is one JSON line on file descriptor 3 -- `cpu_ms`, `exit_code`, `signal`, `error`,
// `reading` -- which the runner opens as a fourth pipe. The command's own standard output and
// standard error are the runner's pipes, untouched, so a test's output and its assertions are
// exactly what they were. A hand run has no fourth pipe: the same line then goes to standard error
// under the `child-cpu:` prefix, so the number is never lost.
//
// A platform that cannot read a child's CPU time (no /proc/self/stat) reports `cpu_ms: null` with
// the reason in `reading`; the runner fails a `cpu_budget_ms` test that has no reading, because a
// budget the runner cannot read is not a bound.
//
// The command's exit status is this process's status: a signal that killed the command is raised
// against this process too, so the test's record keeps the command's own `signal` and not this
// wrapper's.

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'

// /proc/<pid>/stat reports in USER_HZ ticks, 100 on every Linux architecture, the same number
// scripts/perf-gate.mjs and test/tools/startup-cost.mjs divide the same two fields by.
const CLOCK_TICKS = 100

/**
 * The CPU seconds of every child this process has reaped so far, or a reason it cannot be read.
 * `/proc/self/stat` puts the executable's name in parentheses in field 2, and that name may hold
 * spaces and parentheses of its own; the fields after the last ')' are the fixed ones, so `cutime`
 * (field 16) is index 13 there and `cstime` (field 17) is index 14.
 */
function childrenCpuSeconds() {
    let text
    try {
        text = fs.readFileSync('/proc/self/stat', 'utf8')
    } catch (error) {
        return { seconds: null, reason: `no /proc/self/stat (${error.code || error.message})` }
    }
    const fields = text.slice(text.lastIndexOf(')') + 2).split(' ')
    const cutime = Number(fields[13])
    const cstime = Number(fields[14])
    if (!Number.isFinite(cutime) || !Number.isFinite(cstime)) {
        return { seconds: null, reason: '/proc/self/stat has no cutime/cstime fields' }
    }
    return { seconds: (cutime + cstime) / CLOCK_TICKS, reason: null }
}

/** The reading, on file descriptor 3 when the runner opened it, on standard error otherwise. */
function writeReport(report) {
    const line = `${JSON.stringify(report)}\n`
    try {
        fs.writeSync(3, line)
    } catch {
        console.error(`child-cpu: ${line.trim()}`)
    }
}

const separator = process.argv.indexOf('--')
if (separator < 0 || separator + 1 >= process.argv.length) {
    console.error('child-cpu: usage: node test/tools/child-cpu.mjs -- <command> [args...]')
    process.exit(2)
}
const argv = process.argv.slice(separator + 1)
const before = childrenCpuSeconds()
// Standard streams are inherited: the command's output goes to the runner's own pipes unchanged.
// File descriptor 3 is not inherited but pointed at /dev/null, so nothing the command starts can
// write into the report pipe or keep its read end open after this wrapper exits.
const proc = spawnSync(argv[0], argv.slice(1), { stdio: ['inherit', 'inherit', 'inherit', 'ignore'] })
const after = childrenCpuSeconds()

const cpuMs = before.seconds === null || after.seconds === null
    ? null
    : (after.seconds - before.seconds) * 1000

writeReport({
    cpu_ms: cpuMs,
    exit_code: typeof proc.status === 'number' ? proc.status : null,
    signal: proc.signal ?? null,
    error: proc.error ? proc.error.message : null,
    reading: cpuMs === null ? after.reason || before.reason : null,
})
if (proc.error) {
    // The child never existed: the runner's log keeps the reason, and the exit status below is
    // this wrapper's own (127: nothing ran).
    console.error(`child-cpu: cannot run ${argv[0]}: ${proc.error.message}`)
}
if (proc.signal) {
    // Die of the same signal as the command, so the runner's record names the command's end.
    try {
        process.kill(process.pid, proc.signal)
    } catch {
        /* fall through to the status below */
    }
}
process.exit(typeof proc.status === 'number' ? proc.status : 127)