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
// probe builds `vjp(f)` and calls it, so it succeeds exactly when the three steps meet.
//
// The composition can stop in either of two layers, and they are different jobs, so the gate says
// which one it is -- in the order the walls fall:
//
//   1. the *type* layer. `transpose` has no type for what `split` hands back, so the composition has
//      no call at all and no graph is walked. The evidence is the compiler's own diagnostic
//      (`SemanticDiag`) at the line of `stdlib/vjp.cml` that composes it.
//   2. the *graph* layer. The module compiles, and the walk that has to run a function backwards
//      refuses a construct inside the half it was given -- today the half calls another graph and
//      `transpose` does not transpose a call. The evidence is the walk's own reading line
//      (`TRANSPOSE ... skip=...`) and the diagnostic raised at the `transpose` call.
//   3. neither: the composition builds, and the case runs. The gate then runs the comparison too
//      and prints what it answered, so one command gives the numbers as well as the build.
//
// Each answer prints what it was judged by, so a reader never has to take the sentence on trust,
// and the gate walks down a layer by itself as the layers are closed: nothing here names `Call` or
// a line number, so the day the walk takes a call the report moves to 3 without an edit.
//
// A wrong number is *not* hidden here -- it is left to the plan entry, which asserts 12 and 9 and
// goes red on them. A gate that swallowed a wrong number into a skip would be the silent pass this
// whole arrangement exists to avoid.
//
// With `--missing` the question is the other one, for the entry that reads a *refusal*: exit 0
// while the composition does not build, 1 when it does. That is what retires the refusal case on
// the day the assembly lands, without anyone remembering to delete it.

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
    return { status: proc.status, text: `${proc.stdout || ''}${proc.stderr || ''}`.replace(/\r/g, '') }
}

// Which layer refused the composition, read off the run's own words: the compiler says `SemanticDiag`
// when a call has no type, and a transform that declines a node prints its reading line
// (`TRANSPOSE ... skip=...`) before the diagnostic at the call site. Neither is a guess about what
// the code should be doing -- it is what the run said.
function wallOf(text) {
    const all = text.split('\n')
    const reading = all.find((line) => /^TRANSPOSE\s/.test(line.trimStart()))
    if (reading) {
        const raised = all.find((line) => /error\[RuntimeDiag::/.test(line))
        return { layer: 'graph', evidence: [reading, ...(raised ? [raised] : [])] }
    }
    const semantic = all.find((line) => /error\[SemanticDiag::/.test(line))
    if (semantic) {
        const at = all.find((line) => /^\s*-->\s/.test(line) && /stdlib\/vjp\.cml/.test(line))
        return { layer: 'type', evidence: [semantic, ...(at ? [at.trim()] : [])] }
    }
    return { layer: 'other', evidence: all.slice(0, 4) }
}

function say(lines) {
    for (const line of lines) console.log(line)
}

const built = run(probe)
if (built.status !== 0) {
    if (missing) {
        process.exit(0)
    }
    const { layer, evidence } = wallOf(built.text)
    if (layer === 'type') {
        say([
            'the library `vjp` stops at the type layer (M4): `transpose` does not accept the shape `split`',
            'hands back, so `transpose(split(reinterp<jvp>(f)))` has no call at all and no graph is walked.',
            '  evidence (the compiler\'s diagnostic, and the line of the library it points at)',
            ...evidence.map((line) => `    ${line.trim()}`),
            '  what is missing: a type for `transpose` over the second half of a split -- the known values,',
            '  then the linear parameters, then the cotangent.',
        ])
    } else if (layer === 'graph') {
        say([
            'the library `vjp` stops in the graph (M4): the type layer takes the shape `split` hands back and',
            'the module compiles, but the walk that runs a function backwards refuses a construct inside it.',
            '  evidence (the walk\'s own reading, then the diagnostic raised at the `transpose` call)',
            ...evidence.map((line) => `    ${line.trim()}`),
            '  what is missing: transposing that construct -- giving it a transpose of its own and replacing',
            '  the site it is written at, which is the step named to the M3 lane.',
        ])
    } else {
        say([
            'the library `vjp` cannot be built yet (M4), and the wall is neither of the two this gate reads:',
            ...evidence.map((line) => `    ${line.trim()}`),
        ])
    }
    process.exit(1)
}
if (missing) {
    say([
        'the library `vjp` builds now, so the refusal case has nothing to read:',
        '  evidence: the probe program compiled and ran, exit 0',
    ])
    process.exit(1)
}

// The composition builds. Read the comparison here too, so this one command answers with the
// numbers as well as with the build: `library df/dx = 12.0` and `library df/dy = 9.0`.
const numbers = run(comparison)
if (numbers.status !== 0) {
    say([
        'the library `vjp` builds but the comparison does not run:',
        ...numbers.text.split('\n').slice(0, 4).map((line) => `    ${line.trim()}`),
    ])
    process.exit(1)
}
say(numbers.text.split('\n'))
const agree =
    numbers.text.includes('library  df/dx = 12.0') &&
    numbers.text.includes('analytic df/dx = 12.0') &&
    numbers.text.includes('library  df/dy = 9.0') &&
    numbers.text.includes('analytic df/dy = 9.0')
if (!agree) {
    say([
        'the composition builds, and what it answered is above: the plan entry vjp_api.analytic_gradient',
        'asserts 12 and 9 and is where a disagreement goes red',
    ])
}
process.exit(0)