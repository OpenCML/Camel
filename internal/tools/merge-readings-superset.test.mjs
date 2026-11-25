// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What a superset baseline may be used for, and what it may not.
 *
 * `merge-readings.mjs` used to accept one key: one develop commit, one plan set, one entry. A merge
 * that landed on a commit measured once across the whole fast layer, and selected three plans out of
 * it, missed that entry and measured a baseline of its own -- the cost D-1123-123 is about. The rule
 * this file pins down:
 *
 *   an entry whose plans COVER the merge's selection is a baseline for it (take only the selection's
 *   cases), and an entry that covers less than the selection is not a partial baseline but a miss.
 *
 * The second half is the one with teeth. The plans an entry lacks would all read as `added`, where a
 * red case is named and allowed -- so a short baseline is a gate that lets a new red through without
 * saying so, which is worse than a slow one.
 *
 * Every check runs the real script in a throwaway directory, with a plan tree of three plans and an
 * entry stored through the script's own `--accept`, so the keys, the row format and the `.where`
 * records are the ones the merge position really writes.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync, spawnSync } from 'node:child_process'

const here = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1')
const script = path.join(here, '..', '..', 'scripts', 'merge-readings.mjs')
const root = raw(fs.mkdtempSync(path.join(os.tmpdir(), 'merge-readings-superset-')))
let failures = 0
let checks = 0

function raw(p) {
    return process.platform === 'win32' ? p : p
}

function plan(id, cases) {
    return `id = "${id}"\n\n[[tests]]\nname = "${cases}"\n`
}

function row(name, verdict, reading) {
    return JSON.stringify({ name, verdict, reading: reading ?? null })
}

function write(rel, text) {
    const file = path.join(root, rel)
    fs.mkdirSync(path.dirname(file), { recursive: true })
    fs.writeFileSync(file, text)
    return file
}

function run(args, allowFailure = false) {
    try {
        const stdout = execFileSync(process.execPath, [script, ...args], {
            cwd: root,
            encoding: 'utf8',
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        return { code: 0, text: stdout }
    } catch (e) {
        if (!allowFailure) throw e
        return { code: e.status ?? -1, text: `${e.stdout ?? ''}${e.stderr ?? ''}` }
    }
}

// The same, with both streams: the row reader says an unparsed line on stderr and the conclusion on
// stdout, and a check that only saw one of them could not tell "said out loud" from "dropped quietly".
function runBoth(args) {
    const res = spawnSync(process.execPath, [script, ...args], { cwd: root, encoding: 'utf8' })
    return { code: res.status ?? -1, text: `${res.stdout ?? ''}${res.stderr ?? ''}` }
}

function check(label, ok, detail = '') {
    checks += 1
    if (ok) {
        console.log(`  PASS  ${label}`)
    } else {
        failures += 1
        console.log(`  FAIL  ${label}`)
        if (detail) console.log(`        ${String(detail).split('\n').slice(0, 8).join('\n        ')}`)
    }
}

// --- the fixture: three plans, three copies of one case -----------------------
write('test/plans/alpha/a.plan.toml', plan('alpha.plan', 'only'))
write('test/plans/beta/b.plan.toml', plan('beta.plan', 'only'))
write('test/plans/gamma/g.plan.toml', plan('gamma.plan', 'only'))
const allRows = write(
    'rows-all.jsonl',
    [row('alpha.plan/only', 'pass', 'ok'), row('beta.plan/only', 'pass', 'ok'), row('gamma.plan/only', 'fail', 'nope')].join('\n') + '\n'
)
const alphaOnly = write('rows-alpha.jsonl', `${row('alpha.plan/only', 'pass', 'ok')}\n`)
const alphaRed = write('rows-alpha-red.jsonl', `${row('alpha.plan/only', 'fail', 'boom')}\n`)

const supPlans = write('plans-superset.txt', 'test/plans/alpha\ntest/plans/beta\n')
const everythingPlans = write('plans-everything.txt', 'test/plans\n')
const alphaPlans = write('plans-alpha.txt', 'test/plans/alpha\n')
const gammaPlans = write('plans-gamma.txt', 'test/plans/gamma\n')
const alphaMissingPlans = write('plans-alpha-missing.txt', 'test/plans/alpha\nother/tree/plans\n')

const sha = 'a'.repeat(40) // the develop commit both entries are filed under
const cache = path.join(root, 'cache')

// --- the superset entry, stored the way the merge position stores one --------
const stored = run(['--current', allRows, '--cache', cache, '--develop', sha, '--plans', supPlans, '--tree', 'b'.repeat(40), '--accept'])
check('an entry measured across two plans is stored', stored.code === 0 && /stored this run as the baseline/.test(stored.text), stored.text)

// The same commit, measured across the whole plan tree: the shape the idle-slot run writes.
const storedWide = run(['--current', allRows, '--cache', cache, '--develop', sha, '--plans', everythingPlans, '--tree', 'c'.repeat(40), '--accept'])
check('an entry measured across the whole plan tree is stored', storedWide.code === 0, storedWide.text)

// --- 1: a selection inside an entry's plans is a hit, and says "superset" ----
const hit = run(['--cache', cache, '--develop', sha, '--plans', alphaPlans, '--probe'])
check('a selection inside the entry is a cache hit', hit.code === 0 && /cache-hit/.test(hit.text), hit.text)
check('the hit says it is a superset and how wide', /a superset: measured across \d+ target\(s\), covers the 1 this selection names/.test(hit.text), hit.text)

// --- 2: a selection the entries do not cover is a miss, and names the plan ---
const miss = run(['--cache', cache, '--develop', sha, '--plans', write('plans-outside.txt', 'other/tree/plans\n'), '--probe'], true)
check('a selection no entry covers is a miss', miss.code === 2 && /cache-miss/.test(miss.text), miss.text)
check('the miss names the target nobody measured', /other\/tree\/plans/.test(miss.text), miss.text)

// --- 3: the comparison takes only the selection's cases ----------------------
const cmp = run([
    '--current', alphaOnly, '--cache', cache, '--develop', sha, '--plans', alphaPlans,
    '--tree', 'd'.repeat(40), '--changed-files', write('changed-alpha.txt', 'test/plans/alpha/a.plan.toml\n'),
])
check('a comparison against a superset entry runs', cmp.code === 0, cmp.text)
check('only the selected plan is compared', /cases=1 fixed=0 new=0 changed=0/.test(cmp.text), cmp.text)
check('the cases left out are counted, not silently dropped', /2 case\(s\) of plans this merge does not select left out of the comparison; 1 remain/.test(cmp.text), cmp.text)
check('it says the baseline was a superset', /only its own plans are compared/.test(cmp.text), cmp.text)

// A red the superset baseline holds for a plan this merge did not select must not leak in: the same
// comparison against the whole-tree entry would read gamma's red as `unexplained` if the cut were
// missing -- a refusal about a plan this merge never ran.
const wide = run([
    '--current', alphaOnly, '--cache', cache, '--develop', sha, '--plans', alphaPlans,
    '--tree', 'e'.repeat(40), '--changed-files', write('changed-alpha2.txt', 'test/plans/alpha/a.plan.toml\n'),
])
check('a red plan outside the selection is not read as a difference', wide.code === 0 && !/unexplained|not selected/.test(wide.text), wide.text)

// --- 4: the selection's own red is still a red ------------------------------
const red = run([
    '--current', alphaRed, '--cache', cache, '--develop', sha, '--plans', alphaPlans,
    '--tree', 'f'.repeat(40), '--changed-files', write('changed-alpha3.txt', 'test/plans/alpha/a.plan.toml\n'),
], true)
check('a new red inside the selection still refuses', red.code === 1 && /newly red/.test(red.text), red.text)

// --- 5: an entry that covers less than the selection is a miss, not a share --
const partial = run([
    '--current', allRows, '--cache', cache, '--develop', sha, '--plans', alphaMissingPlans,
    '--tree', '1'.repeat(40), '--changed-files', write('changed-alpha4.txt', 'test/plans/alpha/a.plan.toml\n'),
], true)
check('a selection with a plan no entry covers is refused', partial.code === 2, partial.text)
check('and the refusal is a cache miss, not a comparison', /cache miss for/.test(partial.text), partial.text)

// --- 6: a target that is not a plan file at all is refused, not guessed -----
const unreadable = run([
    '--current', alphaOnly, '--cache', cache, '--develop', sha, '--plans', write('plans-dir.txt', 'test/plans/alpha\ntest/plans/nowhere/\n'),
    '--tree', '2'.repeat(40), '--changed-files', write('changed-alpha5.txt', 'test/plans/alpha/a.plan.toml\n'),
], true)
check('a selected target whose plan id cannot be read is refused', unreadable.code === 2 && /plan id could not be read/.test(unreadable.text), unreadable.text)

// --- 7: the exact entry still wins and is not called a superset -------------
const exact = run(['--current', alphaOnly, '--cache', cache, '--develop', sha, '--plans', alphaPlans, '--tree', '3'.repeat(40), '--accept'])
check('an exact entry can be stored beside the wide ones', exact.code === 0, exact.text)
const exactHit = run(['--cache', cache, '--develop', sha, '--plans', alphaPlans, '--probe'])
check('the exact entry is the one the probe answers with', exactHit.code === 0 && !/superset/.test(exactHit.text), exactHit.text)

// --- 8: a line that is not a row does not make the reading unmeasurable ----
// A failing case's own message can begin with `{`, and one such line used to be taken for a row:
// parsed, found to have no name or verdict, and the whole reading refused -- so the red itself was
// what hid the red. Both shapes below are "not a row": JSON with no name/verdict, and a message that
// is not JSON at all.
const strayRows = write(
    'rows-stray.txt',
    [
        row('alpha.plan/only', 'pass', 'ok'),
        '{"phase":"verify","note":"a message that happens to be JSON"}',
        'Error: reason missing: [{...}]',
    ].join('\n') + '\n',
)
const stray = runBoth([
    '--current', strayRows, '--cache', cache, '--develop', sha, '--plans', alphaPlans,
    '--tree', '9'.repeat(40), '--changed-files', write('changed-alpha9.txt', 'test/plans/alpha/a.plan.toml\n'),
])
check('a transcript with two non-row lines still compares', stray.code === 0, stray.text)
check('the two lines are counted and said out loud, not dropped quietly', /unparsed line\(s\): 2/.test(stray.text), stray.text)
check('the conclusion is drawn from the row that is a row', /cases=1 fixed=0 new=0 changed=0/.test(stray.text), stray.text)

// The same rule on the other reader: the row channel inside a transcript. That is the shape the
// defects came from -- a failing case's message printed between the markers -- so the block below
// holds one row and one `{`-leading message, and the baseline must still be stored.
const betaPlans = write('plans-beta.txt', 'test/plans/beta\n')
const strayTranscript = write(
    'transcript-stray.txt',
    [
        '__CAMEL_REMOTE_ROWS_START__',
        row('beta.plan/only', 'pass', 'ok'),
        '{"phase":"verify","note":"a message that happens to be JSON"}',
        '__CAMEL_REMOTE_ROWS_END__',
    ].join('\n') + '\n',
)
const storedStray = runBoth([
    '--transcript', strayTranscript, '--cache', cache, '--develop', '0'.repeat(40), '--plans', betaPlans,
    '--tree', '8'.repeat(40), '--accept',
])
check('a transcript whose block holds a non-row line still stores a baseline', storedStray.code === 0 && /stored this run as the baseline/.test(storedStray.text), storedStray.text)
check('and the transcript reader says the line was unparsed', /unparsed line\(s\): 1/.test(storedStray.text), storedStray.text)

console.log('')
if (failures > 0) {
    console.log(`merge-readings superset test: ${checks} checks, ${failures} failed`)
    process.exit(1)
}
console.log(`merge-readings superset test: ${checks} checks, 0 failed`)