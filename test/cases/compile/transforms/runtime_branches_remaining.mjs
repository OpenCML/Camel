// How many shapes of `jvp(f)` still take the run-time lift -- the ruler for the step that moves the
// lift into the build. The number only goes down: every slice retires one shape, and the day a
// shape comes *back* to the run-time path this checker is red instead of a reader noticing that a
// reading moved.
//
// A shape is counted by the line the operator prints about itself, `REINTERP jvp op lifted-at=run`
// against `lifted-at=compile`, on a program written for that one shape. `compile` rows are the ones
// the build answers; `run` rows are what is left of the run-time kernel. `split(jvp(f))` is checked
// twice: the operator line says `compile` *and* the cut says it cut at build time
// (`REINTERP split halves-at=compile`), because a cut of a run-time lift would print nothing there.
import { spawnSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))

function repositoryRoot(start) {
    let dir = resolve(start)
    for (;;) {
        try {
            readFileSync(join(dir, 'stdlib', 'rulesets', 'vmap.cml'), 'utf8')
            return dir
        } catch {
            const parent = dirname(dir)
            if (parent === dir) throw new Error(`no stdlib/rulesets/vmap.cml above ${start}`)
            dir = parent
        }
    }
}

const root = repositoryRoot(here)
const camel = join(root, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')

function run(file) {
    const result = spawnSync(camel, [file, 'std::nvm'], { cwd: root, encoding: 'utf8', maxBuffer: 1 << 26 })
    if (result.error) throw result.error
    const stdout = result.stdout ?? ''
    const stderr = result.stderr ?? ''
    if (result.status !== 0) {
        throw new Error(`${file} exited ${result.status}\n${stdout}\n${stderr}`)
    }
    return stdout
}

// The shapes: where the call's value goes, which program writes it, and which half answers.
//   `callee_parameter` -- `jvp(g)` with `g` a parameter: nothing to build from at the call. The
//                         author's ruling turns this into a compile error, not into a lift.
//   `body_has_call`    -- the lifted body calls another function; the lift recurses into callees
//                         only after the callee's own graph is built at build time.
//   `split_of_a_branch` -- the cut of a lift whose body branches. The lift builds from a draft, and
//                         a draft has no Join for a branch to answer at, so this one still lifts --
//                         and is cut -- at run time. A gap in the lift, not in the cut.
const shapes = [
    { name: 'direct', file: join(here, 'jvp_lifted.cml'), at: 'compile' },
    { name: 'handed_to_split', file: join(here, 'runtime_branch_handed_to_split.cml'), at: 'compile' },
    { name: 'callee_parameter', file: join(here, 'runtime_branch_callee_parameter.cml'), at: 'run' },
    { name: 'body_has_call', file: join(here, 'jvp_call.cml'), at: 'run' },
    {
        name: 'split_of_a_branch',
        file: join(here, '..', '..', 'transforms', 'split', 'split_branch_author.cml'),
        at: 'run',
    },
]

// The number of run-time branches left. It is written here, and it may only be lowered: a shape that
// goes back to the run-time path makes `remaining` grow, which is the red this file is for.
// Three, down from four: the cut of a straight-line lift is answered while the caller is built, so
// `handed_to_split` left the run-time path; a branch joint, a callee that is only a value at run time,
// and a body that calls are what is left.
const remaining = 3

const answered = new Map()
for (const shape of shapes) {
    const lines = run(shape.file)
        .split('\n')
        .map((line) => /REINTERP jvp op lifted-at=(\S+)/.exec(line))
        .filter((found) => found !== null)
    if (lines.length !== 1) {
        throw new Error(`${shape.name}: ${lines.length} 'REINTERP jvp op' line(s), one shape is one call`)
    }
    answered.set(shape.name, lines[0][1])
}

const wrong = []
for (const shape of shapes) {
    const at = answered.get(shape.name)
    if (at !== shape.at) {
        wrong.push(`${shape.name}: lifted-at=${at}, the table says ${shape.at}`)
    }
}
if (wrong.length > 0) throw new Error(wrong.join('\n'))

// The cut is one step past the operator line: the halves it hands on were built while the caller was
// built, so the reading of the cut has to say so. Both directions are named here -- a straight-line
// lift is cut at build time, a branched one is not yet -- because the row that has to move is the
// second one, and a check that only named the first would pass whatever happened to the second.
const cutAt = (file, callee) => {
    const line = run(file)
        .split('\n')
        .find((one) => one.includes(`REINTERP split halves-at=`) && one.includes(`function=${callee}`))
    if (line === undefined) throw new Error(`no 'REINTERP split halves-at=' line for ${callee}`)
    return /halves-at=(\S+)/.exec(line)[1]
}
const straightCut = cutAt(join(here, 'runtime_branch_handed_to_split.cml'), 'jvp_of_f')
if (straightCut !== 'compile') {
    throw new Error(`split(jvp(f)) of a straight-line lift says halves-at=${straightCut}, not compile`)
}
const branchCut = cutAt(join(here, '..', '..', 'transforms', 'split', 'split_branch_author.cml'), 'jvp_of_f')
if (branchCut !== 'run') {
    throw new Error(
        `split(jvp(f)) of a branched lift says halves-at=${branchCut}: the lift reads a branch from a draft now, so this row moved -- lower 'remaining'`,
    )
}

const atRun = shapes.filter((shape) => answered.get(shape.name) === 'run').map((shape) => shape.name)
const atCompile = shapes.filter((shape) => answered.get(shape.name) === 'compile').map((shape) => shape.name)
if (atRun.length > remaining) {
    throw new Error(
        `the run-time lift answers ${atRun.length} shape(s) (${atRun.join(', ')}); the ruler is ${remaining} -- a shape went back to the run-time path`,
    )
}

console.log(`remaining=${atRun.length} run=${atRun.join(',')} compile=${atCompile.join(',')} cut=${straightCut}/${branchCut}`)