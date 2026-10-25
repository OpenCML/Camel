// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The numbering a merge has to repair, before the merge is what the catalog refuses.
//
// Two lanes cut from the same `develop` each add a diagnostic and each ask for "the next free
// number" of the section they add it to. Both get the same value, because neither could see the
// other's line, and both trees pass `--check` on their own. The duplicate exists only in the merge:
// `node scripts/diagnostics-catalog.mjs --check` then says
//
//   semantic: TagFunctionArgumentNotReadable and SliceStepZero have the same value 0x0001E9;
//   give the new declaration no number and let --write assign the next free one
//
// and leaves the repair to a person, who edits the value, re-emits the four generated files and
// runs the gate again -- once per lane, per merge. This script is that repair, done by the same
// rule the message names:
//
//   node scripts/diagnostics-renumber.mjs            report only; exit 1 when a collision exists
//   node scripts/diagnostics-renumber.mjs --write    blank those values and let --write number them
//   --base <ref>        the baseline the tree is being merged into (default: origin/develop)
//   --base-file <path>  read the baseline registry from a file instead of a git ref (a scratch
//                       tree, where `git show` has nothing to read)
//   --root <dir>        read and write under DIR, as `diagnostics-catalog.mjs --root DIR` does
//
// What it does, in one sentence: it finds every declaration the tree adds that takes a value the
// baseline already published, removes the *number* of those declarations alone, and hands the file
// to `diagnostics-catalog.mjs --write`, which assigns the next free value of each section and
// severity and regenerates the headers, the tables and the reference page.
//
// Three rules make that safe, and they are the whole of the design:
//
//   - Only the *new* declarations lose their number. A declaration the baseline already has keeps
//     the value it was published with, whatever the tree says: its number is history -- a test, a
//     document and a released binary may name it -- and a tool that moves it to get past a
//     collision rewrites what was published. The tree is not even asked; a value that disagrees
//     with the baseline is refused outright, because that is a different defect (see below).
//   - The value is *not* computed here. `--write` assigns it, in registry order, from the numbers
//     the declarations around it already hold (`Math.max(...) + 1` of the section and severity).
//     There is one numbering rule in the repository and this is not a second one; if the rule
//     changes, it changes in the one place that owns it.
//   - It reports by default. Renumbering a lane's declaration after the author has cited the value
//     in a test is a decision about that lane, and the merge person is the one who makes it: the
//     report names each pair and the work is done with an explicit `--write`.
//
// What it refuses, and never repairs:
//
//   - a declaration the baseline has whose value the tree changed (a published number moved);
//   - two declarations of one name in the tree (the catalog's check owns that rule; which of the
//     two lines is the mistake is not for this script to guess);
//   - a line that is not seven tab-separated fields (same reason);
//   - a collision between two declarations the baseline already published: the baseline itself is
//     the defect, and the repair is not a renumbering of the tree.
//
// Exit codes: 0 nothing to renumber (or done), 1 a collision that a report-only run does not
// repair (or a refusal above), 2 the tool could not read the tree or the baseline at all.
//
// Where to use it: `agents/branching.md`'s merge order, at the point where the catalog's `--check`
// is run on the merged tree -- merge, `--check`, then `--write` here when it names a collision,
// then the gate. See section 6 of `docs/technical/30_diagnostics_reporting.md`.

import fs from 'fs'
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const DEFAULT_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const REGISTRY_REL = 'diagnostics/registry.tsv'
const GENERATOR_REL = 'scripts/diagnostics-catalog.mjs'
const DEFAULT_BASE = 'origin/develop'

/** The number column of a registry line, as the catalog writes it: six uppercase hex digits. */
function hex6(value) {
    return '0x' + value.toString(16).toUpperCase().padStart(6, '0')
}

/** `1 collision`, `2 collisions`: the report is read by a person at a merge, so it reads as prose. */
function count(n, word) {
    return `${n} ${word}${n === 1 ? '' : 's'}`
}

function usage(message) {
    if (message) console.log(`diagnostics renumber: ${message}`)
    console.log(
        'usage: node scripts/diagnostics-renumber.mjs [--check | --write] [--base REF] ' +
            '[--base-file PATH] [--root DIR]'
    )
    process.exit(2)
}

// ---- reading ----------------------------------------------------------------------------------

/**
 * The declarations of a registry, in file order, plus the raw lines so a rewritten file differs
 * only in the number column of the lines that were renumbered.
 *
 * Only the four fields that say *which* declaration this is are read here (section, name, number,
 * severity). What a declaration means -- its message, its help, whether it carries an editable
 * fix, whether its number is inside its severity band -- is the catalog's business, and this
 * script never re-decides any of it: it finds the lines whose number a person would have edited by
 * hand, blanks that one field and lets the catalog do the rest.
 */
function readRegistry(file) {
    if (!fs.existsSync(file)) return { text: null, lines: [], records: [], problems: [`${file} does not exist`] }
    const text = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')
    const lines = text.split('\n')
    const records = []
    const problems = []
    for (let i = 0; i < lines.length; i++) {
        const raw = lines[i]
        if (!raw.trim() || raw.startsWith('#')) continue
        const fields = raw.split('\t')
        if (fields.length !== 7) {
            problems.push(
                `${REGISTRY_REL}:${i + 1}: expected seven tab-separated fields (${fields.length} found); ` +
                    `\`node scripts/diagnostics-catalog.mjs --check\` says what the line should be`
            )
            continue
        }
        const number = /^0x[0-9A-F]{6}$/.test(fields[2]) ? Number(fields[2]) : null
        records.push({
            index: i,
            section: fields[0],
            name: fields[1],
            number,
            numberText: fields[2],
            severity: fields[3],
            line: i + 1,
        })
    }
    return { text, lines, records, problems }
}

/** The same reading, for the baseline: from a git ref, or from a file where there is no git. */
function readBaseline(root, base, baseFile) {
    if (baseFile) {
        const file = path.resolve(baseFile)
        if (!fs.existsSync(file)) usage(`--base-file ${file} does not exist`)
        return { source: file, registry: readRegistry(file) }
    }
    const result = spawnSync('git', ['-C', root, 'show', `${base}:${REGISTRY_REL}`], { encoding: 'utf8' })
    if (result.status !== 0) {
        const detail = `${result.stderr || result.stdout || ''}`.trim().split('\n')[0] || 'git failed'
        usage(
            `could not read ${REGISTRY_REL} from '${base}' (${detail}); ` +
                `name an existing baseline with --base, or read one from a file with --base-file`
        )
    }
    const text = result.stdout.replace(/\r\n/g, '\n')
    const lines = text.split('\n')
    const records = []
    for (let i = 0; i < lines.length; i++) {
        const raw = lines[i]
        if (!raw.trim() || raw.startsWith('#')) continue
        const fields = raw.split('\t')
        if (fields.length !== 7) continue
        records.push({
            section: fields[0],
            name: fields[1],
            number: /^0x[0-9A-F]{6}$/.test(fields[2]) ? Number(fields[2]) : null,
        })
    }
    return { source: base, registry: { records, lines, problems: [] } }
}

// ---- the repair, decided ---------------------------------------------------------------

/**
 * The declarations that have to move, and the refusals. Nothing is written here; this is the whole
 * decision, and the report and the write path both read it.
 */
function plan(tree, baseline) {
    const refusals = [...tree.problems]
    const baselineValue = new Map(baseline.records.map((r) => [r.name, r.number]))
    const seenName = new Map()
    for (const record of tree.records) {
        if (seenName.has(record.name)) {
            refusals.push(
                `${record.section}: the name '${record.name}' is declared twice in the tree ` +
                    `(${REGISTRY_REL}:${seenName.get(record.name)} and ${REGISTRY_REL}:${record.line}); ` +
                    `one of the two lines is the mistake, and which one is not for this script to guess`
            )
        } else {
            seenName.set(record.name, record.line)
        }
    }

    // A published number that the tree moved. This is the one thing the tool must never repair
    // quietly: the value left the tree and reached a released binary, a test or a page.
    const movedPublished = []
    const isNew = (record) => !baselineValue.has(record.name)
    for (const record of tree.records) {
        if (isNew(record) || record.number === null) continue
        const published = baselineValue.get(record.name)
        if (published !== null && published !== undefined && published !== record.number) {
            movedPublished.push({ record, published })
        }
    }
    for (const { record, published } of movedPublished) {
        refusals.push(
            `${record.section}: ${record.name} is ${hex6(record.number)} in the tree and ${hex6(published)} ` +
                `in the baseline; a published number does not move, so this is not a collision this ` +
                `script repairs -- restore the baseline value, or say why the number has to change`
        )
    }

    // The collisions, grouped by the pair the catalog's check compares: one section and one value.
    // The same value in two sections is not one of them: the printed code carries the section's
    // type byte (DiagType in base.h), so two sections hold their own numbers independently.
    const groups = new Map()
    for (const record of tree.records) {
        if (record.number === null) continue
        const key = `${record.section}:${record.number}`
        if (!groups.has(key)) groups.set(key, [])
        groups.get(key).push(record)
    }
    // A baseline that already uses one value twice is its own defect, and it is read off the
    // baseline alone: a tree value that agrees with such a baseline is not a collision *the tree*
    // brought, and renumbering the tree around it would leave the defect in place.
    const baselineGroups = new Map()
    for (const record of baseline.records) {
        if (record.number === null) continue
        const key = `${record.section}:${record.number}`
        if (!baselineGroups.has(key)) baselineGroups.set(key, [])
        baselineGroups.get(key).push(record)
    }
    for (const key of [...baselineGroups.keys()].sort()) {
        const members = baselineGroups.get(key)
        if (members.length < 2) continue
        refusals.push(
            `${members[0].section}: the baseline uses ${hex6(members[0].number)} for ${members
                .map((m) => m.name)
                .join(' and ')}; the repair is not a renumbering of this tree`
        )
    }

    const renumber = []
    const collisions = []
    for (const key of [...groups.keys()].sort()) {
        const members = groups.get(key).sort((a, b) => a.index - b.index)
        if (members.length < 2) continue
        // The holder is the declaration that may keep the value: a published one if the group has
        // one (its number is history), and otherwise the first line of the group in registry order
        // -- a stable choice, so two runs on one tree name the same line.
        const publishedMembers = members.filter((m) => !isNew(m))
        const keeper = publishedMembers.length ? publishedMembers[0] : members[0]
        // A second published member of one group is the baseline's defect, refused above; and a
        // published member whose value the tree moved is refused above too. Neither is repaired by
        // taking a number away from it here.
        for (const loser of members.filter((m) => m !== keeper && isNew(m))) {
            renumber.push({ record: loser, keeper, publishedKeeper: !isNew(keeper) })
            collisions.push({ record: loser, keeper, publishedKeeper: !isNew(keeper) })
        }
    }
    renumber.sort((a, b) => a.record.index - b.record.index)
    // The report is read against the file, so it follows the file: the declarations are named in
    // the order their lines are written, whatever order the collisions were found in.
    collisions.sort((a, b) => a.record.index - b.record.index)

    const unnumbered = tree.records.filter((r) => isNew(r) && r.number === null)
    const newCount = tree.records.filter(isNew).length
    return { refusals, renumber, collisions, unnumbered, newCount, movedPublished }
}

// ---- the repair, done ------------------------------------------------------------------------

function report(plan, base, baseline) {
    const { refusals, collisions, renumber, unnumbered, newCount } = plan
    if (refusals.length) {
        console.log(
            `diagnostics renumber: ${count(refusals.length, 'problem')} this script does not repair`
        )
        for (const refusal of refusals) console.log(`  - ${refusal}`)
        return 1
    }
    // The exit code answers the question the tool was called to answer -- is a value used twice --
    // and not "is the registry publishable": a new declaration with no number at all is the
    // catalog's own finding (`has no number yet; run --write`), it is named here so that one run
    // reports the whole state of the numbering, and it does not move the code.
    if (!renumber.length) {
        console.log(
            `diagnostics renumber: no new declaration takes a value ${base} already published ` +
                `(${count(baseline.records.length, 'baseline declaration')}, ${newCount} new in the tree); ` +
                `no value used twice`
        )
        for (const record of unnumbered) {
            console.log(
                `  note: ${record.section}: ${record.name} has no number yet; \`--write\` gives it the ` +
                    `next free value of ${record.section}/${record.severity}`
            )
        }
        return 0
    }
    const touched = new Set(renumber.map((r) => r.record.section))
    console.log(
        `diagnostics renumber: ${count(renumber.length, 'collision')} in ${count(touched.size, 'section')} ` +
            `against ${base}; ${count(renumber.length + unnumbered.length, 'declaration')} to number`
    )
    for (const { record, keeper, publishedKeeper } of collisions) {
        console.log(
            `  - ${record.section}: ${keeper.name} ${hex6(keeper.number)} is ` +
                `${publishedKeeper ? `in ${base} and keeps it` : 'the first of this value in registry order'}` +
                `, and ${record.name} ${hex6(record.number)} is new; the new declaration takes the next ` +
                `free value of ${record.section}/${record.severity}`
        )
    }
    for (const record of unnumbered) {
        console.log(
            `  - ${record.section}: ${record.name} has no number yet; \`--write\` gives it the next free ` +
                `value of ${record.section}/${record.severity}`
        )
    }
    console.log('  run the same command with --write to give them the next free values and regenerate')
    return 1
}

function write(root, plan, base) {
    const { refusals, renumber, unnumbered } = plan
    if (refusals.length) {
        console.log(`diagnostics renumber: ${count(refusals.length, 'problem')}; nothing was written`)
        for (const refusal of refusals) console.log(`  - ${refusal}`)
        return 1
    }
    if (!renumber.length && !unnumbered.length) {
        console.log(
            `diagnostics renumber: no new declaration takes a value ${base} already published; nothing to renumber`
        )
        return 0
    }
    const file = path.join(root, REGISTRY_REL)
    const original = fs.readFileSync(file, 'utf8')
    const before = new Map(renumber.map((r) => [r.record.index, r.record.number]))
    const out = plan.treeLines.map((raw, i) => {
        if (!before.has(i)) return raw
        const fields = raw.split('\t')
        fields[2] = ''
        return fields.join('\t')
    })
    fs.writeFileSync(file, out.join('\n'))

    // The value and every generated file come from the one script that owns them. Its output is
    // this tool's output: `--write` prints what it wrote and how many numbers it assigned.
    const generated = spawnSync(process.execPath, [GENERATOR_REL, '--write', '--root', root], {
        cwd: root,
        encoding: 'utf8',
    })
    if (generated.stdout) process.stdout.write(generated.stdout)
    if (generated.stderr) process.stderr.write(generated.stderr)
    if (generated.status !== 0) {
        // It refuses before it writes anything (its checks run before its first write), so putting
        // the registry back leaves the tree as this run found it: a run either renumbers and
        // regenerates, or changes nothing at all.
        fs.writeFileSync(file, original)
        console.log(
            `diagnostics renumber: ${GENERATOR_REL} refused (exit ${generated.status}); ` +
                `${REGISTRY_REL} was restored and nothing was written`
        )
        return generated.status || 1
    }

    const after = readRegistry(file)
    const byLine = new Map(after.records.map((r) => [r.line - 1, r]))
    console.log(
        `diagnostics renumber: ${count(renumber.length + unnumbered.length, 'declaration')} numbered`
    )
    for (const { record } of renumber) {
        const now = byLine.get(record.index)
        console.log(`  - ${record.section}: ${record.name} ${hex6(record.number)} -> ${hex6(now.number)}`)
    }
    for (const record of unnumbered) {
        const now = byLine.get(record.index)
        console.log(`  - ${record.section}: ${record.name} (none) -> ${hex6(now.number)}`)
    }
    return 0
}

// ---- entry ------------------------------------------------------------------------------------

const argv = process.argv.slice(2)
let mode = '--check'
let root = DEFAULT_ROOT
let base = DEFAULT_BASE
let baseFile = null
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root') root = path.resolve(argv[++i] || '')
    else if (argv[i] === '--base') base = argv[++i] || ''
    else if (argv[i] === '--base-file') baseFile = argv[++i] || ''
    else mode = argv[i]
}
if (!['--check', '--write'].includes(mode)) usage(`'${mode}' is not a mode`)
if (!base && !baseFile) usage('--base needs a revision')

const tree = readRegistry(path.join(root, REGISTRY_REL))
if (!tree.records.length) {
    usage(
        `${path.join(root, REGISTRY_REL)} declares nothing; ` +
            `\`node scripts/diagnostics-catalog.mjs --check --root ${root}\` says what is wrong`
    )
}
const baseline = readBaseline(root, base, baseFile)
if (!baseline.registry.records.length) usage(`the baseline ${baseline.source} declares nothing`)
// The raw lines travel with the decision: the write path rewrites the same file, one field of one
// line at a time, and must not re-read a file that a concurrent run could have moved under it.
const decided = plan(tree, baseline.registry)
decided.treeLines = tree.lines

const code =
    mode === '--write'
        ? write(root, decided, baseline.source)
        : report(decided, baseline.source, baseline.registry)
process.exit(code)