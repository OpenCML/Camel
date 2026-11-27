#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One reading over the whole corpus: what `core.arrangeable` answers and what
// `runtime::nodeIsFreelySchedulable` answers, for every place both graphs call a node.
//
// Why this is a tool and not a plan entry: it runs `camel <case> test::facts_compare` for every
// `.cml` under `test/cases` -- 4037 files, about two minutes at `--jobs 6` -- and the numbers it
// prints move whenever the compiler moves (a node added anywhere moves `nodes`, `agree` and `diff`).
// A plan entry that asserted those numbers would be red on every unrelated change, and one that
// asserted nothing would be decoration. So the numbers live in the reports that cite them, and this
// file is the command that produces them; `test/plans/feat/tools/facts_readers_sweep.plan.toml`
// runs it in the slow layer, where a change to the pass or to the corpus is meant to be looked at.
//
// What it prints, in this order:
//
//   FACTS_READERS_PROBE root=… camel=… built=… tip=…     the tool, the binary and the tree the
//                                                        numbers below belong to (one line, so a
//                                                        reading copied out of a log carries them)
//   FACTS_READERS_SWEEP cases= read= skipped= silent= …   the totals: cases swept, cases the pass
//                                                        answered at all (`read`), cases it declined
//                                                        (`skipped`), cases that said nothing
//                                                        (`silent`), and the sums of the per-case
//                                                        `readers` lines
//   FACTS_READERS_KIND kind=… nodes=… both_free=… …       the same sums split by node kind, so a
//                                                        kind that is "freely arrangeable but not
//                                                        schedulable" has a number of its own
//   FACTS_READERS_DIFF <path>: <line>                     every disagreeing node, as the pass
//                                                        printed it
//
// `silent` is the reading that is a defect when it is not zero: a case the pass neither answered
// nor declined means the program never reached the pass (a compile error is normal and lands in
// `skipped`), and a corpus whose `skipped` grows without `silent` growing says so.
//
// Every case runs in its own scratch directory (`fs.mkdtempSync`), never in the checkout: cases
// that export a model (`onnx::export_model out=m.onnx`) write beside the program, and before this the
// sweep left a dozen files in the tree it measured.
//
// Usage:
//   node test/tools/probe-facts-readers.mjs                      # the whole corpus, ~2 min
//   node test/tools/probe-facts-readers.mjs --jobs 8             # wider
//   node test/tools/probe-facts-readers.mjs --only cases/loops/  # a subset by path substring
//   node test/tools/probe-facts-readers.mjs --limit 200          # the first 200, for a smoke run
//   node test/tools/probe-facts-readers.mjs --camel <path>       # a binary other than $CAMEL_HOME's
//
// The binary is `$CAMEL_HOME/bin/camel` when that exists, else `camel` on `PATH`. The reading is
// only worth what the line 1 provenance says: the machine's binary and the tree it was built from.
//
// The readings this produced, with `test::facts_compare` answering in the shape the gate commit
// introduced: `cases=4037 read=2747 skipped=1290 silent=0 nodes=211894 agree=201518 diff=10376
// no_fact=0 kinds=8`, with `kind=gate … both_kept=3918 kind_free_pred_kept=0`; before that commit,
// `diff=10471` and `kind_free_pred_kept=95`, the 95 gate nodes the four terms had called movable.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const CASES = path.join(ROOT, 'test', 'cases')

// The kinds in the order `test.cmo`'s `nodeKindName` names them; `gate` is last because the kind's
// answer for it is the one this sweep was written to see. A kind this list does not know is printed
// after these, in the order it first appeared, rather than dropped.
const KIND_ORDER = ['data', 'port', 'oper', 'func', 'call', 'brch', 'loop', 'gate']

function usage() {
    const text = fs.readFileSync(fileURLToPath(import.meta.url), 'utf8')
    const start = text.indexOf('// Usage:')
    const end = text.indexOf('// The binary is')
    process.stdout.write(text.slice(start, end).replace(/^\/\/ ?/gm, ''))
    process.stdout.write(
        [
            '',
            'It sweeps every `.cml` under `test/cases` (4037 files today), ~2 min at --jobs 6.',
            'It is not part of the fast layer. `--tier slow test/plans/feat/tools/facts_readers_sweep.plan.toml`',
            'runs the same sweep through the runner, which adds the provenance line the runner prints.',
            '',
        ].join('\n')
    )
}

function parseArgs(argv) {
    const opts = { jobs: 6, only: null, limit: 0, camel: null }
    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i]
        if (arg === '--help' || arg === '-h') {
            usage()
            process.exit(0)
        } else if (arg === '--jobs') {
            opts.jobs = Number(argv[++i])
        } else if (arg === '--only') {
            opts.only = argv[++i]
        } else if (arg === '--limit') {
            opts.limit = Number(argv[++i])
        } else if (arg === '--camel') {
            opts.camel = argv[++i]
        } else {
            process.stderr.write(`probe-facts-readers: unexpected argument '${arg}'\n`)
            process.exit(2)
        }
    }
    if (!Number.isFinite(opts.jobs) || opts.jobs < 1) {
        process.stderr.write('probe-facts-readers: --jobs needs a positive number\n')
        process.exit(2)
    }
    if (!Number.isFinite(opts.limit) || opts.limit < 0) {
        process.stderr.write('probe-facts-readers: --limit needs a number (0 is every case)\n')
        process.exit(2)
    }
    return opts
}

function camelBinary(opts) {
    if (opts.camel) {
        return path.resolve(opts.camel)
    }
    const name = process.platform === 'win32' ? 'camel.exe' : 'camel'
    const home = process.env.CAMEL_HOME
    if (home) {
        const candidate = path.join(home, 'bin', name)
        if (fs.existsSync(candidate)) {
            return candidate
        }
    }
    return name
}

function findCases(dir, out) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            findCases(full, out)
        } else if (entry.name.endsWith('.cml')) {
            out.push(full)
        }
    }
    return out
}

function provenance(binary) {
    let tip = 'unknown'
    const head = spawnSync('git', ['rev-parse', '--short=10', 'HEAD'], { cwd: ROOT, encoding: 'utf8' })
    if (head.status === 0) {
        tip = head.stdout.trim()
        const dirty = spawnSync('git', ['status', '--porcelain'], { cwd: ROOT, encoding: 'utf8' })
        if (dirty.status === 0) {
            const count = dirty.stdout.split('\n').filter((line) => line.trim() !== '').length
            if (count > 0) {
                tip = `(dirty: ${count} files)`
            }
        }
    }
    let built = 'unknown'
    try {
        built = new Date(fs.statSync(binary).mtimeMs).toISOString().replace(/\.\d+Z$/, 'Z')
    } catch {
        // A binary this run cannot stat is named as such; the sweep below will fail to spawn it and
        // `silent` will say so rather than the reading being taken from nothing.
    }
    return `FACTS_READERS_PROBE root=${ROOT} camel=${binary} built=${built} tip=${tip}`
}

function runCase(binary, file) {
    return new Promise((resolve) => {
        const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-readers-'))
        // `stdin` is /dev/null, not inherited: a case that reads its input would otherwise wait on
        // the runner's stdin forever. That is not hypothetical -- the first run of this tool as a
        // plan entry sat at the 1400 s bound with no line printed, because one case and then the
        // whole sweep were waiting for a read that never returns.
        //
        // The per-case clock is the other half of the same guard: a case that waits on a device
        // (or on anything else that does not answer) is killed, counted as `silent`, and named in
        // the summary rather than stopping the corpus reading. The bound is 180 s and not 30: the
        // first bound written here was 30 s, and three cases of the corpus take longer than that
        // (the sweep's 4037 cases are not one cost class), so the sweep counted them `silent` and
        // read `diff=10370` where the corpus has 10376 -- a bound tighter than the slowest case is
        // a reading of the bound and not of the corpus.
        const child = spawn(binary, [file, 'test::facts_compare'], {
            cwd: scratch,
            env: process.env,
            windowsHide: true,
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        let killed = false
        const clock = setTimeout(() => {
            killed = true
            child.kill('SIGKILL')
        }, 180000)
        let stdout = ''
        let stderr = ''
        child.stdout.on('data', (chunk) => {
            stdout += chunk
        })
        child.stderr.on('data', (chunk) => {
            stderr += chunk
        })
        const finish = (failed) => {
            clearTimeout(clock)
            try {
                fs.rmSync(scratch, { recursive: true, force: true })
            } catch {
                // A scratch directory that cannot be removed is not a reading; the sweep does not
                // fail for it.
            }
            resolve({ stdout, stderr, failed: failed || killed })
        }
        child.on('error', () => finish(true))
        child.on('close', () => finish(false))
    })
}

const READERS = /^FACTS_COMPARE kind=(\S+) readers nodes=(\d+) agree=(\d+) diff=(\d+) no_fact=(\d+)$/m
const KIND = /^FACTS_COMPARE readers_kind kind=(\w+) nodes=(\d+) both_free=(\d+) both_kept=(\d+) kind_free_pred_kept=(\d+) kind_kept_pred_free=(\d+) no_fact=(\d+)$/gm
const DIFF = /^FACTS_COMPARE reader_diff (.*)$/gm

function numbers(match, from) {
    return {
        nodes: Number(match[from]),
        bothFree: Number(match[from + 1]),
        bothKept: Number(match[from + 2]),
        kindFreePredKept: Number(match[from + 3]),
        kindKeptPredFree: Number(match[from + 4]),
        noFact: Number(match[from + 5]),
    }
}

async function main() {
    const opts = parseArgs(process.argv.slice(2))
    const binary = camelBinary(opts)
    const all = findCases(CASES, []).sort()
    const relative = (file) => path.relative(ROOT, file).split(path.sep).join('/')

    let selected = all
    if (opts.only) {
        selected = selected.filter((file) => relative(file).includes(opts.only))
    }
    if (opts.limit > 0) {
        selected = selected.slice(0, opts.limit)
    }
    if (selected.length === 0) {
        process.stderr.write('probe-facts-readers: no case matched, so there is nothing to read\n')
        process.exit(2)
    }

    process.stdout.write(`${provenance(binary)}\n`)

    const totals = { cases: selected.length, read: 0, skipped: 0, silent: 0, nodes: 0, agree: 0, diff: 0, noFact: 0 }
    const kinds = new Map()
    const diffs = []
    const silent = []
    let next = 0

    const worker = async () => {
        for (;;) {
            const index = next++
            if (index >= selected.length) {
                return
            }
            const file = selected[index]
            const { stdout, stderr, failed } = await runCase(binary, file)
            const text = `${stdout}\n${stderr}`
            const readers = READERS.exec(text)
            if (!readers) {
                // A case the pass did not answer said so on its own output and is `skipped`; one
                // that said nothing -- or that this tool had to kill -- is `silent`, and is named
                // below, because a corpus read with those leaving no trace reads as coverage it
                // does not have.
                if (!failed && (stdout.length > 0 || stderr.length > 0)) {
                    ++totals.skipped
                } else {
                    ++totals.silent
                    silent.push(relative(file))
                }
                continue
            }
            ++totals.read
            totals.nodes += Number(readers[2])
            totals.agree += Number(readers[3])
            totals.diff += Number(readers[4])
            totals.noFact += Number(readers[5])
            for (const match of text.matchAll(KIND)) {
                const kind = match[1]
                const seen = kinds.get(kind) ?? {
                    nodes: 0,
                    bothFree: 0,
                    bothKept: 0,
                    kindFreePredKept: 0,
                    kindKeptPredFree: 0,
                    noFact: 0,
                }
                const counted = numbers(match, 2)
                for (const key of Object.keys(seen)) {
                    seen[key] += counted[key]
                }
                kinds.set(kind, seen)
            }
            for (const match of text.matchAll(DIFF)) {
                diffs.push(`FACTS_READERS_DIFF ${relative(file)}: ${match[0]}`)
            }
        }
    }

    await Promise.all(Array.from({ length: Math.min(opts.jobs, selected.length) }, worker))

    process.stdout.write(
        `FACTS_READERS_SWEEP cases=${totals.cases} read=${totals.read} skipped=${totals.skipped} ` +
            `silent=${totals.silent} nodes=${totals.nodes} agree=${totals.agree} diff=${totals.diff} ` +
            `no_fact=${totals.noFact} kinds=${kinds.size}\n`
    )
    const ordered = [...KIND_ORDER.filter((kind) => kinds.has(kind)), ...kinds.keys()].filter(
        (kind, index, list) => list.indexOf(kind) === index
    )
    for (const kind of ordered) {
        const counted = kinds.get(kind)
        if (counted.nodes === 0) {
            continue
        }
        process.stdout.write(
            `FACTS_READERS_KIND kind=${kind} nodes=${counted.nodes} both_free=${counted.bothFree} ` +
                `both_kept=${counted.bothKept} kind_free_pred_kept=${counted.kindFreePredKept} ` +
                `kind_kept_pred_free=${counted.kindKeptPredFree} no_fact=${counted.noFact}\n`
        )
    }
    // The cases that said nothing are named before the differences: they are the ones the sweep
    // could not read, and a count without the names is a number nobody can act on.
    silent.sort()
    for (const file of silent) {
        process.stdout.write(`FACTS_READERS_SILENT ${file}\n`)
    }
    // Differences are sorted whole-line: the case path first, then the line the pass printed, so two
    // runs of this tool can be compared with `diff` and a line that moved is a line that moved.
    diffs.sort()
    for (const diff of diffs) {
        process.stdout.write(`${diff}\n`)
    }
}

main().catch((error) => {
    process.stderr.write(`probe-facts-readers: ${error && error.stack ? error.stack : error}\n`)
    process.exit(1)
})
