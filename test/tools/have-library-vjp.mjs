#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the vjp comparison against an analytic gradient (`skip_unless = ["node",
// "test/tools/have-library-vjp.mjs"]` in test/plans/feat/compile/transforms.plan.toml).
//
// The case runs the library's own gradient -- `vjp = transpose . split . reinterp<jvp>`, written in
// stdlib/vjp.cml -- on `f(x, y) = x*x*y` and prints what it answered beside the derivative worked
// out on paper (12 and 9). Until the composition can be built there is nothing to compare, and a
// test that is red because the library is not assembled yet is noise: it stops being read long
// before it can catch anything.
//
// The question is asked by compiling and running the probe program
// (`test/cases/compile/transforms/probe_library_vjp.cml`), not by guessing from file names: the
// probe builds `vjp(f)` and calls it, so it succeeds exactly when the three steps meet -- no
// `existsSync` of a source file, which says a file is there and not that the shapes agree, and no
// expected number, which would make the gate a second, silently weaker copy of the case.
//
// Today it fails the way the design records: `split` stands in with a lifted function
// `(float, float) => (float, float)` and `transpose` takes a function of one parameter, so the call
// inside stdlib/vjp.cml is refused where the library composes itself. That refusal, verbatim, is
// the reason this gate prints.
//
// Exit 0 where the composition can be built, 1 with the reason elsewhere.

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const probe = path.join(ROOT, 'test', 'cases', 'compile', 'transforms', 'probe_library_vjp.cml')
const proc = spawnSync(camel, [probe, 'std::nvm'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 60000,
})
if (proc.status === 0) {
    process.exit(0)
}
const log = `${proc.stdout || ''}${proc.stderr || ''}`.trim().split('\n').slice(0, 4).join(' | ')
console.log(
    'the library `vjp` cannot be built yet (M4): `stdlib/vjp.cml` writes ' +
        '`transpose(split(reinterp<jvp>(f)))`, and `split` hands `transpose` a lifted function ' +
        'where `transpose` takes one of one parameter, so the composition is refused where it is ' +
        `composed; the analytic comparison starts running when the two shapes meet: ${log}`,
)
process.exit(1)