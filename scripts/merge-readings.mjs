// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Two runs of the same set of plans, compared case by case.
 *
 * A gate that counts reds says "three failed before, three failed after" about a tree that broke
 * three new cases and fixed three old ones, and says the same about a tree where one case now fails
 * a different way. So the comparison is per case and has three outcomes, not one number:
 *
 *   fixed   -- red before, green now          (reported, never a reason to refuse)
 *   new     -- green before, red now          (a reason to refuse)
 *   changed -- red before, red now, and the failure reads differently (a reason to refuse)
 *
 * `changed` is the one this program exists for, and it is only meaningful because the first lines
 * are compared after `normalizeReading` (`test/tools/reading-normal.mjs`), which removes the parts
 * that differ between two runs of the *same* failure. A baseline measured under a different set of
 * normalization rules is not this baseline: `reading_version` travels in every row and in every
 * cache key.
 *
 * Exit codes, the same convention the clang syntax step uses:
 *   0  measured, and both `new` and `changed` are empty
 *   1  measured, and `new` or `changed` is not empty -- refuses the merge, and names them
 *   2  not measured (a reading is absent, unreadable, or from another rule set) -- refuses the merge
 *      and says what is missing, because "I could not measure" is not "your tree is wrong"
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { READING_NORMAL_VERSION } from '../test/tools/reading-normal.mjs'

const READINGS = new Map()

function readRows(file) {
    if (!fs.existsSync(file)) die(`no readings at ${file}`)
    const text = fs.readFileSync(file, 'utf8')
    const rows = new Map()
    let version = null
    for (const line of text.split('\n')) {
        if (!line.trim()) continue
        let row
        try {
            row = JSON.parse(line)
        } catch {
            die(`${file} holds a line that is not a reading: ${line.slice(0, 120)}`)
        }
        if (typeof row.name !== 'string' || typeof row.verdict !== 'string') {
            die(`${file} holds a reading with no name or no verdict: ${line.slice(0, 120)}`)
        }
        if (row.reading_version !== undefined) {
            if (version === null) version = row.reading_version
            else if (version !== row.reading_version) die(`${file} mixes normalization versions ${version} and ${row.reading_version}; two rule sets are not one baseline`)
        }
        rows.set(row.name, { verdict: row.verdict, reading: row.reading ?? null })
    }
    if (rows.size === 0) die(`${file} holds no readings: an empty baseline would compare nothing and call it agreement`)
    return { rows, version }
}

function die(why) {
    console.log(`merge-readings: ${why}`)
    console.log('merge-readings: nothing was compared, so nothing is known about this tree')
    process.exit(2)
}

const arg = (name) => {
    const at = process.argv.indexOf(name)
    return at >= 0 ? process.argv[at + 1] : null
}

// --- the baseline cache ------------------------------------------------------
// A baseline is "what this plan set reads on this develop commit", so its key is the commit, the
// plan set and the normalization version -- never the commit alone, because one commit is measured
// under different plan sets by different merges, and a baseline reused across sets compares cases
// that were not both measured. The version is in the key for the reason it exists: after a rule
// change the same commit has a different baseline, and an entry keyed without it would look valid.
//
// Reading the cache has two failures that look alike and are not:
//   - the cache directory is absent  -> nothing was ever cached, so this is "not measured" (exit 2).
//     Treating it as an empty baseline makes every case `fixed`, which is a green that measured
//     nothing; that is the shape this file refuses everywhere else.
//   - the directory is there, this key is not -> a miss. The caller measures and stores, and says so.
const cacheDir = arg('--cache')
const develop = arg('--develop')
const planSet = arg('--plans')
const storeFile = process.argv.includes('--accept')

function planSetHash(names) {
    // Order-independent and content-sensitive: the same plans listed in another order are the same
    // set, and one plan added or removed is a different one.
    const list = [...new Set(names.map((n) => n.trim()).filter(Boolean))].sort()
    let h = 0
    for (const ch of list.join('\n')) h = (h * 31 + ch.codePointAt(0)) >>> 0
    return `${list.length}-${h.toString(16)}`
}

let cacheKey = null
if (cacheDir) {
    if (!fs.existsSync(cacheDir)) {
        die(`the baseline cache does not exist at ${cacheDir}: no baseline was ever measured there, and an empty baseline would call every case fixed -- measure the plans once on the develop commit and store them (--accept) before merging`)
    }
    if (!develop || !planSet) die('--cache needs --develop <sha> and --plans <file or comma list>: a baseline keyed by commit alone is reused across plan sets it was not measured on')
    const names = fs.existsSync(planSet) ? fs.readFileSync(planSet, 'utf8').split('\n') : planSet.split(',')
    cacheKey = path.join(cacheDir, `${develop.slice(0, 12)}-${planSetHash(names)}-v${READING_NORMAL_VERSION}.jsonl`)
}

const currentFile = arg('--current')
if (!currentFile) {
    console.log('usage: merge-readings.mjs --current <rows.jsonl> (--baseline <rows.jsonl> | --cache <dir> --develop <sha> --plans <list> [--accept])')
    process.exit(2)
}
const tree = readRows(currentFile)

// `--accept` stores this run as the baseline for its key and stops: it is what the first merge on a
// new develop commit runs, and it exits 0 because storing a baseline is not a judgement.
if (storeFile && cacheKey) {
    fs.mkdirSync(path.dirname(cacheKey), { recursive: true })
    fs.copyFileSync(currentFile, cacheKey)
    fs.writeFileSync(`${cacheKey}.where`, JSON.stringify({
        develop,
        plans: planSetHash((fs.existsSync(planSet) ? fs.readFileSync(planSet, 'utf8').split('\n') : planSet.split(','))),
        plans_list: fs.existsSync(planSet) ? fs.readFileSync(planSet, 'utf8').split('\n').filter(Boolean) : planSet.split(','),
        measured_at: new Date().toISOString(),
        host: process.env.CAMEL_BASELINE_HOST || os.hostname(),
        cases: tree.rows.size,
        normal: tree.version,
        seconds: Number(process.env.CAMEL_BASELINE_SECONDS || 0),
    }, null, 2))
    console.log(`merge-readings: stored this run as the baseline for ${develop.slice(0, 12)} at ${cacheKey}`)
    process.exit(0)
}

let baseFile = arg('--baseline')
if (cacheKey) {
    if (!fs.existsSync(cacheKey)) {
        // A miss is not a failure: it is the cache telling the caller to measure once and store.
        console.log(`merge-readings: cache miss for ${develop.slice(0, 12)} under this plan set (${path.basename(cacheKey)}); measure the plans on that commit and store them with --accept`)
        process.exit(2)
    }
    baseFile = cacheKey
    const where = `${cacheKey}.where`
    if (fs.existsSync(where)) {
        const meta = JSON.parse(fs.readFileSync(where, 'utf8'))
        console.log(`merge-readings: baseline ${meta.develop.slice(0, 12)} measured ${meta.measured_at} on ${meta.host}, ${meta.cases} case(s), normal=${meta.normal}, plans=${(meta.plans_list || []).length}`)
    }
}
if (!baseFile) {
    console.log('merge-readings: no baseline given (--baseline or --cache): nothing to compare against')
    process.exit(2)
}
const base = readRows(baseFile)
if (base.version !== tree.version) {
    die(`the baseline was measured under normalization ${base.version} and this tree under ${tree.version}; a rule change makes two readings incomparable, so the baseline must be measured again`)
}

const RED = (v) => v === 'fail'
const fixed = []
const regressed = []
const changed = []
for (const [name, before] of base.rows) {
    const after = tree.rows.get(name)
    if (!after) {
        // A case the baseline measured and this run did not is not "fine": it is a reading missing.
        die(`the baseline measured ${name} and this run did not; a case that stopped being selected is not a green`)
    }
    if (RED(before.verdict) && !RED(after.verdict)) fixed.push(name)
    else if (!RED(before.verdict) && RED(after.verdict)) regressed.push({ name, reading: after.reading })
    else if (RED(before.verdict) && RED(after.verdict) && before.reading !== after.reading) {
        changed.push({ name, before: before.reading, after: after.reading })
    }
}
for (const name of tree.rows.keys()) {
    if (!base.rows.has(name)) die(`this run measured ${name}, which the baseline does not hold; the two runs are not the same set of plans`)
}

if (fixed.length > 0) {
    console.log(`merge-readings: ${fixed.length} fixed since the baseline (not a reason to refuse):`)
    for (const name of fixed) console.log(`  ${name}`)
}
if (regressed.length > 0) {
    console.log(`merge-readings: ${regressed.length} newly red:`)
    for (const r of regressed) console.log(`  ${r.name}: ${r.reading ?? '(no reading)'}`)
}
if (changed.length > 0) {
    console.log(`merge-readings: ${changed.length} still red, failing differently:`)
    for (const c of changed) {
        console.log(`  ${c.name}`)
        console.log(`    was: ${c.before ?? '(no reading)'}`)
        console.log(`    now: ${c.after ?? '(no reading)'}`)
    }
}

const out = arg('--out')
if (out) {
    fs.mkdirSync(out, { recursive: true })
    fs.writeFileSync(path.join(out, 'diff.json'), JSON.stringify({ fixed, new: regressed, changed }, null, 2))
}

console.log(
    `merge-readings: cases=${base.rows.size} fixed=${fixed.length} new=${regressed.length} changed=${changed.length} ` +
        `normal=${base.version}`,
)
process.exit(regressed.length > 0 || changed.length > 0 ? 1 : 0)
