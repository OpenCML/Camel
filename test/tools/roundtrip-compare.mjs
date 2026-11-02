#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The `std::cml` round trip, measured as **behaviour**: a program is printed, the printed program is
 * run, and its `rc` and `stdout` are compared with the origin's. Text equality is not the question
 * here -- the printed source is regenerated and looks nothing like the origin -- so what this tool
 * compares is what a user would notice: the exit status and every byte of standard output.
 *
 * The eight cases of the author's defect list (A7) are the ones that behaved differently after a
 * print; a ninth (`compile/escapes/raw_roundtrip`) is the printer's raw form for a string with
 * several backslashes, which is a literal rather than a graph shape. They are green today, and
 * this tool is the lock, not the fix. Its own discriminability is
 * checked by `--negative <case>`: the printed body has two of its `println` statements swapped and
 * the tool must report a difference -- a harness that cannot see a swap it planted itself would
 * report "identical" for everything, which is the failure mode this file exists to avoid.
 *
 * Usage: node roundtrip-compare.mjs [--camel <path>] [--negative <case>]
 *
 * One line per case, always, so that "9 of 9 identical" is a reading and not an absence:
 *   case=<name> rc=<n> stdout=identical
 *   case=<name> rc=<n> stdout=differs at line k
 */

import { spawnSync } from 'node:child_process'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))

/// The repository root: the first ancestor of this tool that holds the standard library. The runner
/// copies a case into a scratch tree, so neither `process.cwd()` nor `HERE` can be assumed to be it.
function repositoryRoot(start) {
    let dir = path.resolve(start)
    for (;;) {
        try {
            readFileSync(path.join(dir, 'stdlib', 'std.cml'), 'utf8')
            return dir
        } catch {
            const parent = path.dirname(dir)
            if (parent === dir) throw new Error(`no stdlib/std.cml above ${start}`)
            dir = parent
        }
    }
}

const ROOT = repositoryRoot(HERE)

function camelBinary() {
    const explicit = argv.get('--camel')
    if (explicit) return path.resolve(explicit)
    const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

const argv = new Map()
for (let i = 2; i < process.argv.length; i += 1) {
    const word = process.argv[i]
    if (!word.startsWith('--')) throw new Error(`unexpected argument '${word}'`)
    const value = process.argv[i + 1]
    if (value === undefined || value.startsWith('--')) throw new Error(`${word} needs a value`)
    argv.set(word, value)
    i += 1
}

const CAMEL = camelBinary()
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest') }

/// The eight cases of A7. Each is a `test/cases/**.cml` path; the printed program goes to a scratch
/// directory of our own, so the tree the runner handed us is not written to.
const CASES = [
    'consistency/fail_never/statement_after_fail',
    'effects/writer_table_through_callee',
    'linear/runtime/var_read_before_region',
    'linear/runtime/var_read_before_write',
    'linear/runtime/order_edges_regions',
    'parse/rulings/power',
    'trans/cpp/exec/print_formats',
    'consistency/values/float_special',
    'compile/escapes/raw_roundtrip',
]

const SCRATCH = path.join(tmpdir(), `camel-roundtrip-${process.pid}`)

function run(args, timeout = 120000) {
    const proc = spawnSync(CAMEL, args, { cwd: ROOT, env, encoding: 'utf8', timeout, maxBuffer: 64 << 20 })
    return { status: proc.status, stdout: proc.stdout ?? '', stderr: proc.stderr ?? '' }
}

/// A path in a diagnostic is the one thing that legitimately differs between the two runs: the
/// printed program lives in the scratch directory. Both spellings become `<PATH>` so that a
/// difference this tool reports is a difference in behaviour.
///
/// A **position** inside the program differs too, and it must not count as a behaviour change: the
/// printed source is reformatted, so `fail<'NoPremise'>` sits at line 10 in the origin and line 3 in
/// the printed file. A position (`:<line>:<column>`) becomes `:<AT>`, and the rendered snippet below
/// it -- the `   |` gutter, the numbered source line and its caret row -- becomes `<SNIPPET>`. What is
/// left is still compared byte for byte: the message, the code, the exit status, and every line the
/// program itself prints.
function normalize(text) {
    let out = text.replace(/\r\n/g, '\n')
    // Every spelling of "a Camel source file" becomes `<PATH>`, and the position that follows one
    // becomes `:<AT>`: the origin is `test/cases/...`, the printed body is `tmp/print_...` or an
    // absolute scratch path, and all three name the same program at a different line. Nothing else
    // about the line is touched, so a change of message, code or wording still shows.
    out = out.replace(/[^\s:()'"|]+\.cml/g, '<PATH>')
    out = out.replace(/<PATH>:\d+:\d+/g, '<PATH>:<AT>')
    out = out
        .split('\n')
        .map((line) => {
            // The gate row and the source row of a rendered snippet, and the caret row under it.
            if (/^\s*\|?\s*$/.test(line) && line.includes('|')) return '<SNIPPET>'
            if (/^\s*\d+ \|/.test(line)) return '<SNIPPET>'
            if (/^\s*\|\s*\^+/.test(line)) return '<SNIPPET>'
            return line.replace(/\s+\|\s*$/, ' <SNIPPET>')
        })
        .join('\n')
    return out
}

function firstDifferingLine(a, b) {
    const left = a.split('\n')
    const right = b.split('\n')
    for (let i = 0; i < Math.max(left.length, right.length); i += 1) {
        if (left[i] !== right[i]) return i + 1
    }
    return 0
}

function printOf(name) {
    const source = path.join(ROOT, 'test', 'cases', `${name}.cml`)
    const printed = run([source, 'std::cml'])
    if (printed.status !== 0) {
        return { error: `std::cml refused it (exit ${printed.status}): ${printed.stderr.split('\n')[0]}` }
    }
    // The printed body keeps the origin's **file name** and only its directory changes: a diagnostic
    // label is drawn from the file name (`(statement_after_fail)`), so a renamed copy would put a
    // difference of spelling into a comparison of behaviour.
    const target = path.join(SCRATCH, name.split('/').join('_'), `${path.basename(name)}.cml`)
    mkdirSync(path.dirname(target), { recursive: true })
    writeFileSync(target, printed.stdout)
    return { source, target, text: printed.stdout }
}

/// One case, both runs, one line out. `expect` lets the negative mode flip what counts as success.
function measure(name) {
    const printed = printOf(name)
    if (printed.error) return { name, ok: false, line: `case=${name} stdout=unmeasurable (${printed.error})` }
    const origin = run([printed.source])
    const again = run([printed.target])
    const left = normalize(origin.stdout)
    const right = normalize(again.stdout)
    const at = left === right ? 0 : firstDifferingLine(left, right)
    const rcSame = origin.status === again.status
    const ok = at === 0 && rcSame
    const reading = at === 0 ? 'identical' : `differs at line ${at}`
    const rcNote = rcSame ? '' : ` (the printed program exited ${again.status}, the origin ${origin.status})`
    return {
        name,
        ok,
        line: `case=${name} rc=${origin.status} stdout=${reading}${rcNote}`,
        at,
        printed,
        origin,
        again,
        left,
        right,
        // The pair the reading was taken from, before and after normalization, for the line that
        // differs: a line this tool says differs must be shown, so that "the normalizer ate it" and
        // "it was skipped" cannot look the same in a report.
        detail:
            at === 0
                ? []
                : [
                      `line ${at} before: ${JSON.stringify((origin.stdout.split('\n')[at - 1] ?? '').trimEnd())}`,
                      `line ${at} after : ${JSON.stringify((left.split('\n')[at - 1] ?? '').trimEnd())}`,
                      `the other side: ${JSON.stringify((right.split('\n')[at - 1] ?? '').trimEnd())}`,
                  ],
    }
}

/// The negative: swap the first two `println` statements of the printed body and require the tool to
/// see it. A swap of two prints changes the order of two lines and nothing else, which is exactly the
/// class of difference A7's order-edge cases are about.
function negative(name) {
    const printed = printOf(name)
    if (printed.error) {
        console.log(`case=${name} negative=unmeasurable (${printed.error})`)
        return 1
    }
    const lines = printed.text.split('\n')
    const all = lines.map((line, index) => (line.includes('println(') ? index : -1)).filter((index) => index >= 0)
    // Two prints only change their order if both run, so the pair has to be siblings: same
    // indentation, same block. The first indentation that has two of them is the pair to swap.
    const indentOf = (index) => (lines[index].match(/^\s*/) || [''])[0].length
    let a = -1
    let b = -1
    for (const index of all) {
        const twin = all.find((other) => other !== index && indentOf(other) === indentOf(index))
        if (twin !== undefined) {
            a = Math.min(index, twin)
            b = Math.max(index, twin)
            if (lines[a] !== lines[b]) break
        }
    }
    if (a < 0 || b < 0 || lines[a] === lines[b]) {
        console.log(`case=${name} negative=unmeasurable (no two sibling println statements that print different text: ${all.length} println statement(s))`)
        return 1
    }
    ;[lines[a], lines[b]] = [lines[b], lines[a]]
    const swapped = path.join(SCRATCH, name.split('/').join('_'), 'swapped', `${path.basename(name)}.cml`)
    mkdirSync(path.dirname(swapped), { recursive: true })
    writeFileSync(swapped, lines.join('\n'))

    const origin = run([printed.source])
    const again = run([swapped])
    const left = normalize(origin.stdout)
    const right = normalize(again.stdout)
    const at = left === right ? 0 : firstDifferingLine(left, right)
    if (at === 0) {
        console.log(`case=${name} negative=NOT SEEN rc=${origin.status} stdout=identical -- a planted swap of two println statements reads as identical, so this tool cannot be trusted`)
        return 1
    }
    console.log(`case=${name} negative=seen rc=${origin.status} stdout=differs at line ${at} (planted: println at line ${a + 1} and line ${b + 1} swapped)`)
    return 0
}

const wanted = argv.get('--negative')
if (wanted) process.exit(negative(wanted))

mkdirSync(SCRATCH, { recursive: true })
let differs = 0
for (const name of CASES) {
    const reading = measure(name)
    console.log(reading.line)
    for (const line of reading.detail ?? []) console.log('  ' + line)
    if (!reading.ok) differs += 1
}
console.log(`ROUNDTRIP cases=${CASES.length} identical=${CASES.length - differs} differs=${differs}`)
process.exit(differs === 0 ? 0 : 1)