#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the M6 combination tests (`skip_unless = ["node",
// "test/tools/have-transforms-library.mjs"]`).
//
// M6 is "the three compositions have numbers", and the numbers are already written down
// (docs/design/program-transforms/m6-combination-numbers.cn.md). The code they are compared
// against does not exist until M4/M5 land, so until then these tests must be SKIPPED, not red: a
// red that says "not written yet" is noise, and a test that is red from the day it is added stops
// being read long before it can catch anything.
//
// What is asked here is whether the library-level entry points exist: `grad`, `jvp`, `vjp` from
// `autodiff` and `vmap` from `transforms`. The probe program
// (`test/cases/transforms/compositions/probe_transforms.cml`) names them and nothing else, so it
// compiles exactly when they do -- no file-existence guess, and no risk of the probe going stale
// while the numbers move.
//
// Exit 0 where they exist, 1 with the reason elsewhere.

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const probe = path.join(ROOT, 'test', 'cases', 'transforms', 'compositions', 'probe_transforms.cml')
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
console.log(`the library-level transforms are not there yet (M4/M5): ${log}`)
process.exit(1)