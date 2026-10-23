// The graph a Camel pass produced against the graph the C++ pass produced, which is the golden the
// reference entry of the plan recorded.
//
// `gir_dot_isomorphic` (test/tools/gir-dot.mjs) is the comparison the snapshot tests use for this
// file, and the one used here: a node is its kind, its type, its shape and its in/out degree, and
// an edge is the kinds of its two endpoints, its link type and its label. That is what makes the
// two graphs the same graph: the same values, of the same types, in the same region structure,
// wired the same way round — and the two dumps cannot be compared as text, because compiling the
// pass's own library renumbers every node's source origin.
//
// The entry's name selects the pair (see GOLDENS): the corpus whose literals and expressions
// repeat, the dead-value corpus, and the var corpus whose expression is reread after a write.

import fs from 'node:fs'
import { compareGirDotIsomorphic } from '../../../tools/gir-dot.mjs'
import { applyNormalizers } from '../../../tools/normalizers.mjs'

// The normalizers the plan lists for these entries; they are applied to the golden here so that
// the comparison sees what a snapshot comparison would see.
const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths', 'normalize_origins']
const GOLDENS = {
    'camel_passes.cse.by_camel': 'camel_passes.cse.dot',
    'camel_passes.dce.by_camel': 'camel_passes.dce.dot',
    'camel_passes.cse_written_variable.by_camel': 'camel_passes.cse_written_variable.dot',
}

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const golden = GOLDENS[input.test_name]
if (!golden) throw new Error(`no golden is recorded for the entry '${input.test_name}'`)
const path = new URL(`../../../plans/feat/opt/golden/${golden}`, import.meta.url)
const expected = applyNormalizers(fs.readFileSync(path, 'utf8').replace(/\r\n/g, '\n'), NORMALIZERS)
const actual = input.normalized.stdout
if (actual.trim() === '') throw new Error(`the run printed no graph: ${JSON.stringify(input.command)}`)
const compared = compareGirDotIsomorphic(actual, expected)
if (!compared.ok) {
    throw new Error(`the Camel pass's graph is not the graph ${golden} records: ${compared.message}`)
}