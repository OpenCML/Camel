#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The gate list, as one command.
 *
 * A check that is on no list is a check nobody runs, and the cost of that is not hypothetical: the
 * reds of 2026-11-22 name it twice. `frontend.ast_snapshot` went red on `2d384c3c4` because a `.cml`
 * change did not re-record `test/plans/feat/parse/golden/ast_hashes.txt`, and the merge that landed
 * it had run what the project called "the six gates" -- a list in somebody's memory, which did not
 * name `scripts/check-ast-hashes.mjs`, a script that existed for exactly that red. The same merge
 * left `internal/questions/` with an answered page still listed as pending, which
 * `check-doc-links.mjs --questions-index` reports. Neither check was broken. Neither was on the list.
 *
 * So the list is this file, and it is ten long. A number in a name goes stale -- "the six gates" was
 * already wrong when it was still being said -- so the count is printed by the run and not written
 * anywhere else.
 *
 * What each one answers, in the order they run (cheapest and most structural first, the one that
 * starts the compiler last):
 *
 *   1  check-doc-links --all            every relative link in the reader-facing pages resolves
 *   2  check-doc-links --citations      no source is cited by line outside internal, where a
 *                                       line number is that day's reading and the index says so
 *   3  check-doc-links --questions-index  no page answered in reviews/ is still listed as pending
 *   4  check-process-traces             every trace a page describes is a reading the tree takes
 *   5  check-plan-fields                a field the entry's kind never reads is not a test, and no
 *                                       rule of test/plan-selection.json names a path that is not
 *                                       there (a rule that matches no file selects nothing)
 *   6  check-grammar --check            the grammar blocks, the token table and the node kinds agree
 *   7  headers --check                  every hand-written source carries the two-line header
 *   8  check-ast-hashes                 every `.cml` file's AST is the one the golden records
 *   9  check-operator-panic             every operator that can fail declares that it can
 *  10  check-generated                  every file a generator writes is byte for byte what that
 *                                       generator produces now, and the artifacts it could not
 *                                       regenerate are named rather than counted as green
 *
 * Exit status is 1 when any gate fails, and the summary says which ones: the first line of a failing
 * gate's block carries its number, its exit code and the exact command line, because "one of the
 * gates is red" is the least useful sentence this script could print.
 *
 * Each generated artifact has one gate that reads it. Gate 10 compares the four files the node
 * generators write, and lists the AST golden as gate 8's to read: it prints the exit gate 8 reported
 * for it in the same run, because reading it twice would look like two pieces of evidence and be one,
 * at the price of a whole corpus of hashing to agree with the first.
 *
 * This is the whole-tree static layer: it needs no install and no build except where a gate says so
 * (8 reads the tree through the parsecheck tool, which a build produces; 10 regenerates the files the
 * node generators write and names the one that needs a build instead of passing over it). It is not
 * `scripts/release-gate.mjs`: that one runs the plan layers and the thirteen static checks a release
 * decision needs. These ten are the ones a change must pass before it is handed over.
 *
 *   node scripts/gates.mjs              # all ten
 *   node scripts/gates.mjs --list       # the list, and nothing run
 *   node scripts/gates.mjs --only 5,8   # a PARTIAL run, and it says so in the summary
 */
import { spawnSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

const GATES = [
    {
        name: 'document links: every relative link resolves',
        args: ['scripts/check-doc-links.mjs', '--all'],
    },
    {
        name: 'citations: no source cited by line outside internal',
        args: ['scripts/check-doc-links.mjs', '--citations'],
    },
    {
        name: 'questions: no page answered in reviews/ is still listed as pending',
        args: ['scripts/check-doc-links.mjs', '--questions-index'],
    },
    {
        name: 'process traces: every trace a page describes is a reading the tree takes',
        // The tree is named here rather than left to the checker's own default: a checker that
        // chose its tree from the process's directory would read the main checkout (iron rule 17)
        // when this runs from anywhere else. Every gate already runs with `cwd: ROOT` below.
        args: ['scripts/check-process-traces.mjs', '--root', ROOT],
    },
    {
        name: 'plan fields: no field a kind does not read, no selection rule naming a path the tree lacks',
        args: ['test/tools/check-plan-fields.mjs'],
    },
    {
        name: 'grammar: the grammar blocks, the token table and the node kinds agree',
        args: ['scripts/check-grammar.mjs', '--check'],
    },
    {
        name: 'headers: every hand-written source carries the two-line header',
        args: ['scripts/headers.mjs', '--check'],
    },
    {
        name: 'ast hashes: every .cml file is the AST the golden records',
        args: ['scripts/check-ast-hashes.mjs'],
    },
    {
        name: 'operator panic: every operator that can fail declares that it can',
        args: ['scripts/check-operator-panic.mjs'],
    },
    {
        name: 'generated artifacts: every generator\'s file is the bytes it produces now',
        args: ['scripts/check-generated.mjs'],
    },
]

function usage() {
    console.log('Usage: node scripts/gates.mjs [--list] [--only N,M,...]')
    console.log('')
    console.log('Runs the ten gates a change must pass before it is handed over, one after another, and')
    console.log("prints each one's exit code. Exit status 1 when any gate failed, 2 when the command line")
    console.log('is not understood (nothing is run in that case).')
}

function main(argv) {
    let list = false
    let only = null
    for (let index = 0; index < argv.length; index++) {
        const argument = argv[index]
        if (argument === '--list') {
            list = true
        } else if (argument === '--only') {
            if (index + 1 >= argv.length) {
                console.error('gates: --only needs one or more gate numbers, as in --only 5,8')
                return 2
            }
            only = []
            for (const word of argv[++index].split(',')) {
                const number = Number(word.trim())
                if (!Number.isInteger(number) || number < 1 || number > GATES.length) {
                    console.error(`gates: '${word.trim()}' is not a gate number (1-${GATES.length})`)
                    return 2
                }
                only.push(number)
            }
        } else if (argument === '--help' || argument === '-h') {
            usage()
            return 0
        } else {
            console.error(`gates: unknown argument '${argument}'`)
            usage()
            return 2
        }
    }
    if (list) {
        for (let index = 0; index < GATES.length; index++) {
            const gate = GATES[index]
            console.log(`${index + 1}. ${gate.name}`)
            console.log(`   node ${gate.args.join(' ')}`)
        }
        return 0
    }
    const picked = only === null ? GATES.map((_, index) => index + 1) : only
    const failed = []
    // The exits of the gates that already ran, handed to the next one: a gate whose subject is another
    // gate's to read points at it with a reading rather than a guess, instead of repeating the work to
    // produce a second copy of the same evidence.
    const ran = []
    const startedAll = Date.now()
    for (const number of picked) {
        const gate = GATES[number - 1]
        const command = `node ${gate.args.join(' ')}`
        console.log(`[gate ${number}/${GATES.length}] ${gate.name}`)
        const started = Date.now()
        const result = spawnSync(process.execPath, gate.args, {
            cwd: ROOT,
            stdio: 'inherit',
            env: { ...process.env, CAMEL_GATES_EXITS: JSON.stringify(ran) },
        })
        const seconds = ((Date.now() - started) / 1000).toFixed(1)
        // A gate that a signal ended has no exit code, and a missing code is not a pass: `null`
        // there means the process was killed, which is a failed gate and not a green one.
        const code = result.status === null ? `killed by ${result.signal || 'a signal'}` : result.status
        ran.push({ number, code: result.status })
        if (code !== 0) {
            failed.push({ number, name: gate.name, code, command })
            console.log(`[gate ${number}/${GATES.length}] FAIL exit=${code} after ${seconds} s: ${command}`)
        } else {
            console.log(`[gate ${number}/${GATES.length}] ok (${seconds} s)`)
        }
    }
    const failedNumbers = new Set(failed.map((entry) => entry.number))
    const green = picked.filter((number) => !failedNumbers.has(number)).length
    const partial = picked.length === GATES.length ? '' : ` -- PARTIAL: ${picked.length} of ${GATES.length} gates ran`
    console.log(
        `gates: ${green} of ${picked.length} ran green in ${((Date.now() - startedAll) / 1000).toFixed(1)} s${partial}`,
    )
    for (const entry of failed) {
        console.log(`gates: FAIL gate ${entry.number} (${entry.name}) exit=${entry.code}: node ${entry.command.slice(5)}`)
    }
    return failed.length > 0 ? 1 : 0
}

process.exitCode = main(process.argv.slice(2))