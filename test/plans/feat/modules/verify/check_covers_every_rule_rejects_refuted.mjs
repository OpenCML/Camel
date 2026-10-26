// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The negative space of `check_covers_every_rule.mjs`, the guard on the library's coverage rule.
// That script is exercised on the run's own report by `rules.every_exported_rule_enters_the_check`,
// and a guard that only ever sees a green report cannot show that it would refuse one. This program
// feeds it reports built out of the library's export list and asserts both directions:
//
//   * a report in which every exported rule is `sampled` is accepted (exit 0) -- a checker that
//     demanded some rule be `refuted`, or that carried a list of names it let a `refuted` line
//     through for, fails here;
//   * a report in which exactly one exported rule is `refuted` is refused (a non-zero exit), once
//     per exported rule, so no name of the library is excused. The names come from
//     `stdlib/rules.cml`, never from a list written here, so a rule added to the library is probed
//     too and the guard cannot be left behind by a rename.
//
// Its red readings: put an excuse list back into `check_covers_every_rule.mjs` and the first half
// fails; make it accept a report with a `refuted` line and the second half fails for the name that
// was refuted.
//
// Each payload carries the real case path -- the checker reads it for the names the case hands over,
// and the real repository root -- and a report made up here: nothing of this run's own verdicts is
// reused, so the two directions are read whatever the check itself says.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const checker = path.join(here, 'check_covers_every_rule.mjs')
const repoRoot = path.resolve(here, '..', '..', '..', '..', '..')
const casePath = path.join(repoRoot, 'test', 'cases', 'modules', 'rules', 'check_every_rule.cml')
const library = path.join(repoRoot, 'stdlib', 'rules.cml')
const scratch = process.argv[2]

if (!scratch) {
    console.error('this guard needs its scratch directory as its first argument (${test_tmp})')
    process.exit(1)
}

const problems = []
const exported = []
for (const line of fs.readFileSync(library, 'utf8').split('\n')) {
    const declared = /^\s*export\s+rule\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(line)
    if (declared) exported.push(declared[1])
}
if (exported.length === 0) {
    // A guard that examined zero rules would pass without asking anything.
    problems.push(`${library} exports no rule, so this guard examines nothing`)
}

fs.mkdirSync(scratch, { recursive: true })

// One report per question: every exported rule `sampled`, and `refutedName` -- when it is one --
// `refuted` instead. `undefined` asks the accepting direction.
function reportWith(refutedName) {
    const lines = exported.map((name) => (name === refutedName
        ? `refuted ${name}: proposition equality, evidence counterexample, the two sides differ`
        : `sampled ${name}: proposition equality, evidence samples 200`))
    return `${lines.join('\n')}\n`
}

function checkerOn(refutedName) {
    const payloadPath = path.join(scratch, `payload-${refutedName ?? 'not-refuted'}.json`)
    fs.writeFileSync(payloadPath, JSON.stringify({
        case_path: casePath,
        repo_root: repoRoot,
        test_name: `coverage guard probe (${refutedName ?? 'no rule refuted'})`,
        exit_code: 0,
        normalized: { stdout: reportWith(refutedName) },
    }))
    return spawnSync(process.execPath, [checker, payloadPath], { encoding: 'utf8' })
}

const accepted = checkerOn(undefined)
if (accepted.status !== 0) {
    problems.push(
        `a report in which no rule is refuted was refused (exit ${accepted.status}): `
            + `${`${accepted.stdout}${accepted.stderr}`.trim()}`
    )
}
let refused = 0
for (const name of exported) {
    const verdict = checkerOn(name)
    if (verdict.status === 0) {
        problems.push(`a report in which '${name}' is refuted was accepted`)
    } else {
        refused += 1
    }
}

if (problems.length > 0) {
    for (const problem of problems) {
        console.error(problem)
    }
    process.exit(1)
}
console.log(
    `${exported.length} exported rule(s) probed: a report with no rule refuted is accepted, and a `
        + `report in which any one of them is refuted is refused (${refused} of ${exported.length})`
)