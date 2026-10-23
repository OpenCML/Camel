// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/run.mjs (H143): the payload a `verify_script_path` receives must
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
    'cpu_budget_ms',
]) {
    assert.ok(Object.hasOwn(input, key), `payload lacks ${key}`)
}
assert.ok(Array.isArray(input.diagnostics))
assert.equal(input.benchmark, null)
// This plan declares no CPU budget, so the two cost fields are present and null: a script can tell
// "this test is judged on its own CPU time" from the value, not from the key's absence.
assert.equal(input.cpu_ms, null)
assert.equal(input.cpu_budget_ms, null)