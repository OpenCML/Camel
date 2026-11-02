#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The value domains of the run options a plan writes in `args`, and the check that both sides of
 * that boundary still agree.
 *
 * A plan hands the CLI its words verbatim: `args = ["--gc-check", "verify=1"]`. The CLI refuses
 * `verify=1` when it parses the options -- but by then a test is already running, so the plan's
 * mistake arrives as a failed test, indistinguishable from a defect in the program that test runs.
 * Six entries of `test/plans/feat/linear/jit_framed.plan.toml` were read that way for a night:
 * `--gc-check verify=1` was investigated as a GC defect before anyone looked at the option.
 *
 * Two readers ask this module:
 *
 *   * the runner (`test/tools/run-tests.mjs`), at load, through `argProblems(args)`: a plan that
 *     names a value the CLI does not take is a *plan* error, reported with `plan:line` and the
 *     legal values, and the run refuses instead of turning it into a red test;
 *   * this file, run as a script: it reads `tools/camel-cli/config.cpp` and checks that the two
 *     sides say the same thing in both directions -- a value kept here that the CLI dropped, and a
 *     value the CLI gained that is not here yet (which would refuse a legal plan). A table that
 *     mirrors a parser has to be pinned to the parser, or it becomes a second truth.
 *
 * The words below were read out of `tools/camel-cli/config.cpp`: the `--gc-stress` and
 * `--gc-check` blocks of the run options, and `parseCollectorMode` for `--gc`. Written as the
 * parser reads them: `splitSpec` splits on commas into `key=value` words, and a key with no `=`
 * is a bare word whose value is "". `moves`, `poison` and `config` are matched by key alone, so
 * `moves=1` is accepted by the CLI and must not be refused here.
 *
 *   node test/tools/cli-arg-domains.mjs      check this table against tools/camel-cli/config.cpp
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const CWD = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(CWD, '..', '..')
const CLI_SOURCE = path.join(REPO_ROOT, 'tools', 'camel-cli', 'config.cpp')
// `--gc` is read by the allocator rather than by the option parser: the CLI hands the word to this
// function, which decides which collector modes exist.
const MM_SOURCE = path.join(REPO_ROOT, 'src', 'core', 'mm', 'mm.cpp')

// What the CLI takes, per option.
//
//   kind 'words'  -- a comma-separated list of words; `values: null` means the parser matches the
//                    key and ignores the value (`moves=1` is accepted), an array means the value
//                    must be one of them, 'number' means an unsigned decimal that fits the bound.
//   kind 'value'  -- one word, and it must be one of `values`.
export const ARG_DOMAINS = [
    {
        option: '--gc-check',
        kind: 'words',
        words: new Map([
            ['moves', null],
            ['poison', null],
            ['config', null],
            ['mark', null],
            ['verify', ['before', 'pre', 'after', 'post', 'both']],
        ]),
        summary: 'moves, poison, config, mark or verify=before|after|both (also pre, post)',
    },
    {
        option: '--gc-stress',
        kind: 'words',
        words: new Map([
            ['kind', ['minor', 'major', 'both']],
            ['alloc', 'number'],
            ['safepoint', 'number'],
        ]),
        summary: 'kind=minor|major|both, alloc=<n>, safepoint=<n>',
    },
    {
        option: '--gc',
        kind: 'value',
        values: ['generational', 'gen', '1', 'mark-sweep', 'marksweep', '0'],
        summary: 'generational|gen|1|mark-sweep|marksweep|0',
    },
]

const DOMAIN_BY_OPTION = new Map(ARG_DOMAINS.map((domain) => [domain.option, domain]))
// `readNumberOption` in the CLI: `from_chars` over the whole word, and this ceiling.
const MAX_NUMBER = 1 << 20

function numberValue(text) {
    if (!/^\d+$/.test(text)) return false
    return Number(text) <= MAX_NUMBER
}

/**
 * Does this word list name an option value the CLI does not take? Returns one entry per problem,
 * each carrying the option, the word as written and what the CLI does take.
 *
 * Only the options above are looked at: every other word (a case path, a pass, a pass parameter
 * like `safe_fp=true`, `-vv`, `${case}`) is the runner's or the pass's business, and a check that
 * guessed at those would refuse plans that work.
 */
export function argProblems(args) {
    const problems = []
    const words = (args || []).map((word) => String(word))
    for (let index = 0; index < words.length; index++) {
        const word = words[index]
        let option = null
        let value = null
        let consumed = 1
        if (word.startsWith('--') && word.includes('=')) {
            option = word.slice(0, word.indexOf('='))
            value = word.slice(word.indexOf('=') + 1)
        } else if (DOMAIN_BY_OPTION.has(word)) {
            option = word
            const next = words[index + 1]
            if (next === undefined || next.startsWith('--')) {
                problems.push({ option, word, reason: `${option} needs a value after it` })
                continue
            }
            value = next
            consumed = 2
        }
        if (option === null || !DOMAIN_BY_OPTION.has(option)) continue
        const domain = DOMAIN_BY_OPTION.get(option)
        index += consumed - 1
        const reason = checkValue(domain, value)
        if (reason) problems.push({ option, word: consumed === 2 ? `${option} ${value}` : word, reason })
    }
    return problems
}

function checkValue(domain, value) {
    if (domain.kind === 'value') {
        // An empty value is "not given" to the CLI, which then keeps its default; only a word it
        // cannot read is an error.
        if (value === '') return null
        if (!domain.values.includes(value)) return `expected ${domain.summary}`
        return null
    }
    let said = false
    for (const word of String(value).split(',')) {
        if (word === '') continue
        const equals = word.indexOf('=')
        const key = equals === -1 ? word : word.slice(0, equals)
        const itemValue = equals === -1 ? '' : word.slice(equals + 1)
        if (!domain.words.has(key)) {
            return `'${key}' is not one of the words ${domain.option} takes: ${domain.summary}`
        }
        const allowed = domain.words.get(key)
        if (allowed === null) continue
        if (allowed === 'number') {
            if (!numberValue(itemValue)) {
                return `${key}= takes a whole number from 0 to ${MAX_NUMBER}, got '${itemValue}'`
            }
            continue
        }
        if (!allowed.includes(itemValue)) {
            said = true
            return `'${key}=${itemValue}' is not one the CLI takes: ${domain.option} reads ${domain.summary}`
        }
    }
    return said ? 'unreachable' : null
}

// ---------------------------------------------------------------------------------------------
// The check against the CLI's own parser.

function readSource(file) {
    if (!fs.existsSync(file)) {
        return { error: `${path.relative(REPO_ROOT, file)} does not exist` }
    }
    return { lines: fs.readFileSync(file, 'utf8').split(/\r?\n/) }
}

function region(lines, from, to) {
    const start = lines.findIndex((line) => line.includes(from))
    if (start === -1) return null
    const end = lines.findIndex((line, index) => index > start && line.includes(to))
    return lines.slice(start, end === -1 ? lines.length : end)
}

function parseWordBlocks(block) {
    // `splitSpec` words are matched as `word.key == "..."` and their values as `word.value == "..."`,
    // and one line may match several values (`value == "before" || value == "pre"`).
    const keys = new Set()
    const values = new Map()
    let current = null
    for (const line of block) {
        const key = line.match(/word\.key\s*==\s*"([^"]+)"/)
        if (key) {
            current = key[1]
            keys.add(current)
            if (!values.has(current)) values.set(current, new Set())
            continue
        }
        for (const match of line.matchAll(/word\.value\s*==\s*"([^"]+)"/g)) {
            if (current !== null) values.get(current).add(match[1])
        }
    }
    return { keys, values }
}

function parseCollectorMode(block) {
    const values = new Set()
    for (const line of block) {
        for (const match of line.matchAll(/text\s*==\s*"([^"]+)"/g)) values.add(match[1])
    }
    return values
}

function compareSets(said, found, what, report) {
    const extra = [...said].filter((item) => !found.has(item))
    const missing = [...found].filter((item) => !said.has(item))
    if (extra.length === 0 && missing.length === 0) {
        report('ok', `${what}: ${[...said].sort().join(', ') || '(none)'}`)
        return true
    }
    if (extra.length > 0) report('FAIL', `${what}: this table keeps ${extra.join(', ')}, which the CLI no longer matches`)
    if (missing.length > 0) report('FAIL', `${what}: the CLI matches ${missing.join(', ')}, which this table would refuse`)
    return false
}

function selfCheck() {
    let failed = false
    const report = (kind, line) => {
        console.log(`${kind === 'ok' ? 'ok  ' : 'FAIL'} ${line}`)
        if (kind !== 'ok') failed = true
    }
    const cli = readSource(CLI_SOURCE)
    if (cli.error) {
        report('FAIL', cli.error)
        return false
    }
    const mm = readSource(MM_SOURCE)
    if (mm.error) {
        report('FAIL', mm.error)
        return false
    }
    const lines = cli.lines

    const checks = region(lines, 'splitSpec(Run::gcCheckOpt)', 'selectAllocatorChecks')
    const stress = region(lines, 'splitSpec(Run::gcStressOpt)', 'splitSpec(Run::gcCheckOpt)')
    const collector = region(mm.lines, 'parseCollectorMode(std::string_view text)', 'return std::nullopt;')
    if (!checks || !stress || !collector) {
        report('FAIL', `${path.relative(REPO_ROOT, CLI_SOURCE)} or ${path.relative(REPO_ROOT, MM_SOURCE)} no ` +
            `longer has the blocks this table mirrors (--gc-stress, --gc-check, parseCollectorMode)` +
            (!checks ? ' [--gc-check block missing]' : '') +
            (!stress ? ' [--gc-stress block missing]' : '') +
            (!collector ? ' [parseCollectorMode missing]' : ''))
        return false
    }

    const checkSaid = DOMAIN_BY_OPTION.get('--gc-check')
    const stressSaid = DOMAIN_BY_OPTION.get('--gc-stress')
    const collectorSaid = DOMAIN_BY_OPTION.get('--gc')
    const checkFound = parseWordBlocks(checks)
    const stressFound = parseWordBlocks(stress)
    const collectorFound = parseCollectorMode(collector)

    if (!compareSets(new Set(checkSaid.words.keys()), checkFound.keys, '--gc-check words', report)) failed = true
    if (!compareSets(new Set(stressSaid.words.keys()), stressFound.keys, '--gc-stress keys', report)) failed = true
    for (const [option, said, found] of [
        ['--gc-check', checkSaid.words.get('verify'), checkFound.values.get('verify')],
        ['--gc-stress', stressSaid.words.get('kind'), stressFound.values.get('kind')],
    ]) {
        if (!compareSets(new Set(said || []), found || new Set(), `${option} ${option === '--gc-check' ? 'verify=' : 'kind='}`, report)) {
            failed = true
        }
    }
    if (!compareSets(new Set(collectorSaid.values), collectorFound, '--gc collectors', report)) failed = true

    // A block whose words this table does not mention at all would pass every comparison above by
    // being empty; both sides must have been read.
    if (checkFound.keys.size === 0 || stressFound.keys.size === 0 || collectorFound.size === 0) {
        report('FAIL', 'a block of the CLI source was read as empty: the selection above is wrong, not the CLI')
        failed = true
    }
    return !failed
}

if (process.argv[1] && path.resolve(process.argv[1]) === url.fileURLToPath(import.meta.url)) {
    const ok = selfCheck()
    console.log(ok ? 'PASS' : 'FAIL')
    process.exit(ok ? 0 : 1)
}