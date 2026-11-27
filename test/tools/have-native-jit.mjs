#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the tests that assert that std::jit compiled a function to native code
// (`skip_unless = ["node", "test/tools/have-native-jit.mjs"]`).
//
// The JIT has one code generator, for x86-64. A build without it (ENABLE_FASTVM_JIT off: every
// other processor, e.g. arm64 macOS, and the no-JIT build of the CI) runs std::jit on the
// bytecode interpreter of std::fvm: programs give the same results, so every test of results
// under std::jit still runs there; what cannot hold is a line of the log that says a graph was
// compiled, and a run whose compared output carries the warning such a build prints once.
//
// The question is asked of the build itself, by executing a program and reading what it says about
// compiling it: a graph called more than once is compiled when there is a code generator
// (`compiled & cached graph ...` at -vv), and when there is not, the run says so once instead
// (`no native code generator`). Neither line is an answer: a probe that gets neither reading
// exits non-zero and the tests it guards are skipped, because a skipped test is a loss of coverage
// and worse is a green one that measured nothing.
//
// Exit 0 in the default mode where the build compiles a graph, 1 elsewhere, with the reading on
// stderr. `--absent` asks the opposite question, for the tests of what a build without a code
// generator says: it exits 0 where the build compiles nothing, 1 where it compiles.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const absent = process.argv.slice(2).includes('--absent')

// The probe program is written to a scratch directory: it is not a case, because a case is a thing
// a plan asserts an output for, and this one is a question put to the build.
const PROBE_PROGRAM = `func step(x: int) => int {
    return x + 1
}

func repeat(n: int, acc: int) => int {
    return if n == 0 then acc else repeat(n - 1, step(acc))
}

func main() => int sync {
    let a = repeat(50, 0)
    println(a)
    return 0
}
`

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-have-native-jit-'))
const program = path.join(scratch, 'have-native-jit.cml')
fs.writeFileSync(program, PROBE_PROGRAM)
process.on('exit', () => fs.rmSync(scratch, { recursive: true, force: true }))
const started = spawnSync(camel, ['-vv', program, 'std::jit'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 60000,
})
const log = `${started.stdout || ''}${started.stderr || ''}`

// Two readings, one answer, and the absence of both is not an answer.
const compiled = log.includes('compiled & cached graph')
const noCodeGenerator = log.includes('no native code generator')
const hasCodeGenerator = compiled && !noCodeGenerator ? true : noCodeGenerator && !compiled ? false : null

if (hasCodeGenerator === null) {
    const why = started.error
        ? `could not start '${camel}': ${started.error.message}`
        : `'${camel} -vv have-native-jit.cml std::jit' said neither 'compiled & cached graph' nor ` +
          `'no native code generator' (exit ${started.status})`
    console.error(`cannot tell whether this build of camel generates native code: ${why}`)
    process.exit(1)
}
console.error(
    hasCodeGenerator
        ? 'this build of camel generates native code with std::jit'
        : 'this build of camel has no JIT (x86-64 only, ENABLE_FASTVM_JIT): std::jit runs the bytecode interpreter',
)
process.exit(hasCodeGenerator !== absent ? 0 : 1)