#!/usr/bin/env node
/**
 * `expect_stdout_matches` -- the regular-expression assertion over a test's standard output.
 *
 * The semantics, written down so a reader does not have to infer them: the pattern is matched
 * **line by line against the normalized stdout, and any line matching passes**. It is a search, like
 * `expect_stdout_contains`, not a judgement on every line -- so an unanchored `24\\.0` is "some line
 * contains 24.0" and `^24\\.0$` is "some line is exactly 24.0". Both are asserted below, because the
 * difference between them is the whole reason this field can be misread as stricter than it is.
 *
 * And the three ways a plan can be wrong about it are plan errors, not failing tests:
 * a pattern that does not compile (`"["`), an empty pattern (it matches everything and would be
 * green forever), and a second assertion on the same stdout. A run must refuse instead of starting a
 * test whose verdict means nothing.
 *
 * Red when: a pattern that does not match passes; a match reports itself as a plan error; a plan
 * error reports itself as a failing test; the diagnostics stop naming the pattern or the head of the
 * output; or a legal pattern is refused.
 *
 * Run: node stdout_matches.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'stdout-matches-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// The case every probe plan runs: it prints four lines, so a pattern can be aimed at one of them.
const CASE = path.join(SCRATCH, 'shape.mjs')
fs.writeFileSync(CASE, 'console.log("width 24.0 height 7")\nconsole.log("24.0")\nconsole.log("done")\n')

function writePlan(name, extraFields, args = ['${case}']) {
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    const text = [
        `id = "probe.${name}"`,
        'tier = "fast"',
        '',
        '[[tests]]',
        `name = "probe.${name}.entry"`,
        'kind = "behavior"',
        'tool = "node"',
        `case = "${CASE.replace(/\\/g, '/')}"`,
        `args = ${JSON.stringify(args)}`,
        'expect_exit = 0',
        ...extraFields,
        'timeout_ms = 30000',
        '',
    ].join('\n')
    fs.writeFileSync(planPath, text)
    return { planPath, text }
}

function runPlans(plans) {
    const result = spawnSync(process.execPath, [RUNNER, ...plans, '--jobs', '1', '--tier', 'fast'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH, CAMEL_TEST_SHOW_OUTPUT: '' },
    })
    return { status: result.status, output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}

// 1. A pattern that matches: unanchored is a substring of a line, anchored is a whole line.
for (const pattern of ['24\\.0', '^24\\.0$', '^width \\d+\\.\\d+ height \\d+$']) {
    const good = writePlan('match', [`expect_stdout_matches = ["${pattern.replace(/\\/g, '\\\\')}"]`])
    const out = runPlans([good.planPath])
    check(`a pattern matching a line passes: /${pattern}/`, out.status === 0, `status ${out.status}\n${out.output}`)
}

// 2. The anchor is the difference: `^24.0$` cannot match `width 24.0 height 7`.
{
    const anchored = writePlan('anchored', ['expect_stdout_matches = ["^width 24\\.0$"]'])
    const out = runPlans([anchored.planPath])
    check('an anchored pattern that matches no whole line is red', out.status !== 0, `status ${out.status}\n${out.output}`)
}

// 3. A legal pattern that matches nothing: red, and the message carries the pattern, the verdict
//    ("a legal regular expression") and the head of the output.
{
    const missing = writePlan('missing', ['expect_stdout_matches = ["^never printed$"]'])
    const out = runPlans([missing.planPath])
    check('a legal pattern that matches nothing is red', out.status === 1, `status ${out.status}`)
    check('the message quotes the pattern', out.output.includes('^never printed$'), out.output)
    check('the message says the pattern is a legal regular expression', /legal regular expression/.test(out.output), out.output)
    check('the diagnostics print the head of the output', out.output.includes('[stdout head]'), out.output)
    check('the head is the first lines, not the tail', out.output.includes('width 24.0 height 7'), out.output)
    check('the head stops at twelve lines', !/13 of/.test(out.output), out.output)
}

// 4. A pattern that does not compile is a plan error: exit 2, `[PLAN ERROR]`, no test started.
{
    const broken = writePlan('broken', ['expect_stdout_matches = ["["]'])
    const out = runPlans([broken.planPath])
    check('an uncompilable pattern makes the run refuse (exit 2)', out.status === 2, `status ${out.status}\n${out.output}`)
    check('it is reported as a plan error', /\[PLAN ERROR\]/.test(out.output), out.output)
    check('the plan error names plan:line', /broken\.plan\.toml:\d+/.test(out.output), out.output)
    check('the plan error quotes the pattern', out.output.includes('/[/'), out.output)
    check('no test ran', !/\[PASS\]|\[FAIL\]/.test(out.output), out.output)
}

// 5. An empty pattern is a plan error: it would match every line and be green forever.
{
    const empty = writePlan('empty', ['expect_stdout_matches = [""]'])
    const out = runPlans([empty.planPath])
    check('an empty pattern makes the run refuse (exit 2)', out.status === 2, `status ${out.status}\n${out.output}`)
    check('the empty-pattern message says why', /green forever/.test(out.output), out.output)
}

// 6. Two assertions on one stdout are refused, and the message names both.
for (const [field, value] of [
    ['expect_stdout_contains', '["done"]'],
    ['expect_stdout_exact', '"done\\\\n"'],
]) {
    const clash = writePlan('clash', [`expect_stdout_matches = ["done"]`, `${field} = ${value}`])
    const out = runPlans([clash.planPath])
    check(`expect_stdout_matches with ${field} is a plan error (exit 2)`, out.status === 2, `status ${out.status}\n${out.output}`)
    check(`the message names both fields (${field})`, out.output.includes(field) && out.output.includes('expect_stdout_matches'), out.output)
}

// 7. A list: every pattern must find a line, and one that does not is enough to fail.
{
    const two = writePlan('list', ['expect_stdout_matches = ["^width", "^done$"]'])
    const out = runPlans([two.planPath])
    check('a list of patterns that all match passes', out.status === 0, `status ${out.status}\n${out.output}`)

    const oneMisses = writePlan('list_miss', ['expect_stdout_matches = ["^width", "^never$"]'])
    const out2 = runPlans([oneMisses.planPath])
    check('one pattern of the list missing is red', out2.status === 1, `status ${out2.status}\n${out2.output}`)
    check('the failing list entry is the one quoted', out2.output.includes('^never$'), out2.output)
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)