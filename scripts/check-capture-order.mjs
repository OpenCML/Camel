#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The capture tables are walked in the program's order: one program translated twice gives one
 * byte-identical document, and two writings of it that differ only in the order two declarations
 * are written give one document too.
 *
 * The tables this reads are the compile-time ones keyed by a node handle, `runtime::DraftNodeRef`,
 * whose hash is `std::hash<const void*>(draft) * 31 + id` (`kernel/include/camel/runtime/node_ref.h`) -- an
 * address. Two of them are walked to decide what a body builds: `Builder::captureOriginOf_` and
 * `Builder::machinePortOrigin_` are unioned to answer "these two handles are one value", and
 * `builder_expr.cpp#collapseNonEscapingVariableReads` / `#dropCarriedVariableUnits` fold and drop
 * the units of `Builder::variableReadSource_`, `Builder::carriedVersions_` and
 * `Builder::valueCapturedCells_` in the order they were collected. A graph numbers its nodes in
 * creation order, so a walk that followed the allocator numbered one body differently from run to
 * run: the reading that started this was "node 1/2/3 differ between runs, close to uniform" (wins1,
 * 2026-11-20).
 *
 * --- the two readings ------------------------------------------------------------------------
 * 1. The same file, translated three times. Each run is a fresh process, so the tables are filled in
 *    three different orders -- the allocator's, which is the order the program itself cannot state
 *    and the one this defect lives on. The document is `std::jgir`, version 1, and it is compared as
 *    bytes with nothing taken out: the three runs read one file, so every position in it is the same
 *    position, and any difference at all is the walk's. This is the strict reading, and it is the
 *    one that needs no second program.
 * 2. Two writings of the program that differ only in the order two `var` declarations are written --
 *    a real input difference, not an instrument: nothing in the compiler exists for this reading,
 *    and no environment variable decides which order it sees. Their documents cannot be the version
 *    1 display document, which records the position of every node, and those positions ARE the
 *    input difference. They are compared through `std::jgir canonical=true` -- version 2 of the
 *    format, the shape `lib/std/gir/json/gir_canonical.cpp` writes and the equivalence checker
 *    reads, which carries no line, column or file name, so two writings of one program can be
 *    compared as bytes at all. `canonical=true` is a parameter of the pass, not a switch of this
 *    reading.
 *
 * A program that does not translate gives no reading and is refused (exit 2) rather than reported:
 * a refusal read as "the documents agree" is the one answer this case must never give.
 *
 * --- what this check does NOT prove (measured, 2026-11-20, on the build host) ----------------
 * It is a regression guard, not a proof that the walks' key is load-bearing, and the arm that was
 * asked for came back green: `walksBefore` cut to `left.epoch < right.epoch`, and then the sorts of
 * `dropCarriedVariableUnits` deleted outright (the pre-`f26529338` walk) both still give one
 * document. A fresh process on that host lays the same nodes at the same addresses, so the address
 * order and the program order coincide there for these tables and no reading of this shape can see
 * the difference. `f26529338` was measured on a run where they did not coincide. A case that
 * separates them has to be found by a reading; until one is, this file says that two writings agree
 * and nothing stronger.
 *
 * Usage: node scripts/check-capture-order.mjs [--camel <path>] [--reading <path>]
 *
 * `--reading <path>` also writes the lines below to that path. A passing test's stdout is not shown
 * by the plan runner, and a lane reporting the sha256 of an acceptance needs the number it measured
 * rather than the one it expected; the file is the reading, so it is written where a run can be
 * asked for it back.
 */

import crypto from 'node:crypto'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../test/tools/camel-arg.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const NAME = 'capture-order'
const RUNS = 3
const DISPLAY = 'std::jgir'
const CANONICAL = 'std::jgir canonical=true'

// One program, two writings. `A` and `B` differ only in the order of two `var` declarations: the
// cells are made in one order or the other, the two closures capture both of them, and the branch
// carries one of them. Everything a reader sees -- the values, the statements, the printed line --
// is the same.
const DECLARATIONS = [
    ['    var first: int = 1', '    var second: int = 2'],
    ['    var second: int = 2', '    var first: int = 1'],
]
const BODY = [
    '    if first > 0 then { sync { first = 3 } } else { sync { second = 4 } }',
    '    let sum = (): int => { return first + second }',
    '    let diff = (): int => { return first - second }',
    '    println(sum(), diff(), first, second)',
    '    return 0',
]
const program = (order) => ['func main(): int sync {', ...DECLARATIONS[order], ...BODY, '}', ''].join('\n')

const usage = (message) => {
    console.error(`${NAME}: ${message}`)
    console.error(`usage: node scripts/check-capture-order.mjs [--camel <path>] [--reading <path>]`)
    process.exit(2)
}

const argv = process.argv.slice(2)
const taken = takeCamel(argv, ROOT, usage)
const camel = taken.camel
const INSTALL = path.dirname(path.dirname(camel))
let readingPath = null
for (let i = 0; i < taken.rest.length; i++) {
    if (taken.rest[i] === '--reading' && i + 1 < taken.rest.length) {
        readingPath = path.resolve(taken.rest[++i])
    } else {
        usage(`unknown argument ${taken.rest[i]}`)
    }
}
if (!fs.existsSync(camel)) {
    usage(`no camel binary at ${camel}: --camel names the install a run measures, or build this tree once`)
}

const sha256 = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex')

/** One translation of one file, with the install this run measures. */
function translate(file, pass) {
    return new Promise((resolve) => {
        const child = spawn(camel, [file, pass], {
            cwd: ROOT,
            env: { ...process.env, CAMEL_HOME: INSTALL },
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        const out = []
        const err = []
        child.stdout.on('data', (chunk) => out.push(chunk))
        child.stderr.on('data', (chunk) => err.push(chunk))
        child.on('close', (code) =>
            resolve({ file, code, stdout: Buffer.concat(out), stderr: Buffer.concat(err).toString('utf8') }),
        )
    })
}

/** A translation that gave no document is refused, and says what the compiler said. */
function document(reading) {
    if (reading.code === 0 && reading.stdout.length > 0) return reading.stdout
    console.error(`${NAME}: ${path.basename(reading.file)} did not translate (exit ${reading.code})`)
    if (reading.stderr !== '') console.error(reading.stderr.trimEnd())
    const said = reading.stdout.toString('utf8').trimEnd()
    if (said !== '') console.error(said.split('\n').slice(0, 24).join('\n'))
    process.exit(2)
}

const dir = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-capture-order-'))
fs.mkdirSync(dir, { recursive: true })
const lines = []
let failed = false

// Reading 1: one file, three processes.
const same = path.join(dir, 'capture_order.cml')
fs.writeFileSync(same, program(0))
const runs = []
for (let i = 0; i < RUNS; i++) runs.push(document(await translate(same, DISPLAY)))
for (const [at, bytes] of runs.entries()) {
    lines.push(`${NAME} run=${at + 1} sha256=${sha256(bytes)} bytes=${bytes.length}`)
}
for (let at = 1; at < runs.length; at++) {
    if (!runs[at].equals(runs[0])) {
        console.error(
            `${NAME}: run 1 and run ${at + 1} of ${path.basename(same)} wrote two documents ` +
                `(${sha256(runs[0])} and ${sha256(runs[at])}). One file, one pass, three processes: nothing ` +
                'in the input separates them, so what did is a compile-time table walked in the order it ' +
                'was filled -- the address order, which is a different order in every run. The walks are ' +
                'sorted by `walksBefore` (compile/src/gir/builder_helpers.h): the graph a node belongs to, ' +
                'then the id that graph gave it.',
        )
        fs.writeFileSync(path.join(dir, 'capture_order.run1.jgir.json'), runs[0])
        fs.writeFileSync(path.join(dir, `capture_order.run${at + 1}.jgir.json`), runs[at])
        failed = true
        break
    }
}
if (!failed) {
    console.log(`${NAME}: ${RUNS} translations of one file write one document (${sha256(runs[0])}, ${runs[0].length} byte(s))`)
}

// Reading 2: one program, two writings.
const writings = []
for (const order of [0, 1]) {
    const file = path.join(dir, order === 0 ? 'capture_order_a.cml' : 'capture_order_b.cml')
    fs.writeFileSync(file, program(order))
    writings.push(document(await translate(file, CANONICAL)))
}
for (const [at, bytes] of writings.entries()) {
    lines.push(
        `${NAME} ${at === 0 ? 'a' : 'b'} order=${at === 0 ? 'first-then-second' : 'second-then-first'} ` +
            `sha256=${sha256(bytes)} bytes=${bytes.length}`,
    )
}
if (!writings[0].equals(writings[1])) {
    console.error(
        `${NAME}: the two writings translated to two documents (${sha256(writings[0])} and ` +
            `${sha256(writings[1])}). They are one program: the same declarations in another order, the ` +
            'same statements, the same printed line. A compile-time table walked in the order it was ' +
            'filled makes the compiled graph a function of the order the source happened to write its ' +
            'cells in, which is not a property of the program.',
    )
    fs.writeFileSync(path.join(dir, 'capture_order_a.jgir.json'), writings[0])
    fs.writeFileSync(path.join(dir, 'capture_order_b.jgir.json'), writings[1])
    failed = true
} else {
    console.log(`${NAME}: the two writings write one document (${sha256(writings[0])}, ${writings[0].length} byte(s))`)
}

for (const line of lines) console.log(line)
if (readingPath !== null) fs.writeFileSync(readingPath, `${lines.join('\n')}\n`)
if (failed) {
    console.error(`${NAME}: the documents are in ${dir} for the diff that says which node moved`)
    process.exit(1)
}
console.log(`${NAME}: the two insertion orders write one document`)
process.exit(0)