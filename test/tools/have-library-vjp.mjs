#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the vjp comparison against an analytic gradient (`skip_unless = ["node",
// "test/tools/have-library-vjp.mjs"]` in test/plans/feat/compile/transforms.plan.toml), and the one
// command that answers "is the assembly done?".
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
// `existsSync` of a source file, which says a file is there and not that the shapes agree.
//
// Two readings, in this order:
//   * the probe does not build: the gate exits 1 and prints the compiler's own words, so a skip
//     always says which shape is missing;
//   * it builds: the gate runs the comparison case and prints the numbers it answers, then exits 0.
//     A wrong answer is *not* hidden here -- it is left to the plan entry, which asserts 12 and 9
//     and goes red on them. A gate that swallowed a wrong number into a skip would be the silent
//     pass this whole arrangement exists to avoid.
//
// With `--missing` the question is the other one, for the entries that read a *refusal*: exit 0
// when the composition still does not build, 1 when it does. That is what retires the refusal case
// on the day the assembly lands, without anyone remembering to delete it.
//
// Today it fails the way the design records: `split` builds its two halves and hands back the pair,
// `jvp` lifts every parameter, and `transpose` still takes a function of a single parameter -- so
// `transpose(split(...))` has no call, and the second half, which takes the residuals first and the
// seeds after them, is the shape transpose has to learn to walk.

import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const probe = path.join(ROOT, 'test', 'cases', 'compile', 'transforms', 'probe_library_vjp.cml')
const comparison = path.join(ROOT, 'test', 'cases', 'compile', 'transforms', 'vjp_analytic_gradient.cml')
const missing = process.argv.includes('--missing')

function run(file) {
    const proc = spawnSync(camel, [file, 'std::nvm'], {
        cwd: ROOT,
        env: { ...process.env, CAMEL_HOME: home },
        encoding: 'utf8',
        timeout: 60000,
    })
    return { status: proc.status, text: `${proc.stdout || ''}${proc.stderr || ''}`.trim() }
}

const first = (result, lines) => result.text.split('\n').slice(0, lines).join(' | ')

const built = run(probe)
if (built.status !== 0) {
    if (missing) {
        process.exit(0)
    }
    console.log(
        'the library `vjp` cannot be built yet (M4): `stdlib/vjp.cml` writes ' +
            '`transpose(split(reinterp<jvp>(f)))`, `split` hands back its two halves -- the pair ' +
            '`((float) => (float, (float,)), ((float, float), float, float) => float)` -- and ' +
            '`transpose` takes a function of exactly one parameter, so the pair is not the function ' +
            'it walks backwards; what `transpose` has to learn is the half that takes the residuals ' +
            `first and the seeds after them. The comparison starts running when it does: ${first(built, 3)}`,
    )
    process.exit(1)
}
if (missing) {
    console.log('the library `vjp` builds now, so the refusal case has nothing to read: it is retired')
    process.exit(1)
}

// The composition builds. Read the comparison here too, so this one command answers with the
// numbers as well as with the build: `library df/dx = 12.0` and `library df/dy = 9.0`.
const numbers = run(comparison)
if (numbers.status !== 0) {
    console.log(`the library \`vjp\` builds but the comparison does not run: ${first(numbers, 3)}`)
    process.exit(1)
}
for (const line of numbers.text.split('\n')) {
    console.log(line)
}
const agree =
    numbers.text.includes('library  df/dx = 12.0') &&
    numbers.text.includes('analytic df/dx = 12.0') &&
    numbers.text.includes('library  df/dy = 9.0') &&
    numbers.text.includes('analytic df/dy = 9.0')
if (!agree) {
    console.log(
        'the composition builds, and what it answered is above: the plan entry vjp_api.analytic_gradient ' +
            'asserts 12 and 9 and is where a disagreement goes red',
    )
}
process.exit(0)