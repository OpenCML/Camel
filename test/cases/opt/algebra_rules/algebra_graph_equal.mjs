// The graph the rule library produced against the graph the C++ algebraic pass produced, which the
// reference entry of the plan recorded as a golden.
//
// `compareGirDotIsomorphic` (test/tools/gir-dot.mjs) is the comparison the snapshot tests use for a
// `.dot` dump, and the one used here: a node is its kind, its type, its shape and its in/out degree,
// and an edge is the kinds of its two endpoints, its link type and its label. That is what makes the
// two graphs the same graph. The two dumps cannot be compared as text: the rule engine's commit
// leaves the port without the span the C++ replacement copies onto it (H160 section 4), so the
// normalizers this plan lists — paths and origin ids — are applied to the golden and the comparison
// stops at the structure, exactly as the Camel-pass differential does.

import fs from 'node:fs'
import { compareGirDotIsomorphic } from '../../../tools/gir-dot.mjs'
import { applyNormalizers } from '../../../tools/normalizers.mjs'

const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths', 'normalize_origins']
const GOLDENS = {
    'algebra_rules.agreement.by_library': 'algebra_rules.agreement.cpp.dot',
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
    throw new Error(`the rule library's graph is not the graph ${golden} records: ${compared.message}`)
}