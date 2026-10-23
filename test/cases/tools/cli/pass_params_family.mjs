#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A pass's `with` parameters are checked by the same machinery as a function's parameters
 * (design principles 4.4, 9.3): the same diagnostic codes and the same message text, located at
 * the call — the pipeline line for a pass, the source line for a function. If a pass grew its
 * own checker, the two messages would drift apart; this test compares them.
 *
 * Usage: node pass_params_family.mjs
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
const CAMEL = path.join(HOME, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const SCRATCH =
    process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-pass-family-'))
const PLAIN = path.join(REPO_ROOT, 'test', 'cases', 'opt', 'pass_params', 'plain.cml')

fs.mkdirSync(SCRATCH, { recursive: true })

function program(name, body) {
    const file = path.join(SCRATCH, name)
    fs.writeFileSync(file, `module family\n\n${body}\n`)
    return file
}

const camel = (args) =>
    spawnSync(CAMEL, args, {
        env: { ...process.env, CAMEL_HOME: HOME },
        encoding: 'utf8',
        cwd: SCRATCH,
        timeout: 60000,
    })

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

function output(run) {
    return `${run.stdout}${run.stderr}`
}

/// Runs the same mistake twice — once in a function call, once in a pass call — and asserts the
/// code and the message template agree, with only the callee and the parameter name differing.
function compare(label, callProgram, passArgs, code, functionText, passText) {
    const call = camel(['check', callProgram])
    const pipeline = camel([PLAIN, ...passArgs])
    const callOut = output(call)
    const passOut = output(pipeline)
    check(`${label}: the function call is refused`, call.status === 1, callOut)
    check(`${label}: the pass call is refused`, pipeline.status === 1, passOut)
    check(
        `${label}: both report ${code}`,
        callOut.includes(code) && passOut.includes(code),
        `call: ${callOut} / pass: ${passOut}`
    )
    check(`${label}: the function message`, callOut.includes(functionText), callOut)
    check(`${label}: the pass message`, passOut.includes(passText), passOut)
}

compare(
    'unknown parameter',
    program(
        'unknown.cml',
        'func f(a: int, b: int = 0): int {\n    return a + b\n}\n\n' +
            'func main(): int sync {\n    return f(1, z: 2)\n}'
    ),
    ['std::opt z=2'],
    'ParamArgumentUnknown',
    "'z' is not a parameter of 'f'",
    "'z' is not a parameter of 'std::opt'"
)

compare(
    'duplicate parameter',
    program(
        'duplicate.cml',
        'func f(a: int, b: int = 0): int {\n    return a + b\n}\n\n' +
            'func main(): int sync {\n    return f(1, a: 2)\n}'
    ),
    ['std::opt safe_fp=true safe_fp=false'],
    'ParamArgumentDuplicate',
    "The parameter 'a' of 'f' is given twice.",
    "The parameter 'safe_fp' of 'std::opt' is given twice."
)

compare(
    'missing parameter',
    program(
        'missing.cml',
        'func g(a: int, b: int): int {\n    return a + b\n}\n\n' +
            'func main(): int sync {\n    return g(a: 1)\n}'
    ),
    ['test::params n=1'],
    'ParamArgumentMissing',
    "The call gives no value for the parameter 'b' of 'g', and it has no default.",
    "The call gives no value for the parameter 'flag' of 'test::params', and it has no default."
)

console.log(`pass parameters match calls: ${failed ? 'FAIL' : 'PASS'}`)
process.exit(failed ? 1 : 0)