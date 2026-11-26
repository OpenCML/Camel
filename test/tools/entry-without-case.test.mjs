// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// An entry with no `case` that fails is reported as that entry failing.
//
// A plan entry may run a tool with inline arguments and name no case file. The runner resolved the
// case path in several places of its failure handling, and `path.resolve(dir, undefined)` throws, so
// the first such entry to fail took the whole run down before `[SUMMARY]`: every verdict of the plan
// was lost behind one crash. This pins the other behaviour -- the entry is red, the plan's other
// entries keep their verdicts, the summary is printed, and the exit code is non-zero.
//
// The plan is written into `test/results/` (gitignored): a plan that is red on purpose must not be
// part of the tree a gate reads.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const WORK = path.join(ROOT, 'test', 'results', 'entry-without-case-selftest')
let checks = 0
let failed = 0

function check(name, ok, detail) {
    checks++
    if (ok) {
        console.log(`ok    ${name}`)
    } else {
        failed++
        console.log(`FAIL  ${name}${detail ? `: ${detail}` : ''}`)
    }
}

fs.mkdirSync(WORK, { recursive: true })
const planPath = path.join(WORK, 'inner.plan.toml')
fs.writeFileSync(
    planPath,
    [
        'id = "self.entry_without_case"',
        '',
        '[[tests]]',
        'name = "no_case_fails"',
        'kind = "behavior"',
        'tool = "node"',
        'args = ["-e", "process.exit(1)"]',
        'expect_exit = 0',
        'timeout_ms = 30000',
        '',
        '[[tests]]',
        'name = "no_case_passes"',
        'kind = "behavior"',
        'tool = "node"',
        'args = ["-e", "process.exit(0)"]',
        'expect_exit = 0',
        'timeout_ms = 30000',
        '',
    ].join('\n'),
)

const result = spawnSync(
    process.execPath,
    [path.join('test', 'tools', 'run-tests.mjs'), planPath, '--jobs', '1'],
    { cwd: ROOT, encoding: 'utf8', timeout: 180000 },
)
const out = `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '')

check('the run does not crash on the entry without a case', !/TypeError|ERR_INVALID_ARG_TYPE/.test(out), out.slice(-900))
check('the summary is printed', /\[SUMMARY\]/.test(out), out.slice(-900))
check('the failing entry is reported by name as failed', /\[FAIL\]\s+no_case_fails/.test(out), out.slice(-900))
check('the other entry keeps its verdict', /\[PASS\]\s+no_case_passes/.test(out), out.slice(-900))
check('the run exits non-zero', result.status === 1, `exit ${result.status}`)

console.log(`entry-without-case: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 ? 0 : 1)
