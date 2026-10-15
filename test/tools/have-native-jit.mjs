#!/usr/bin/env node
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
if (log.includes('no native code generator')) {
    console.log('this build of camel has no JIT (x86-64 only, ENABLE_FASTVM_JIT): std::jit runs the bytecode interpreter')
    process.exit(1)
}
process.exit(0)
