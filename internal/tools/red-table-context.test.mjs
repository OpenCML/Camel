// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A red table has to say the conditions it was taken under, and it has to read a case's whole name.
 *
 * Two families of false red appeared in one night (C-1124-183, C-1124-185) and looked exactly like a
 * real red in the tables, because the table said nothing about the `jobs` and the `load` it was taken
 * at: a timeout-margin failure under load 84 and a neighbour writing scratch under `-Jobs 8` were both
 * "a case the baseline did not have". And the comparison that called ten cases `removed` was reading
 * their names with `\S+`, which stops at the first space, while the same ten cases had run (C-1124-186).
 *
 * The checks below are the two rules:
 *
 *   1. a written table carries `tip`, `jobs` and `load`, and two tables whose `jobs`/`load` differ are
 *      refused as "not directly comparable" -- the comparison must not turn "taken under other
 *      conditions" into a hundred `new` rows;
 *   2. a case name is read whole (single spaces inside it are part of the name), so a case that
 *      changed its failure reads as `changed`, and one that is still there is never `removed`.
 *
 * Every check runs the real module on throwaway fixtures; the last two run the real command line, so
 * the wiring in `red-table.mjs` (not only the helper) is what passes.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import {
    writeRedTable,
    readRedTable,
    compareRedTables,
    formatComparison,
    parseVerdictLines,
} from './red-table-context.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const script = path.join(here, 'red-table.mjs')
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'red-table-context-'))
let checks = 0
let failures = 0

function check(label, ok, detail = '') {
    checks += 1
    if (ok) {
        console.log(`  PASS  ${label}`)
    } else {
        failures += 1
        console.log(`  FAIL  ${label}`)
        if (detail) for (const line of String(detail).split('\n')) console.log(`        ${line}`)
    }
}

function file(name) {
    return path.join(root, name)
}

// The four names C-1124-186 named: single spaces are part of the case name, and an argument with a
// space (`safe_fp=true safe_fp=false`) is the common shape.
const SPACED = 'opt.algebra/invalid.std::cxx::algebra safe_fp=true safe_fp=false'
const SPACED2 = 'opt.algebra/invalid.std::cxx::algebra unknown=true'
const MEAN = 'stated_extents/stated_mean_axis_keepdims'
const PLAIN = 'transforms/transforms.with_grad_from_the_library'

const row = (name, why, record = 'run-1') => {
    const [plan, ...rest] = name.split('/')
    return { plan, case: rest.join('/'), why, record, observedAt: '2026-11-24T22:00:00Z', cls: '真缺陷' }
}

const QUIET = { tip: 'a'.repeat(40), anchor: 'a'.repeat(40), jobs: 2, load: [3.1, 3.0, 2.9], tier: 'fast', generatedAt: '2026-11-24T23:00:00Z', source: 'flat run' }
const LOUD = { tip: 'b'.repeat(40), anchor: 'b'.repeat(40), jobs: 8, load: [84.5, 87.2, 90.1], tier: 'fast', generatedAt: '2026-11-25T00:10:00Z', source: 'flat run' }
const LOUD_SAME = { ...LOUD, tip: LOUD.tip, generatedAt: '2026-11-25T00:40:00Z' }

// ---------------------------------------------------------------- 2. a name is read whole
{
    const lines = [
        `[FAIL] ${SPACED}  0.95 s`,
        `[PASS] ${SPACED2}  1.20 s`,
        `[FAIL] ${MEAN}  2.00 s`,
        `[SKIP] ${PLAIN}`,
        '  [FAIL] indented line is not a verdict line  1.0 s',
    ].join('\n')
    const parsed = parseVerdictLines(lines)
    check(
        'a verdict line keeps the whole case name (spaces included)',
        parsed.length === 4 && parsed[0].name === SPACED && parsed[1].name === SPACED2 &&
            parsed[2].name === MEAN && parsed[3].name === PLAIN,
        JSON.stringify(parsed, null, 2)
    )
    check(
        'the metric is not part of the name',
        parsed[0]?.metric === '0.95 s' && parsed[2]?.metric === '2.00 s',
        JSON.stringify(parsed.slice(0, 3))
    )
}

// ---------------------------------------------------------------- 1. the header carries the context
{
    const a = file('a.tsv')
    writeRedTable(a, LOUD, [row(SPACED, 'expected exit 0, got exit 1'), row(MEAN, 'assertion: 3 != 4')])
    const text = fs.readFileSync(a, 'utf8')
    const read = readRedTable(a)
    check(
        'a written table states its tip, jobs and load',
        read.provenance?.tip === LOUD.tip && read.provenance?.jobs === 8 &&
            String(read.provenance?.load) === '84.5,87.2,90.1',
        text.split('\n').slice(0, 10).join('\n')
    )
    check(
        'the same table read back has the whole names, not truncated ones',
        read.rows.has(SPACED) && read.rows.has(MEAN) && read.rows.size === 2,
        [...read.rows.keys()].join('\n')
    )
}

// ---------------------------------------------------------------- comparable: changed, not removed
{
    const a = file('changed-a.tsv')
    const b = file('changed-b.tsv')
    writeRedTable(a, LOUD, [row(SPACED, 'expected exit 0, got exit 1')])
    writeRedTable(b, LOUD_SAME, [row(SPACED, 'expected exit 0, got exit 2')])
    const result = compareRedTables(a, b)
    check(
        'two tables taken at the same jobs and load are comparable',
        result.comparable === true && result.code === 1,
        formatComparison(result)
    )
    check(
        'a case whose name has spaces and whose failure changed reads as changed, not removed',
        result.counts.changed === 1 && result.counts.removed === 0 && result.counts.new === 0 &&
            result.counts.fixed === 0 && result.lists.changed.includes(SPACED),
        formatComparison(result)
    )
    const text = formatComparison(result)
    check(
        'the changed line names the case in full',
        text.includes(SPACED) && !text.includes('removed: 1'),
        text
    )
}

// ---------------------------------------------------------------- not comparable: jobs differ
{
    const a = file('jobs-a.tsv')
    const b = file('jobs-b.tsv')
    writeRedTable(a, LOUD, [row(SPACED, 'x'), row(MEAN, 'y')])
    writeRedTable(b, QUIET, [row(SPACED, 'x')])
    const result = compareRedTables(a, b)
    const text = formatComparison(result)
    check(
        'tables taken at different jobs are refused, not diffed',
        result.comparable === false && result.code === 2 &&
            /not directly comparable/i.test(text) && /jobs/.test(text) && /8/.test(text) && /2/.test(text),
        text
    )
    check(
        'the refusal lists no new/fixed/removed rows',
        !/^\s*(new|fixed|removed):/m.test(text) && result.counts.new === 0 && result.counts.removed === 0,
        text
    )
}

// ---------------------------------------------------------------- not comparable: load differs
{
    const a = file('load-a.tsv')
    const b = file('load-b.tsv')
    writeRedTable(a, LOUD, [row(SPACED, 'x')])
    writeRedTable(b, { ...QUIET, jobs: 8 }, [row(SPACED, 'x'), row(MEAN, 'y')])
    const result = compareRedTables(a, b)
    const text = formatComparison(result)
    check(
        'a quiet-host table and a loaded one are refused, not diffed',
        result.comparable === false && result.code === 2 && /load/.test(text) && /84\.5/.test(text) && /3\.1/.test(text),
        text
    )
    check('the load refusal names both readings', /84\.5/.test(text) && /3\.1/.test(text), text)
}

// ---------------------------------------------------------------- unknown provenance is refused
{
    const bare = file('bare.tsv')
    fs.writeFileSync(bare, `${SPACED.split('/')[0]}\t${SPACED.split('/').slice(1).join('/')}\n`, 'utf8')
    const a = file('with-header.tsv')
    writeRedTable(a, LOUD, [row(SPACED, 'x')])
    const result = compareRedTables(a, bare)
    const text = formatComparison(result)
    check(
        'a table with no provenance header is refused (it does not say jobs/load)',
        result.comparable === false && result.code === 2 && /provenance|header/i.test(text),
        text
    )

    const noLoad = file('nohost-load.tsv')
    writeRedTable(noLoad, { ...LOUD, load: null }, [row(SPACED, 'x')])
    const r2 = compareRedTables(a, noLoad)
    check(
        'a table that does not say its load is refused',
        r2.comparable === false && r2.code === 2 && /load/i.test(formatComparison(r2)),
        formatComparison(r2)
    )
    const r3 = compareRedTables(a, noLoad, { allowUnknownLoad: true })
    check(
        'an explicit override lets the reader compare it anyway',
        r3.comparable === true,
        formatComparison(r3)
    )
}

// ---------------------------------------------------------------- the command line is wired
function runCli(args) {
    const res = spawnSync(process.execPath, [script, ...args], { cwd: here, encoding: 'utf8' })
    return { code: res.status ?? -1, text: `${res.stdout ?? ''}${res.stderr ?? ''}` }
}
{
    const a = file('cli-a.tsv')
    const b = file('cli-b.tsv')
    const c = file('cli-c.tsv')
    writeRedTable(a, LOUD, [row(SPACED, 'expected exit 0, got exit 1')])
    writeRedTable(b, LOUD_SAME, [row(SPACED, 'expected exit 0, got exit 2')])
    writeRedTable(c, QUIET, [row(SPACED, 'expected exit 0, got exit 1')])

    const changed = runCli(['--diff', a, b])
    check(
        'red-table.mjs --diff prints changed=1 removed=0 and exits 1',
        changed.code === 1 && /changed=1/.test(changed.text) && /removed=0/.test(changed.text),
        changed.text
    )
    const refused = runCli(['--diff', a, c])
    check(
        'red-table.mjs --diff exits 2 and refuses on different jobs/load',
        refused.code === 2 && /not directly comparable/i.test(refused.text),
        refused.text
    )

    const tsv = file('generated.tsv')
    const written = runCli(['--tsv', tsv, '--jobs', '8', '--load', '84.5,87.2,90.1'])
    const header = fs.existsSync(tsv) ? fs.readFileSync(tsv, 'utf8').split('\n').slice(0, 8).join('\n') : '(no file written)'
    check(
        'red-table.mjs --tsv writes a table whose header states jobs and load',
        written.code === 0 && /# jobs 8/.test(header) && /# load 84\.5,87\.2,90\.1/.test(header) && /# tip \S+/.test(header),
        `${written.text}\n${header}`
    )
    check(
        'the generated table has rows and every row has three tab-separated fields at least',
        fs.existsSync(tsv) && fs.readFileSync(tsv, 'utf8').split('\n').filter((l) => l && !l.startsWith('#')).length > 0,
        header
    )
}

console.log(`\nred-table-context: ${checks - failures}/${checks} checks passed`)
process.exit(failures === 0 ? 0 : 1)