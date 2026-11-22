#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The two merge conditions of stage 2, as **one command each**, so that a lane can answer "did I
 * change behaviour?" without a reader in the loop:
 *
 *   node tools/diff-gate/gate.mjs --gate view      # item 2.1, the view
 *   node tools/diff-gate/gate.mjs --gate nodes     # item 2.3, the four node kinds
 *
 * Both are the differential comparison of `test/tools/diff-compare.mjs` -- two installs, one built
 * from the pinned baseline commit and one from this checkout -- over a **slice** of the plan corpus
 * that the item's change can move (`baseline.json`, `gates.<name>.plans`).  What this file adds is
 * what makes the reading fit into a dispatch ticket:
 *
 *   * **the baseline is written down, and checked.**  A comparison against an install that is not a
 *     clean build of the commit in `baseline.json` is the one failure that looks exactly like a
 *     green reading, so the gate reads each side's `build-info.json` and refuses (exit 2) when the
 *     baseline side is not that commit or was built dirty.  The lane side is checked the same way
 *     and its revision is printed, so the reading carries the coordinate it was taken at.
 *   * **one line that can be read by a program or a person.**  Everything else the comparison
 *     prints is detail; the last line is the reading:
 *
 *       [GATE 2.1 view] baseline=2576db796 lane=<sha> cases=34 output_differ=0 graph_differ=0 \
 *       differences=0 unmeasurable=0 verdict=GREEN
 *
 *     `differences` is the number the item is accepted on ("差分工具比对无差异").  Exit is 0 when it
 *     is 0, 1 when it is not, 2 when the reading could not be taken.
 *   * **it refuses to report green when nothing was measured.**  A slice that selects no case, a
 *     side whose every case errored, or a `[TOTAL]` line this file cannot find is exit 2 with the
 *     reason: "identical" over an empty set is not a reading, and it is the failure mode a gate
 *     that only ever says GREEN would have.
 *
 * Why the slice rather than the whole suite: the whole suite is 6,000+ blocks and two runs each, so
 * it is a merge-position reading, not a lane reading.  The slice is fixed in `baseline.json` and is
 * the same for every lane, which is what makes two lanes' greens comparable at all.
 *
 * Detail (the `[DIFF]` lines, the `[TOTAL]` line of the comparison, the coverage) is printed when
 * the gate is red and always with `--verbose`; `--print-config` writes the configuration the gate
 * would hand to the comparison and exits, so what the two sides and the slice are is inspectable
 * without running anything.
 */

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const ROOT_MARKERS = [
    ['stdlib', 'std.cml'],
    ['lib', 'std', 'std.cml']
]
const holdsLibrary = (dir) =>
    ROOT_MARKERS.some((parts) => {
        try {
            fs.readFileSync(path.join(dir, ...parts), 'utf8')
            return true
        } catch {
            return false
        }
    })
const REPO_ROOT = (() => {
    let dir = path.resolve(HERE)
    for (;;) {
        if (holdsLibrary(dir)) return dir
        const parent = path.dirname(dir)
        if (parent === dir) throw new Error(`no stdlib/std.cml or lib/std/std.cml above ${HERE}`)
        dir = parent
    }
})()

const BASELINE_FILE = path.join(HERE, 'baseline.json')
const COMPARE_TOOL = path.join(REPO_ROOT, 'test', 'tools', 'diff-compare.mjs')
const CONFIG_DIR = path.join(REPO_ROOT, 'tmp', 'diff-gate')
const IS_WINDOWS = process.platform === 'win32'
const CAMEL_BINARY = IS_WINDOWS ? 'camel.exe' : 'camel'

// --- the command line --------------------------------------------------------

function usage() {
    return [
        'usage: node tools/diff-gate/gate.mjs --gate <view|nodes|2.1|2.3> [--jobs N] [--verbose]',
        '       node tools/diff-gate/gate.mjs --gate <name> --print-config',
        '',
        '  view    item 2.1 -- 视图一致性: the graph the view presents and the output the program',
        '          prints are the same before and after (the encoder\'s frozen/draft two-reading',
        '          check is inside this reading: a graph whose readings disagree is refused).',
        '  nodes   item 2.3 -- 改前改后无差异: the programs that build a Cast, Copy, Fill or Access',
        '          answer the same thing and draw the same graph before and after.',
        '',
        'The baseline commit, the two install roots and the two slices come from `baseline.json`;',
        'nothing about them is a command-line argument, so two lanes cannot read two baselines.',
        'Exit: 0 no difference, 1 a difference, 2 the reading could not be taken.'
    ].join('\n')
}

function parseArguments(argv) {
    const options = { gate: null, jobs: null, verbose: false, printConfig: false, help: false }
    for (let index = 0; index < argv.length; index += 1) {
        const word = argv[index]
        const valueOf = () => {
            const next = argv[(index += 1)]
            if (next === undefined || String(next).startsWith('--')) {
                throw new Error(`${word} needs a value`)
            }
            return next
        }
        switch (word) {
            case '--gate':
                options.gate = valueOf()
                break
            case '--jobs':
                options.jobs = Number(valueOf())
                break
            case '--verbose':
                options.verbose = true
                break
            case '--print-config':
                options.printConfig = true
                break
            case '--help':
            case '-h':
                options.help = true
                break
            default:
                throw new Error(`unexpected argument '${word}'`)
        }
    }
    return options
}

// --- what a side is ----------------------------------------------------------

/**
 * An install root, read through the stamp the build writes into it (`scripts/collect-out.js`,
 * `writeBuildInfo`).  The stamp is what makes the baseline check possible at all: without it a
 * directory named `df2-base` is a hope, with it the gate can say "this baseline side is the build
 * of 2576db796, clean" before it compares anything.
 */
function installOf(root, role) {
    const dir = path.resolve(root)
    const binary = path.join(dir, 'bin', CAMEL_BINARY)
    let stamp = null
    try {
        stamp = JSON.parse(fs.readFileSync(path.join(dir, 'build-info.json'), 'utf8'))
    } catch {
        stamp = null
    }
    return { role, dir, binary, exists: fs.existsSync(binary), stamp }
}

function describeSide(side) {
    const stamp = side.stamp
    const revision = stamp && stamp.revision ? String(stamp.revision) : null
    return {
        role: side.role,
        dir: side.dir,
        revision,
        short: revision ? revision.slice(0, 9) : 'unknown',
        dirty: stamp ? Boolean(stamp.dirty) : null,
        config: stamp && stamp.config ? String(stamp.config) : 'unknown'
    }
}

/**
 * The baseline side must be a clean build of the commit `baseline.json` names.  Anything else --
 * another commit, a dirty tree, no stamp -- means every number the gate would print is a reading of
 * some other pair of compilers, and the one thing such a number cannot be trusted to do is be
 * non-zero when it should be.  So it is exit 2, and the message names what was found.
 */
function checkBaselineSide(side, pin) {
    if (!side.exists) {
        return (
            `the baseline install is not there: ${side.dir} has no bin/${CAMEL_BINARY}. ` +
            'Build the baseline commit once and copy the install aside -- ' +
            '`cp -a ~/camel-work/<checkout>/out/latest ' + side.dir + '` -- then re-run.'
        )
    }
    if (!side.stamp) {
        return (
            `the baseline install has no build-info.json: ${side.dir}. Without the stamp the gate ` +
            'cannot say which commit it is comparing against, and a baseline nobody can name is not ' +
            'a baseline. Copy the whole install root, not only bin/.'
        )
    }
    const found = String(side.stamp.revision || '')
    if (found !== pin.commit) {
        return (
            `the baseline install is a build of ${found || '(no revision)'}, and baseline.json pins ` +
            `${pin.commit}. Either copy the install of the pinned commit or move the pin -- the gate ` +
            'will not compare against an unnamed baseline.'
        )
    }
    if (side.stamp.dirty) {
        return (
            'the baseline install was built from a dirty tree ' +
            `(${Number(side.stamp.dirtyCount) || 'some'} path(s)); a dirty build is not the commit ` +
            'the pin names.'
        )
    }
    return null
}

function checkLaneSide(side) {
    if (!side.exists) {
        return (
            `this checkout has no install at ${side.dir}: build it on the host first -- ` +
            '`internal/tools/camel-remote.ps1 -Path <this worktree> -Plan <any plan> -Jobs 8` -- ' +
            'and re-run the gate.'
        )
    }
    return null
}

// --- the configuration handed to the comparison ------------------------------

function configFor(gate, pin, lane, jobs) {
    return {
        '//': [
            'Generated by tools/diff-gate/gate.mjs from baseline.json; do not edit and do not commit',
            'a copy of it. Rewriting it by hand is how a gate starts comparing two questions.'
        ],
        label: `stage2-${gate.item}-${Object.keys(pin.gates).find((key) => pin.gates[key] === gate)}`,
        a: { name: 'baseline', commit: pin.baseline.commit, install: pin.baseline.install },
        b: { name: 'lane', commit: lane.revision || null, install: pin.lane.install },
        select: { plans: gate.plans, skip_tiers: ['debug'] },
        compare: { output: true, graph: true, time: true },
        output: { bound_ulp: 2, bits: 53, overrides: [] },
        graph: { pass: 'std::gir' },
        timeout_scale: 2,
        jobs
    }
}

// --- running the comparison --------------------------------------------------

function runComparison(configPath, verbose) {
    return new Promise((resolve) => {
        const args = [COMPARE_TOOL, '--config', configPath]
        const child = spawn(process.execPath, args, {
            cwd: REPO_ROOT,
            stdio: ['ignore', 'pipe', 'pipe'],
            env: { ...process.env }
        })
        const out = []
        const err = []
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdout.on('data', (chunk) => {
            out.push(chunk)
            if (verbose) process.stdout.write(chunk)
        })
        child.stderr.on('data', (chunk) => {
            err.push(chunk)
            if (verbose) process.stderr.write(chunk)
        })
        child.on('error', (error) => resolve({ code: 2, stdout: out.join(''), stderr: `${err.join('')}\n${error.message}` }))
        child.on('close', (code) => resolve({ code: code === null ? 2 : code, stdout: out.join(''), stderr: err.join('') }))
    })
}

/// The `[TOTAL]` line is where the comparison states the numbers the gate reads; the `[OUTPUT]` and
/// `[GRAPH]` lines are read for the two counters it does not print there. A line this file cannot
/// find is not "no differences" -- it is a reading that was not taken.
function readTotals(stdout) {
    const totals = { output_differ: null, graph_differ: null, measured: null, compared: null, unmeasurable: null }
    const patterns = {
        output_differ: /\[TOTAL\][^\n]*\boutput_differ=(\d+)/,
        graph_differ: /\[TOTAL\][^\n]*\bgraph_differ=(\d+)/,
        measured: /\[TOTAL\][^\n]*\bmeasured=(\d+)/,
        compared: /\[TOTAL\][^\n]*\bcompared=(\d+)/,
        unmeasurable: /\[TOTAL\][^\n]*\bunmeasurable=(\d+)/
    }
    let sawTotal = false
    for (const line of stdout.split('\n')) {
        if (!line.startsWith('[TOTAL]')) continue
        sawTotal = true
        for (const [key, pattern] of Object.entries(patterns)) {
            const match = line.match(pattern)
            if (match) totals[key] = Number(match[1])
        }
    }
    return { sawTotal, totals }
}

function differenceLines(stdout) {
    const kept = []
    for (const line of stdout.split('\n')) {
        if (line.startsWith('[DIFF]') || line.startsWith('[UNMEASURABLE]') || line.startsWith('  ')) kept.push(line)
    }
    return kept.slice(0, 60)
}

// --- the reading -------------------------------------------------------------

async function main(argv) {
    let options
    try {
        options = parseArguments(argv)
    } catch (error) {
        console.error(`[GATE] ${error.message}`)
        console.error(usage())
        return 2
    }
    if (options.help) {
        console.log(usage())
        return 0
    }
    if (!options.gate) {
        console.error('[GATE] --gate is required')
        console.error(usage())
        return 2
    }

    const pin = JSON.parse(fs.readFileSync(BASELINE_FILE, 'utf8'))
    // A gate is named by its key (`view`) or by the item it answers for (`2.1`); both land on one
    // entry, so a dispatch ticket may write either and read one thing.
    const key = Object.keys(pin.gates).find(
        (name) => name === options.gate || pin.gates[name].item === options.gate
    )
    if (!key) {
        console.error(
            `[GATE] no gate named '${options.gate}': baseline.json defines ` +
                Object.entries(pin.gates)
                    .map(([name, gate]) => `${name} (${gate.item})`)
                    .join(', ')
        )
        return 2
    }
    const gate = pin.gates[key]

    const baselineSide = describeSide(installOf(pin.baseline.install, 'baseline'))
    const laneSide = describeSide(installOf(pin.lane.install, 'lane'))
    const jobs = options.jobs || pin.jobs || 8
    const configPath = path.join(CONFIG_DIR, `${key}.json`)
    const config = configFor(gate, pin, laneSide, jobs)

    if (options.printConfig) {
        console.log(JSON.stringify({ gate: key, item: gate.item, config }, null, 2))
        return 0
    }

    const refusedBaseline = checkBaselineSide(installOf(pin.baseline.install, 'baseline'), pin.baseline)
    if (refusedBaseline) {
        console.error(`[GATE ${gate.item} ${key}] refused: ${refusedBaseline}`)
        return 2
    }
    const refusedLane = checkLaneSide(installOf(pin.lane.install, 'lane'))
    if (refusedLane) {
        console.error(`[GATE ${gate.item} ${key}] refused: ${refusedLane}`)
        return 2
    }
    if (!fs.existsSync(COMPARE_TOOL)) {
        console.error(`[GATE ${gate.item} ${key}] refused: the comparison tool is missing: ${COMPARE_TOOL}`)
        return 2
    }

    fs.mkdirSync(CONFIG_DIR, { recursive: true })
    fs.writeFileSync(configPath, JSON.stringify(config, null, 2))
    const result = await runComparison(configPath, options.verbose)

    // The child's own exit 2 is "the configuration or a side is unusable", which is not a reading
    // and must not be reported as one.
    if (result.code === 2) {
        console.error(`[GATE ${gate.item} ${key}] refused: the comparison could not run`)
        console.error(result.stderr.split('\n').filter(Boolean).slice(-10).join('\n'))
        return 2
    }

    const { sawTotal, totals } = readTotals(result.stdout)
    if (!sawTotal) {
        console.error(`[GATE ${gate.item} ${key}] refused: the comparison printed no [TOTAL] line`)
        console.error(result.stdout.split('\n').filter(Boolean).slice(-10).join('\n'))
        return 2
    }
    for (const field of ['output_differ', 'graph_differ', 'measured']) {
        if (totals[field] === null) {
            console.error(`[GATE ${gate.item} ${key}] refused: the [TOTAL] line does not name ${field}`)
            return 2
        }
    }
    const differences = totals.output_differ + totals.graph_differ
    const unmeasurable = totals.unmeasurable === null ? 0 : totals.unmeasurable
    const measured = totals.measured
    const verdict = differences > 0 ? 'RED' : 'GREEN'
    console.log(
        `[GATE ${gate.item} ${key}] baseline=${pin.baseline.short} lane=${laneSide.short} ` +
            `cases=${measured} output_differ=${totals.output_differ} graph_differ=${totals.graph_differ} ` +
            `differences=${differences} unmeasurable=${unmeasurable} verdict=${verdict}`
    )

    // "Identical" over a set that measured nothing is not a reading, and it is exactly what a gate
    // that only ever says GREEN would print.
    if (measured === 0 || measured - unmeasurable === 0) {
        console.error(
            `[GATE ${gate.item} ${key}] refused: ${measured} case(s) measured, ${unmeasurable} ` +
                'unmeasurable -- there is nothing here to be the same as anything'
        )
        return 2
    }
    if (unmeasurable > 0) {
        console.error(
            `[GATE ${gate.item} ${key}] warning: ${unmeasurable} case(s) could not be measured; ` +
                'they are not counted as differences and they are not evidence either'
        )
    }

    if (differences > 0) {
        for (const line of differenceLines(result.stdout)) console.error(line)
    }
    return differences > 0 ? 1 : 0
}

process.exitCode = await main(process.argv.slice(2))