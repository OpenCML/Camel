#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the C2 implementation comparison (`skip_unless = ["node",
// "test/tools/have-library-grad.mjs"]` in test/plans/perf/showcase_c2.plan.toml).
//
// The comparison runs the C2 training program twice -- once with the gradient the pass mechanism
// builds today, once with the library's own (`vjp = transpose . split . reinterp<jvp>`, M4) -- and
// asks whether the two runs end on the same loss. Until the library half exists there is nothing
// to compare, and a test that is red because the other implementation is not written yet is noise:
// it stops being read long before it can catch anything.
//
// What is asked here is whether those entry points exist, and it is asked by compiling the probe
// program (`test/cases/modules/autodiff/probe_library_grad.cml`), not by guessing from file names:
// the program names `with_grad`, `grad` and `vjp` from `autodiff` and nothing else, so it compiles
// exactly when they do. Today it fails on `vjp`, which is the one the library does not have yet.
//
// Exit 0 where they exist, 1 with the reason elsewhere.

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const probe = path.join(ROOT, 'test', 'cases', 'modules', 'autodiff', 'probe_library_grad.cml')
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
    'the library-level gradient is not there yet (M4): `vjp` is only the decoration tag of a ' +
        'custom rule, so the C2 comparison has no second implementation to run against the ' +
        `with_grad one; it starts running when the transform lands: ${log}`,
)
process.exit(1)