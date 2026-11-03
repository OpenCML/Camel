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
// compiled. Such a build says so once in its log (-v), and that line is what is asked for here,
// so the answer is the build's own and does not depend on whether the JIT works.
// Exit 0 where the JIT generates code, 1 with the reason elsewhere.
//
// `--absent` asks the opposite question, for the tests of what such a build says: it exits 0
// where the build has no code generator and 1 where it has one, so one precondition serves both
// `skip_unless = ["node", "test/tools/have-native-jit.mjs"]` (results under std::jit, asserting a
// compiled line) and `skip_unless = ["node", "test/tools/have-native-jit.mjs", "--absent"]` (the
// warning a build without a code generator prints).

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const proc = spawnSync(camel, ['-v', path.join(ROOT, 'hello.cml'), 'std::jit'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 60000,
})
const log = `${proc.stdout || ''}${proc.stderr || ''}`
const absent = process.argv.slice(2).includes('--absent')
const noCodeGenerator = log.includes('no native code generator')
if (noCodeGenerator) {
    console.log('this build of camel has no JIT (x86-64 only, ENABLE_FASTVM_JIT): std::jit runs the bytecode interpreter')
}
process.exit(noCodeGenerator === absent ? 0 : 1)
