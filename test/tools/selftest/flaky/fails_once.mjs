// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A fixture case of the runner's self-test: it fails the first time it is started in a run, and
// passes when the runner starts it again for its second attempt.
//
// The memory has to outlive one attempt and not the next, so it is a marker file named after the
// *run*: the case climbs its own process tree to the `run-tests.mjs` that started it, and that pid is
// the same for both attempts (each attempt gets a fresh `bash` between it and the case, so
// `process.ppid` differs -- measured here: a marker keyed on it left one file per attempt and the
// second attempt failed like the first). The first attempt writes the file and exits 1; the second
// finds it, removes it and exits 0 printing `second attempt`. Nothing is left behind, and a later
// run of the same plan starts from the failing state again.
//
// `process.exitCode` rather than `process.exit`, for the reason in the other fixtures here: a
// process that exits immediately after writing to a piped stdout can lose both the text and the code.
import fs from 'fs'
import os from 'os'
import path from 'path'

function runnerAncestor() {
    let pid = process.ppid
    for (let hop = 0; hop < 8 && pid > 1; hop++) {
        try {
            if (fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8').includes('run-tests.mjs')) return pid
            const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8')
            // `pid (comm) state ppid ...` -- comm may hold spaces, so read past the last ')'.
            const fields = stat.slice(stat.lastIndexOf(')') + 2).trim().split(/\s+/)
            pid = Number(fields[1])
        } catch {
            break
        }
    }
    return process.ppid
}

const marker = path.join(os.tmpdir(), `camel-selftest-flaky-${runnerAncestor()}`)
if (fs.existsSync(marker)) {
    fs.rmSync(marker, { force: true })
    console.log('second attempt')
} else {
    fs.writeFileSync(marker, 'first attempt')
    console.log('first attempt')
    process.exitCode = 1
}