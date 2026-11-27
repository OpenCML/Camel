#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One reading over the whole corpus: what `core.arrangeable` answers at every node of every program
// the tree tracks, and -- beside it -- how that answer compares with the older reading of the same
// question, `runtime::nodeIsFreelySchedulable`.
//
// Why this is a tool and not a plan entry: it runs `camel <case> test::facts_compare std::nop` for
// every `.cml` under `test/cases`, and the numbers it prints move whenever the compiler moves (a
// node added anywhere moves `nodes` and `free`). A plan entry that asserted those numbers would be
// red on every unrelated change, and one that asserted nothing would be decoration. So the numbers
// live in the reports that cite them, and this file is the command that produces them;
// `test/plans/feat/tools/facts_readers_sweep.plan.toml` runs it in the slow layer, where a change to
// the pass or to the corpus is meant to be looked at.
//
// What it prints, in this order:
//
//   FACTS_READERS_PROBE root=… camel=… built=… tip=…     the tool, the binary and the tree the
//                                                        numbers below belong to (one line, so a
//                                                        reading copied out of a log carries them)
//   FACTS_READERS_SWEEP cases= read= skipped= silent= …   the totals: cases swept, cases the pass
//                                                        answered at all (`read`), cases it did not
//                                                        (`skipped`, split into the two reasons
//                                                        below), cases that said nothing (`silent`),
//                                                        the sums of the per-case answers, the
//                                                        cases the directory rule left out
//                                                        (`fixtures`), and the older comparison's
//                                                        sums
//   FACTS_READERS_KIND kind=… nodes=… free=… kept=… …     the same answers split by node kind, so a
//                                                        kind that is "freely arrangeable" has a
//                                                        number of its own
//   FACTS_READERS_FAMILY family=… …                       the same answers split by the family the
//                                                        answer comes from
//   FACTS_READERS_SKIP <path> <refused|declined>          every case the sweep did not read, with
//                                                        which of the two reasons it was
//   FACTS_READERS_SILENT <path>                           every case that said nothing at all
//
// `silent` and `no_fact` are the readings that are a defect when they are not zero. `silent`: the
// case neither answered nor said why, so the program did not reach the pass and the sweep cannot say
// anything about it. `no_fact`: a node the fact layer answered nothing for -- an absent answer is
// not a placement decision, so it cannot be folded into either column.
//
// `skipped` is not one thing, and the sweep prints which of the two it is for every case:
//
//   refused   the output carries no `FACTS_COMPARE` line at all, so the compiler never ran the
//             pass: the case is one its own plan gives arguments or module roots to, and on its own
//             it does not compile. It is a case the sweep cannot read, not one it read and rejected.
//   declined  the pass ran and printed, but not the answer table: the graph was there and the store
//             answered nothing for it (`state=no-store`, `state=no-kept-ids`). Those are named,
//             because a corpus whose decline group grows is a fact-layer reading of its own.
//
// The program is never run: the pipeline is `test::facts_compare std::nop`. The comparison pass hands
// the program on, and without a stopper every case would be *executed* as the next fallback -- which
// is also why a case whose program never returns was able to hold the whole sweep for minutes when
// this tool was written without one.
//
// `test/cases/**/fixtures/` is left out by a directory rule: those files are inputs other tools read
// (the debugger's and the language server's own cases), never a program the corpus runs. A rule and
// not a list, so a fixture added tomorrow is left out by the same line; the cases it left out are
// counted in `fixtures=`, so the exclusion is visible rather than silent. No other case is skipped by
// where it lives: what the sweep cannot read, it says so about.
//
// Every case runs in its own scratch directory (`fs.mkdtempSync`), never in the checkout: cases
// that export a model (`onnx::export_model out=m.onnx`) write beside the program, and before this the
// sweep left a dozen files in the tree it measured.
//
// Usage:
//   node test/tools/probe-facts-readers.mjs                      # the whole corpus
//   node test/tools/probe-facts-readers.mjs --jobs 8             # wider
//   node test/tools/probe-facts-readers.mjs --only cases/loops/  # a subset by path substring
//   node test/tools/probe-facts-readers.mjs --limit 200          # the first 200, for a smoke run
//   node test/tools/probe-facts-readers.mjs --camel <path>       # a binary other than $CAMEL_HOME's
//
// The binary is `$CAMEL_HOME/bin/camel` when that exists, else `camel` on `PATH`. The reading is
// only worth what the line 1 provenance says: the machine's binary and the tree it was built from.

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

// The directory the rule leaves out, and the only one: a fixture is another tool's input.
const FIXTURE_DIRECTORY = 'fixtures'

function usage() {
    const text = fs.readFileSync(fileURLToPath(import.meta.url), 'utf8')
    const start = text.indexOf('// Usage:')
    const end = text.indexOf('// The binary is')
    process.stdout.write(text.slice(start, end).replace(/^\/\/ ?/gm, ''))
    process.stdout.write(
        [
            '',
            'It sweeps every `.cml` under `test/cases` except the `fixtures/` directories, which are',
            'other tools’ inputs. It is not part of the fast layer.',
            '`--tier slow test/plans/feat/tools/facts_readers_sweep.plan.toml` runs the same sweep',
            'through the runner, which adds the provenance line the runner prints.',
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

/// Every `.cml` under `test/cases` except the ones under a `fixtures/` directory, and how many the
/// rule left out. The rule is the directory name and nothing else: a list of files would be a rule
/// that has to be edited every time another tool adds its input.
function findCases(dir, out) {
    const left = { fixtures: 0 }
    const walk = (current) => {
        for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
            const full = path.join(current, entry.name)
            if (entry.isDirectory()) {
                if (entry.name === FIXTURE_DIRECTORY) {
                    left.fixtures += countCases(full)
                    continue
                }
                walk(full)
            } else if (entry.name.endsWith('.cml')) {
                out.push(full)
            }
        }
    }
    walk(dir)
    return left
}

function countCases(dir) {
    let count = 0
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            count += countCases(full)
        } else if (entry.name.endsWith('.cml')) {
            ++count
        }
    }
    return count
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
        // the runner's stdin forever.
        //
        // `std::nop` is the second half of that guard and the reason this sweep does not run
        // programs: the comparison pass hands the graph on, and without a stopper the scheduling
        // fallback executes it. Reading a fact needs the graph, not the program.
        //
        // The per-case clock stays as the last guard, for a case that hangs before the pass runs.
        const child = spawn(binary, [file, 'test::facts_compare', 'std::nop'], {
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

// The pass's own census (`facts_compare.h` is the contract): one total line for `core.arrangeable`'s
// answer, then the same answers cut by node kind and by family, then one line per (node kind,
// operator). `free` is "a pass may arrange it anywhere", `kept` is "a pass must leave it where the
// program wrote it", and `no_fact` is a node the store answered nothing for.
const ANSWERS =
    /^FACTS_COMPARE kind=core\.arrangeable answers graphs=(\d+) nodes=(\d+) free=(\d+) kept=(\d+) no_fact=(\d+)\s*$/m
const ANSWERS_KIND =
    /^FACTS_COMPARE answers_kind kind=(\w+) nodes=(\d+) free=(\d+) kept=(\d+) no_fact=(\d+)/gm
const ANSWERS_FAMILY =
    /^FACTS_COMPARE answers_family family=(\w+) nodes=(\d+) free=(\d+) kept=(\d+) no_fact=(\d+)/gm
// The older reading of the same question, kept because the two are what a report compares: the fact
// against `runtime::nodeIsFreelySchedulable`, per node.
const OLD_FACT =
    /^FACTS_COMPARE kind=core\.arrangeable graphs=(\d+) nodes=(\d+) same=(\d+) diff=(\d+) no_old=(\d+) conversions=(\d+)\s*$/m
const ANY_CENSUS = /^FACTS_COMPARE /m

function numbers(match, from) {
    return {
        nodes: Number(match[from]),
        free: Number(match[from + 1]),
        kept: Number(match[from + 2]),
        noFact: Number(match[from + 3]),
    }
}

async function main() {
    const opts = parseArgs(process.argv.slice(2))
    const binary = camelBinary(opts)
    const all = []
    const left = findCases(CASES, all)
    all.sort()
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

    const totals = {
        cases: selected.length,
        read: 0,
        skipped: 0,
        silent: 0,
        nodes: 0,
        free: 0,
        kept: 0,
        noFact: 0,
        kinds: 0,
        fixtures: left.fixtures,
        oldSame: 0,
        oldDiff: 0,
        oldNoOld: 0,
        oldConversions: 0,
    }
    const kinds = new Map()
    const families = new Map()
    const skips = []
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
            const answers = ANSWERS.exec(text)
            if (!answers) {
                if (failed || (stdout.length === 0 && stderr.length === 0)) {
                    ++totals.silent
                    silent.push(relative(file))
                } else {
                    // Two reasons, printed per case: the pass never ran (`refused`), or it ran and
                    // the store answered nothing for the graph (`declined`).
                    ++totals.skipped
                    skips.push(`${relative(file)} ${ANY_CENSUS.test(text) ? 'declined' : 'refused'}`)
                }
                continue
            }
            ++totals.read
            totals.nodes += Number(answers[2])
            totals.free += Number(answers[3])
            totals.kept += Number(answers[4])
            totals.noFact += Number(answers[5])
            const old = OLD_FACT.exec(text)
            if (old) {
                totals.oldSame += Number(old[3])
                totals.oldDiff += Number(old[4])
                totals.oldNoOld += Number(old[5])
                totals.oldConversions += Number(old[6])
            }
            for (const match of text.matchAll(ANSWERS_KIND)) {
                const kind = match[1]
                const seen = kinds.get(kind) ?? { nodes: 0, free: 0, kept: 0, noFact: 0 }
                const counted = numbers(match, 2)
                for (const key of Object.keys(seen)) {
                    seen[key] += counted[key]
                }
                kinds.set(kind, seen)
            }
            for (const match of text.matchAll(ANSWERS_FAMILY)) {
                const family = match[1]
                const seen = families.get(family) ?? { nodes: 0, free: 0, kept: 0, noFact: 0 }
                const counted = numbers(match, 2)
                for (const key of Object.keys(seen)) {
                    seen[key] += counted[key]
                }
                families.set(family, seen)
            }
        }
    }

    await Promise.all(Array.from({ length: Math.min(opts.jobs, selected.length) }, worker))

    totals.kinds = kinds.size
    process.stdout.write(
        `FACTS_READERS_SWEEP cases=${totals.cases} read=${totals.read} skipped=${totals.skipped} ` +
            `silent=${totals.silent} nodes=${totals.nodes} free=${totals.free} kept=${totals.kept} ` +
            `no_fact=${totals.noFact} kinds=${totals.kinds} fixtures=${totals.fixtures} ` +
            `old_same=${totals.oldSame} old_diff=${totals.oldDiff} old_no_old=${totals.oldNoOld} ` +
            `old_conversions=${totals.oldConversions}\n`
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
            `FACTS_READERS_KIND kind=${kind} nodes=${counted.nodes} free=${counted.free} ` +
                `kept=${counted.kept} no_fact=${counted.noFact}\n`
        )
    }
    for (const [family, counted] of [...families].sort(([a], [b]) => a.localeCompare(b))) {
        if (counted.nodes === 0) {
            continue
        }
        process.stdout.write(
            `FACTS_READERS_FAMILY family=${family} nodes=${counted.nodes} free=${counted.free} ` +
                `kept=${counted.kept} no_fact=${counted.noFact}\n`
        )
    }
    // Every case the sweep did not read is named with its reason, and every case that said nothing
    // is named on its own line: a count without the names is a number nobody can act on.
    skips.sort()
    for (const line of skips) {
        process.stdout.write(`FACTS_READERS_SKIP ${line}\n`)
    }
    silent.sort()
    for (const file of silent) {
        process.stdout.write(`FACTS_READERS_SILENT ${file}\n`)
    }
}

main().catch((error) => {
    process.stderr.write(`probe-facts-readers: ${error && error.stack ? error.stack : error}\n`)
    process.exit(1)
})