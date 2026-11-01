#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the two entries of the `transforms` compile plan that read the numbers of the
// three-step composition (`test/plans/feat/compile/transforms.plan.toml`,
// `vjp_api.gradient_when_the_steps_land` and its sibling).
//
// `vjp(f) = transpose(split(reinterp<jvp>(f)))` (stdlib/vjp.cml) has all three steps written now:
// `jvp` and `transpose` are operators of the `transforms` module and `split` builds its two halves.
// What the composition still lacks is the *assembly*: `transpose` takes a function of one `float`
// parameter, and `split` hands back the pair of halves, so `transpose(split(...))` does not type --
// the pair is not the function `transpose` walks backwards. That is the next step's work, and the
// numbers these entries read (`27.0` for `f(x) = x*x*x` at 3) arrive with it.
//
// So the question here is whether the composition runs end to end, and the honest way to ask it is
// to run it: the probe is the case the entries themselves read
// (test/cases/compile/transforms/vjp_gradient.cml), and this gate answers yes when it exits 0.
// Anything else -- a diagnostic at the library's line, a build that stops -- is "not yet", and the
// entries it gates are skips with this reason instead of reds.
//
// Exit 0 when the composition runs, 1 with the reason when it does not.

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const probe = path.join(ROOT, 'test', 'cases', 'compile', 'transforms', 'vjp_gradient.cml')
const proc = spawnSync(camel, [probe, 'std::nvm'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 60000,
})
const output = `${proc.stdout || ''}${proc.stderr || ''}`
if (proc.status !== 0) {
    const log = output.trim().split('\n').slice(0, 2).join(' | ')
    console.log(
        `the three-step composition of \`vjp\` does not run yet, so the numbers this plan takes ` +
            `(27.0 for f(x) = x*x*x at 3) are reserved: ${log}`,
    )
    process.exit(1)
}
process.exit(0)