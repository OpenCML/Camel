#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Where one build's time went, read from the two logs the build leaves: the generator's own log of
 * every step it ran (`build/.ninja_log`: start and end in milliseconds, then the output) and
 * ccache's statistics log for this run (`CCACHE_STATSLOG`: one line per counter a compilation
 * bumped). A slow build is then a number with a cause beside it: how many units were compiled, how
 * many of them ccache answered, how long the link steps took, and which steps took longest.
 *
 * Usage:
 *   node scripts/build-report.mjs --ninja-log <file> [--from-line <n>] [--ccache-log <file>] [--top <n>]
 *
 * `--from-line` is how many lines the generator's log had before the build started; only the
 * lines after it belong to this build. Either log may be missing (no ccache on the host, a tree
 * that never built): the report says so and the exit code stays 0, because a report must not stop
 * the run it describes.
 *
 * Output, one line each:
 *   BUILD_STEPS steps=<n> compile_ms=<sum> link_ms=<sum> longest=<output>:<ms>;...
 *   BUILD_CCACHE hits=<n> misses=<n>
 */

import fs from 'node:fs'
import { fileURLToPath } from 'node:url'

const LINK_OUTPUT = /(\.so(\.\d+)*|\.dll|\.dylib|\.a|\.lib|\.exe)$|(^|\/)camel$/

function option(args, name, fallback) {
    const at = args.indexOf(name)
    return at >= 0 && at + 1 < args.length ? args[at + 1] : fallback
}

/** The steps of one build: the generator's log lines after `fromLine`, a header line excluded. */
export function buildSteps(text, fromLine) {
    const lines = text.split(/\r?\n/).slice(fromLine)
    const steps = []
    for (const line of lines) {
        if (!line || line.startsWith('#')) continue
        const fields = line.split('\t')
        if (fields.length < 4) continue
        const start = Number(fields[0])
        const end = Number(fields[1])
        if (!Number.isFinite(start) || !Number.isFinite(end)) continue
        steps.push({ output: fields[3], ms: Math.max(0, end - start) })
    }
    return steps
}

/** The counts of one run's ccache statistics log: a line ending in `_cache_hit` is a hit. */
export function ccacheCounts(text) {
    let hits = 0
    let misses = 0
    for (const line of text.split(/\r?\n/)) {
        const word = line.trim()
        if (word.endsWith('_cache_hit')) hits += 1
        else if (word === 'cache_miss') misses += 1
    }
    return { hits, misses }
}

export function summarize(steps, top) {
    let compileMs = 0
    let linkMs = 0
    for (const step of steps) {
        if (LINK_OUTPUT.test(step.output)) linkMs += step.ms
        else compileMs += step.ms
    }
    const longest = [...steps]
        .sort((a, b) => b.ms - a.ms)
        .slice(0, top)
        .map((step) => `${step.output}:${step.ms}`)
        .join(';')
    return `BUILD_STEPS steps=${steps.length} compile_ms=${compileMs} link_ms=${linkMs} longest=${longest || '-'}`
}

function main() {
    const args = process.argv.slice(2)
    const ninjaLog = option(args, '--ninja-log', '')
    const fromLine = Number(option(args, '--from-line', '0')) || 0
    const ccacheLog = option(args, '--ccache-log', '')
    const top = Number(option(args, '--top', '5')) || 5

    if (ninjaLog && fs.existsSync(ninjaLog)) {
        process.stdout.write(`${summarize(buildSteps(fs.readFileSync(ninjaLog, 'utf8'), fromLine), top)}\n`)
    } else {
        process.stdout.write(`BUILD_STEPS unread: no generator log at ${ninjaLog || '(not given)'}\n`)
    }
    if (ccacheLog && fs.existsSync(ccacheLog)) {
        const { hits, misses } = ccacheCounts(fs.readFileSync(ccacheLog, 'utf8'))
        process.stdout.write(`BUILD_CCACHE hits=${hits} misses=${misses}\n`)
    } else {
        process.stdout.write(`BUILD_CCACHE unread: no statistics log at ${ccacheLog || '(not given)'}\n`)
    }
}

if (process.argv[1] && fs.realpathSync(process.argv[1]) === fs.realpathSync(fileURLToPath(import.meta.url))) {
    main()
}
