// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// An entry that runs over several back ends must put `${backend}` in its command.
//
// `backends` expands an entry into one test per back end, and `${backend}` in `args` is what makes
// the copies differ. An entry that writes its own `args` replaces the plan's default, so it can
// leave the variable out: it then runs once per back end, every copy the same command with no back
// end pass, and the plan stays green without ever running the case on most of them. The loader
// refuses such an entry, naming the plan file, the entry and the back ends.
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { loadPlanTests } from './run-tests.mjs'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const WORK = path.join(ROOT, 'test', 'results', 'backend-arg-selftest')
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

function plan(entryArgs) {
    return [
        'id = "self.backend_arg"',
        '',
        '[defaults]',
        'kind = "behavior"',
        'args = ["${case}", "${backend}"]',
        'backends = ["nvm", "fvm"]',
        'expect_exit = 0',
        '',
        '[[tests]]',
        'name = "entry"',
        'case = "case.cml"',
        ...(entryArgs === null ? [] : [`args = ${entryArgs}`]),
        '',
    ].join('\n')
}

function load(name, text) {
    const file = path.join(WORK, `${name}.plan.toml`)
    fs.writeFileSync(file, text)
    try {
        return { tests: loadPlanTests(file).tests, file }
    } catch (error) {
        return { error: String(error.message), file }
    }
}

fs.mkdirSync(WORK, { recursive: true })

const bad = load('without', plan('["${case}"]'))
check('an entry over two back ends without ${backend} is refused', bad.error !== undefined, 'it loaded')
check(
    'the refusal names the plan file, the entry and the back ends',
    bad.error !== undefined &&
        bad.error.includes('without.plan.toml') && bad.error.includes('entry') &&
        bad.error.includes('nvm') && bad.error.includes('fvm') && bad.error.includes('${backend}'),
    bad.error,
)

const good = load('with', plan('["${case}", "${backend}"]'))
check('the same entry with ${backend} loads', good.error === undefined, good.error)
check('it loads one test per back end', good.tests !== undefined && good.tests.length === 2)

const inherited = load('inherited', plan(null))
check('an entry that keeps the default args loads', inherited.error === undefined, inherited.error)

console.log(`backend-arg: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 ? 0 : 1)
