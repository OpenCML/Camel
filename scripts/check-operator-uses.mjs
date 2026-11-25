#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The operators whose effect neither entrance stated, and the operators whose two entrances
 * disagree: the debt this change is paying off, as a number the gate keeps.
 *
 * --- why the reading is taken from the record ---------------------------------
 * The effect of an operator is stated at one of two equal entrances (design principles 9.3): the
 * Camel declaration under `#op('<uri>')`, and the operator table a module declares it in
 * (`OperatorTable::declare` / `declareResources` / `set` / `setResolver`, or the record API's
 * `CppRegistration`). Both entrances write the operator record, and the record is what a reader of
 * the effect asks (`camel/ops/operator_record.h`, item `uses`; `core.arrangeable`,
 * `tiles::touchesNothing`). So the question "which operators say nothing" is a question about the
 * record, and the record is read through `camel inspect --ops <program>`, which prints for every
 * operator of the modules a program imports:
 *
 *   `record`            -- the operator has a record at all;
 *   `uses_stated`       -- its `uses` item is stated (by either entrance);
 *   `clause_pure`       -- the record's own reading of the clause;
 *   `purity`            -- what the declaration in the table says, for comparison;
 *   `accesses_declared` -- the table holds an access list for it (a `pure` word is the empty list).
 *
 * Reading the source instead was tried and refused: a module writes most of its statements in
 * loops over the names of an implementation map (`for (const auto &[name, op] : getOpsImplMap())`),
 * so the source names the operator in a place no reader of the source can attribute to it, and a
 * scan of the source called 243 of the 382 operators in the maps silent while the record said most
 * of them were stated. A check that cannot tell "stated" from "written where I cannot read it" is
 * not a list anybody can work through.
 *
 * --- the two numbers -----------------------------------------------------------
 * `silent` is the list to declare, one operator at a time: no clause in the record means no
 * entrance spoke, and design principles 3.3 reads that as writing the global resource, so the
 * operator keeps the order the program gives it among every effect.
 * `disagreed` is not a debt but a defect: an operator whose table declaration and record read
 * differently is one statement stored twice, which is the thing the record exists to prevent. A
 * non-zero count here fails the check apart from the debt count.
 *
 * --- exit code -----------------------------------------------------------------
 * 1 while the list is not empty (the gate runs it report-only, so the count comes down without
 * blocking a merge; the entry becomes a wall the day it reaches zero), 3 when the two entrances
 * disagree, 2 when the reading cannot be taken at all -- a check that cannot see the tree is
 * refused, never passed.
 */

import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const NAME = 'operator-uses'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const PROGRAM = 'test/cases/modules/stdlib/registry_all.cml'

function binary() {
  if (process.env.CAMEL_BIN) return process.env.CAMEL_BIN
  const name = process.platform === 'win32' ? 'camel.exe' : 'camel'
  const candidates = [
    path.join(ROOT, 'out', 'latest', 'bin', name),
    path.join(process.env.CAMEL_HOME ?? '', 'bin', name),
  ]
  for (const candidate of candidates) {
    if (candidate && fs.existsSync(candidate)) return candidate
  }
  return null
}

const bin = binary()
if (bin === null) {
  console.error(`[${NAME}] no camel binary at out/latest/bin or under CAMEL_HOME`)
  console.error(`[${NAME}] the reading is taken from the record, so it needs the build: run the check after the build`)
  process.exit(2)
}

let output
try {
  output = execFileSync(bin, ['inspect', '--ops', PROGRAM], {
    cwd: ROOT,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
    timeout: 600000,
  })
} catch (error) {
  console.error(`[${NAME}] ${bin} inspect --ops ${PROGRAM} failed: ${error.message}`)
  process.exit(2)
}

const rows = []
for (const line of output.split(/\r?\n/)) {
  const text = line.trim()
  if (!text.startsWith('OPERATOR ')) continue
  try {
    rows.push(JSON.parse(text.slice('OPERATOR '.length)))
  } catch (error) {
    console.error(`[${NAME}] an OPERATOR line is not JSON (${error.message}): ${text.slice(0, 120)}`)
    process.exit(2)
  }
}

if (!rows.length) {
  console.error(`[${NAME}] ${bin} inspect --ops ${PROGRAM} printed no operator`)
  process.exit(2)
}

const withRecord = rows.filter(row => row.record)
const stated = rows.filter(row => row.uses_stated)
const silent = rows.filter(row => row.record && !row.uses_stated)
const disagreed = rows.filter(
  row => row.record && (row.purity === 'pure') !== Boolean(row.clause_pure),
)

/**
 * One operator, one line: `describeOperators` lists an operator once per declaration it has (an
 * overload is a second declaration of the same URI), and the item to write is the operator's, so
 * the rows are folded onto `module:uri` before anything is counted. The row count is printed beside
 * the operator count, because a reader who sees `pyplot:plot` once and eight rows above should be
 * able to tell which number means what.
 */
const byOperator = new Map()
for (const row of silent) {
  const key = `${row.module === '' ? '<builtin>' : row.module}:${row.uri}`
  if (!byOperator.has(key)) byOperator.set(key, row)
}

const sorted = [...byOperator.values()].sort((a, b) =>
  (a.module + a.uri).localeCompare(b.module + b.uri),
)
const byModule = new Map()
for (const row of sorted) {
  const module = row.module === '' ? '<builtin>' : row.module
  if (!byModule.has(module)) byModule.set(module, [])
  byModule.get(module).push(row)
}

console.log(
  `[${NAME}] ${rows.length} operators of ${PROGRAM}'s modules: ${withRecord.length} have a record, ` +
    `${stated.length} state \`uses\` in it, ${sorted.length} state nothing` +
    (silent.length !== sorted.length ? ` (${silent.length} declaration rows)` : ''),
)
console.log(
  `[${NAME}] the table's declaration and the record disagree on ${disagreed.length} operator(s)`,
)
for (const row of disagreed.slice(0, 20)) {
  console.log(
    `[${NAME}]   ${row.module} ${row.uri}: purity=${row.purity} clause_pure=${row.clause_pure}`,
  )
}
if (disagreed.length > 20) {
  console.log(`[${NAME}]   ... and ${disagreed.length - 20} more`)
}

if (sorted.length) {
  console.log(`[${NAME}] the operators to declare (module:uri), ${sorted.length} of them:`)
  for (const [module, list] of [...byModule].sort((a, b) => a[0].localeCompare(b[0]))) {
    console.log(`[${NAME}]   ${module} (${list.length})`)
    for (const row of list) console.log(`[${NAME}]     ${row.uri}`)
  }
}

console.log(
  `[${NAME}] check-operator-uses: ${sorted.length} operator(s) with no effect statement at either entrance`,
)
if (disagreed.length) {
  console.log(
    `[${NAME}] check-operator-uses: ${disagreed.length} operator(s) whose declaration and record disagree`,
  )
  process.exit(3)
}
process.exit(sorted.length ? 1 : 0)