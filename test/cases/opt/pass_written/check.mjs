#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The operators the passes write, and the number of cases that hold each of them.
 *
 * These are the operators whose writer is a pass and not the builder, so "is it reachable?" has no
 * answer in the table -- the answer is the corpus. `:op/keep` is written by `replaceNode` when the
 * node it removes has no sync place; `:enum/tag`, `:enum/field` and `:enum/make` are written by the
 * defunctionalizer. The numbers below are pinned by name, because a count with no names goes red
 * without saying anything: one more case means a rule started writing the operator where it did
 * not, one fewer means a rule stopped, and either way someone has to look at which case moved.
 *
 * A count of zero is pinned like any other, and it has an owner: `:enum/make` is written by the
 * defunctionalizer and no case of `test/cases/opt/defun` reaches that path today. Zero is a
 * measurement here, not an absence of one -- the day a case starts holding one, this goes red.
 *
 * Usage: node check.mjs
 */

import { spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import url from 'url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CASES = path.join(REPO_ROOT, 'test', 'cases', 'opt')
const CAMEL = path.join(
    REPO_ROOT,
    'out',
    'latest',
    'bin',
    process.platform === 'win32' ? 'camel.exe' : 'camel'
)
const env = {
    ...process.env,
    CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
}

/// One entry per operator: the corpus that answers the question for it, and what was measured on
/// 2026-11-01, by file. An operator pinned to nothing is pinned to zero (`names` empty): that is a
/// reading too, and the owner line says who would write it.
const FAMILY = [
    {
        uri: ':op/keep',
        corpus: 'effects',
        owner:
            '`replaceNode` -> `keepThroughValue` (src/passes/opt/generic/rewrite.cpp), for a node ' +
            'with no sync user that takes a last read',
        names: {
            'branch_keeps_outer_effect.cml': 2,
            'dce_effect_under_pure_arithmetic.cml': 2,
            'fold_keeps_arm_effect.cml': 2
        }
    },
    {
        uri: ':enum/tag',
        corpus: 'defun',
        owner: 'the defunctionalizer (src/passes/opt/defun/defunctionalize.cpp)',
        names: {
            'captured.cml': 1,
            'closure_dispatch.cml': 1,
            'effects.cml': 1,
            'higher_order.cml': 1,
            'nested_signature.cml': 1,
            'widened_own_values.cml': 1
        }
    },
    {
        uri: ':enum/field',
        corpus: 'defun',
        owner: 'the defunctionalizer (src/passes/opt/defun/defunctionalize.cpp)',
        names: {
            'captured.cml': 1,
            'closure_dispatch.cml': 2,
            'fold_and_dispatch.cml': 1,
            'widened_own_values.cml': 1
        }
    },
    {
        uri: ':enum/make',
        corpus: 'defun',
        owner:
            'the defunctionalizer, for a value that becomes an enum -- no case of ' +
            'test/cases/opt/defun reaches that path today, and this pin is what says so',
        names: {}
    }
]

const CORPUS_DIRS = ['effects', 'defun']

/// Every `:oper` label of one program's graph after `std::opt`, once per program.
const histogram = (file) => {
    const run = spawnSync(CAMEL, [file, 'std::opt', 'std::jgir'], { env, encoding: 'utf8' })
    if (run.status !== 0) {
        throw new Error(`camel ${file} std::opt std::jgir exited ${run.status}: ${run.stderr}`)
    }
    const counts = {}
    for (const match of run.stdout.matchAll(/"label": "(:[^"]+)"/g)) {
        counts[match[1]] = (counts[match[1]] || 0) + 1
    }
    return counts
}

/// The measured map for one corpus: `{ 'case.cml': { ':op/keep': 2, ... } }`.
const sweep = (corpus) => {
    const files = fs
        .readdirSync(path.join(CASES, corpus))
        .filter((name) => name.endsWith('.cml'))
        .sort()
    const measured = {}
    for (const name of files) {
        measured[name] = histogram(path.join(CASES, corpus, name))
    }
    return { files, measured }
}

const corpora = Object.fromEntries(CORPUS_DIRS.map((corpus) => [corpus, sweep(corpus)]))

const counted = (map) => Object.values(map).reduce((sum, n) => sum + n, 0)

/// Both directions of one pin: what stopped holding the operator, and what started holding it
/// without being pinned. The walking of each corpus also names every file, so a red line can be
/// read next to the whole population it moved in.
const compare = (entry, found) => {
    const lines = []
    const pinned = entry.names
    for (const name of Object.keys(pinned).sort()) {
        if (!(name in found)) {
            lines.push(`[FAMILY] RED ${entry.uri} ${name} no longer holds it (a rule stopped)`)
        } else if (found[name] !== pinned[name]) {
            lines.push(
                `[FAMILY] RED ${entry.uri} ${name} holds ${found[name]} node(s), pinned ${pinned[name]}`
            )
        }
    }
    for (const name of Object.keys(found).sort()) {
        if (!(name in pinned)) {
            lines.push(`[FAMILY] RED ${entry.uri} ${name} holds it and is not pinned (a rule started)`)
        }
    }
    if (Object.keys(found).length !== Object.keys(pinned).length) {
        lines.push(
            `[FAMILY] RED ${entry.uri} pinned in ${Object.keys(pinned).length} case(s), found in ` +
                `${Object.keys(found).length}`
        )
    }
    return lines
}

const lines = []
let red = 0
for (const entry of FAMILY) {
    const corpus = corpora[entry.corpus]
    const found = {}
    for (const [name, counts] of Object.entries(corpus.measured)) {
        const nodes = counts[entry.uri] || 0
        if (nodes > 0) {
            found[name] = nodes
        }
    }
    const head =
        `[FAMILY] ${entry.uri} cases=${Object.keys(found).length} nodes=${counted(found)} ` +
        `of ${corpus.files.length} (corpus ${entry.corpus})`
    const verdicts = compare(entry, found)
    red += verdicts.length
    lines.push(head)
    for (const [name, nodes] of Object.entries(found).sort()) {
        lines.push(`[FAMILY]   ${entry.uri} ${name} nodes=${nodes}`)
    }
    if (Object.keys(found).length === 0) {
        lines.push(`[FAMILY]   ${entry.uri} pinned to zero -- owner: ${entry.owner}`)
    }
    lines.push(...verdicts)
}
lines.push(red === 0 ? '[FAMILY] ok' : `[FAMILY] RED ${red} pinned number(s) moved`)
process.stdout.write(lines.join('\n') + '\n')
process.exit(red === 0 ? 0 : 1)