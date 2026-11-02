#!/usr/bin/env node
/**
 * The numbers of a run, and the artifact they belong to, on one line.
 *
 * Four rewrites in one night came from a correct number attributed to the wrong artifact: a stale
 * `refs/remotes`, `build.ninja` read as `build-Release.ninja`, a lane's readings filed as
 * `develop`'s, a `-NoBuild` exit code read as a failure. So every run prints, beside `[SUMMARY]`:
 *
 *   [provenance] tip <sha> | binary built <ISO> | tree <abs> | tier <fast> | jobs <8>
 *
 * The three rules this case pins:
 *
 *   * `tip` is **this checkout's** `git rev-parse HEAD`, character for character -- not a branch ref,
 *     which names another tree's commit;
 *   * a **dirty tree has no tip**: `(dirty: N files)` and no sha at all, because the code that ran is
 *     that commit plus edits nobody recorded, and a sha would claim a provenance that does not exist;
 *   * `binary built` is the **binary's mtime**, not the build's log time: "the build ran" is not "the
 *     binary is new".
 *
 * The dirty reading is taken for real: the case creates one untracked file inside the checkout, runs
 * the runner, and removes it again -- an untracked file is part of what ran, so the count must move.
 *
 * Red when: the line is missing; the clean tip differs from `git rev-parse HEAD`; a dirty tree still
 * prints a sha; the binary field is not an ISO time taken from the binary; or the line sits away from
 * the summary it describes.
 *
 * Run: node provenance.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'
import { formatIsoTime, provenanceLine } from '../../tools/provenance.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'provenance-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

function git(args) {
    const result = spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8' })
    return result.status === 0 ? result.stdout.trim() : null
}

// ---- the rule, without running anything --------------------------------------------------------
{
    const sha = 'a'.repeat(40)
    const clean = provenanceLine({ sha, dirty: 0, binaryBuilt: '2026-11-02T11:37:43+08:00', tree: '/t', tier: 'fast', jobs: 8 })
    check('a clean tree prints the sha as given', clean.includes(`tip ${sha}`), clean)
    check('the line carries the tree, layer and width', clean.includes('tree /t') && clean.includes('tier fast') && clean.includes('jobs 8'), clean)

    const dirty = provenanceLine({ sha, dirty: 3, binaryBuilt: null, tree: '/t', tier: 'milestone', jobs: 2 })
    check('a dirty tree prints the count instead of a sha', dirty.includes('tip (dirty: 3 files)'), dirty)
    check('a dirty line contains no sha at all', !/[0-9a-f]{40}/.test(dirty), dirty)
    check('a missing binary reads unknown, not empty', dirty.includes('binary built unknown'), dirty)

    // A commit-9 tree must not be moved into the count by a null: `dirty: null` is unknown, not clean.
    const unknown = provenanceLine({ sha: null, dirty: 0, binaryBuilt: null, tree: null, tier: null, jobs: null })
    check('nothing known prints unknown rather than a plausible value', /tip unknown/.test(unknown), unknown)
}

{
    const local = new Date(2026, 10, 2, 11, 37, 43)
    const text = formatIsoTime(local)
    check('the time is ISO with a zone offset', /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}$/.test(text), text)
    check('a broken date reads unknown', formatIsoTime(new Date(Number.NaN)) === 'unknown', formatIsoTime(new Date(Number.NaN)))
}

// ---- the runner's own line, clean --------------------------------------------------------------
const CASE = path.join(SCRATCH, 'ok.mjs')
fs.writeFileSync(CASE, 'console.log("ok")\n')

function writePlan(name) {
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    fs.writeFileSync(
        planPath,
        [
            `id = "probe.${name}"`,
            'tier = "fast"',
            '',
            '[[tests]]',
            `name = "probe.${name}.entry"`,
            'kind = "behavior"',
            'tool = "node"',
            `case = "${CASE.replace(/\\/g, '/')}"`,
            'args = ["${case}"]',
            'expect_exit = 0',
            'expect_stdout_contains = ["ok"]',
            'timeout_ms = 30000',
            '',
        ].join('\n'),
    )
    return planPath
}

function run(planPath) {
    const result = spawnSync(process.execPath, [RUNNER, planPath, '--jobs', '1', '--tier', 'fast'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH, CAMEL_TEST_SHOW_OUTPUT: '' },
    })
    return { status: result.status, output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}

function provenanceOf(output) {
    const match = output.match(/^\[provenance\] (.*)$/m)
    return match ? match[1] : null
}

const plan = writePlan('clean')

{
    const before = (git(['status', '--porcelain']) || '').split('\n').filter((line) => line.trim() !== '').length
    if (before === 0) {
        const out = run(plan)
        const line = provenanceOf(out.output)
        const head = git(['rev-parse', 'HEAD'])
        check('a run prints a provenance line', line !== null, out.output)
        check('the clean tip is this checkout\'s HEAD, character for character', line !== null && line.includes(`tip ${head} |`), `${line} vs ${head}`)
        check('the binary field is an ISO time', /binary built \d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}/.test(line || ''), line)
        check('the tree field is the checkout the tests ran in', (line || '').includes(`tree ${REPO_ROOT}`), line)
        // The summary block opens with a blank line, so "right before" is one blank line away.
        check('the provenance line comes right before the summary', /\[provenance\][^\n]*\n\s*\n\[SUMMARY\]/.test(out.output), out.output)
        check(
            'the run is green and the summary keeps its shape',
            out.status === 0 && /^\[SUMMARY\] total: 1, pass: 1, fail: 0$/m.test(out.output),
            out.output,
        )
    } else {
        console.log(`ok   (skipped the clean reading: this checkout has ${before} changed file(s) -- its provenance is the dirty case below)`)
    }
}

// ---- the runner's own line, dirty --------------------------------------------------------------
{
    const before = (git(['status', '--porcelain']) || '').split('\n').filter((line) => line.trim() !== '').length
    const marker = path.join(REPO_ROOT, 'test', `.provenance-probe-${process.pid}.tmp`)
    try {
        fs.writeFileSync(marker, 'an untracked file is part of what ran\n')
        const out = run(plan)
        const line = provenanceOf(out.output) || ''
        check('a dirty tree prints the count', line.includes(`(dirty: ${before + 1} files)`), `${line} (was ${before})`)
        check('a dirty tree prints no sha', !/[0-9a-f]{40}/.test(line), line)
        check('a dirty run is still green', out.status === 0, `status ${out.status}`)
    } finally {
        fs.rmSync(marker, { force: true })
    }
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)