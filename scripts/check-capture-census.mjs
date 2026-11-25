#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The capture census: how many `:var/new` nodes and how many by-reference capture ports this tree
 * builds, over a file set whose own fingerprint is printed beside the counts.
 *
 * The language author accepts the closure-capture work by two numbers reaching zero -- the number of
 * `:var/new` nodes and the number of capture ports bound by reference (a capture port whose value is
 * a cell, `var T`). Those two numbers are not in `std::stats` (`GRAPH_STATS` counts graphs, nodes,
 * operators as a total, calls, closures and branches; it never says which operator, and it has no
 * capture table), so every lane that reported them wrote its own throwaway sweep and its own idea of
 * which files were in the reading. `:var/new 410 -> 408` and `621 -> 604` cannot be checked against
 * anything today for exactly that reason, and the file count those readings came with (`5687`) is not
 * the repository's either.
 *
 * This script is that reading, in the tree, with its denominator attached:
 *
 * --- what is counted, and out of which output ---------------------------------
 * Every file in scope is translated with `std::jgir` and the versioned JSON GIR is read:
 *
 *   var_new          nodes whose `label` is `:var/new` (exact; `:var/new/empty` is counted apart
 *                    as `var_new_empty`, because it is another operator and folding the two would
 *                    make the author's number move for a reason he did not name);
 *   capture_ports    the length of every graph's `capturePorts` table whose port's `outputType` is a
 *                    cell (`var T`): the port is bound by reference, i.e. the captured value is a
 *                    cell and not a value;
 *   capture_ports_value  the same table's ports that carry a value (not a cell);
 *   capture_ports_untyped  a port whose `outputType` the document does not print. It is neither of
 *                    the two above, and it is counted out loud rather than folded into one of them: a
 *                    reading that silently parked an unknown type in "value" would be the same
 *                    defect as counting a graph it did not read;
 *   capture_ports_total  the whole capture table, the three parts above;
 *   cell_typed_nodes every node of every graph whose `outputType` is a cell (`var T`), which is the
 *                    wider reading the earlier sweep reported beside the ports (3223 / 3206).
 *
 * --- the split: two kinds of port, and the one that has to reach zero --------------------
 * Not every by-reference port is the program's. A generator keeps the state it carries from one
 * `yield` to the next in cells of its own body (`src/compile/src/gct/builder.cpp#Builder::
 * visitGeneratorBody`: `__gen_pc__`, and every local the body declares), and hands
 * those cells to its caller inside a closure the machine builds itself (`streamValue()`: an
 * unnamed function whose whole body is `return __gen_step__()`). The state carried a second
 * member until 2026-11-18: the rule ("answer: drop the generator's halt
 * bit") removed it, merged, so `S = (pc, halt)` is now `S = (pc)`. Nothing renamed
 * it and nothing took its place, so this script names it nowhere else. A reading taken before that
 * date counted one more cell -- and one more capture port -- per generator body, which is why an
 * older tree's `capture_ports_generator` is not this tree's reading and the two are never
 * subtracted (the header's rule about `list_sha` is the same rule). The language author ruled
 * (2026-11-16, item 4 of the answer on the generator page): that state is the generator's
 * implementation and not `var` semantics, it must not be counted among the by-reference captures
 * that remain, and the merge must report the two kinds apart.
 *
 * So `capture_ports` is split into three buckets, decided by the graph a port lives in and by
 * nothing else:
 *
 *   capture_ports_generator  the port sits in a graph the generator machine built. Two markers,
 *                            both facts about the graph:
 *                            (a) the graph names itself `__gen_*`. The machine's step function is
 *                                `src/compile/src/gct/builder_func.cpp#Builder::visitGeneratorBody`'s
 *                                `stepName` (`const std::string stepName = "__gen_step__"`), and
 *                                nothing else in the compiler gives a graph that prefix (an
 *                                ordinary anonymous closure is `__g_<n>__`);
 *                            (b) the graph's body calls a graph named `__gen_*` -- that is exactly
 *                                `streamValue()`'s `return __gen_step__()`, so a graph with this
 *                                marker is the stream value of one pull, i.e. a direct product of a
 *                                generator body. Its ports are the step's cells plus the body's own
 *                                hoisted locals, in a closure the program never wrote.
 *   capture_ports_closure    the port sits in any graph neither marker names: a closure the
 *                            program wrote (or a call site that holds its ports), capturing a cell
 *                            by reference. This is the number the author's target 0 is about.
 *   capture_ports_unknown    the port cannot be attributed: the document prints no graph name, or a
 *                            port of the machine's own kind (`__gen_*`, a name only the compiler
 *                            writes) sits in a graph neither marker names -- the two signals
 *                            disagree, so the port is counted out loud rather than parked in one of
 *                            the two classes to make the other one smaller.
 *
 * Why the markers are facts about the graph and not the port's name: a generator body's own local
 * keeps its source spelling across `yield` (`i`, `x`, `e` in
 * `test/cases/compile/stream/generators.cml` captures `__gen_pc__`, `e`; it captured
 * `__gen_halt__` beside them too, until the halt bit was dropped), so a
 * name match on the port would leave those cells in `capture_ports_closure` while the machine port
 * beside them left -- a split by spelling, not by what the port is. The graph, on the other hand,
 * is the thing the compiler builds: `__gen_step__` is a literal bound inside the generator's own
 * scope, so no program can write a name resolving to it, and no program closure can be a graph
 * whose body calls it. The one shape the markers deliberately leave out is a closure the *program*
 * wrote inside a generator body (`let read = () => x` in the same case): it is the program's
 * closure and it stays in `capture_ports_closure` even though the cell it captures is generator
 * state -- which is worth knowing, and is why the buckets are named by the graph and not by the
 * cell.
 *
 * The three buckets must add up to `capture_ports`. A run where they do not (a port shape neither
 * bucket claims) is refused with exit 2 instead of printing a total that does not close.
 *
 * Why `std::jgir` and not `std::gir` or `std::stats`:
 *
 *   - `std::stats` prints one `GRAPH_STATS {json}` line per graph with aggregate counts and no
 *     per-operator breakdown, so neither `:var/new` nor a capture port can be read out of it at all;
 *   - `std::gir` is DOT: the operator is a `label` folded into a node's attribute string and a port's
 *     type is styling, so a reader parses the printer's formatting, and `capturePorts` does not exist
 *     as a table there;
 *   - `std::jgir` is the versioned JSON GIR built for tooling. A node carries `label`, `kind`,
 *     `outputType` and `parameter` as fields, a graph carries `capturePorts` as a table of node ids,
 *     and the document says `format: camel.gir` / `version: 1`. That is the one reading whose shape
 *     this script can assert instead of guess, and the shape check below refuses (exit 2) when it
 *     does not hold rather than counting zero over a document it did not understand.
 *
 * --- the scope, and the denominator it is a part of ---------------------------------
 * The default scope is every `.cml` file under `test/cases/`, sorted: those are the programs the
 * plans select (a plan picks the cases of its own directory; no plan entry names a `.cml` path, so
 * `test/plans/**` adds no file this walk misses). The whole repository (4129 `.cml`)
 * is available with `--all`, but not compiled by default: 4129 translations cost
 * minutes, and the plans' corpus is the reading the author's acceptance is about.
 *
 * `list_sha` is the fingerprint of that denominator: SHA-256 over the sorted lines
 * `<sha256 of the file's bytes>  <path relative to the repository root, forward slashes>`, one line
 * per file, in the listing's order. It changes when a file is added, removed, renamed or edited, and
 * it does not change between two runs over one tree. A number without it is a number whose scope
 * nobody can check, which is the defect this script exists to remove.
 *
 * The tree's full corpus is printed too, with the same walk `test/tools/parsecheck/corpus.py
 * --scope repository` uses (its `SKIP_DIRS`, its exclusion of `internal`), and the Markdown
 * ```cml blocks it extracts are counted beside it. `outside_scope` is the number of a full-corpus
 * `.cml` this run did not compile, with its first few paths: so a file read here can never be
 * mistaken for the whole repository's reading.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-capture-census.mjs                 # test/cases, this tree's out/latest
 *   node scripts/check-capture-census.mjs --all           # every .cml of the repository
 *   node scripts/check-capture-census.mjs --detail        # one line per file, for a diff of two trees
 *   node scripts/check-capture-census.mjs --jobs 8        # translations in flight (default 8)
 *   node scripts/check-capture-census.mjs --limit 20      # a sample: the first N of the sorted list
 *   node scripts/check-capture-census.mjs --enforce       # exit 1 unless closure ports and :var/new are 0
 *
 * Exit 0 is a reading (report-only, the mode the gate runs). 1 is `--enforce` over a non-zero
 * reading. 2 is a run that could not take a reading: no install, a usage error, an empty scope, a
 * split whose three buckets do not add up to `capture_ports`, or a file whose translation outlived
 * `--timeout` or could not start -- those are named and counted, and a run that lost files or a
 * reading that does not close is refused rather than reported as if it were whole.
 */

import fs from 'node:fs'
import path from 'node:path'
import crypto from 'node:crypto'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../test/tools/camel-arg.mjs'
import { refuseInstallNotOfThisTree } from './install-stamp.mjs'

const NAME = 'check-capture-census'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The default scope: the programs the plans select. */
const SCOPE_ROOT = 'test/cases'

/**
 * The directories `test/tools/parsecheck/corpus.py` leaves out of the repository corpus, kept here
 * with the same names and the same `out_lr*` prefix rule: `outside_scope` is only an honest
 * subtraction from that corpus if both walks agree on what the corpus is.
 */
const SKIP_DIRS = new Set([
    '.git',
    'node_modules',
    'build',
    'out',
    'tmp',
    'third_party',
    'dist',
    'vendor',
    '.cache',
    '__pycache__',
    'tmp_q',
    'results',
])
/** The author's handover directory: draft material, not the repository's language surface. */
const HANDOVER = ['internal']

/** The one field of the document this script reads its shape from; anything else is refused. */
const FORMAT = 'camel.gir'

/**
 * The prefix of the graphs the generator machine builds for itself: `__gen_step__`
 * (`src/compile/src/gct/builder_func.cpp#Builder::visitGeneratorBody`, `stepName`). It is matched on a
 * graph's `name`, never on a port's, and the header says why the graph is the reliable marker.
 */
const GENERATOR_GRAPH = '__gen_'

function usage(message) {
    if (message) console.error(`[${NAME}] ${message}`)
    console.error(
        'usage: node scripts/check-capture-census.mjs [--camel <path>] [--all] [--path <p>] ' +
            '[--detail] [--jobs N] [--limit N] [--timeout MS] [--enforce]',
    )
    process.exit(2)
}

const argv = process.argv.slice(2)
const taken = takeCamel(argv, ROOT, usage)
const camel = taken.camel
let wholeRepository = false
let detailed = false
let enforce = false
let jobs = 8
let limit = 0
let timeout = 120000
const paths = []
for (let i = 0; i < taken.rest.length; i++) {
    const arg = taken.rest[i]
    if (arg === '--all') wholeRepository = true
    else if (arg === '--detail') detailed = true
    else if (arg === '--enforce') enforce = true
    else if (arg === '--path') {
        // The sample mode the other source checkers offer: a file or a directory, read from a
        // scope that is named in the reading, never a scope that quietly became the default one.
        const value = taken.rest[++i]
        if (!value) usage('--path needs a file or a directory')
        const resolved = path.resolve(value)
        if (!fs.existsSync(resolved)) usage(`--path names a path that does not exist: ${value}`)
        paths.push(resolved)
    } else if (arg === '--jobs' || arg === '--limit' || arg === '--timeout') {
        const value = taken.rest[++i]
        if (value === undefined || !/^\d+$/.test(value)) usage(`${arg} needs a non-negative integer`)
        const number = Number(value)
        if (arg === '--jobs') {
            if (number < 1) usage('--jobs needs at least 1')
            jobs = number
        } else if (arg === '--limit') limit = number
        else timeout = number
    } else if (arg === '-h' || arg === '--help') usage()
    else usage(`unknown argument '${arg}'`)
}
if (paths.length > 0 && wholeRepository) usage('--path and --all name two different scopes; pick one')

if (!fs.existsSync(camel)) {
    usage(`no camel binary at ${camel}: --camel names the install a run measures, or build this tree once`)
}
refuseInstallNotOfThisTree(camel, ROOT, NAME)
const INSTALL = path.dirname(path.dirname(camel))

/** A path as the listing and `list_sha` spell it: relative to the root, forward slashes. */
const shown = (file) => path.relative(ROOT, file).split(path.sep).join('/')

/**
 * Every `.cml` under `dir`, sorted, with `corpus.py`'s exclusions: a skipped directory name, a
 * directory whose name starts with `out_lr`, and the handover tree.
 */
function cmlFilesUnder(dir) {
    // `--path` may name one file: the function is "the .cml files under this path", and one file is
    // the smallest set of them.
    if (fs.statSync(dir).isFile()) return dir.endsWith('.cml') ? [dir] : []
    const files = []
    const walk = (current) => {
        const entries = fs.readdirSync(current, { withFileTypes: true })
        for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
            const full = path.join(current, entry.name)
            if (entry.isDirectory()) {
                if (SKIP_DIRS.has(entry.name) || entry.name.startsWith('out_lr')) continue
                walk(full)
            } else if (entry.isFile() && entry.name.endsWith('.cml')) {
                files.push(full)
            }
        }
    }
    walk(dir)
    return files
}

function sha256(bytes) {
    return crypto.createHash('sha256').update(bytes).digest('hex')
}

/**
 * The denominator's fingerprint: one line per file, `<content sha256>  <path>`, sorted by path.
 * The listing is read once, so the hash and the translation list cannot describe two sets.
 */
function fingerprint(files) {
    const lines = files.map((file) => `${sha256(fs.readFileSync(file))}  ${shown(file)}`)
    return sha256(lines.join('\n') + '\n')
}

/** The Markdown ```cml blocks, counted the way `corpus.py` extracts them (one file per block). */
function markdownBlockCount() {
    let blocks = 0
    const fence = /^(\s*)```(cml|camel)\s*$/
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) {
                if (SKIP_DIRS.has(entry.name) || entry.name.startsWith('out_lr')) continue
                walk(full)
            } else if (entry.isFile() && entry.name.endsWith('.md')) {
                let open = false
                for (const line of fs.readFileSync(full, 'utf8').split('\n')) {
                    if (!open) {
                        if (fence.test(line)) open = true
                    } else if (line.trim() === '```') {
                        blocks += 1
                        open = false
                    }
                }
            }
        }
    }
    walk(ROOT)
    return blocks
}

/**
 * Which kind of graph a capture port lives in: the generator machine's, the program's, or one the
 * document does not let this script attribute. The two markers are the ones the header states: the
 * graph names itself `__gen_*`, or its body calls one of those (the stream value the machine makes).
 */
function machineBuiltGraph(graph, nameOf) {
    if (typeof graph.name !== 'string') return 'unknown'
    if (graph.name.startsWith(GENERATOR_GRAPH)) return 'generator'
    for (const node of graph.nodes) {
        if (typeof node.callee !== 'string') continue
        const callee = nameOf.get(node.callee)
        if (typeof callee === 'string' && callee.startsWith(GENERATOR_GRAPH)) return 'generator'
    }
    return 'closure'
}

/** The parts of one file's GIR document this census reads, or a refusal naming what was missing. */
function census(document) {
    if (!document || document.format !== FORMAT || !Array.isArray(document.graphs)) {
        return { refused: `not a ${FORMAT} document` }
    }
    // A graph names the graphs it calls by id, so the name is resolved over the whole document
    // before any graph is classified: a body may call a graph printed after it.
    const nameOf = new Map()
    for (const graph of document.graphs) {
        if (typeof graph.id === 'string' && typeof graph.name === 'string') {
            nameOf.set(graph.id, graph.name)
        }
    }
    let varNew = 0
    let varNewEmpty = 0
    let capturePorts = 0
    let capturePortsGenerator = 0
    let capturePortsClosure = 0
    let capturePortsUnknown = 0
    let capturePortsValue = 0
    let capturePortsUntyped = 0
    let cellTypedNodes = 0
    for (const graph of document.graphs) {
        if (!Array.isArray(graph.nodes)) return { refused: 'a graph without a node table' }
        const byId = new Map()
        for (const node of graph.nodes) {
            byId.set(node.id, node)
            if (node.label === ':var/new') varNew += 1
            else if (node.label === ':var/new/empty') varNewEmpty += 1
            if (typeof node.outputType === 'string' && node.outputType.startsWith('var ')) {
                cellTypedNodes += 1
            }
        }
        const captures = graph.capturePorts
        if (!Array.isArray(captures)) return { refused: 'a graph without a capture table' }
        for (const id of captures) {
            const port = byId.get(id)
            if (!port) return { refused: `a capture table naming a node this graph does not hold` }
            if (typeof port.outputType !== 'string') capturePortsUntyped += 1
            else if (port.outputType.startsWith('var ')) capturePorts += 1
            else capturePortsValue += 1
        }
    }
    // The split is counted in a second walk over the same tables, not accumulated inside the walk
    // above: `capture_ports` is the historical number and stays computed the way it always was, and
    // the two walks disagreeing is what the sum check at the end of the run is able to see. A split
    // accumulated in the same `+= 1` as the total could never fail to add up.
    for (const graph of document.graphs) {
        const captures = graph.capturePorts
        if (captures.length === 0) continue
        const owner = machineBuiltGraph(graph, nameOf)
        const byId = new Map(graph.nodes.map((node) => [node.id, node]))
        for (const id of captures) {
            const port = byId.get(id)
            if (!port || typeof port.outputType !== 'string' || !port.outputType.startsWith('var ')) {
                continue
            }
            const portName =
                port.parameter && typeof port.parameter.name === 'string' ? port.parameter.name : ''
            // Only the by-reference ports are split, and each one is claimed by a named bucket. A
            // port the machine named but a graph neither marker attributes is a disagreement
            // between the two facts, so it is counted out loud instead of being parked in the class
            // that would look smaller.
            if (owner === 'generator') capturePortsGenerator += 1
            else if (owner === 'closure' && !portName.startsWith(GENERATOR_GRAPH)) {
                capturePortsClosure += 1
            } else capturePortsUnknown += 1
        }
    }
    return {
        varNew,
        varNewEmpty,
        capturePorts,
        capturePortsGenerator,
        capturePortsClosure,
        capturePortsUnknown,
        capturePortsValue,
        capturePortsUntyped,
        cellTypedNodes,
    }
}

/** One translation: `camel <file> std::jgir`, with the install this run measures. */
function translate(file) {
    return new Promise((resolve) => {
        const child = spawn(camel, [shown(file), 'std::jgir'], {
            cwd: ROOT,
            env: { ...process.env, CAMEL_HOME: INSTALL },
            stdio: ['ignore', 'pipe', 'pipe'],
            windowsHide: true,
        })
        const out = []
        let stderr = ''
        let settled = false
        const finish = (result) => {
            if (settled) return
            settled = true
            clearTimeout(timer)
            resolve(result)
        }
        const timer = setTimeout(() => {
            child.kill('SIGKILL')
            finish({ outcome: 'timeout', detail: `no answer within ${timeout} ms` })
        }, timeout)
        child.on('error', (error) => finish({ outcome: 'failed', detail: error.message }))
        child.stdout.on('data', (chunk) => out.push(chunk))
        child.stderr.on('data', (chunk) => {
            if (stderr.length < 2000) stderr += chunk.toString()
        })
        child.on('close', (code, signal) => {
            if (signal) return finish({ outcome: 'crashed', detail: `killed by ${signal}` })
            const stdout = Buffer.concat(out).toString('utf8')
            if (code !== 0) {
                // A program the compiler refuses is a case that must be refused, not a defect of
                // this run: the shape of its answer is a diagnostic, and a diagnostic is printed on
                // stdout. The first line of it is what the log carries.
                const line =
                    stderr.trim().split('\n')[0] || stdout.trim().split('\n')[0] || `exit ${code}`
                return finish({ outcome: 'refused', detail: line.slice(0, 160) })
            }
            try {
                finish({ outcome: 'ok', document: JSON.parse(stdout) })
            } catch (error) {
                finish({ outcome: 'failed', detail: `stdout is not JSON (${error.message})` })
            }
        })
    })
}

/** Run `tasks` through at most `jobs` translations in flight; the results keep the listing's order. */
async function translateAll(files) {
    const results = new Array(files.length)
    let next = 0
    const worker = async () => {
        for (;;) {
            const index = next++
            if (index >= files.length) return
            results[index] = await translate(files[index])
        }
    }
    await Promise.all(Array.from({ length: Math.min(jobs, files.length) }, worker))
    return results
}

const scopeDir = path.join(ROOT, wholeRepository ? '.' : SCOPE_ROOT)
let listed = paths.length > 0 ? paths.flatMap((target) => cmlFilesUnder(target)) : cmlFilesUnder(scopeDir)
if (wholeRepository) {
    listed = listed.filter((file) => !shown(file).startsWith(`${HANDOVER.join('/')}/`))
}
listed.sort((left, right) => shown(left).localeCompare(shown(right)))
// A path named twice (a file inside a directory that was also named) is one file, not two: the
// listing the fingerprint and the translations describe has to be a set.
listed = [...new Map(listed.map((file) => [shown(file), file])).values()]
const sampled = limit > 0 ? listed.slice(0, limit) : listed
if (sampled.length === 0) {
    usage(`${paths.length > 0 ? paths.join(', ') : shown(scopeDir)} holds no .cml file, so this census would count over nothing`)
}

const listShaListed = fingerprint(listed)
const listSha = fingerprint(sampled)

const results = await translateAll(sampled)

const totals = {
    varNew: 0,
    varNewEmpty: 0,
    capturePorts: 0,
    capturePortsGenerator: 0,
    capturePortsClosure: 0,
    capturePortsUnknown: 0,
    capturePortsValue: 0,
    capturePortsUntyped: 0,
    cellTypedNodes: 0,
}
const outcomes = { ok: 0, refused: 0, crashed: 0, timeout: 0, failed: 0 }
const perFile = []
const notCounted = []
for (let index = 0; index < sampled.length; index++) {
    const result = results[index]
    const file = shown(sampled[index])
    if (result.outcome !== 'ok') {
        outcomes[result.outcome] += 1
        notCounted.push(`${file}: ${result.outcome} -- ${result.detail}`)
        continue
    }
    const reading = census(result.document)
    if (reading.refused) {
        outcomes.failed += 1
        notCounted.push(`${file}: unreadable GIR -- ${reading.refused}`)
        continue
    }
    outcomes.ok += 1
    totals.varNew += reading.varNew
    totals.varNewEmpty += reading.varNewEmpty
    totals.capturePorts += reading.capturePorts
    totals.capturePortsGenerator += reading.capturePortsGenerator
    totals.capturePortsClosure += reading.capturePortsClosure
    totals.capturePortsUnknown += reading.capturePortsUnknown
    totals.capturePortsValue += reading.capturePortsValue
    totals.capturePortsUntyped += reading.capturePortsUntyped
    totals.cellTypedNodes += reading.cellTypedNodes
    perFile.push({ file, ...reading })
}

// The full corpus the reading is a part of, with the same walk `corpus.py --scope repository` takes,
// so the subtraction below is between two walks that agree on what the corpus is.
const fullFiles = cmlFilesUnder(ROOT).filter(
    (file) => !shown(file).startsWith(`${HANDOVER.join('/')}/`),
)
const fullTotal = fullFiles.length
const blocks = markdownBlockCount()
const listedSet = new Set(listed.map(shown))
const outside = fullFiles.map(shown).filter((file) => !listedSet.has(file))

const scopeWords =
    paths.length > 0
        ? `the paths named with --path (${paths.map(shown).join(', ')}), every .cml under them`
        : wholeRepository
          ? `every .cml file of the repository (test/tools/parsecheck/corpus.py --scope repository, its exclusions included)`
          : `every .cml file under ${SCOPE_ROOT}/, sorted (the programs the plans select)`
const sampleNote = limit > 0 ? `, first ${sampled.length} of ${listed.length} by path (--limit)` : ''

console.log(`[${NAME}] scope=${scopeWords}${sampleNote}`)
console.log(
    `[${NAME}] scope_root=${paths.length > 0 ? paths.map(shown).join(',') : wholeRepository ? '.' : SCOPE_ROOT}`,
)
console.log(`[${NAME}] files=${sampled.length} listed=${listed.length} counted=${outcomes.ok}`)
console.log(
    `[${NAME}] var_new=${totals.varNew} var_new_empty=${totals.varNewEmpty} ` +
        `cell_typed_nodes=${totals.cellTypedNodes}`,
)
console.log(
    `[${NAME}] capture_ports=${totals.capturePorts} ` +
        `capture_ports_generator=${totals.capturePortsGenerator} ` +
        `capture_ports_closure=${totals.capturePortsClosure} ` +
        `capture_ports_unknown=${totals.capturePortsUnknown} ` +
        `capture_ports_value=${totals.capturePortsValue} ` +
        `capture_ports_total=${totals.capturePorts + totals.capturePortsValue + totals.capturePortsUntyped} ` +
        `capture_ports_untyped=${totals.capturePortsUntyped}`,
)
// The split is a reading only if it adds up: the three buckets are the whole of `capture_ports`,
// so a run where they do not is refused rather than printed (a number whose parts do not close is
// the kind of reading this script exists to stop).
const splitSum =
    totals.capturePortsGenerator + totals.capturePortsClosure + totals.capturePortsUnknown
console.log(
    `[${NAME}] capture_ports_split ` +
        `${totals.capturePortsGenerator} + ${totals.capturePortsClosure} + ${totals.capturePortsUnknown} ` +
        `= ${splitSum} ${splitSum === totals.capturePorts ? '==' : '!='} ${totals.capturePorts} capture_ports`,
)
console.log(`[${NAME}] list_sha=${listSha}`)
if (limit > 0) console.log(`[${NAME}] list_sha_all=${listShaListed}`)
console.log(
    `[${NAME}] outcomes: ok=${outcomes.ok} refused=${outcomes.refused} crashed=${outcomes.crashed} ` +
        `timeout=${outcomes.timeout} unreadable=${outcomes.failed}`,
)
console.log(
    `[${NAME}] corpus: full_cml=${fullTotal} markdown_blocks=${blocks} ` +
        `corpus_py_total=${fullTotal + blocks} outside_scope=${outside.length}`,
)
if (outside.length > 0) {
    console.log(`[${NAME}] outside_scope_first=${outside.slice(0, 5).join(' ')}`)
}
if (detailed) {
    for (const entry of perFile) {
        console.log(
            `[${NAME}] file ${entry.file}: var_new=${entry.varNew} capture_ports=${entry.capturePorts} ` +
                `capture_ports_generator=${entry.capturePortsGenerator} ` +
                `capture_ports_closure=${entry.capturePortsClosure} ` +
                `capture_ports_unknown=${entry.capturePortsUnknown} ` +
                `capture_ports_value=${entry.capturePortsValue} ` +
                `capture_ports_untyped=${entry.capturePortsUntyped} cell_typed_nodes=${entry.cellTypedNodes}`,
        )
    }
}
if (notCounted.length > 0) {
    // A refusal is expected in this corpus (a syntax-negative case is a program that must be
    // refused), so the list is a sample rather than a report: the counts above are the reading.
    console.log(
        `[${NAME}] ${notCounted.length} file(s) of the scope gave no graph: refused=${outcomes.refused} ` +
            `crashed=${outcomes.crashed} timeout=${outcomes.timeout} unreadable=${outcomes.failed}`,
    )
    for (const line of notCounted.slice(0, 6)) console.log(`[${NAME}]   ${line}`)
    if (notCounted.length > 6) console.log(`[${NAME}]   ... and ${notCounted.length - 6} more`)
}
// The line the gate reduces to its count (`findings` in internal/tools/camel-remote.ps1). Its first
// number is `capture_ports`: the reading the author's acceptance is stated in, kept at the same
// place so a historical reading and this one line up. The split the author asked to see beside it
// is in the parentheses: the generator's own state, and the program's closures -- the second is the
// number the target 0 is about.
console.log(
    `${NAME}: ${totals.capturePorts} reference capture port(s) ` +
        `(generator ${totals.capturePortsGenerator}, closure ${totals.capturePortsClosure}, ` +
        `unknown ${totals.capturePortsUnknown}), ` +
        `${totals.varNew} :var/new node(s), over ${outcomes.ok} of ${sampled.length} file(s) of the scope`,
)
if (splitSum !== totals.capturePorts) {
    console.error(
        `${NAME}: the split does not close: generator ${totals.capturePortsGenerator} + closure ` +
            `${totals.capturePortsClosure} + unknown ${totals.capturePortsUnknown} = ${splitSum}, ` +
            `not ${totals.capturePorts} reference capture port(s). A port that neither the ` +
            'generator marker nor the closure class claims is a defect of this reading, so it is ' +
            'refused instead of reported.',
    )
    process.exit(2)
}
if (outcomes.crashed + outcomes.timeout + outcomes.failed > 0) {
    console.error(
        `${NAME}: ${outcomes.crashed + outcomes.timeout + outcomes.failed} file(s) did not give a ` +
            'reading: a census over a set it lost files from is not a census',
    )
    process.exit(2)
}
if (enforce && (totals.capturePortsClosure > 0 || totals.varNew > 0)) {
    // `--enforce` is the mode this reading becomes once the work is done, so it asks the question
    // the author's target asks: the program's closures, not the generator's own state (which stays
    // until the generator stops keeping its state in cells).
    console.error(
        `${NAME}: the closure-capture work is not done: ${totals.capturePortsClosure} capture ` +
            `port(s) are bound by reference outside the generator machines ` +
            `(generator ${totals.capturePortsGenerator} counted apart) and ${totals.varNew} ` +
            `:var/new node(s) remain (the author's target is 0/0)`,
    )
    process.exit(1)
}
process.exit(0)