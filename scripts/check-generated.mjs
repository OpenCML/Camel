#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Every file this tree regenerates, compared byte for byte with what its generator produces now.
//
// A declaration is written once and read by several files that are generated from it. Editing the
// declaration moves the copy under the editor's hands; the others keep the shape of the declarations
// as they were, and nothing about them looks wrong. So this check re-runs the generators and
// compares the file each one would write with the copy in the tree.
//
// Nothing under the tree is opened for writing. The generators run against a throwaway copy of the
// inputs they read (SHADOW_DIRS, SHADOW_FILES below) in the system temporary directory, so a
// generator cannot reach the tree even by writing blindly, and a comparison cannot be mistaken for
// an edit. The copy is deleted at the end; `--keep` leaves it, which is what a run whose result needs
// looking at wants.
//
// Every artifact carries the command that regenerates it, and the line this check prints for it
// carries that command, because a red line that does not say how to fix itself is a line somebody
// deletes.
//
// An artifact belongs to one gate. The AST golden is not regenerated here: recording it runs a
// build, and `scripts/check-ast-hashes.mjs` is the gate that reads it. This check lists it and prints
// who owns it and what that gate reported in the same run, rather than reading it a second time. Two
// gates reading one file look like two pieces of evidence and are one, and the second reading costs a
// whole corpus to agree with the first. So the summary says "4 compared, 1 owned by gate 8": the
// artifact is accounted for, and the gate that can go red about it is the one that does. A gate that
// never goes green because it took on work it cannot do is a gate people learn to skip.
//
// The two generators are not independent, and the order they run in is not free: the signatures
// catalogue is generated from the standard library page, so the page is regenerated first and the
// catalogue in the copy reads the fresh one. One edited declaration therefore moves the page and the
// catalogue built from it, and a run names both -- one cause with two consequences, not one finding
// reported twice. Suppressing either would leave a stale file unnamed.
//
// Exit status: 0 when every artifact that belongs to this gate agrees with its generator (the ones it
// names but does not own are printed as such and are not part of that count); 1 when one or more are
// stale, with the summary naming the commands in the order they must run; 2 when nothing was compared
// or a comparison could not be made (a generator that failed, an input the tree does not have), with
// what stopped it. A run that compared nothing is not a pass.
//
//   node scripts/check-generated.mjs            # compare every artifact this machine can regenerate
//   node scripts/check-generated.mjs --keep     # the same, leaving the temporary copy behind
//   node scripts/check-generated.mjs --list     # the list and the commands, and nothing run

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const NAME = 'check-generated'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// A source root in either layout: `src/` after the move, the tree's own top level before it. The
// generators read wherever the tree kept the thing, so the copy carries whichever exists. Naming
// both and filtering is what keeps this file honest through a move: a list that named only one
// layout would find nothing, and a copy of nothing compares equal to nothing.
const SOURCE_ROOTS = ['src', '.'].filter((dir) => fs.existsSync(path.join(ROOT, dir, 'stdlib')))
const SOURCE_ROOT = SOURCE_ROOTS[0]

// The library in either layout: `lib/` after the move, `stdlib/` before it. The generator reads
// whichever the tree has, so the copy carries whichever exists.
const LIBRARY_DIRS = ['lib', 'stdlib']
    .map((dir) => path.join(SOURCE_ROOT, dir))
    .filter((dir) => fs.existsSync(path.join(ROOT, dir)))

// What the two node generators read. A directory is copied whole, so a module or a source file added
// to it is read by the next run without this list naming it; a file is named where copying the
// directory around it would carry the rest of the tree for nothing. A generator that grows an input
// outside these entries fails loudly (`no such file under <copy>`) instead of comparing against a
// copy it could not read.
const SHADOW_DIRS = [
    ...LIBRARY_DIRS,
    path.join(SOURCE_ROOT, 'parse'),
    path.join(SOURCE_ROOT, 'kernel/diagnostics'),
]
const SHADOW_FILES = [
    'docs/stdlib-reference.md',
    'docs/reference/camel-for-ai.md',
    'docs/reference/camel-for-ai-diagnostics.md',
    'docs/reference/camel-for-ai-stdlib-signatures.md',
    'docs/language/02_lexical.md',
    'scripts/stdlib-reference.mjs',
    'scripts/camel-for-ai.mjs',
    'scripts/install-stamp.mjs',
    'test/tools/camel-arg.mjs',
]

// The generators, in the order they run: each reads the tree, and the second reads the page the first
// has just regenerated, so a stale declaration reaches both files in one run.
const REGENERATIONS = [
    {
        script: 'scripts/stdlib-reference.mjs',
        argv: ['--write'],
        command: 'node scripts/stdlib-reference.mjs --write',
        produces: ['docs/stdlib-reference.md'],
    },
    {
        script: 'scripts/camel-for-ai.mjs',
        argv: ['--write'],
        command: 'node scripts/camel-for-ai.mjs --write',
        produces: [
            'docs/reference/camel-for-ai.md',
            'docs/reference/camel-for-ai-diagnostics.md',
            'docs/reference/camel-for-ai-stdlib-signatures.md',
        ],
    },
]

// The artifacts compared, in the order a reader has to fix them. `count` names what the file is an
// account of, so the line says how much was regenerated and not only that the bytes were the same.
const ARTIFACTS = [
    {
        rel: 'docs/stdlib-reference.md',
        command: 'node scripts/stdlib-reference.mjs --write',
        from: 'the declarations under lib/ (stdlib/ in the earlier layout)',
        count: (text) => `${(text.match(/^#### /gm) || []).length} entries`,
    },
    {
        rel: 'docs/reference/camel-for-ai-stdlib-signatures.md',
        command: 'node scripts/camel-for-ai.mjs --write',
        from: 'docs/stdlib-reference.md, as regenerated above',
        count: (text) =>
            `${[...text.matchAll(/— (\d+) name\(s\)/g)].reduce((total, found) => total + Number(found[1]), 0)} names`,
    },
    {
        rel: 'docs/reference/camel-for-ai.md',
        command: 'node scripts/camel-for-ai.mjs --write',
        from: 'the keyword table, the parser and the diagnostic registry',
    },
    {
        rel: 'docs/reference/camel-for-ai-diagnostics.md',
        command: 'node scripts/camel-for-ai.mjs --write',
        from: 'kernel/diagnostics/registry.tsv',
    },
]

// The generated artifacts this gate does not read, with the gate that does. They are listed rather
// than left out: a list that holds only what this run reads reads as a cover of everything.
const OWNED = [
    {
        rel: 'test/plans/feat/parse/golden/ast_hashes.txt',
        gate: 8,
        owner: 'node scripts/check-ast-hashes.mjs',
        needs: 'a build of this tree, for `camel-parsecheck`',
        re_record:
            'internal/tools/camel-remote.ps1 -Path <this tree> -Plan test/plans/feat/parse -UpdateAstHashes',
    },
]

// The exit a gate that ran earlier reported, when `scripts/gates.mjs` started this run: it hands each
// gate the exits of the gates before it, so the pointer below can carry a reading instead of a guess.
// A direct run has no such reading and says so; it does not borrow one.
function gateExit(number) {
    const handed = process.env.CAMEL_GATES_EXITS
    if (!handed) return undefined
    let exits
    try {
        exits = JSON.parse(handed)
    } catch {
        return undefined
    }
    if (!Array.isArray(exits)) return undefined
    const found = exits.find((entry) => entry && entry.number === number)
    return found ? found.code : undefined
}

/** How the gate that owns an artifact closed in this run, in the words the line can use. */
function ownerReported(number) {
    const code = gateExit(number)
    if (code === undefined) return `this run recorded no exit for gate ${number}`
    if (code === null) return `gate ${number} was killed by a signal in this run`
    return `gate ${number} exits ${code} in this run`
}

function usage(message) {
    if (message) console.log(`[${NAME}] ${message}`)
    console.log(
        'usage: node scripts/check-generated.mjs [--keep] [--list]\n' +
            '  --keep   leave the temporary copy of the generators\' inputs behind, and say where\n' +
            '  --list   the artifacts, the command for each, and nothing run',
    )
    process.exit(2)
}

function copyInto(shadow, rel) {
    const source = path.join(ROOT, rel)
    const target = path.join(shadow, rel)
    if (!fs.existsSync(source)) return `${rel}: the tree does not have it`
    fs.mkdirSync(path.dirname(target), { recursive: true })
    fs.cpSync(source, target, { recursive: true })
    return null
}

function makeShadow() {
    const shadow = fs.mkdtempSync(path.join(os.tmpdir(), `${NAME}-`))
    const missing = []
    for (const rel of SHADOW_DIRS.concat(SHADOW_FILES)) {
        const problem = copyInto(shadow, rel)
        if (problem) missing.push(problem)
    }
    return { shadow, missing }
}

function runGenerator(shadow, regeneration) {
    const script = path.join(shadow, regeneration.script)
    if (!fs.existsSync(script)) {
        return { ok: false, output: `${regeneration.script}: the copy does not have it` }
    }
    const result = spawnSync(process.execPath, [script, ...regeneration.argv], {
        cwd: shadow,
        encoding: 'utf8',
        timeout: 600000,
    })
    const output = `${result.stdout || ''}${result.stderr || ''}`.trimEnd()
    if (result.status === 0) return { ok: true, output }
    const reason = result.status === null ? `killed by ${result.signal || 'a signal'}` : `exit ${result.status}`
    return { ok: false, output, reason }
}

/** The first byte where two readings part, as a line number a reader can open. */
function firstDifference(want, got) {
    const shared = Math.min(want.length, got.length)
    let index = 0
    while (index < shared && want[index] === got[index]) index++
    let line = 1
    for (let position = 0; position < index; position++) if (want[position] === 0x0a) line++
    return line
}

function compare(shadow, artifact) {
    const treePath = path.join(ROOT, artifact.rel)
    const generatedPath = path.join(shadow, artifact.rel)
    if (!fs.existsSync(generatedPath)) {
        return {
            stale: true,
            said: `[${NAME}] NO OUTPUT ${artifact.rel}: \`${artifact.command}\` wrote no such file in the copy of the tree`,
        }
    }
    const generated = fs.readFileSync(generatedPath)
    const counted = artifact.count ? `, ${artifact.count(generated.toString('utf8'))}` : ''
    const described = (verdict) =>
        `[${NAME}] ${verdict} ${artifact.rel}: ${generated.length} bytes${counted} as \`${artifact.command}\`` +
        ` writes them from ${artifact.from}`
    if (!fs.existsSync(treePath)) {
        return { stale: true, said: `${described('MISSING')} -- the tree does not have this file yet` }
    }
    const tree = fs.readFileSync(treePath)
    if (tree.equals(generated)) return { stale: false, said: `${described('ok')}, the same bytes as the tree's copy` }
    const at = firstDifference(tree, generated)
    return {
        stale: true,
        said:
            `${described('STALE')} -- the tree's copy is not those bytes ` +
            `(${tree.length} bytes, first difference at line ${at})`,
    }
}

function main(argv) {
    let keep = false
    for (const argument of argv) {
        if (argument === '--keep') keep = true
        else if (argument === '--list') {
            for (const artifact of ARTIFACTS) console.log(`[${NAME}] ${artifact.rel}  <-  ${artifact.command}`)
            for (const entry of OWNED) {
                console.log(
                    `[${NAME}] ${entry.rel}  <-  owned by gate ${entry.gate} (${entry.owner}; ${entry.needs}); ` +
                        `re-recorded by ${entry.re_record}`,
                )
            }
            return 0
        } else if (argument === '-h' || argument === '--help') usage()
        else usage(`unknown argument '${argument}'`)
    }

    const { shadow, missing } = makeShadow()
    const fail = (lines) => {
        for (const one of [].concat(lines)) console.log(`[${NAME}] ${one}`)
        if (keep) console.log(`[${NAME}] the copy is at ${shadow}`)
        else fs.rmSync(shadow, { recursive: true, force: true })
        console.log(`[${NAME}] refusing: a run that did not produce a comparison has no stale count to report`)
        return 2
    }
    if (missing.length > 0) {
        return fail([
            ...missing.map((one) => `an input of the generators is not in the tree: ${one}`),
            `the copy is built from ${SHADOW_DIRS.concat(SHADOW_FILES).join(', ')}`,
        ])
    }
    console.log(`[${NAME}] the generators are running in ${shadow}, a copy of what they read; the tree is read only`)

    const produced = []
    for (const regeneration of REGENERATIONS) {
        const run = runGenerator(shadow, regeneration)
        if (!run.ok) {
            return fail([
                `\`${regeneration.command}\` could not produce ${regeneration.produces.join(', ')} (${run.reason})`,
                ...run.output.split('\n').slice(-6).map((one) => `  ${one}`),
            ])
        }
        for (const rel of regeneration.produces) produced.push(rel)
    }

    const stale = []
    const agreed = []
    for (const artifact of ARTIFACTS) {
        if (!produced.includes(artifact.rel)) {
            return fail([`${artifact.rel}: no generator in this run produced it, so it was not compared`])
        }
        const verdict = compare(shadow, artifact)
        console.log(verdict.said)
        if (verdict.stale) stale.push(artifact)
        else agreed.push(artifact)
    }

    for (const entry of OWNED) {
        console.log(
            `[${NAME}] owned by gate ${entry.gate} ${entry.rel}: \`${entry.owner}\` is the gate that compares it ` +
                `(${entry.needs}); ${ownerReported(entry.gate)}, so nothing compared it here. ` +
                `It is re-recorded by \`${entry.re_record}\`. This line is a pointer, not a verdict`,
        )
    }

    if (!keep) fs.rmSync(shadow, { recursive: true, force: true })
    else console.log(`[${NAME}] the copy is at ${shadow}`)

    const total = ARTIFACTS.length + OWNED.length
    if (stale.length > 0) {
        console.log(
            `[${NAME}] ${stale.length} of ${total} artifact(s) are stale: ${stale.map((one) => one.rel).join(', ')}`,
        )
        for (const artifact of stale) {
            console.log(`[${NAME}] regenerate ${artifact.rel}: ${artifact.command}`)
        }
        if (stale.some((one) => one.rel === 'docs/stdlib-reference.md')) {
            console.log(
                `[${NAME}] the order is not free: \`node scripts/stdlib-reference.mjs --write\` first, ` +
                    'then `node scripts/camel-for-ai.mjs --write`, because the second reads the page the first writes',
            )
        }
        return 1
    }
    if (OWNED.length > 0) {
        console.log(
            `[${NAME}] ${agreed.length} of ${total} artifact(s) were compared and the tree agrees: ` +
                `${agreed.length} compared, ${OWNED.length} owned by gate ${OWNED.map((one) => one.gate).join(', ')} ` +
                `and not compared here (${OWNED.map((one) => `${one.rel} -- ${ownerReported(one.gate)}`).join('; ')})`,
        )
        return 0
    }
    console.log(`[${NAME}] all ${total} artifact(s) were compared and the tree agrees`)
    return 0
}

process.exitCode = main(process.argv.slice(2))