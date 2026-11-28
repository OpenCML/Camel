#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Merge a batch of lane branches onto the integration tip, build once, push.
 *
 * What a merge seat does with a queue of lane branches:
 *
 *   1. Put the seat worktree on the current integration tip (detached, so no local branch can
 *      drift away from it).
 *   2. Merge each lane in turn as a real merge commit: the first parent is the tip the merge lands
 *      on, the second is the lane. A lane that does not merge textually is skipped and handed back
 *      to its owner to rebase; the seat never resolves a conflict.
 *   3. Build the result once, incrementally, running no plan. A tip that does not build stops every
 *      lane that rebases on it, which costs far more than one red test, and the build is the only
 *      thing checked here.
 *   4. A build that fails is searched inside the batch: the merges are a chain, so the first prefix
 *      of the chain that does not build names the lane that broke it (a binary search over
 *      prefixes). That lane is dropped, the lanes after it are merged again on top of the last good
 *      prefix, and the result is built again until it is green. The search presumes the tip the
 *      batch started from builds; so, once, when the first build fails, the tip itself is built
 *      before any prefix is. A tip that is red stops the tool: no lane is to blame, none is dropped.
 *   5. Read the built tip. A lane's own reading, taken on the lane's branch, says nothing about the
 *      merged tip. A batch whose files reach the build can change the answer of any case, so the
 *      whole fast layer is run; a batch that changes only cases, plans or pages is run through the
 *      plans the selector maps it to (`node scripts/test.js --changed --list`, with the integration
 *      tip the batch started from as the base). The run goes through the same command family as the
 *      build, and the cases that are red are compared by name with the red table of the newest
 *      promotion that published develop (see `newestRedTable`). A red that is
 *      not in that table is new: the
 *      tool prints it with its plan and pushes nothing. It does not look for the lane to blame; the
 *      seat decides. With no new red, the tool writes `<git common dir>/camel-read/<sha>` beside the
 *      build record (`plans=`, `reds=`, `baseline=`, `tier=fast`, `new=0`), which the pre-push hook requires
 *      before a change to the build's inputs reaches integration. A batch that changes no path the
 *      build reads (the hook's own list) is not read and leaves no record; the hook does not ask for
 *      one either.
 *   6. With --push, the result is pushed to integration.
 *
 * What lanes do to each other beyond the plans a batch touches is not this tool's question. The
 * promotion measures the tip with the full fast layer and compares it with the previous promotion.
 *
 * Every merge message lists the lane's own commit titles and ends with the trailer
 * `Lane-Reading: <the line the lane handed in>`; the pre-push hook refuses a merge to integration
 * that has no such trailer.
 *
 * Usage:
 *   node scripts/merge-batch.mjs --seat <worktree> --lane <branch>[@<commit>]=<reading> [--lane ...]
 *        [--lanes <file.json>]      a JSON array of {"branch": ..., "commit": ..., "reading": ...}
 *                                   (`commit` optional). A lane with a commit is merged at it -- the
 *                                   commit the reading was taken on -- not at whatever its branch
 *                                   grew to after it was queued; the commit must be on the branch.
 *        [--build <command>]        run in the seat; {seat} is replaced by the seat path
 *        [--red-exit <n>]           the build's exit code that means "the sources do not build"
 *        [--build-timeout <s>]      a build still running after this long is ended with its whole tree
 *                                   and counts as "no verdict", not as red (default 5400)
 *        [--read <command>]         the plan run of step 5; {seat} is the seat path and {plans} the
 *                                   touched plans, comma-joined and relative to the seat. It must leave
 *                                   {seat}/test/results/fetched/summary.json (the runner's summary)
 *        [--read-timeout <s>]       the same limit for the plan run (default 5400)
 *        [--remote origin] [--integration integration] [--base <ref>] [--no-fetch] [--push]
 *        [--stack-on <commit> [--stack-from <seat>] [--stack-wait <s>] [--stack-poll <s>]]
 *                                   build and read on top of another batch's tip that has not landed
 *                                   yet (fetched from that batch's seat when it is not here), so the
 *                                   next batch is read while that one pushes; with --push it waits
 *                                   until integration contains that tip (default 3600 s, polling every
 *                                   30 s) and pushes then, and pushes nothing (exit 6) if it never does
 *
 * The default build command runs `internal/tools/camel-remote.ps1` on the seat with no plan, which
 * builds on the host and runs nothing else. The default plan run is the same script with `-Plan` and
 * `-Tier fast`, the layer the promotion's red table is measured on.
 *
 * A lane whose own added lines carry process traces (the check the pre-push hook runs, when the seat
 * has it) is skipped before the build, with the lines it found; a check that cannot read a lane is
 * reported and the lane merges unchecked.
 *
 * Exit codes: 0 done (lanes may have been skipped; read the summary), 2 the tool could not do its
 * job (bad arguments, dirty seat, a git command that failed, a build or plan run that gave no
 * verdict, touched plans that could not be selected, a plan run that left no summary or no timing
 * rows, no promotion red table to compare with), 3 the push was refused, 4 the seat's checkout was
 * held by another run (the code camel-remote gives for it), 5 the plans the batch touched have reds
 * the newest promotion does not (nothing pushed; the reds are printed), 6 the batch this one stacks
 * on did not land (nothing pushed), 7 the integration tip itself does not build (nothing dropped,
 * nothing pushed; fix the tip first). One code, one meaning.
 */

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { BUILD_INFO_ROOT_FILES, BUILD_INFO_ROOTS } from './common.js'
import { runLimitedSync } from './lib/job.mjs'

const DEFAULT_BUILD =
    'pwsh -NoProfile -File "{seat}/internal/tools/camel-remote.ps1" -Path "{seat}" -Jobs 8 -TokenClass funnel'

const DEFAULT_READ =
    'pwsh -NoProfile -File "{seat}/internal/tools/camel-remote.ps1" -Path "{seat}" -Plan "{plans}" -Tier fast -Jobs 8 -TokenClass funnel -NoGpuWatch -FetchFile summary.json -FromThisRun'

// The tier the promotion's red table is measured on; the reading compares only that tier's cases.
const BASELINE_TIER = 'fast'

function fail(code, why) {
    process.stderr.write(`merge-batch: ${why}\n`)
    process.exit(code)
}

// `<branch>` or `<branch>@<commit>`: the second merges the branch at the commit its reading was
// taken on. A branch name holds no `@`, so the last one splits the two.
function laneSpec(name, reading) {
    const at = name.lastIndexOf('@')
    return at > 0 ? { branch: name.slice(0, at), pin: name.slice(at + 1), reading } : { branch: name, reading }
}

function parseArgs(argv) {
    const out = {
        seat: null,
        lanes: [],
        lanesFile: null,
        build: DEFAULT_BUILD,
        redExit: 1,
        buildTimeout: 5400,
        read: DEFAULT_READ,
        readTimeout: 5400,
        remote: 'origin',
        integration: 'integration',
        base: null,
        fetch: true,
        push: false,
        stackOn: null,
        stackFrom: null,
        stackWait: 3600,
        stackPoll: 30,
    }
    for (let i = 0; i < argv.length; i += 1) {
        const name = argv[i]
        const value = () => {
            if (i + 1 >= argv.length) fail(2, `${name} needs a value`)
            i += 1
            return argv[i]
        }
        if (name === '--seat') out.seat = value()
        else if (name === '--lane') {
            const spec = value()
            const at = spec.indexOf('=')
            if (at <= 0) fail(2, `--lane needs <branch>=<reading>, got '${spec}'`)
            out.lanes.push(laneSpec(spec.slice(0, at), spec.slice(at + 1)))
        } else if (name === '--lanes') out.lanesFile = value()
        else if (name === '--build') out.build = value()
        else if (name === '--red-exit') out.redExit = Number(value())
        else if (name === '--build-timeout') out.buildTimeout = Number(value())
        else if (name === '--read') out.read = value()
        else if (name === '--read-timeout') out.readTimeout = Number(value())
        else if (name === '--remote') out.remote = value()
        else if (name === '--integration') out.integration = value()
        else if (name === '--base') out.base = value()
        else if (name === '--no-fetch') out.fetch = false
        else if (name === '--stack-on') out.stackOn = value()
        else if (name === '--stack-from') out.stackFrom = value()
        else if (name === '--stack-wait') out.stackWait = Number(value())
        else if (name === '--stack-poll') out.stackPoll = Number(value())
        else if (name === '--push') out.push = true
        else fail(2, `unknown argument '${name}'`)
    }
    if (!out.seat) fail(2, '--seat <worktree> is required')
    if (out.lanesFile) {
        let list
        try {
            list = JSON.parse(fs.readFileSync(out.lanesFile, 'utf8'))
        } catch (error) {
            fail(2, `${out.lanesFile} could not be read as JSON: ${error.message}`)
        }
        if (!Array.isArray(list)) fail(2, `${out.lanesFile} must hold a JSON array`)
        for (const one of list) {
            const named = laneSpec(String(one?.branch ?? ''), String(one?.reading ?? ''))
            if (one?.commit) named.pin = String(one.commit)
            out.lanes.push(named)
        }
    }
    if (out.lanes.length === 0) fail(2, 'no lane given (--lane <branch>=<reading> or --lanes <file>)')
    const seen = new Set()
    for (const lane of out.lanes) {
        lane.reading = lane.reading.trim()
        if (!lane.branch) fail(2, 'a lane has no branch name')
        if (!lane.reading || /[\r\n]/.test(lane.reading)) {
            fail(2, `the reading of ${lane.branch} must be one non-empty line`)
        }
        if (seen.has(lane.branch)) fail(2, `${lane.branch} is given twice`)
        seen.add(lane.branch)
    }
    if (!Number.isInteger(out.redExit)) fail(2, '--red-exit needs an integer')
    if (!(out.buildTimeout > 0)) fail(2, '--build-timeout needs a positive number of seconds')
    if (!(out.readTimeout > 0)) fail(2, '--read-timeout needs a positive number of seconds')
    if (!out.base) out.base = `${out.remote}/${out.integration}`
    return out
}

let seat = ''

function git(args, { allowFailure = false } = {}) {
    const run = spawnSync('git', ['-C', seat, ...args], { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 })
    if (run.status !== 0 && !allowFailure) {
        fail(2, `git ${args.join(' ')} failed in ${seat}: ${(run.stderr || run.stdout || '').trim()}`)
    }
    return { code: run.status ?? -1, out: (run.stdout || '').replace(/\n+$/, ''), err: (run.stderr || '').trim() }
}

function head() {
    return git(['rev-parse', 'HEAD']).out
}

function mergeOne(lane, laneSha, intoName) {
    const titles = git(['log', '--no-merges', '--format=%s (%h)', `HEAD..${laneSha}`]).out
    const lines = titles ? titles.split('\n').map((line) => `- ${line}`) : []
    const message = [
        `Merge ${lane.branch} into ${intoName}`,
        '',
        ...lines,
        '',
        `Lane-Reading: ${lane.reading}`,
        '',
    ].join('\n')
    const merged = git(['merge', '--no-ff', '--no-commit', laneSha], { allowFailure: true })
    if (merged.code !== 0) {
        const files = git(['diff', '--name-only', '--diff-filter=U'], { allowFailure: true }).out.split('\n').filter(Boolean)
        git(['merge', '--abort'], { allowFailure: true })
        const why = files.length ? `textual conflict in ${files.join(', ')}` : `git merge failed: ${merged.err || merged.out}`
        return { ok: false, reason: why, conflict: files.length > 0 }
    }
    if (git(['diff', '--cached', '--quiet'], { allowFailure: true }).code === 0) {
        git(['merge', '--abort'], { allowFailure: true })
        return { ok: false, reason: 'the merge changes nothing: its content is already in the tip' }
    }
    const file = path.join(os.tmpdir(), `merge-batch-${process.pid}.msg`)
    fs.writeFileSync(file, message)
    try {
        git(['commit', '--quiet', '-F', file])
    } finally {
        fs.rmSync(file, { force: true })
    }
    return { ok: true, sha: head() }
}

let buildTimeout = 5400

/**
 * The holder record camel-remote leaves in the seat when it exits 4 (`test/results/log/<run>/HELD`):
 * the merge seat's exit 4 is the one message a program reads, and the checkout's holder -- its host,
 * pid, start time and run id, and the command that ends it -- has to be in that message rather than
 * left for a person to find in the stream. The newest record wins; a run that left none is said to
 * have left none, which is not the same as "nothing held it".
 */
function newestHeldRecord() {
    const root = path.join(seat, 'test', 'results', 'log')
    let entries = []
    try {
        entries = fs.readdirSync(root, { withFileTypes: true })
    } catch {
        return null
    }
    let newest = null
    for (const entry of entries) {
        if (!entry.isDirectory()) continue
        const file = path.join(root, entry.name, 'HELD')
        try {
            const stat = fs.statSync(file)
            if (newest === null || stat.mtimeMs > newest.mtimeMs) newest = { file, mtimeMs: stat.mtimeMs }
        } catch {
            // no HELD in this run: a normal end
        }
    }
    try {
        return newest ? fs.readFileSync(newest.file, 'utf8').trim() : null
    } catch {
        return null
    }
}

function runBuild(command, label) {
    const started = Date.now()
    process.stdout.write(`merge-batch: build (${label}) ...\n`)
    const run = runLimitedSync(command.split('{seat}').join(seat), { cwd: seat, shell: true, timeoutMs: buildTimeout * 1000 })
    const seconds = (Date.now() - started) / 1000
    process.stdout.write(
        run.timedOut
            ? `merge-batch: build (${label}) ran past its limit of ${buildTimeout} s and was ended with its process tree\n`
            : `merge-batch: build (${label}) exit ${run.code} in ${seconds.toFixed(1)} s\n`,
    )
    // Exit 4 is this tool's own answer too, and it is read by a program rather than a person: the
    // holder record the run left in the seat goes into the message, so "the seat is held" always
    // arrives with the facts that end it.
    if (run.code === 4) {
        const held = newestHeldRecord()
        process.stdout.write("merge-batch: the seat's checkout was held by another run\n")
        if (held) {
            for (const line of held.split(/\r?\n/)) if (line.trim()) process.stdout.write(`  ${line}\n`)
        } else {
            process.stdout.write('  (the run left no HELD record under test/results/log; read the lines above)\n')
        }
        // The run's own code is kept: 4 means "this checkout has an owner already" for camel-remote,
        // and a batch that reported it as "the sources do not build" would send its reader to the wrong
        // problem. The record above says which of the two this was.
        fail(4, "the seat's checkout was held by another run; the record above names the holder and the command that ends it")
    }
    return { code: run.code ?? -1, seconds, timedOut: run.timedOut }
}

/** Why a build gave no verdict: it was cut off at its limit, or it exited with something other than 0 and the red code. */
function noVerdict(run, redExit) {
    return run.timedOut
        ? `the build did not run to a verdict (it ran past its limit of ${buildTimeout} s and was ended)`
        : `the build did not run to a verdict (exit ${run.code}, a red build exits ${redExit})`
}

let readTimeout = 5400

const toSlashes = (text) => text.replace(/\\/g, '/').replace(/^\.\//, '')

/** Whether any of the files is one the build reads; the list is the one the build's stamp hashes. */
function reachesBuild(files) {
    return files.some((file) => BUILD_INFO_ROOT_FILES.includes(file) || BUILD_INFO_ROOTS.includes(file.split('/')[0]))
}

/**
 * The plans the batch touched, relative to the seat: the seat's own selector, asked about the change
 * against the integration tip the batch started from. A selector that cannot answer is no answer, and
 * the caller stops: an unknown selection is not an empty one.
 */
function touchedPlans(baseSha) {
    const script = path.join(seat, 'scripts', 'test.js')
    const run = spawnSync(process.execPath, [script, '--changed', '--list'], {
        cwd: seat,
        encoding: 'utf8',
        env: { ...process.env, CAMEL_TEST_BASE: baseSha },
        maxBuffer: 64 * 1024 * 1024,
    })
    if (run.error || run.status !== 0) {
        const why = run.error ? run.error.message : `exit ${run.status}: ${(run.stdout + run.stderr).trim().split('\n').slice(-3).join(' | ')}`
        fail(2, `the plans the batch touched could not be selected (${why}); nothing was pushed`)
    }
    const plans = []
    for (const raw of (run.stdout || '').split(/\r?\n/)) {
        const line = raw.trim()
        if (!line || line.startsWith('[') || line.startsWith('(no plan')) continue
        const rel = path.isAbsolute(line) ? path.relative(seat, line) : line
        plans.push(toSlashes(rel))
    }
    return [...new Set(plans)]
}

/**
 * The red table of the newest promotion that published develop, in the seat's tree, or null when there
 * is none. A promotion that found new reds still writes its table, and that table lists those reds:
 * comparing with it would accept them as known. So a table counts only beside a record of the same
 * promotion number that names the develop sha it pushed.
 */
function newestRedTable() {
    const dir = path.join(seat, 'internal', 'reports')
    let names = []
    try {
        names = fs.readdirSync(dir)
    } catch {
        return null
    }
    const published = new Set()
    for (const name of names) {
        const record = /^\d{4}-\d{2}-\d{2}-promotion-(\d+)\.record\.json$/.exec(name)
        if (!record) continue
        try {
            const parsed = JSON.parse(fs.readFileSync(path.join(dir, name), 'utf8'))
            if (parsed && typeof parsed.pushed_develop_sha === 'string' && parsed.pushed_develop_sha) published.add(Number(record[1]))
        } catch {
            // A record that does not parse publishes nothing.
        }
    }
    let best = null
    for (const name of names) {
        const found = /^\d{4}-\d{2}-\d{2}-promotion-(\d+)-reds\.tsv$/.exec(name)
        if (!found || !published.has(Number(found[1]))) continue
        if (best === null || Number(found[1]) > best.n) best = { n: Number(found[1]), name, file: path.join(dir, name) }
    }
    return best
}

/**
 * The names a red table holds, as keys a summary row can be looked up by. The promotion writes
 * `<plan id>\t<case>`; a table written by hand has `<case>\t<plan path>\t...`. A row is looked up
 * under both shapes, so either table answers.
 */
function parseRedTable(text) {
    const keys = new Set()
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trimEnd()
        if (!line || line.startsWith('#')) continue
        const cells = line.split('\t')
        if (cells.length < 2) continue
        keys.add(`${cells[0]}\t${cells[1]}`)
        keys.add(`${cells[0]}\t${toSlashes(cells[1])}`)
    }
    return keys
}

function readSummary(file) {
    let doc
    try {
        doc = JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch (error) {
        fail(2, `the plan run left no readable summary.json at ${file}: ${error.message}; nothing was pushed`)
    }
    if (!Array.isArray(doc.test_timings) || doc.test_timings.length === 0) {
        fail(2, `${file} holds no test_timings rows, so nothing was measured; nothing was pushed`)
    }
    return doc.test_timings
}

/**
 * Step 5: run the plans the batch touched on the built tip and compare the reds with the newest
 * promotion. Returns `{ skipped }` when the batch does not reach the build, otherwise the reds that
 * are new. The record the hook asks for is written only when there are none.
 */
function readTip(args, baseSha, tip) {
    const files = git(['diff', '--name-only', baseSha, tip]).out.split('\n').filter(Boolean)
    // What is read follows what changed. A change the build reads can change the answer of any case
    // (a compiler file maps to the plans that name it, but what it prints reaches plans nobody
    // mapped), so the whole fast layer is read. A change to cases, plans or pages without one is read
    // through the plans the selector maps it to. A batch that changes neither is not read.
    const code = reachesBuild(files)
    const cases = files.some((file) => /^(test|examples|docs)\//.test(file))
    if (!code && !cases) {
        process.stdout.write('merge-batch: the batch changes no path the build or a plan reads; no plan is run and no reading record is written\n')
        return { skipped: true, newReds: [] }
    }
    const common = path.resolve(seat, git(['rev-parse', '--git-common-dir']).out)
    const recordFile = path.join(common, 'camel-read', tip)
    // A record of an earlier reading of this commit does not speak for this one.
    fs.rmSync(recordFile, { force: true })

    const baseline = newestRedTable()
    if (!baseline) fail(2, 'no promotion red table (internal/reports/*-promotion-<n>-reds.tsv) was found to compare with; nothing was pushed')
    const known = parseRedTable(fs.readFileSync(baseline.file, 'utf8'))

    const plans = code ? ['test/plans'] : touchedPlans(baseSha)
    process.stdout.write(
        code
            ? 'merge-batch: reading the whole fast layer (test/plans): the batch changes a path the build reads\n'
            : `merge-batch: reading ${plans.length} touched plan(s): ${plans.join(', ') || '(none)'}\n`,
    )
    let rows = []
    const summary = path.join(seat, 'test', 'results', 'fetched', 'summary.json')
    // Beside the record, the whole-layer summary a promotion may reuse; an earlier one is not this reading's.
    const summaryCopy = `${recordFile}.summary.json`
    fs.rmSync(summaryCopy, { force: true })
    if (plans.length > 0) {
        fs.rmSync(summary, { force: true })
        const command = args.read.split('{seat}').join(seat).split('{plans}').join(plans.join(','))
        const started = Date.now()
        const run = runLimitedSync(command, { cwd: seat, shell: true, timeoutMs: readTimeout * 1000 })
        process.stdout.write(
            run.timedOut
                ? `merge-batch: plan run ran past its limit of ${readTimeout} s and was ended with its process tree\n`
                : `merge-batch: plan run exit ${run.code} in ${((Date.now() - started) / 1000).toFixed(1)} s\n`,
        )
        // The runner exits 0 with no red and 1 with reds; anything else is a run that gave no reading.
        if (run.timedOut || (run.code !== 0 && run.code !== 1)) {
            fail(2, `the plan run did not run to a reading (${run.timedOut ? 'it was cut off at its limit' : `exit ${run.code}`}); nothing was pushed`)
        }
        rows = readSummary(summary)
    }
    // A plan file named as a target runs every case in it, whatever the tier asked for, while the
    // table is measured on one tier. Only the cases of that tier are compared; a row that says
    // nothing about its tier is compared, because a red that cannot be placed must not be dropped.
    const reds = rows.filter((row) => row.red !== false && (row.tier === undefined || row.tier === BASELINE_TIER))
    const outside = reds.filter((row) => !known.has(`${row.plan}\t${row.test}`) && !known.has(`${row.test}\t${toSlashes(String(row.plan_path ?? ''))}`))
    // A case whose plan declares it `flaky` names the defect that makes it fail on its own, and the
    // runner has already given it its second attempt. Its red is printed and recorded and does not
    // stop the batch: the defect has its own row, and a batch is not where it is decided. Every other
    // red outside the table is new.
    const intermittent = outside.filter((row) => typeof row.flaky === 'string' && row.flaky.length > 0)
    const newReds = outside
        .filter((row) => !intermittent.includes(row))
        .map((row) => ({ plan: String(row.plan), case: String(row.test) }))
    process.stdout.write(
        `merge-batch: ${rows.length} case(s) read, ${reds.length} red, ${newReds.length} not in ${baseline.name}` +
            `${intermittent.length > 0 ? `, ${intermittent.length} more declared flaky` : ''}\n`,
    )
    for (const row of intermittent) process.stdout.write(`flaky red  ${row.plan}  ${row.test}  (${row.flaky})\n`)
    if (newReds.length > 0) {
        for (const one of newReds) process.stdout.write(`new red  ${one.plan}  ${one.case}\n`)
        return { skipped: false, newReds, plans, reds: reds.length, baseline: baseline.name }
    }
    fs.mkdirSync(path.dirname(recordFile), { recursive: true })
    fs.writeFileSync(
        recordFile,
        [
            `plans=${plans.join(',')}`,
            `reds=${reds.length}`,
            `baseline=${baseline.name}`,
            `tier=${BASELINE_TIER}`,
            ...(intermittent.length > 0 ? [`flaky=${intermittent.map((row) => `${row.plan}/${row.test}`).join(',')}`] : []),
            'new=0',
            '',
        ].join('\n'),
    )
    // Only a reading of the whole layer can stand in for a promotion's own; touched plans cannot.
    if (code && rows.length > 0) fs.copyFileSync(summary, summaryCopy)
    return { skipped: false, newReds, plans, reds: reds.length, baseline: baseline.name }
}

function main() {
    const args = parseArgs(process.argv.slice(2))
    seat = path.resolve(args.seat)
    buildTimeout = args.buildTimeout
    readTimeout = args.readTimeout
    const startedAt = Date.now()
    if (git(['status', '--porcelain']).out) fail(2, `the seat ${seat} has uncommitted changes; a batch starts on a clean tree`)
    if (args.fetch) {
        git(['fetch', '--quiet', args.remote])
    }
    // A stacked batch starts from the tip of a batch that has not landed yet, so it is built and read
    // while that one pushes: the commit is fetched from the seat that holds it when it is not here.
    if (args.stackOn && args.stackFrom) git(['fetch', '--quiet', args.stackFrom, 'HEAD'], { allowFailure: true })
    if (args.stackOn && git(['cat-file', '-e', `${args.stackOn}^{commit}`], { allowFailure: true }).code !== 0) {
        fail(2, `--stack-on ${args.stackOn} is not a commit in this seat${args.stackFrom ? ` (fetched ${args.stackFrom} HEAD)` : ''}; give --stack-from <the seat that holds it>`)
    }
    const baseSha = git(['rev-parse', '--verify', `${args.stackOn ?? args.base}^{commit}`]).out
    git(['checkout', '--quiet', '--detach', baseSha])

    const skipped = []
    const merged = [] // { lane, sha } in merge order; the chain the build search runs over
    // A lane given as <branch>@<commit> is merged at that commit: the reading was taken there, and a
    // lane that went on committing after it was queued would otherwise bring commits nobody read.
    // The commit must still be on the branch, so a pin cannot name work the lane has dropped.
    const resolved = args.lanes.map((lane) => {
        const target = lane.pin ?? lane.branch
        const found = git(['rev-parse', '--verify', '--quiet', `${target}^{commit}`], { allowFailure: true })
        if (found.code !== 0) return { lane, sha: null }
        if (lane.pin) {
            const onBranch = git(['merge-base', '--is-ancestor', found.out, `${lane.branch}^{commit}`], { allowFailure: true })
            if (onBranch.code !== 0) {
                skipped.push({ branch: lane.branch, reason: `${lane.pin} is not on the branch` })
                return { lane, sha: undefined }
            }
            const tip = git(['rev-parse', `${lane.branch}^{commit}`]).out
            if (tip !== found.out) {
                const ahead = git(['rev-list', '--count', `${found.out}..${tip}`]).out
                process.stdout.write(`merge-batch: ${lane.branch} is merged at ${found.out.slice(0, 12)}, ${ahead} commit(s) before its tip\n`)
            }
        }
        return { lane, sha: found.out }
    }).filter((one) => one.sha !== undefined)

    // The pre-push hook refuses a push whose added lines carry process traces, and it says so only
    // after the build and the reading; asking the same checker about each lane's own added lines
    // first keeps one lane from costing the whole batch that wait. Returns the first hit lines
    // (up to three, joined) when the lane carries traces, otherwise null. A seat without the checker
    // is not asked; a checker that cannot read the lane is reported and the lane merges unchecked.
    const processTraces = (lane, sha) => {
        const checker = path.join(seat, 'scripts', 'check-process-traces.mjs')
        if (!fs.existsSync(checker)) return null
        const unchecked = (why) => {
            process.stdout.write(`merge-batch: the process-trace check could not read ${lane.branch} (${why}); merging it unchecked\n`)
            return null
        }
        const base = git(['merge-base', baseSha, sha], { allowFailure: true })
        if (base.code !== 0 || !base.out) return unchecked('no merge base with the integration tip')
        const run = spawnSync(process.execPath, [checker, '--root', seat, '--range', `${base.out}..${sha}`], {
            encoding: 'utf8',
            maxBuffer: 256 * 1024 * 1024,
        })
        if (run.error) return unchecked(run.error.message)
        const out = run.stdout || ''
        if (run.status === 0) return null
        if (run.status !== 1) return unchecked(`exit ${run.status}: ${out.trim().split('\n')[0] || (run.stderr || '').trim().split('\n')[0] || 'no output'}`)
        const hits = out.split('\n').filter((line) => /^ {2}\S/.test(line)).map((line) => line.trim())
        if (hits.length === 0) return unchecked(`exit 1 with no listed line: ${out.trim().split('\n')[0] || 'no output'}`)
        return hits.slice(0, 3).join('; ')
    }

    const mergeAll = (candidates, afterReason) => {
        for (const { lane, sha } of candidates) {
            if (sha === null) {
                skipped.push({ branch: lane.branch, reason: 'the branch does not exist in the seat' })
                continue
            }
            if (git(['merge-base', '--is-ancestor', sha, 'HEAD'], { allowFailure: true }).code === 0) {
                skipped.push({ branch: lane.branch, reason: 'already in the tip' })
                continue
            }
            const traces = processTraces(lane, sha)
            if (traces) {
                skipped.push({ branch: lane.branch, reason: `its added lines carry process traces, which the pre-push hook refuses: ${traces}` })
                continue
            }
            const result = mergeOne(lane, sha, args.integration)
            if (result.ok) merged.push({ lane, sha: result.sha })
            else skipped.push({ branch: lane.branch, reason: afterReason ? `${afterReason}: ${result.reason}` : result.reason, conflict: result.conflict })
        }
    }
    mergeAll(resolved, '')

    let buildSeconds = 0
    let builds = 0
    let baseBuilds = false // the integration tip is known to build once this is true
    if (merged.length > 0) {
        for (;;) {
            git(['checkout', '--quiet', '--detach', merged[merged.length - 1].sha])
            const full = runBuild(args.build, `${merged.length} merge(s)`)
            buildSeconds += full.seconds
            builds += 1
            if (full.code === 0 && !full.timedOut) break
            if (full.timedOut || full.code !== args.redExit) {
                fail(2, `${noVerdict(full, args.redExit)}; nothing was pushed and no lane was dropped`)
            }
            if (!baseBuilds) {
                git(['checkout', '--quiet', '--detach', baseSha])
                const probe = runBuild(args.build, 'the integration tip itself')
                buildSeconds += probe.seconds
                builds += 1
                if (!probe.timedOut && probe.code === args.redExit) {
                    fail(7, `the integration tip itself does not build (${baseSha.slice(0, 12)}); no lane is to blame, none was dropped and nothing was pushed; fix the tip first`)
                }
                if (probe.timedOut || probe.code !== 0) {
                    fail(2, `${noVerdict(probe, args.redExit)}; nothing was pushed and no lane was dropped`)
                }
                baseBuilds = true
                git(['checkout', '--quiet', '--detach', merged[merged.length - 1].sha])
            }
            // The chain is a sequence of tips; the first one that does not build names its lane.
            let good = 0 // prefix length known to build (0 is the integration tip itself, built above)
            let bad = merged.length
            while (bad - good > 1) {
                const mid = Math.floor((good + bad) / 2)
                git(['checkout', '--quiet', '--detach', merged[mid - 1].sha])
                const probe = runBuild(args.build, `first ${mid} merge(s)`)
                buildSeconds += probe.seconds
                builds += 1
                if (probe.timedOut) fail(2, `${noVerdict(probe, args.redExit)}; nothing was pushed and no lane was dropped`)
                if (probe.code === 0) good = mid
                else if (probe.code === args.redExit) bad = mid
                else fail(2, `${noVerdict(probe, args.redExit)}; nothing was pushed and no lane was dropped`)
            }
            const culprit = merged[bad - 1]
            skipped.push({ branch: culprit.lane.branch, reason: 'the build fails with it (found by building prefixes of the batch)' })
            process.stdout.write(`merge-batch: dropping ${culprit.lane.branch}\n`)
            const keep = merged.slice(0, bad - 1)
            const rest = merged.slice(bad).map((one) => ({ lane: one.lane, sha: resolved.find((r) => r.lane === one.lane).sha }))
            merged.length = 0
            merged.push(...keep)
            git(['checkout', '--quiet', '--detach', keep.length ? keep[keep.length - 1].sha : baseSha])
            mergeAll(rest, `after dropping ${culprit.lane.branch}`)
            if (merged.length === 0) break
        }
    }

    const tip = merged.length ? merged[merged.length - 1].sha : baseSha
    git(['checkout', '--quiet', '--detach', tip])

    const reading = merged.length > 0 ? readTip(args, baseSha, tip) : { skipped: true, newReds: [] }
    const held = reading.newReds.length > 0

    let pushed = false
    let pushCode = 0
    // A stacked batch pushes only once the batch under it is on integration: its build and its
    // reading are of a tree that holds that batch, and pushed before it they would put that batch
    // on integration under this one's reading. It waits for integration to contain the commit it
    // stacked on (landed as it is, or merged onto later arrivals by its own retry), and stops when
    // that does not happen within --stack-wait: the batch under it was held or refused, and this
    // reading includes what kept it off.
    let stackLost = false
    if (args.push && merged.length > 0 && !held && args.stackOn) {
        const deadline = Date.now() + args.stackWait * 1000
        const pause = new Int32Array(new SharedArrayBuffer(4))
        for (;;) {
            const fetched = git(['fetch', '--quiet', args.remote, args.integration], { allowFailure: true })
            const landed = fetched.code === 0 && git(['merge-base', '--is-ancestor', baseSha, 'FETCH_HEAD'], { allowFailure: true }).code === 0
            if (landed) {
                process.stdout.write(`merge-batch: the batch this one stacks on (${baseSha.slice(0, 12)}) is on ${args.integration}; pushing\n`)
                break
            }
            if (Date.now() >= deadline) {
                stackLost = true
                process.stdout.write(`merge-batch: the batch this one stacks on (${baseSha.slice(0, 12)}) did not reach ${args.integration} within ${args.stackWait} s; nothing is pushed\n`)
                break
            }
            Atomics.wait(pause, 0, 0, Math.max(1, args.stackPoll) * 1000)
        }
    }
    if (args.push && merged.length > 0 && !held && !stackLost) {
        // A batch builds and reads for minutes, and integration may move meanwhile. When what arrived
        // touches nothing the build reads (a page, a backlog row), the batch is merged onto the new tip
        // as one merge: its first parent is the tip integration now has, as the hook asks of every
        // arrival, and its tree differs from the batch's only in what the build does not read. The
        // build and the reading recorded for the batch's tip therefore hold for that merge too, and are
        // carried to it, saying where they were taken. An arrival the build reads means the reading is
        // of a tree that is not the one integration would hold, so the batch stops there.
        const common = path.resolve(seat, git(['rev-parse', '--git-common-dir']).out)
        for (let attempt = 1; ; attempt++) {
            const run = spawnSync('git', ['-C', seat, 'push', args.remote, `HEAD:refs/heads/${args.integration}`], { encoding: 'utf8' })
            process.stdout.write(`${run.stdout || ''}${run.stderr || ''}`)
            pushCode = run.status ?? -1
            pushed = pushCode === 0
            if (pushed || attempt >= 5) break
            if (git(['fetch', '--quiet', args.remote, args.integration], { allowFailure: true }).code !== 0) break
            const arrived = git(['rev-parse', 'FETCH_HEAD']).out
            const since = git(['merge-base', tip, arrived], { allowFailure: true }).out
            if (!since) break
            const files = git(['diff', '--name-only', since, arrived]).out.split('\n').filter(Boolean)
            if (reachesBuild(files)) {
                process.stdout.write(`merge-batch: integration moved to ${arrived.slice(0, 12)} with a change the build reads; run the batch again from the new tip\n`)
                break
            }
            process.stdout.write(`merge-batch: integration moved to ${arrived.slice(0, 12)} with ${files.length} file(s) the build does not read; merging the batch onto it and pushing again\n`)
            git(['checkout', '--quiet', '--detach', arrived])
            const lanes = merged.map((one) => one.lane.branch).join(', ')
            const merge = git(
                ['merge', '--no-ff', '--quiet', '-m', `Merge the batch built and read at ${tip.slice(0, 12)}: ${lanes}\n\nLane-Reading: built and read at ${tip.slice(0, 12)}; the commits that arrived meanwhile change nothing the build reads`, tip],
                { allowFailure: true },
            )
            if (merge.code !== 0) {
                git(['merge', '--abort'], { allowFailure: true })
                process.stdout.write(`merge-batch: the batch does not merge onto ${arrived.slice(0, 12)} (${merge.err}); run the batch again from the new tip\n`)
                break
            }
            const landed = head()
            if (reachesBuild(git(['diff', '--name-only', tip, landed]).out.split('\n').filter(Boolean))) {
                process.stdout.write(`merge-batch: the merge onto ${arrived.slice(0, 12)} differs from the batch in a file the build reads; run the batch again from the new tip\n`)
                break
            }
            for (const kind of ['camel-built', 'camel-read']) {
                const from = path.join(common, kind, tip)
                if (!fs.existsSync(from)) continue
                fs.writeFileSync(path.join(common, kind, landed), `${fs.readFileSync(from, 'utf8').replace(/\n*$/, '\n')}carried-from=${tip}\n`)
            }
            const summaryFrom = path.join(common, 'camel-read', `${tip}.summary.json`)
            if (fs.existsSync(summaryFrom)) fs.copyFileSync(summaryFrom, path.join(common, 'camel-read', `${landed}.summary.json`))
        }
    }

    const total = (Date.now() - startedAt) / 1000
    process.stdout.write('\nmerge-batch summary\n')
    process.stdout.write(`  base      ${baseSha}\n  tip       ${tip}\n`)
    process.stdout.write(`  merged    ${merged.length}\n`)
    for (const one of merged) process.stdout.write(`    ${one.lane.branch}  ${one.sha.slice(0, 12)}\n`)
    process.stdout.write(`  skipped   ${skipped.length}\n`)
    for (const one of skipped) process.stdout.write(`    ${one.branch}: ${one.reason}\n`)
    process.stdout.write(`  builds    ${builds}, ${buildSeconds.toFixed(1)} s\n`)
    process.stdout.write(`  total     ${total.toFixed(1)} s\n`)
    process.stdout.write(
        `  reading   ${reading.skipped ? 'not needed (no path the build reads)' : `${reading.plans.length} plan(s), ${reading.reds} red, ${reading.newReds.length} new against ${reading.baseline}`}\n`,
    )
    for (const one of reading.newReds) process.stdout.write(`    new red  ${one.plan}  ${one.case}\n`)
    process.stdout.write(`  pushed    ${held ? 'no (new reds)' : stackLost ? 'no (the batch it stacks on did not land)' : args.push ? (merged.length === 0 ? 'nothing to push' : pushed ? 'yes' : `REFUSED (exit ${pushCode})`) : 'no (--push not given)'}\n`)
    process.stdout.write(
        `MERGE_BATCH_RESULT ${JSON.stringify({
            base: baseSha,
            stackedOn: args.stackOn ?? null,
            tip,
            merged: merged.map((one) => ({ branch: one.lane.branch, sha: one.sha })),
            skipped: skipped.map((one) => ({ branch: one.branch, reason: one.reason })),
            builds,
            buildSeconds,
            totalSeconds: total,
            pushed,
            newReds: reading.newReds,
        })}\n`,
    )
    if (held) {
        fail(5, `the plans the batch touched have ${reading.newReds.length} red(s) that ${reading.baseline} does not list; nothing was pushed (the seat decides which lane brought them)`)
    }
    if (stackLost) {
        fail(6, `the batch this one stacks on (${baseSha.slice(0, 12)}) did not land; nothing was pushed -- run the batch again from integration's tip`)
    }
    if (args.push && merged.length > 0 && !pushed) {
        fail(3, 'the push was refused (the integration tip may have moved, or a hook refused it); run the batch again from the new tip')
    }
}

main()
