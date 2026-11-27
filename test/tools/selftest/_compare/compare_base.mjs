// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One-off comparison harness (not tracked): the claim it checks is that a run in which nothing
// timed out prints exactly what the same run printed before the second-attempt mechanism existed.
//
// Method. Both runners are the same commit except for one file: the base is
// `git show <BASE>:test/tools/run-tests.mjs` written to test/tools/run-tests-base.mjs (that
// directory, because each runner derives its repository root from its own location and imports its
// siblings relatively), the branch runner is the tracked test/tools/run-tests.mjs. Both are started
// with the same command line over the same plan (no_timeout.plan.toml, two passing cases), one
// after the other, and their stdout+stderr are compared line by line after replacing the values a
// run measures about itself with a placeholder. The line count and the blank-line count must also
// agree: a new summary line or an extra blank line moves them.
//
// What is masked, and why it may be: every masked value is a duration, an identity or a count of
// what the run itself had on disk -- a second run of the SAME runner differs in exactly these. What
// is not masked: every label, every case line's status and name, every sentence, `[SUMMARY]`'s
// fields and their order, and the blank lines.
import fs from 'fs'
import path from 'path'
import { execFileSync } from 'child_process'
import { fileURLToPath } from 'url'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, '..', '..', '..', '..')
// The commit this lane branched from. Named as a sha and not as a branch: a branch ref is another
// tree's commit the moment someone pushes.
const BASE = 'f6f5ea2a80a10c2fbf2f37f7d716b1d9a15fec93'
const baseRunner = path.join(repoRoot, 'test', 'tools', 'run-tests-base.mjs')
const branchRunner = path.join(repoRoot, 'test', 'tools', 'run-tests.mjs')
const quietPlan = path.join(here, 'no_timeout.plan.toml')

function run(runner, args) {
    try {
        const output = execFileSync(process.execPath, [runner, quietPlan, '--jobs', '1', ...args], {
            cwd: repoRoot,
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'pipe'],
            timeout: 240000,
        })
        return { output, status: 0 }
    } catch (error) {
        const stdout = error.stdout ? String(error.stdout) : ''
        const stderr = error.stderr ? String(error.stderr) : ''
        return { output: `${stdout}${stderr}`, status: error.status === undefined ? -1 : error.status }
    }
}

const maskedLine = (line) => {
    const status = /^\[(PASS|FAIL|SKIP|XFAIL)\] (\S+)(?:\s{2,}.*)?$/.exec(line)
    if (status) return `[${status[1]}] ${status[2]} <metric>`
    for (const [pattern, placeholder] of [
        [/^\[provenance\]/, '[provenance] <identity of this run>'],
        [/^\[RETAIN\]/, '[RETAIN] <what retention kept>'],
        [/^\[SLOT\]/, '[SLOT] <slot readings>'],
        [/^\[CPU\]/, '[CPU] <cpu readings>'],
        [/^\[SPLIT\]/, '[SPLIT] <phase readings>'],
        [/^\[INFO\] wall: /, '[INFO] wall: <duration>'],
    ]) {
        if (pattern.test(line)) return placeholder
    }
    return line
}

const strip = (text) => text.replace(/\x1b\[[0-9;]*m/g, '')

let failure = null
if (!fs.existsSync(quietPlan)) failure = `no plan at ${quietPlan}`
let baseText = ''
try {
    baseText = execFileSync('git', ['-C', repoRoot, 'show', `${BASE}:test/tools/run-tests.mjs`], {
        encoding: 'utf8',
    })
} catch (error) {
    failure = `git show ${BASE}:test/tools/run-tests.mjs failed: ${error.message}`
}

if (failure === null) {
    fs.writeFileSync(baseRunner, baseText)
    try {
        const before = run(baseRunner, process.env.COMPARE_BASE_ARGS ? process.env.COMPARE_BASE_ARGS.split(' ') : [])
        const after = run(branchRunner, [])
        const beforeLines = strip(before.output).split('\n').map(maskedLine)
        const afterLines = strip(after.output).split('\n').map(maskedLine)
        const blankBefore = beforeLines.filter((line) => line.trim() === '').length
        const blankAfter = afterLines.filter((line) => line.trim() === '').length
        const differences = []
        for (let index = 0; index < Math.max(beforeLines.length, afterLines.length); index++) {
            if (beforeLines[index] !== afterLines[index]) {
                differences.push(`${index + 1}: base ${JSON.stringify(beforeLines[index])} | branch ${JSON.stringify(afterLines[index])}`)
            }
        }
        console.log(`base exit ${before.status}, branch exit ${after.status}`)
        console.log(`lines: base ${beforeLines.length}, branch ${afterLines.length}; blank lines: base ${blankBefore}, branch ${blankAfter}`)
        console.log(`case lines before [SUMMARY]: ${/^\[SUMMARY\]/m.test(strip(after.output)) ? 'summary present' : 'NO SUMMARY'}`)
        if (differences.length > 0) {
            console.log(`differences (${differences.length}):`)
            for (const line of differences.slice(0, 40)) console.log(`  ${line}`)
        }
        if (before.status !== 0 || after.status !== 0) {
            console.log('NOT COMPARABLE: a run did not exit 0')
        } else if (differences.length > 0) {
            console.log('DIFFERS')
        } else {
            console.log('BYTE-IDENTICAL')
            console.log(`line count ${afterLines.length}, blank lines ${blankAfter}`)
        }
    } finally {
        fs.rmSync(baseRunner, { force: true })
    }
} else {
    console.log(`NOT COMPARABLE: ${failure}`)
}