#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the tests that assert something only a build with the CUDA back end can do
// (`skip_unless = ["node", "test/tools/have-cuda-build.mjs"]`).
//
// `modules/tiles/CMakeLists.txt` defines CAMEL_TILES_CUDA only where `find_package(CUDAToolkit)`
// succeeds; elsewhere it prints "tiles: source inspection only (CUDA Toolkit not found)" and the
// module keeps its result behaviour -- programs still give the same answers, the host runs them --
// but there is no back end to construct. Nothing probes a driver there: `available()` is `false`
// without asking one, and `deviceInfo()` refuses before any backend is built. So every test of
// results still runs in such a build; what cannot hold is a line of the log a probe writes.
//
// The build says which one it is in the explanation of the startup check of `--devices`: a build
// without the back end answers "this build of Camel has no CUDA support", a build with it names
// the machine or the driver ("this machine has no CUDA device", or the driver's own sentence).
// The probe asks through that explanation, so the answer is the build's own and does not depend
// on a device or a driver being present here.
// Exit 0 where the build has the CUDA back end, 1 with the reason elsewhere.

import os from 'node:os'
import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')

const NO_BACKEND = 'this build of Camel has no CUDA support'

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-cuda-build-'))
const source = path.join(dir, 'probe.cml')
// The startup check of `--devices` belongs to the module that owns devices, so the probe imports
// tiles; it asks for no device operation, exactly like the case of the startup tests.
fs.writeFileSync(
    source,
    'module probe_cuda_build\nimport { available } from tiles\nfunc main(): int sync {\n    println(available())\n    return 0\n}\n',
)
const proc = spawnSync(camel, ['--devices', '0', source, 'std::nvm'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 120000,
})
fs.rmSync(dir, { recursive: true, force: true })
if (proc.error) {
    console.log(`cannot run ${camel}: ${proc.error.message}`)
    process.exit(1)
}
const output = `${proc.stdout || ''}${proc.stderr || ''}`
if (output.includes(NO_BACKEND)) {
    console.log(
        'this build of camel has no CUDA back end (tiles was built without the CUDA Toolkit headers): ' +
            'nothing probes a driver, so a test of a probe line cannot hold',
    )
    process.exit(1)
}
process.exit(0)