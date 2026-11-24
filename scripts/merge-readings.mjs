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
 *
 * A baseline is a reading of *some tree*, and the one thing this program cannot see in its own
 * numbers is a baseline measured on the tree it is comparing. Every set would be empty -- fixed, new
 * and changed are all computed by comparing the rows with themselves -- and an empty diff is this
 * program's green. So the sha of the measured tree travels with every entry (`--tree`) and is
 * checked against the tree this run read: same sha is exit 2, "there was nothing to compare", never
 * a green.
 *
 * The rows come from one of two places, and the difference is the defect that made this program
 * unusable for a night:
 *   --current <rows.jsonl>   the rows as their own file, which is how a baseline is measured
 *   --transcript <console>   one run's whole console output, in which the row channel brackets the
 *                            rows between `__CAMEL_REMOTE_ROWS_START__` and `__CAMEL_REMOTE_ROWS_END__`
 *
 * The second is the merge position: what a gate has there is the transcript of the run it just
 * made, and there are three readings of it, not two. Both markers and at least one row is
 * "measured". Both markers and no row is still "not measured" -- a plan set with no case in it and
 * a channel that records nothing look the same from here, and neither is a green. A missing marker
 * is "the channel did not run": the run may have been green, red, or absent, and this program has
 * nothing to say about any of them. Zero rows is never "no reds" at this position.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { READING_NORMAL_VERSION } from '../test/tools/reading-normal.mjs'

/** The two lines the plan stage brackets its rows with (`internal/tools/camel-remote.ps1`). */
const ROWS_START = '__CAMEL_REMOTE_ROWS_START__'
const ROWS_END = '__CAMEL_REMOTE_ROWS_END__'

/** The rows and the rule set they were taken under, from the text that held them. */
function readRows(label, text) {
    const rows = new Map()
    let version = null
    for (const line of text.split('\n')) {
        if (!line.trim()) continue
        let row
        try {
            row = JSON.parse(line)
        } catch {
            die(`${label} holds a line that is not a reading: ${line.slice(0, 120)}`)
        }
        if (typeof row.name !== 'string' || typeof row.verdict !== 'string') {
            die(`${label} holds a reading with no name or no verdict: ${line.slice(0, 120)}`)
        }
        if (row.reading_version !== undefined) {
            if (version === null) version = row.reading_version
            else if (version !== row.reading_version) die(`${label} mixes normalization versions ${version} and ${row.reading_version}; two rule sets are not one baseline`)
        }
        rows.set(row.name, { verdict: row.verdict, reading: row.reading ?? null })
    }
    if (rows.size === 0) die(`${label} holds no readings: an empty baseline would compare nothing and call it agreement`)
    return { rows, version, text }
}

function readRowsFile(file) {
    if (!fs.existsSync(file)) die(`no readings at ${file}`)
    return readRows(file, fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n'))
}

/**
 * The rows out of one run's transcript, or the reading that says why there are none.
 *
 * A line is a row when it starts with `{`: the channel prints the runner's `{name, verdict, reading}`
 * lines and nothing else between its markers, and the run's own narration around them does not.
 * The markers are matched after trimming, because the console carries them beside whatever
 * indentation a wrapper gave them; the payload is matched as JSON, not as a shape this file
 * invents -- a row that does not parse is `readRows`' refusal, which names it.
 *
 * Both refusals are exit 2 and neither says anything about the tree: a green transcript with no
 * block is a merge position that never installed this channel, and a block with no row is a plan
 * set that started no case or a channel that recorded none. Both are "not measured" -- the one
 * reading a merge must never round down to "no reds".
 */
function rowsFromTranscript(file) {
    if (!fs.existsSync(file)) die('the row channel did not run')
    const lines = fs.readFileSync(file, 'utf8').split('\n').map((line) => line.replace(/\r$/, ''))
    const start = lines.findIndex((line) => line.trim() === ROWS_START)
    const end = start < 0 ? -1 : lines.indexOf(ROWS_END, start + 1)
    if (start < 0 || end < 0) die('the row channel did not run')
    const rows = lines.slice(start + 1, end).filter((line) => line.trimStart().startsWith('{'))
    if (rows.length === 0) {
        die('the plan produced no rows: either it has no cases or the row channel is not recording')
    }
    return readRows(`the row channel in ${file}`, rows.join('\n'))
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
// The sha of the tree these rows were read on. Not the key: the key is the develop this merge lands
// on (its first parent), and an entry is filed under the key its rows *describe*. The two shas are
// printed together on every comparison so the pair is a reading and not an assumption.
const treeSha = (arg('--tree') || '').trim()
const storeFile = process.argv.includes('--accept')
const probeOnly = process.argv.includes('--probe')

function planSetHash(names) {
    // Order-independent and content-sensitive: the same plans listed in another order are the same
    // set, and one plan added or removed is a different one.
    const list = [...new Set(names.map((n) => n.trim()).filter(Boolean))].sort()
    let h = 0
    for (const ch of list.join('\n')) h = (h * 31 + ch.codePointAt(0)) >>> 0
    return `${list.length}-${h.toString(16)}`
}

/** The names `--plans` names: a file of one name per line, or a comma list. */
function planSetNames(list) {
    return fs.existsSync(list) ? fs.readFileSync(list, 'utf8').split('\n') : list.split(',')
}

/** A target as both sides spell it: trimmed, forward slashes, no trailing slash. */
const normTarget = (s) => String(s).trim().replace(/\\/g, '/').replace(/\/+$/, '')
const isPlanFileTarget = (s) => s.endsWith('.plan.toml')

/**
 * Does one target an entry measured cover one target this merge names?
 *
 * A directory covers itself and everything under it (`test/plans/feat` covers
 * `test/plans/feat/modules/tensor`); a plan file covers itself and nothing else. Containment of the
 * path, not string equality: the whole point of a superset baseline is that the entry names a wider
 * set than the merge does, and two spellings of the same plans never match.
 */
function targetCovers(measured, wanted) {
    const m = normTarget(measured)
    const w = normTarget(wanted)
    if (m === w) return true
    if (isPlanFileTarget(m)) return false
    return w.startsWith(`${m}/`)
}

/** This merge's selection, as the names it hands to the comparer. */
function requestedTargets() {
    return [...new Set(planSetNames(planSet).map(normTarget).filter(Boolean))].sort()
}

/** The entries the cache holds for this develop commit and this rule set, in a stable order. */
function entriesAt(dir, sha, normal) {
    if (!fs.existsSync(dir)) return []
    const head = `${sha.slice(0, 12)}-`
    const tail = `-v${normal}.jsonl`
    return fs.readdirSync(dir)
        .filter((n) => n.startsWith(head) && n.endsWith(tail))
        .sort()
        .map((n) => path.join(dir, n))
}

/** The targets an entry records, or null when the record does not say. */
function plansOf(key) {
    const meta = readWhere(key)
    if (!meta || !Array.isArray(meta.plans_list)) return null
    return meta.plans_list.map(normTarget).filter(Boolean)
}

/**
 * The entry this merge may compare against.
 *
 * The exact key first -- one command, one entry, the same reading as before. When it is not there,
 * the smallest entry at this develop commit whose targets *cover* this selection: a baseline measured
 * once across the whole fast layer answers every merge that lands on that commit and selects
 * something inside it, which is the difference between measuring a baseline per merge and measuring
 * one when the tree moved.
 *
 * Coverage is checked target by target, and an entry that does not cover this selection is not a
 * candidate at any size. That is the refusal `--probe` reports and the comparison repeats: a baseline
 * that holds fewer plans than the merge selects would put every case of the missing plans in
 * `added`, where a red is named and allowed, and a gate that allowed a new red because its baseline
 * did not reach the plan would be the loose direction of the same mistake.
 */
function resolveBaseline() {
    if (fs.existsSync(cacheKey)) return { key: cacheKey, exact: true, plans: plansOf(cacheKey) }
    const wanted = requestedTargets()
    let best = null
    for (const key of entriesAt(cacheDir, develop, READING_NORMAL_VERSION)) {
        const plans = plansOf(key)
        if (!plans || plans.length === 0) continue
        if (!wanted.every((w) => plans.some((m) => targetCovers(m, w)))) continue
        if (!best || plans.length < best.plans.length) best = { key, exact: false, plans }
    }
    return best
}

/** The record beside an entry, or null: what was measured, when, and on which tree. */
function readWhere(key) {
    const file = `${key}.where`
    if (!fs.existsSync(file)) return null
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch {
        return null
    }
}

// --- one side holding a case the other does not ------------------------------
// The evidence that a change removed a case: it touched something that holds that case. A case is
// named `<plan id>/<case id>`; its plan is the entry in the plan set whose `id` matches, and a plan
// target that is a directory holds every plan under it. Without that evidence, a case missing from
// one run is the two runs selecting differently, which is a fact about the selection rules.
const changedFiles = (() => {
    const file = arg('--changed-files')
    if (!file || !fs.existsSync(file)) return []
    return fs.readFileSync(file, 'utf8').split('\n').map((s) => s.trim().replace(/\\/g, '/')).filter(Boolean)
})()
const mergeMessage = (() => {
    const file = arg('--message-file')
    return file && fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : ''
})()

/** A path as the change lists it: repo-relative, forward slashes. */
function repoRelative(name) {
    const s = name.trim().replace(/\\/g, '/')
    const at = s.indexOf('test/plans/')
    return at >= 0 ? s.slice(at) : s
}

/** Does a changed file sit on this path or under it? */
function touched(rel) {
    const dir = rel.endsWith('/') ? rel : `${rel}/`
    return (changedFiles || []).some((f) => f === rel || f.startsWith(dir))
}

/** The plan files a target names: the file itself, or every plan under a directory target. */
function planFilesOf(target) {
    const rel = repoRelative(target)
    if (rel.endsWith('.toml')) return [{ file: target, rel }]
    const out = []
    const walk = (dir, relDir) => {
        let entries = []
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true })
        } catch {
            return
        }
        for (const e of entries) {
            const child = path.join(dir, e.name)
            const childRel = `${relDir}/${e.name}`
            if (e.isDirectory()) walk(child, childRel)
            else if (e.name.endsWith('.plan.toml')) out.push({ file: child, rel: childRel })
        }
    }
    walk(target, rel)
    return out
}

/** The plan id a plan file declares, or null. */
function planIdOf(file) {
    try {
        const m = fs.readFileSync(file, 'utf8').match(/^\s*id\s*=\s*"([^"]+)"/m)
        return m ? m[1] : null
    } catch {
        return null
    }
}

/**
 * Did this change remove the case, or did the two runs select differently? True only on evidence:
 * the merge touches a file of the plan that holds the case -- the plan file itself, or anything in a
 * directory target that contains it.
 */
function removalExplained(caseName, _changedFiles, planSetPath) {
    if (!planSetPath) return false
    const id = caseName.split('/')[0]
    for (const target of planSetNames(planSetPath).map((s) => s.trim()).filter(Boolean)) {
        for (const plan of planFilesOf(target)) {
            if (planIdOf(plan.file) !== id) continue
            if (touched(plan.rel)) return true
        }
    }
    return false
}

/** Does the merge message name this case (or its plan)? A removal has to be said out loud. */
function messageNames(caseName, message) {
    if (!message) return false
    const lower = message.toLowerCase()
    const id = caseName.split('/')[0]
    return lower.includes(caseName.toLowerCase()) || lower.includes(id.toLowerCase())
}

// --- derived case names ------------------------------------------------------
// A plan may declare `[[sweeps]]`: it runs the tests of other plans again under an extra
// environment, and names each copy `<sweep name>:<source plan id>:<the source test's name>`
// (`test/tools/run-tests.mjs`, `rename` in the sweep expansion, and the format spelled out at the
// `[[sweeps]]` comment above it). The copy is not a case of its own: it exists because the source
// does, and it stops existing when the source does -- or when the sampling that decides which
// source tests get copied picks a different one, which is the shape that got this wrong.
//
// So a copy that a baseline holds and a run does not says nothing until the source is looked up.
// The sweep names come from the plan set's own `[[sweeps]]` declarations, not from the shape of the
// string: a name with a colon in it is not evidence that anything was derived.
const sweepNames = (() => {
    const names = new Set()
    const planSetPath = arg('--plans')
    if (!planSetPath) return names
    for (const target of planSetNames(planSetPath).map((s) => s.trim()).filter(Boolean)) {
        for (const plan of planFilesOf(target)) {
            let text = ''
            try {
                text = fs.readFileSync(plan.file, 'utf8')
            } catch {
                continue
            }
            for (const block of text.split('[[sweeps]]').slice(1)) {
                const name = block.match(/^\s*name\s*=\s*"([^"]+)"/m)
                const plans = block.match(/^\s*plans\s*=\s*\[/m)
                // The first `name` after `[[sweeps]]` belongs to the sweep only while no other table
                // has started; a block that runs into the next table is not a sweep this can read.
                const stop = block.search(/^\[\[(?!sweeps)/m)
                if (name && plans && (stop < 0 || name.index < stop)) names.add(name[1])
            }
        }
    }
    return names
})()

/** Contains a colon, so it is shaped like a derived name even if no sweep in the set claims it. */
const looksDerived = (caseName) => caseName.slice(caseName.indexOf('/') + 1).includes(':')

/**
 * The source case a derived name came from, or null. Returns `{ sweep, source }` when the plan set
 * declares the sweep; `{ sweep: null, source: null }`-shaped input (a colon with no declaration) is
 * the caller's "this derived name cannot be resolved" case and is reported, never guessed.
 */
function sourceCaseOf(caseName) {
    const slash = caseName.indexOf('/')
    if (slash < 0) return null
    const casePart = caseName.slice(slash + 1)
    const at = casePart.indexOf(':')
    if (at <= 0) return null
    const sweep = casePart.slice(0, at)
    const rest = casePart.slice(at + 1)
    const sep = rest.indexOf(':')
    if (sep <= 0) return null
    const sourceId = rest.slice(0, sep)
    const original = rest.slice(sep + 1)
    if (!sweepNames.has(sweep) || !sourceId || !original) return { sweep, source: null }
    return { sweep, source: `${sourceId}/${original}` }
}

let cacheKey = null
if (cacheDir) {
    if (!develop || !planSet) die('--cache needs --develop <sha> and --plans <file or comma list>: a baseline keyed by commit alone is reused across plan sets it was not measured on')
    cacheKey = path.join(cacheDir, `${develop.slice(0, 12)}-${planSetHash(planSetNames(planSet))}-v${READING_NORMAL_VERSION}.jsonl`)
    // An absent directory is the strongest "nothing was ever measured here", and the compare path
    // must refuse it rather than read it as an empty baseline; `--probe` is the caller asking the
    // same question before it measures, and `--accept` is the call that creates it, so a miss is the
    // answer those two came for.
    if (!probeOnly && !storeFile && !fs.existsSync(cacheDir)) {
        die(`the baseline cache does not exist at ${cacheDir}: no baseline was ever measured there, and an empty baseline would call every case fixed -- measure the plans once on the develop commit and store them (--accept) before merging`)
    }
}

// `--probe`: is there a baseline for this key? It reads no rows and compares nothing -- it is what a
// merge asks before it spends a second run on measuring the tree it is about to land on. Hit is 0,
// miss is 2, and the two words are part of the output because the caller branches on them.
if (probeOnly) {
    if (!cacheKey) die('--probe needs --cache --develop <sha> --plans <file or comma list>: it answers about one key')
    const found = resolveBaseline()
    if (!found) {
        const wanted = requestedTargets()
        const known = entriesAt(cacheDir, develop, READING_NORMAL_VERSION)
        // Which of this selection no entry at this commit covers, named: "measure it again" is useless
        // without the targets the old measurement did not reach.
        const missing = wanted.filter((w) => !known.some((k) => (plansOf(k) || []).some((m) => targetCovers(m, w))))
        const detail =
            known.length === 0
                ? (fs.existsSync(cacheDir) ? '' : `; the cache at ${cacheDir} does not exist yet`)
                : `; ${known.length} entry(ies) exist for that commit and none covers ${missing.slice(0, 3).join(', ')}${missing.length > 3 ? ` and ${missing.length - 3} more` : ''}`
        console.log(
            `merge-readings: cache-miss: no baseline for ${develop.slice(0, 12)} covers this selection ` +
                `(${wanted.length} target(s))${detail}`
        )
        process.exit(2)
    }
    const meta = readWhere(found.key)
    const where = meta ? `, ${meta.cases} case(s), tree ${(meta.tree || 'not recorded').slice(0, 9)}, measured ${meta.measured_at} on ${meta.host}` : ''
    const how = found.exact
        ? ''
        : ` (a superset: measured across ${found.plans.length} target(s), covers the ${requestedTargets().length} this selection names)`
    console.log(`merge-readings: cache-hit: ${path.basename(found.key)}${where}${how}`)
    process.exit(0)
}

const currentFile = arg('--current')
const transcriptFile = arg('--transcript')
if (!currentFile && !transcriptFile) {
    console.log('usage: merge-readings.mjs (--current <rows.jsonl> | --transcript <console output>) (--baseline <rows.jsonl> | --cache <dir> --develop <sha> --plans <list> --tree <sha> [--accept])')
    console.log('       merge-readings.mjs --cache <dir> --develop <sha> --plans <list> --probe')
    process.exit(2)
}
const tree = transcriptFile ? rowsFromTranscript(transcriptFile) : readRowsFile(currentFile)

// `--accept` stores this run as the baseline for its key and stops: it is what the first merge on a
// new develop commit runs, and it exits 0 because storing a baseline is not a judgement.
//
// `--tree` is required with it. An entry that does not say which tree it read cannot be told apart
// from one read on the tree it is later compared with, and that comparison is a green of nothing.
if (storeFile && cacheKey) {
    if (!treeSha) {
        die('--accept needs --tree <sha>: the sha of the tree these rows were measured on, because an entry without it cannot be told apart from a tree compared with itself')
    }
    fs.mkdirSync(path.dirname(cacheKey), { recursive: true })
    // The rows alone, whether they arrived as their own file or out of a transcript: a cache entry
    // that held a console transcript would be a baseline with narration inside it.
    fs.writeFileSync(cacheKey, tree.text)
    fs.writeFileSync(`${cacheKey}.where`, JSON.stringify({
        develop,
        tree: treeSha,
        plans: planSetHash(planSetNames(planSet)),
        plans_list: planSetNames(planSet).filter(Boolean),
        measured_at: new Date().toISOString(),
        host: process.env.CAMEL_BASELINE_HOST || os.hostname(),
        cases: tree.rows.size,
        normal: tree.version,
        seconds: Number(process.env.CAMEL_BASELINE_SECONDS || 0),
    }, null, 2))
    console.log(`merge-readings: stored this run as the baseline for ${develop.slice(0, 12)} at ${cacheKey}, measured on tree ${treeSha.slice(0, 9)}`)
    process.exit(0)
}

let baseFile = arg('--baseline')
let baselineEntry = null
if (cacheKey) {
    baselineEntry = resolveBaseline()
    if (!baselineEntry) {
        // A miss is not a failure: it is the cache telling the caller to measure once and store.
        console.log(`merge-readings: cache miss for ${develop.slice(0, 12)} covering this selection; measure the plans on that commit and store them with --accept`)
        process.exit(2)
    }
    baseFile = baselineEntry.key
    const meta = readWhere(baseFile)
    if (meta) {
        console.log(`merge-readings: baseline ${meta.develop.slice(0, 12)} measured ${meta.measured_at} on ${meta.host}, ${meta.cases} case(s), normal=${meta.normal}, plans=${(meta.plans_list || []).length}`)
    }
    if (!baselineEntry.exact) {
        console.log(
            `merge-readings: that baseline is a superset measured across ${baselineEntry.plans.length} target(s); ` +
                `this merge names ${requestedTargets().length}, so only its own plans are compared`
        )
    }
}
if (!baseFile) {
    console.log('merge-readings: no baseline given (--baseline or --cache): nothing to compare against')
    process.exit(2)
}

// The one failure the three sets cannot show: a baseline measured on the tree being compared. Both
// runs are then the same rows, so `fixed`, `new` and `changed` are empty by construction and this
// program exits 0 -- a green that measured one tree twice and called it agreement. So the shas are
// put side by side and a match is a refusal, not a pass. This is why `--tree` is required with a
// cache: the record is the only thing here that knows which tree the rows came from.
if (cacheKey) {
    const meta = readWhere(baseFile)
    const measured = meta && typeof meta.tree === 'string' && meta.tree ? meta.tree : null
    if (!measured) {
        die(`the baseline ${path.basename(baseFile)} does not record the tree it was measured on, so nothing proves it is not this tree: measure it again with --tree <sha> --accept`)
    }
    if (!treeSha) {
        die('--cache needs --tree <sha>: the sha of the tree these rows were measured on, which the baseline is checked against')
    }
    if (measured === treeSha) {
        die(`the baseline was measured on this very tree (${measured.slice(0, 9)}): comparing it with itself is 0 fixed, 0 new and 0 changed by construction, so nothing has been said about this change`)
    }
    console.log(`merge-readings: baseline tree ${measured.slice(0, 9)} against this tree ${treeSha.slice(0, 9)} -- two different trees`)
}
const base = readRowsFile(baseFile)
if (base.version !== tree.version) {
    die(`the baseline was measured under normalization ${base.version} and this tree under ${tree.version}; a rule change makes two readings incomparable, so the baseline must be measured again`)
}

// --- only the plans this merge selects ---------------------------------------
// A superset baseline holds every case of every plan it measured; this merge selected a few of those
// plans and ran only them. Comparing the rest would call every case of an untouched plan "missing
// from this run", which is the `unexplained` refusal -- for a plan this merge never ran and never
// said it would. So the baseline rows are cut to the selection's own plan ids, and a selection whose
// plans cannot be read as plan files is refused rather than cut wrongly: cutting too much hides a
// case, and a hidden case is a red that was never compared.
if (baselineEntry && !baselineEntry.exact) {
    const ids = new Set()
    const unreadable = []
    for (const target of requestedTargets()) {
        const files = planFilesOf(target)
        if (files.length === 0) {
            unreadable.push(target)
            continue
        }
        for (const plan of files) {
            const id = planIdOf(plan.file)
            if (id) ids.add(id)
            else unreadable.push(plan.rel)
        }
    }
    if (unreadable.length > 0) {
        die(
            `the selection names ${unreadable.length} target(s) whose plan id could not be read (${unreadable.slice(0, 3).join(', ')}), ` +
                'and the baseline is a superset: without the ids there is no way to tell which of its cases this merge selected, ' +
                'so cutting it would compare a subset of the merge'
        )
    }
    const before = base.rows.size
    for (const name of [...base.rows.keys()]) {
        if (!ids.has(name.split('/')[0])) base.rows.delete(name)
    }
    if (base.rows.size === 0) {
        die(
            `the baseline holds ${before} case(s) and not one belongs to a plan this merge selects ` +
                `(${[...ids].slice(0, 3).join(', ')}): comparing it would compare nothing`
        )
    }
    console.log(
        `merge-readings: ${before - base.rows.size} case(s) of plans this merge does not select left out of the comparison; ` +
            `${base.rows.size} remain, over ${ids.size} plan(s)`
    )
}

const RED = (v) => v === 'fail'
const fixed = []
const regressed = []
const changed = []
const addedRed = []
const addedGreen = []
const removed = []
const removedRedUnnamed = []
const unexplained = []
const derivedMissing = []
const unresolvedDerived = []
for (const [name, before] of base.rows) {
    const after = tree.rows.get(name)
    if (!after) continue
    if (RED(before.verdict) && !RED(after.verdict)) fixed.push(name)
    else if (!RED(before.verdict) && RED(after.verdict)) regressed.push({ name, reading: after.reading })
    else if (RED(before.verdict) && RED(after.verdict) && before.reading !== after.reading) {
        changed.push({ name, before: before.reading, after: after.reading })
    }
}
// A case only one side holds is not automatically a lie. The equality this replaced died on every
// lane that adds a case, and lanes add cases constantly: the baseline tree does not hold them and
// never will, so the equality made the gate refuse every such merge whatever it measured. Which side
// a case is missing from, and what it read, decides what it means.
for (const [name, after] of tree.rows) {
    if (!base.rows.has(name)) (RED(after.verdict) ? addedRed : addedGreen).push(name)
}
for (const [name, before] of base.rows) {
    if (tree.rows.has(name)) continue
    // Missing from this run. Three very different facts wear that shape:
    //   - it is a derived copy and the source case it copies is still here. Then the copy did not
    //     come out of a source that exists, so the two runs selected differently: exit 2, and say
    //     that the derivation is what is missing, not the case.
    //   - the change removed the case (its file, its plan entry). Then it is `removed` and it is NOT
    //     `fixed` -- a red case that disappears must not read as a case that went green. For a copy,
    //     the evidence is the removal of its source, since the copy lives and dies with it.
    //   - the change did not touch anything that holds the case, so the two runs selected
    //     differently. That is a fact about the selection, not about this merge: exit 2, and name it.
    const derived = sourceCaseOf(name)
    if (derived && !derived.source) {
        // Shaped like a copy, but no sweep in this plan set declares the prefix: the source cannot
        // be resolved. Reading it as "still here" would exit 2 for the wrong reason and reading it as
        // "removed" would let a deletion through, so it is reported as what it is.
        unresolvedDerived.push(name)
        continue
    }
    if (derived && tree.rows.has(derived.source)) {
        derivedMissing.push({ name, source: derived.source })
        continue
    }
    const subject = derived ? derived.source : name
    if (!removalExplained(subject, changedFiles, planSet)) {
        unexplained.push(name)
        continue
    }
    // Deleting a red case makes the three sets emptier without fixing anything: the oldest way to
    // turn a gate green. It is allowed only when the merge says out loud that it is going, so the
    // record carries the decision instead of the absence carrying it.
    if (RED(before.verdict) && !messageNames(name, mergeMessage)) removedRedUnnamed.push(name)
    else removed.push(name)
}

if (fixed.length > 0) {
    console.log(`merge-readings: ${fixed.length} fixed since the baseline (not a reason to refuse):`)
    for (const name of fixed) console.log(`  ${name}`)
}
if (regressed.length > 0) {
    console.log(`merge-readings: ${regressed.length} newly red:`)
    for (const r of regressed) console.log(`  ${r.name}: ${r.reading ?? '(no reading)'}`)
}
if (addedRed.length > 0) {
    console.log(`merge-readings: ${addedRed.length} case(s) this change adds that are red:`)
    for (const name of addedRed) console.log(`  ${name}: ${tree.rows.get(name)?.reading ?? '(no reading)'}`)
}
if (addedGreen.length > 0) {
    console.log(`merge-readings: ${addedGreen.length} case(s) this change adds that are green (allowed, named so the record has them):`)
    for (const name of addedGreen) console.log(`  ${name}`)
}
if (removed.length > 0) {
    console.log(`merge-readings: ${removed.length} case(s) this change removes (not counted as fixed):`)
    for (const name of removed) console.log(`  ${name}`)
}
if (removedRedUnnamed.length > 0) {
    console.log(`merge-readings: ${removedRedUnnamed.length} red case(s) this change removes without naming them in the merge message:`)
    for (const name of removedRedUnnamed) console.log(`  ${name}`)
    console.log('merge-readings: deleting a red case to make this gate green is the oldest cheat; name it in the merge message if the removal is meant')
}
if (unexplained.length > 0) {
    console.log(`merge-readings: ${unexplained.length} case(s) the baseline measured and this run did not, and this change does not remove:`)
    for (const name of unexplained) console.log(`  ${name}`)
    console.log('merge-readings: the two runs selected differently -- that is the selection, not this merge, so nothing here is a verdict on the change')
    process.exit(2)
}
if (derivedMissing.length > 0) {
    console.log(`merge-readings: ${derivedMissing.length} derived case(s) the baseline measured and this run did not, whose source case is still here and unchanged:`)
    for (const d of derivedMissing) console.log(`  ${d.name}`)
    for (const d of derivedMissing) console.log(`    the copy did not come out of ${d.source}, which this run measured`)
    console.log('merge-readings: a copy that is absent while its source is present was not deleted by anyone -- the derivation, not the case, is what is missing, so this is a fact about the selection and exits 2')
    process.exit(2)
}
if (unresolvedDerived.length > 0) {
    console.log(`merge-readings: ${unresolvedDerived.length} case(s) shaped like a derived copy that no sweep in this plan set declares:`)
    for (const name of unresolvedDerived) console.log(`  ${name}`)
    console.log('merge-readings: the source case cannot be resolved, and an unresolved derived name is neither "still here" nor "removed" -- a reading that cannot be resolved says nothing, so it exits 2')
    process.exit(2)
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
    fs.writeFileSync(path.join(out, 'diff.json'), JSON.stringify({ fixed, new: regressed, changed, added: addedGreen, addedRed, removed }, null, 2))
}

console.log(
    `merge-readings: cases=${base.rows.size} fixed=${fixed.length} new=${regressed.length} changed=${changed.length} ` +
        `added=${addedGreen.length + addedRed.length} removed=${removed.length} ` +
        `normal=${base.version}` +
        // The two shas on the line that carries the verdict: a reader who sees 0/0/0 has to be able to
        // see, without going back up the transcript, that the baseline and this run are two trees.
        (cacheKey ? ` baseline_tree=${(readWhere(baseFile)?.tree || '?').slice(0, 9)} this_tree=${treeSha.slice(0, 9)}` : ''),
)
process.exit(regressed.length > 0 || changed.length > 0 || addedRed.length > 0 || removedRedUnnamed.length > 0 ? 1 : 0)
