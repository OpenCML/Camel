// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The context a red table was taken under, and the comparison that refuses to ignore it.
 *
 * A red table is a claim about a tree *and* about the machine that measured it. Two false reds in one
 * night (a timeout margin under load 84 a neighbour writing scratch under
 * `-Jobs 8`) were indistinguishable from a real red, because the tables did not say what `jobs` and
 * `load` they were taken at. The rule this module implements:
 *
 *   a table states its `tip`, `jobs` and `load`; a comparison of two tables whose `jobs` or `load`
 *   differ says "not directly comparable" and exits 2, instead of listing every row as `new`.
 *
 * Two reasons the comparison is deliberately strict:
 *
 *   * `jobs` changes the case set as well as the pressure: a narrower run starts fewer plans, and a
 *     case that never started is "missing", which the diff would read as `removed`.
 *   * `load` changes whether a timing case can measure its own margin at all. A quiet-host table and
 *     a loaded one are two different instruments, not two readings of one instrument.
 *
 * A name is read from a whole field (a tab-separated column, or the whole remainder of a `[FAIL]`
 * line) and never with `\S+`: a case whose name contains a space (`... safe_fp=true safe_fp=false`)
 * would otherwise be truncated, and a truncated name is `removed` in the diff even though the case
 * ran. `parseVerdictLines` is the one reader of a transcript's verdict lines.
 */

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname } from 'node:path'

export const FORMAT = 'red-table v1'

/** The quiet/loud boundary the runner's own `[load]` warning uses (camel-remote.ps1). */
export const LOAD_BOUNDARY = 8

const clean = (s) => String(s ?? '').replace(/[\t\r\n]+/g, ' ').trim()

/**
 * One verdict line, read whole.
 *
 * The runner prints `[FAIL] <case name><padding><metric>` and the padding is at least one space; a
 * case name may itself contain single spaces, so the only safe cut is the column gap (two or more
 * spaces) before the metric. A line with no gap is all name. The leading tag may be indented by a
 * nested run, which is why the tag is matched anywhere at the start of the trimmed line.
 *
 * `\S+` is exactly the bug this replaces: it stops at the first space and hands the caller a shorter
 * name, which the diff then reports as a case that disappeared.
 */
export function parseVerdictLines(text) {
    const out = []
    for (const raw of String(text ?? '').split('\n')) {
        // A verdict line starts in column 0. An indented line is a detail of the case above it
        // (`    [INFO] ...`), and reading one as a verdict invents a case the run never started.
        const line = raw.replace(/\x1b\[[0-9;]*m/g, '').replace(/\r$/, '')
        const m = line.match(/^\[(PASS|FAIL|SKIP|XFAIL)\]\s+(.*)$/)
        if (!m) continue
        const rest = m[2]
        const gap = rest.match(/^(.*?)\s{2,}(.*)$/)
        const name = (gap ? gap[1] : rest).trim()
        const metric = gap ? gap[2].trim() : ''
        if (!name) continue
        out.push({ verdict: m[1], name, metric })
    }
    return out
}

/**
 * Write a table that carries the conditions it was taken under.
 *
 * The header is `#` lines so `cut`/`awk` and every existing reader of a two-column table keep
 * working; the fields are the four a comparison needs and two that say where the numbers came from
 * (`jobs-source`, `load-source`): a table whose `jobs` was passed on the command line is not the same
 * evidence as one whose `jobs` was read from the runs that produced the rows, and a reader that
 * cannot tell them apart will trust the wrong one.
 */
export function writeRedTable(file, provenance, rows) {
    const p = provenance ?? {}
    const load = Array.isArray(p.load) && p.load.length ? p.load.map((n) => String(n)) : null
    const head = [
        `# ${FORMAT}`,
        `# generated-at ${clean(p.generatedAt ?? new Date().toISOString())}`,
        `# tip ${clean(p.tip ?? 'unknown')}`,
        `# anchor ${clean(p.anchor ?? 'unknown')}`,
        `# jobs ${p.jobs ?? 'unknown'}`,
        `# jobs-source ${clean(p.jobsSource ?? (p.jobs ? 'stated' : 'unknown'))}`,
        `# load ${load ? load.join(',') : 'unknown'}`,
        `# load-source ${clean(p.loadSource ?? (load ? 'stated' : 'unknown'))}`,
        `# tier ${clean(p.tier ?? 'unknown')}`,
        `# source ${clean(p.source ?? '')}`,
        `# rows ${rows.length}`,
    ]
    const body = rows.map((r) =>
        [clean(r.plan), clean(r.case), clean(r.why), clean(r.record), clean(r.observedAt), clean(r.cls)].join('\t')
    )
    mkdirSync(dirname(file), { recursive: true })
    writeFileSync(file, `${head.join('\n')}\nplan\tcase\twhy\trecord\tobserved-at\tclass\n${body.join('\n')}${body.length ? '\n' : ''}`, 'utf8')
}

function parseHeader(lines) {
    const head = new Map()
    for (const line of lines) {
        if (!line.startsWith('#')) continue
        const m = line.match(/^#\s*([A-Za-z][A-Za-z0-9-]*)\s*(.*)$/)
        if (!m) continue
        head.set(m[1], m[2].trim())
    }
    const load = head.get('load')
    return {
        format: head.get('format') === FORMAT ? FORMAT : head.get('format') ?? null,
        generatedAt: head.get('generated-at') ?? null,
        tip: head.get('tip') && head.get('tip') !== 'unknown' ? head.get('tip') : null,
        anchor: head.get('anchor') && head.get('anchor') !== 'unknown' ? head.get('anchor') : null,
        jobs: /^\d+$/.test(String(head.get('jobs') ?? '')) ? Number(head.get('jobs')) : head.get('jobs') ?? null,
        jobsSource: head.get('jobs-source') ?? null,
        load: load && load !== 'unknown' ? load.split(',').map((s) => s.trim()) : null,
        loadSource: head.get('load-source') ?? null,
        tier: head.get('tier') && head.get('tier') !== 'unknown' ? head.get('tier') : null,
        source: head.get('source') ?? '',
        rows: Number(head.get('rows') ?? NaN),
    }
}

/**
 * Read a red table, a transcript, or a bare `plan<TAB>case` list.
 *
 * `provenance` is `null` for the last two, and that is the point: a file that does not say what it was
 * taken at is not a table this module will compare (the caller can override deliberately). The bare
 * two-column form is what `internal/reports/*-reds.tsv` used before this module; refusing it is not a
 * judgment on that file, it is the statement that a comparison needs more than two columns.
 */
export function readRedTable(file) {
    const text = readFileSync(file, 'utf8')
    const lines = text.split('\n').map((l) => l.replace(/\r$/, ''))
    const rows = new Map()
    const hasHeader = lines.some((l) => l.trim() === `# ${FORMAT}`)
    if (hasHeader) {
        const provenance = parseHeader(lines)
        for (const line of lines) {
            if (!line.trim() || line.startsWith('#')) continue
            const [plan, kase, why = '', record = '', observedAt = '', cls = ''] = line.split('\t')
            if (plan === 'plan' && kase === 'case') continue // the column header
            if (!plan || !kase) continue
            rows.set(`${plan}/${kase}`, { plan, case: kase, why, record, observedAt, cls })
        }
        return { provenance, rows, format: 'tsv' }
    }
    const verdicts = parseVerdictLines(text)
    if (verdicts.length) {
        for (const v of verdicts) {
            if (v.verdict !== 'FAIL') continue
            const [plan, ...rest] = v.name.split('/')
            rows.set(v.name, { plan, case: rest.join('/'), why: '', record: '', observedAt: '', cls: '' })
        }
        return { provenance: null, rows, format: 'verdicts' }
    }
    for (const line of lines) {
        if (!line.trim() || line.startsWith('#')) continue
        const fields = line.split('\t')
        if (fields.length < 2) continue
        const [plan, kase, why = ''] = fields
        if (plan === 'plan' && kase === 'case') continue
        rows.set(`${plan}/${kase}`, { plan, case: kase, why, record: '', observedAt: '', cls: '' })
    }
    return { provenance: null, rows, format: 'bare' }
}

const whyKey = (row) => clean(row?.why).replace(/\s+/g, ' ')

/**
 * Compare two tables, and refuse when their conditions differ.
 *
 * What is **not** compared: the `tip`. Two tables taken at different tips are the ordinary case (a
 * baseline and the tree it is compared against), and the tip is printed so the difference is visible.
 * What **is**: `jobs`, `load`, and `tier` -- `tier` changes the case set the same way `jobs` does.
 * Unknown or mixed values refuse rather than pass: an override that says "compare anyway" is a
 * decision the caller makes out loud (`allowUnknownLoad` / `allowUnknownJobs`).
 */
export function compareRedTables(fileA, fileB, options = {}) {
    const a = readRedTable(fileA)
    const b = readRedTable(fileB)
    const base = { counts: { new: 0, fixed: 0, changed: 0, removed: 0 }, lists: { new: [], fixed: [], changed: [], removed: [] } }
    const refuse = (reason) => ({ ...base, comparable: false, code: 2, reason, a, b })

    if (!a.provenance || !b.provenance) {
        const missing = [!a.provenance ? 'A' : null, !b.provenance ? 'B' : null].filter(Boolean)
        return refuse(
            `not directly comparable: ${missing.join(' and ')} ${missing.length > 1 ? 'carry' : 'carries'} no provenance header, so ${missing.length > 1 ? 'they do' : 'it does'} not say at which jobs/load ${missing.length > 1 ? 'they were' : 'it was'} taken. ` +
            `A two-column list or a transcript is an observation, not a baseline: add the header (red-table.mjs --tsv) or compare it with itself.`
        )
    }

    const jobValue = (p) => {
        const v = String(p.jobs ?? 'unknown').trim()
        if (/^\d+$/.test(v)) return Number(v)
        return null // `unknown` or `mixed a,b`
    }
    const ja = jobValue(a.provenance)
    const jb = jobValue(b.provenance)
    const jobsText = `A jobs=${a.provenance.jobs ?? 'unknown'}, B jobs=${b.provenance.jobs ?? 'unknown'}`
    if (ja === null || jb === null) {
        if (!options.allowUnknownJobs) {
            return refuse(`not directly comparable: ${jobsText}; a table whose jobs is unknown or mixed cannot be compared with a reading taken at a known width.`)
        }
    } else if (ja !== jb) {
        return refuse(
            `not directly comparable: ${jobsText}. A narrower run starts fewer plans, and a case that never started is missing, not removed -- ` +
            `take both tables at one width before diffing them.`
        )
    }

    const loadValue = (p) => (p.load && p.load.length && p.load[0] !== 'unknown' && Number.isFinite(Number(p.load[0])) ? Number(p.load[0]) : null)
    const la = loadValue(a.provenance)
    const lb = loadValue(b.provenance)
    const loadText = `A load=${a.provenance.load ? a.provenance.load.join('/') : 'unknown'}, B load=${b.provenance.load ? b.provenance.load.join('/') : 'unknown'}`
    if (la === null || lb === null) {
        if (!options.allowUnknownLoad) {
            return refuse(`not directly comparable: ${loadText}; a table that does not say its load cannot be told from one taken under load.`)
        }
    } else {
        const quietA = la <= LOAD_BOUNDARY
        const quietB = lb <= LOAD_BOUNDARY
        if (quietA !== quietB) {
            return refuse(
                `not directly comparable: ${loadText} -- one was taken on a quiet host (<= ${LOAD_BOUNDARY}) and the other under load. ` +
                `A timing case measures its own margin against that load, so a red there says "this machine could not measure it", not "this case is wrong".`
            )
        }
    }

    const tierA = a.provenance.tier
    const tierB = b.provenance.tier
    if (tierA && tierB && tierA !== tierB && !options.allowDifferentTier) {
        return refuse(`not directly comparable: A tier=${tierA}, B tier=${tierB}; a tier decides which cases exist.`)
    }

    // A red table holds reds only, so "in A and not in B" means **no longer red**, not "the case
    // is gone". Those are two different claims and only a case inventory can tell them apart: the
    // older comparison reported the ten names with spaces as `removed` because it had a list of
    // cases and the truncated names were not in it. Without an inventory this module
    // says `fixed` and leaves `removed` at zero -- "I cannot tell" is not "it was deleted".
    const inventory = options.cases ? new Set([...options.cases]) : null
    const names = new Set([...a.rows.keys(), ...b.rows.keys()])
    for (const name of names) {
        const ra = a.rows.get(name)
        const rb = b.rows.get(name)
        if (ra && !rb) {
            if (inventory && !inventory.has(name)) base.lists.removed.push(name)
            else base.lists.fixed.push(name)
        } else if (!ra && rb) base.lists.new.push(name)
        else if (whyKey(ra) !== whyKey(rb)) base.lists.changed.push(name)
        else base.countsUnchanged = (base.countsUnchanged ?? 0) + 1
    }
    for (const k of Object.keys(base.lists)) {
        base.lists[k].sort()
        base.counts[k] = base.lists[k].length
    }
    const differences = base.counts.new + base.counts.changed + base.counts.removed + base.counts.fixed
    return {
        ...base,
        comparable: true,
        code: differences > 0 ? 1 : 0,
        hasInventory: !!inventory,
        caution: la !== null && la > LOAD_BOUNDARY ? `both tables were taken under load (${la} and ${lb}); every new/changed row is still suspect` : '',
        a,
        b,
    }
}

/** The comparison on stdout: the verdict first, then the rows it is about -- never the other order. */
export function formatComparison(result) {
    if (!result.comparable) {
        return `[red-table] ${result.reason}\n`
    }
    const p = result.a.provenance
    const q = result.b.provenance
    const lines = []
    lines.push(
        `[red-table] comparable: jobs ${p.jobs} both; load ${p.load ? p.load.join('/') : 'unknown'} vs ${q.load ? q.load.join('/') : 'unknown'}; ` +
        `tier ${p.tier ?? 'unknown'}/${q.tier ?? 'unknown'}`
    )
    lines.push(`[red-table] tip ${p.tip ? p.tip.slice(0, 12) : 'unknown'} -> ${q.tip ? q.tip.slice(0, 12) : 'unknown'}`)
    if (result.caution) lines.push(`[red-table] caution: ${result.caution}`)
    if (result.counts.fixed > 0 && !result.hasInventory) {
        lines.push(
            `[red-table] no case inventory (--cases): "red in A, absent from B" is counted as fixed -- it may also have been deleted, and this table cannot tell the two apart`
        )
    }
    lines.push(
        `[red-table] cases=${result.a.rows.size} new=${result.counts.new} fixed=${result.counts.fixed} ` +
        `changed=${result.counts.changed} removed=${result.counts.removed}`
    )
    for (const kind of ['new', 'changed', 'removed', 'fixed']) {
        if (!result.lists[kind].length) continue
        lines.push(`${kind}:`)
        for (const name of result.lists[kind]) lines.push(`  ${name}`)
    }
    return `${lines.join('\n')}\n`
}

/**
 * The provenance a generator knows at the moment it writes, with the load it was told about.
 *
 * `load` accepts `84.5` or `84.5,87.2,90.1`; a table that does not say is `unknown`, not zero, and the
 * comparison treats it as such.
 */
export function provenanceFrom({ tip, anchor, jobs, jobsSource, load, loadSource, tier, generatedAt, source } = {}) {
    const nums = load === null || load === undefined || load === '' ? null :
        String(load).split(/[,\s]+/).filter(Boolean)
    return {
        tip, anchor, jobs: jobs ?? 'unknown', jobsSource: jobsSource ?? (jobs ? 'stated' : 'unknown'),
        load: nums && nums.length ? nums : null, loadSource: loadSource ?? (nums ? 'stated' : 'unknown'),
        tier, generatedAt, source,
    }
}