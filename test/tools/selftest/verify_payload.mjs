// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/run.mjs: the payload a `verify_script_path` receives must
// carry every field docs/test-plans.md documents. `case_path` used to be assembled from a result
// that did not have the resolved case path yet, and `JSON.stringify` dropped the key, so this
// assertion fails with `undefined` instead of a path.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const repo = input.repo_root

assert.equal(input.case_path, path.join(repo, 'test/tools/selftest/backend_echo.mjs'))
assert.equal(input.plan_path, path.join(repo, 'test/tools/selftest/verify_payload.plan.toml'))
assert.equal(input.test_root, path.join(repo, 'test'))
assert.equal(input.test_name, 'payload.case_path')
assert.ok(path.isAbsolute(input.case_path), `case_path is not absolute: ${input.case_path}`)
assert.ok(input.test_tmp.startsWith(input.repo_root), `test_tmp is outside the repository: ${input.test_tmp}`)
assert.ok(path.isAbsolute(input.test_tmp), `test_tmp is not absolute: ${input.test_tmp}`)
assert.ok(input.test_tmp_relative, 'test_tmp_relative is empty')
assert.equal(input.exit_code, 0)
assert.equal(input.normalized.stdout, '42\n')
assert.ok(input.command.includes(input.case_path), `command lacks case_path: ${input.command}`)

// The fields a script may read even when their value is null or empty must be present, not
// dropped: the key's absence and a null value are the same thing to `input.signal` but not to a
// script that checks the payload.
for (const key of [
    'signal',
    'timed_out',
    'error_message',
    'wall_ms',
    'stdout',
    'stderr',
    'output',
    'diagnostics',
    'benchmark',
    'cpu_ms',
    'cpu_reading',
    'cpu_budget_ms',
    'cpu_limit_ms',
    'cpu_margin_x',
    'cpu_margin_basis',
]) {
    assert.ok(Object.hasOwn(input, key), `payload lacks ${key}`)
}
assert.ok(Array.isArray(input.diagnostics))
assert.equal(input.benchmark, null)
// This plan declares no CPU budget, so the budget is null and the line is the timeout margin's own:
// the runner records the reading, the line it was read against and the room under it for every
// test, so a script can tell which rule judged this test by `cpu_margin_basis`, not by a missing
// key. `cpu_ms` is the host's to give: a host without `/proc/self/stat` reports no figure at all,
// and then the ratio is null while the line is still recorded.
assert.equal(input.cpu_budget_ms, null)
// A null CPU figure always travels with the sentence saying why (the runner's `cpuReadingRecord`):
// a figure and "no figure on this platform" must never read the same, and neither may be `0`.
assert.equal(typeof input.cpu_reading, 'string')
assert.ok(input.cpu_reading.length > 0, 'cpu_reading is empty')
if (input.cpu_ms === null) assert.ok(input.cpu_reading.startsWith('absent:'), input.cpu_reading)
assert.equal(input.cpu_margin_basis, 'timeout_margin')
// The line and the ratio are one statement, and the payload has to carry both: the ratio is the
// line divided by the reading, so a script that reads either can check the other. The line itself
// is the test's `timeout_ms / 4` here (this plan declares no timeout, so the runner's 10 s default),
// and it is not asserted as a literal because `CAMEL_TEST_TIMEOUT_SCALE` moves it deliberately.
if (input.cpu_ms !== null && input.cpu_ms > 0) {
    assert.equal(input.cpu_margin_x, Math.round((input.cpu_limit_ms / input.cpu_ms) * 1000) / 1000)
} else {
    assert.equal(input.cpu_margin_x, null)
}