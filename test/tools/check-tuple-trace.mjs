#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The collector's trace against a value and a type that disagree about how many members there are.
//
// `Tuple::updateRefs` walks a tuple value's slots and reads the type's code array with the same
// index. When the value has more members than the type declares, that index leaves the array the
// type owns: a garbage code that happens to name a traced kind crashed in `typeAt`, and one that
// does not was skipped in silence and its reference was never relocated -- the stale pointer the
// poison check reports somewhere else, in a frame that never saw the tuple.
//
// So the walk compares the two counts first (author, 2026-11-02 05:30: the collector trusts a
// value's static type, so a disagreement is a memory-safety bug and not a diagnostic question), and
// it stops rather than reading on (06:30). This case reads both halves of that:
//
//   agree    -- `std::gc::tuple_members_agree` traces a value of two members with a type of two. It
//               must not fire, the walk runs, and the program goes on to print its reading.
//   mismatch -- `std::gc::tuple_members` traces a value of three members with a type of two. It must
//               stop the process with both counts in the message. A guard that reports this and then
//               reads anyway is worse than no guard: it turns "I know this is wrong" into a read that
//               may be taken as data, which is why "did not return" is the second assertion and not
//               only "said something".
//
// Usage: node test/tools/check-tuple-trace.mjs <case.cml>

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const program = process.argv[2]

if (!program) {
    console.log('check-tuple-trace: no case given')
    process.exit(1)
}

function environment() {
    const env = { ...process.env, CAMEL_HOME: home, CAMEL_ERROR_FORMAT: 'text' }
    // The test runner tells its children where a newer `libstdc++` lives through this variable
    // rather than through `LD_LIBRARY_PATH`; joining it here is what `check-vjp-ruleset.mjs` does
    // for the same reason, and without it the binary does not start at all.
    const dirs = (process.env.CAMEL_TEST_EXTRA_LIB_DIRS || '').split(':').filter(Boolean)
    if (dirs.length > 0 && process.platform !== 'win32') {
        env.LD_LIBRARY_PATH = [...dirs, env.LD_LIBRARY_PATH].filter(Boolean).join(':')
    }
    return env
}

function run(pass) {
    const proc = spawnSync(camel, [program, pass], {
        cwd: ROOT,
        encoding: 'utf8',
        env: environment(),
        timeout: 120000,
    })
    return {
        status: proc.status,
        signal: proc.signal,
        out: `${proc.stdout || ''}`.replace(/\r/g, ''),
        text: `${proc.stdout || ''}${proc.stderr || ''}`.replace(/\r/g, ''),
    }
}

const said = []
const failures = []
const agree = run('std::gc::tuple_members_agree')
const mismatch = run('std::gc::tuple_members')

// The control: two members and a type of two is not a disagreement, so nothing fires.
said.push(`agree: two members, a type of two -- exit ${agree.status}`)
if (agree.status !== 0) {
    failures.push(`the agreeing trace did not run (exit ${agree.status})`)
}
if (!agree.out.includes('"kind":"gc.tuple_members"')) {
    failures.push('the agreeing trace printed no reading of its own')
}
if (agree.text.includes('memory-safety bug')) {
    failures.push('the guard fired on a value that agrees with its type')
}

// The guard: three members against a type of two must be said, and must stop the process.
const died = mismatch.status !== 0 || mismatch.signal !== null
said.push(
    `mismatch: three members, a type of two -- ` +
        (mismatch.signal !== null ? `signal ${mismatch.signal}` : `exit ${mismatch.status}`)
)
if (!died) {
    failures.push('the guard let a value with more members than its type go on')
}
for (const fragment of ['has 3 member(s) but its type declares 2', 'memory-safety bug']) {
    if (!mismatch.text.includes(fragment)) {
        failures.push(`the guard's message does not say \`${fragment}\``)
    }
}
if (mismatch.out.includes('"kind":"gc.tuple_members"')) {
    failures.push('the pass returned after the guard, so the disagreement was reported and then read')
}

for (const line of said) {
    console.log(line)
}
if (failures.length > 0) {
    for (const failure of failures) {
        console.log(`check-tuple-trace: ${failure}`)
    }
    process.exit(1)
}
console.log('check-tuple-trace: the guard is silent on agreement and stops on a disagreement')