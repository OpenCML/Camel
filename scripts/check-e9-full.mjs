#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The E9 differential at the tier its ratchet is defined at, as a check `camel-remote.ps1` can
// start by name.
//
// `test/tools/e9diff/e9diff.py` is the harness the three numbers of a merge are read from, and its
// own README says why it has no plan of the two slower tiers: a full run is 25 minutes on a quiet
// host and much longer on this one, and "a gate that starts one by itself is a gate people learn to
// skip". That reasoning is right, and it left the run reachable only from a hand-written shell on
// the host -- which, on the books, is the same as not being reachable at all. This script is the
// missing half: the invocation, its arguments and its exit code, in one place, so that
// `-Check e9-full` is the whole command and nobody retypes the tool's command line.
//
// It adds no judgement of its own. The tool compares the run with `test/tools/e9diff/baseline.json`
// and exits 1 when the comparison fails -- a program worse than the baseline on the intersection of
// the two runs' programs, a program the run did not reach, or an intersection and a per-program
// record that cannot be computed at all (a comparison that cannot be made is not a pass); it exits 0
// when the baseline held or when the run is one this ratchet does not cover (a slow run, or another
// seed range, both of which say so). This script passes that code through, prints the tool's own
// output verbatim, and adds one machine-readable line naming the three numbers so a lane can paste
// them.
//
// Usage (what the table entry passes, plus the optional overrides):
//   node scripts/check-e9-full.mjs --matrix full [--camel out/latest/bin/camel] [--jobs 4]
//   E9_OUT=/tmp/e9-full   the directory the run writes (must be outside the tree)
//   E9_PYTHON=python3     the interpreter that runs the tool
//
// The three numbers are programs, not cells: `wrong` counts the programs whose output differs from
// the generator's own evaluator under at least one cell, and likewise `refused` / `crashed`.

import { spawn } from 'node:child_process'
import { mkdirSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import process from 'node:process'

const TOOL = 'test/tools/e9diff/e9diff.py'

// The line a `--record` run writes into `baseline.json` beside the numbers. It is here and not in the
// table because the table's words are handed over unquoted, and a sentence is not a word; and it is
// not optional, because a zero that moves without saying why is a regression turned into a
// qualification. Whoever records again edits this line, and the diff is the record of what changed.
const RECORD_NOTE = 'recorded 2026-11-16, after RUN_ERROR appeared as a class (198 cells in the '
    + '500-seed run of that day) and over a program population the generator had changed: 273 of the '
    + '500 seeds are now rejected and 227 reach the matrix, where the 10/17/3 baseline had 180 and '
    + '320. The crash class rose from 3 programs / 3 cells to 10 / 56 before this file was written. '
    + 'This is a new zero, not a statement that the old numbers did not happen: do not subtract '
    + 'these numbers from 10/17/3, the two are not the same measurement. `crashed 10` is the zero '
    + 'recorded after it rose to 10: it is not a fixed 10 and it is not an acceptable 10 -- those '
    + 'two look identical in a file, and only this sentence tells them apart. '
    + 'crashed 10 是「涨到 10 之后才录的」零点；它不是「修好的 10」，也不是「可接受的 10」。'

function parseArgs(argv) {
    const opts = { matrix: 'full', camel: '', jobs: '4', n: '', record: false, note: '' }
    for (let i = 0; i < argv.length; i += 1) {
        const word = argv[i]
        if (word === '--matrix' && i + 1 < argv.length) { opts.matrix = argv[++i]; continue }
        if (word === '--camel' && i + 1 < argv.length) { opts.camel = argv[++i]; continue }
        if (word === '--jobs' && i + 1 < argv.length) { opts.jobs = argv[++i]; continue }
        // `--n` is not passed by the table: it is how a lane iterates on this entry without paying
        // for 500 seeds, and the run it makes says in its own line that it was a short run.
        if (word === '--n' && i + 1 < argv.length) { opts.n = argv[++i]; continue }
        // `--record` writes this run's numbers as the new baseline, and `--note` says why in the
        // file. Neither is something the gate does by itself: a zero that moves without somebody
        // asking for it is a ratchet that cannot fail.
        if (word === '--record') { opts.record = true; continue }
        if (word === '--note' && i + 1 < argv.length) { opts.note = argv[++i]; continue }
        // Anything else is the gate's business, not this script's: the table's `args` and the
        // `--camel` the script appends for a `compiler` entry are the whole surface, and a word this
        // does not read would otherwise be handed to a tool that would refuse it.
    }
    return opts
}

function stamp() {
    const now = new Date()
    const two = (n) => String(n).padStart(2, '0')
    return `${now.getFullYear()}${two(now.getMonth() + 1)}${two(now.getDate())}-${two(now.getHours())}${two(now.getMinutes())}${two(now.getSeconds())}`
}

function run(command, args) {
    return new Promise((resolve) => {
        const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'] })
        let stdout = ''
        let stderr = ''
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdout.on('data', (chunk) => { stdout += chunk; process.stdout.write(chunk) })
        // The gate keeps a check entry's stdout and drops its stderr, so the child's stderr is
        // forwarded on stdout as well: a traceback that nobody sees is a red with no reason, and
        // this entry is the one place the tool's own words reach the lane.
        child.stderr.on('data', (chunk) => { stderr += chunk; process.stdout.write(chunk) })
        child.on('error', (error) => resolve({ code: 2, stdout, stderr: String(error) }))
        child.on('close', (code, signal) => resolve({ code: code === null ? 2 : code, signal, stdout, stderr }))
    })
}

const opts = parseArgs(process.argv.slice(2))
const out = process.env.E9_OUT || path.join(tmpdir(), `e9-${opts.matrix}-${stamp()}`)
mkdirSync(out, { recursive: true })

// `--camel` is a global option of the tool and has to stand before the subcommand; the gate appends
// it after the script's own arguments, so it is moved here rather than forwarded in place.
const args = []
// Absolute, because the tool hands this path to every cell it starts and a cell runs with its
// program's own directory as its working directory: the gate's `out/latest/bin/camel` is relative to
// the checkout, and a relative one reaches the first cell as "no such file or directory" and dies
// there with a traceback instead of a reading.
if (opts.camel) args.push('--camel', path.resolve(opts.camel))
args.push('run', '--matrix', opts.matrix, '--out', out, '--jobs', opts.jobs)
if (opts.n) args.push('--n', opts.n)
if (opts.record) args.push('--update-baseline', '--baseline-note', opts.note || RECORD_NOTE)

const python = process.env.E9_PYTHON || 'python3'
console.log(`[e9-full] ${python} ${TOOL} ${args.join(' ')}`)
const result = await run(python, [TOOL, ...args])

const numbers = /\[E9\] wrong (\d+)\s+refused (\d+)\s+crashed (\d+)/.exec(result.stdout)
// The verdict in the tool's own words: one phrase per outcome it can print, most decisive first.
// The order is not the order they appear in the output -- a run whose population moved prints
// `not compared` *and* `the comparison cannot be made`, and the second is the one that decides.
const VERDICT_PHRASES = [
    'the comparison cannot be made',
    'the intersection is empty',
    'ABOVE THE BASELINE',
    'below the baseline',
    'baseline held',
    'recorded as the new zero',
    'not compared',
]
const verdict = VERDICT_PHRASES.find((phrase) => result.stdout.includes(phrase))
const seconds = /\[E9\] (\d+) s,/.exec(result.stdout)
// The seed range the tool itself printed, so a short run cannot be read as the ratchet's run.
const seeds = /\[E9\] matrix \w+: \d+ pipelines, \d+ cells per program, seeds ([\d.]+)/.exec(result.stdout)
// The population, because two runs are only comparable over the same one: the fitness of a class is
// read against the programs it was measured on, and the tool refuses the comparison when it moved.
const population = /\[E9\] seeds [\d.]+: (\d+) programs, (\d+) generator rejects, (\d+) in the matrix/.exec(result.stdout)

// One line, and every field on it is the tool's own reading: a number this script did not parse is
// `unknown` rather than a zero it never measured.
console.log(
    `[e9-full] matrix=${opts.matrix} seeds=${seeds ? seeds[1] : 'unknown'} ` +
    `rejects=${population ? population[2] : 'unknown'} ` +
    `in_matrix=${population ? population[3] : 'unknown'} ` +
    `wrong=${numbers ? numbers[1] : 'unknown'} ` +
    `refused=${numbers ? numbers[2] : 'unknown'} ` +
    `crashed=${numbers ? numbers[3] : 'unknown'} ` +
    `seconds=${seconds ? seconds[1] : 'unknown'} ` +
    `verdict=${verdict ? verdict.toLowerCase().replaceAll(' ', '-') : 'unknown'} ` +
    `exit=${result.code}${result.signal ? ` signal=${result.signal}` : ''} out=${out}`,
)
if (opts.record && result.code === 0) {
    // A recording run changes a file in the tree, and a lane's tree is not the one that ran: the
    // tool writes it on the host. So the file is printed here, between markers, and carried back
    // byte for byte rather than retyped -- a baseline retyped from a summary line is a baseline
    // nobody can tell from one somebody edited.
    const written = readFileSync('test/tools/e9diff/baseline.json', 'utf8')
    console.log('[e9-full] baseline-begin')
    process.stdout.write(written.endsWith('\n') ? written : `${written}\n`)
    console.log('[e9-full] baseline-end')
}
if (!numbers) {
    // A run that printed no three numbers measured nothing, and a green over nothing is the failure
    // this line exists to prevent: it is a red with its reason, not a pass. The child's own last
    // words are printed first, because the caller reads one line from this script and would
    // otherwise have to guess why a row went red.
    const tail = (result.stderr || '(nothing on stderr)').trim().split('\n').slice(-25).join('\n')
    console.log(`[e9-full] the tool printed no "wrong/refused/crashed" line; nothing was measured`)
    console.log(`[e9-full] last words from ${python}:`)
    console.log(tail)
    process.exit(result.code === 0 ? 2 : result.code)
}
process.exit(result.code)
