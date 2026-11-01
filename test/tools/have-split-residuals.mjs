#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the three entries of the `split` plan that read the numbers of `g(x) = x*x`
// (test/plans/feat/transforms/split.plan.toml, `split_forward_half`, `split_backward_tangent`,
// `split_linear_in_dx`).
//
// Those entries read what the two halves hand on: the forward half gives `y = 9` **and** `r0 = 3`,
// and the backward half turns that residual into the tangent 6. The plumbing of `split`
// (modules/transforms/split.cpp) builds the two graphs and cuts the exit pair, but it carries no
// residual: the backward half of a `jvp` lift of `x*x` reads the value of `x`, and the body writes
// its tangent as a call to the rule's function, so the readings are not reachable yet. They are
// reserved, not removed, and they open by themselves when the transform that rewrites those bodies
// lands -- which is the day this gate starts answering yes.
//
// So the question here is *not* whether `split` exists (`test/tools/have-split.mjs` is), and not
// whether the numbers are right (the entries themselves are): it is whether the forward half hands
// any residual on at all. `split` prints that count in its own reading
// (`REINTERP split function=<name> ... residuals=<n>`), and the probe program
// (`test/cases/compile/transforms/probe_split_residuals.cml`) asks for it on the function of the
// author's example. A build that refuses the function, or one that hands on the empty tuple, exits 1
// with the reason.
//
// Exit 0 when the forward half hands residuals on, 1 with the reason when it does not.

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const probe = path.join(ROOT, 'test', 'cases', 'compile', 'transforms', 'probe_split_residuals.cml')
const proc = spawnSync(camel, [probe, 'std::nvm'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 60000,
})
const output = `${proc.stdout || ''}${proc.stderr || ''}`
if (proc.status !== 0) {
    const log = output.trim().split('\n').slice(0, 3).join(' | ')
    console.log(
        `split does not hand residuals on yet, so the readings this plan takes (r0 = 3) are ` +
            `reserved: ${log}`,
    )
    process.exit(1)
}
// Two lines carry the count (`split` prints it once for the graph and once for the call); the
// largest is what the forward half handed on.
const counts = [...output.matchAll(/\bresiduals=(\d+)/g)].map((m) => Number(m[1]))
if (counts.length === 0) {
    console.log(
        'split printed no residual count, so whether it hands residuals on cannot be read here',
    )
    process.exit(1)
}
const handed = Math.max(...counts)
if (handed === 0) {
    console.log(
        'split hands the empty tuple on as residuals: the backward half of `x*x` still reads the ' +
            'value of x from outside the pair, so the readings this plan takes (r0 = 3) are reserved',
    )
    process.exit(1)
}
process.exit(0)