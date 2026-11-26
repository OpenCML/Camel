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
 *   5. With --push, the result is pushed to integration.
 *
 * What lanes do to each other that a build cannot see is not this tool's question. The promotion
 * measures the tip with the full fast layer and compares it with the previous promotion.
 *
 * Every merge message lists the lane's own commit titles and ends with the trailer
 * `Lane-Reading: <the line the lane handed in>`; the pre-push hook refuses a merge to integration
 * that has no such trailer.
 *
 * Usage:
 *   node scripts/merge-batch.mjs --seat <worktree> --lane <branch>=<reading> [--lane ...]
 *        [--lanes <file.json>]      a JSON array of {"branch": ..., "reading": ...}
 *        [--build <command>]        run in the seat; {seat} is replaced by the seat path
 *        [--red-exit <n>]           the build's exit code that means "the sources do not build"
 *        [--remote origin] [--integration integration] [--base <ref>] [--no-fetch] [--push]
 *
 * The default build command runs `internal/tools/camel-remote.ps1` on the seat with no plan, which
 * builds on the host and runs nothing else.
 *
 * Exit codes: 0 done (lanes may have been skipped; read the summary), 2 the tool could not do its
 * job (bad arguments, dirty seat, a build that did not run), 3 the push was refused, 4 the
 * integration tip itself does not build (nothing dropped, nothing pushed; fix the tip first).
 */

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const DEFAULT_BUILD =
    'pwsh -NoProfile -File "{seat}/internal/tools/camel-remote.ps1" -Path "{seat}" -Jobs 8'

function fail(code, why) {
    process.stderr.write(`merge-batch: ${why}\n`)
    process.exit(code)
}

function parseArgs(argv) {
    const out = {
        seat: null,
        lanes: [],
        lanesFile: null,
        build: DEFAULT_BUILD,
        redExit: 1,
        remote: 'origin',
        integration: 'integration',
        base: null,
        fetch: true,
        push: false,
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
            out.lanes.push({ branch: spec.slice(0, at), reading: spec.slice(at + 1) })
        } else if (name === '--lanes') out.lanesFile = value()
        else if (name === '--build') out.build = value()
        else if (name === '--red-exit') out.redExit = Number(value())
        else if (name === '--remote') out.remote = value()
        else if (name === '--integration') out.integration = value()
        else if (name === '--base') out.base = value()
        else if (name === '--no-fetch') out.fetch = false
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
        for (const one of list) out.lanes.push({ branch: String(one?.branch ?? ''), reading: String(one?.reading ?? '') })
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

function runBuild(command, label) {
    const started = Date.now()
    process.stdout.write(`merge-batch: build (${label}) ...\n`)
    const run = spawnSync(command.split('{seat}').join(seat), { cwd: seat, shell: true, stdio: 'inherit' })
    const seconds = (Date.now() - started) / 1000
    process.stdout.write(`merge-batch: build (${label}) exit ${run.status} in ${seconds.toFixed(1)} s\n`)
    return { code: run.status ?? -1, seconds }
}

function main() {
    const args = parseArgs(process.argv.slice(2))
    seat = path.resolve(args.seat)
    const startedAt = Date.now()
    if (git(['status', '--porcelain']).out) fail(2, `the seat ${seat} has uncommitted changes; a batch starts on a clean tree`)
    if (args.fetch) {
        git(['fetch', '--quiet', args.remote])
    }
    const baseSha = git(['rev-parse', '--verify', `${args.base}^{commit}`]).out
    git(['checkout', '--quiet', '--detach', baseSha])

    const skipped = []
    const merged = [] // { lane, sha } in merge order; the chain the build search runs over
    const resolved = args.lanes.map((lane) => {
        const found = git(['rev-parse', '--verify', '--quiet', `${lane.branch}^{commit}`], { allowFailure: true })
        return { lane, sha: found.code === 0 ? found.out : null }
    })

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
            if (full.code === 0) break
            if (full.code !== args.redExit) {
                fail(2, `the build did not run to a verdict (exit ${full.code}, a red build exits ${args.redExit}); nothing was pushed and no lane was dropped`)
            }
            if (!baseBuilds) {
                git(['checkout', '--quiet', '--detach', baseSha])
                const probe = runBuild(args.build, 'the integration tip itself')
                buildSeconds += probe.seconds
                builds += 1
                if (probe.code === args.redExit) {
                    fail(4, `the integration tip itself does not build (${baseSha.slice(0, 12)}); no lane is to blame, none was dropped and nothing was pushed; fix the tip first`)
                }
                if (probe.code !== 0) {
                    fail(2, `the build did not run to a verdict (exit ${probe.code}); nothing was pushed and no lane was dropped`)
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
                if (probe.code === 0) good = mid
                else if (probe.code === args.redExit) bad = mid
                else fail(2, `the build did not run to a verdict (exit ${probe.code}); nothing was pushed and no lane was dropped`)
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

    let pushed = false
    let pushCode = 0
    if (args.push && merged.length > 0) {
        const run = spawnSync('git', ['-C', seat, 'push', args.remote, `HEAD:refs/heads/${args.integration}`], { encoding: 'utf8' })
        process.stdout.write(`${run.stdout || ''}${run.stderr || ''}`)
        pushCode = run.status ?? -1
        pushed = pushCode === 0
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
    process.stdout.write(`  pushed    ${args.push ? (merged.length === 0 ? 'nothing to push' : pushed ? 'yes' : `REFUSED (exit ${pushCode})`) : 'no (--push not given)'}\n`)
    process.stdout.write(
        `MERGE_BATCH_RESULT ${JSON.stringify({
            base: baseSha,
            tip,
            merged: merged.map((one) => ({ branch: one.lane.branch, sha: one.sha })),
            skipped: skipped.map((one) => ({ branch: one.branch, reason: one.reason })),
            builds,
            buildSeconds,
            totalSeconds: total,
            pushed,
        })}\n`,
    )
    if (args.push && merged.length > 0 && !pushed) {
        fail(3, 'the push was refused (the integration tip may have moved, or a hook refused it); run the batch again from the new tip')
    }
}

main()
