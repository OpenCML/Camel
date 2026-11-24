#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the tests whose verdict is a figure of a child's CPU time
// (`skip_unless = ["node", "test/tools/have-child-cpu.mjs"]`).
//
// The reading is `/proc/self/stat` (fields 16 and 17, `cutime` + `cstime`), the one Node can make of
// a child and the one `test/tools/child-cpu.mjs` takes: the runner's timeout margin and a test's
// `cpu_budget_ms` are both judged on it. A host without that file -- Windows, and macOS -- has no
// CPU figure for any test, and the runner says so once and skips the judgement
// (`[WARN] no /proc/self/stat here: a test's timeout margin is skipped on this host`). An entry
// whose subject *is* that judgement therefore has nothing to judge there: an assertion about a
// margin, or a self-test that asserts the margin's own fixtures go red, would read as a failure of
// the entry while the fact is the host's. It declares the precondition and is skipped there.
//
// The test is asked the same question the wrapper asks (the file exists and its two fields parse),
// so a host that passes here cannot fail the entry for a reading the wrapper then refuses.
// Exit 0 where a child's CPU time can be read, 1 with the reason elsewhere.

import fs from 'node:fs'

// The fields after the last ')' are the fixed ones: `cutime` (field 16) is index 13 there and
// `cstime` (field 17) is index 14 (test/tools/child-cpu.mjs).
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
    return { seconds: (cutime + cstime) / 100, reason: null }
}

const reading = childrenCpuSeconds()
if (reading.seconds === null) {
    console.log(
        `${reading.reason}: this host cannot report a child's CPU time, so a margin or a ` +
            `cpu_budget_ms judged on it is skipped (the wall clock measures the neighbours)`
    )
    process.exit(1)
}
console.log(`/proc/self/stat is readable: a child's CPU time can be judged on this host`)
process.exit(0)