// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which merge made a plan red: revert the merges of a red tree one at a time until the plan goes
 * green, and name the revert that did it.
 *
 * The other direction -- start from a green tree and merge the lanes one at a time until the plan
 * goes red -- needs one build per merge AND a conflict resolution per merge, because every lane is
 * branched off a moving develop. This direction starts from the tree that is already known red, so
 * the build directory is the one the red run left, and a revert is a single `git revert -m 1` with
 * no hand resolution unless the merge itself conflicted.
 *
 * The plan is the judge, not the diff: a step counts as green only when the plan exits 0, and every
 * step prints the plan's own `[SUMMARY]` line so the log says what was measured, not just what was
 * concluded. A tree that is already green is answered as such instead of being reverted: the point
 * of the script is to find the first revert that turns the plan green, and on a green tree that
 * question has no answer but "none".
 *
 *   node scripts/bisect-merge.mjs --path D:/Projects/Camel-wt/h-guard --start 4fbdc4e3a \
 *        --plan test/plans/feat/consistency/runner.plan.toml
 *
 *   * `--merge` may be repeated to name the merges to revert, in the order to try them; without it
 *     the merges are read from `git log --merges --first-parent <start> --not <base>`, newest
 *     first.
 *   * each step runs the plan through the workstation wrapper (camel-remote.ps1), with `--jobs 1`
 *     by default: a red that only shows under load is a different question, and this script
 *     answers one question at a time.
 *   * exit 0: a culprit was found (or the baseline was already green); 3: a revert conflicted and
 *     the series stops there; 4: every merge was reverted one at a time and the plan stayed red,
 *     which means no single merge is the cause.
 */
import { spawnSync } from 'child_process'
import path from 'path'

function parseArgs(argv) {
    const args = {
        path: null,
        start: null,
        plan: null,
        base: 'origin/develop',
        branch: 'probe/bisect-merge',
        jobs: 1,
        tier: 'fast',
        wrapper: null,
        merges: [],
        before: [],
    }
    for (let i = 0; i < argv.length; i += 1) {
        const key = argv[i]
        const value = argv[i + 1]
        switch (key) {
            case '--path':
                args.path = value
                i += 1
                break
            case '--start':
                args.start = value
                i += 1
                break
            case '--plan':
                args.plan = value
                i += 1
                break
            case '--base':
                args.base = value
                i += 1
                break
            case '--branch':
                args.branch = value
                i += 1
                break
            case '--jobs':
                args.jobs = Number(value)
                i += 1
                break
            case '--tier':
                args.tier = value
                i += 1
                break
            case '--wrapper':
                args.wrapper = value
                i += 1
                break
            case '--merge':
                args.merges.push(value)
                i += 1
                break
            case '--before':
                args.before.push(value)
                i += 1
                break
            default:
                throw new Error(`unknown argument: ${key}`)
        }
    }
    for (const required of ['path', 'start', 'plan']) {
        if (args[required] === null) {
            throw new Error(`--${required} is required`)
        }
    }
    args.path = path.resolve(args.path)
    args.wrapper ??= path.resolve(args.path, '..', 'camel-remote.ps1')
    return args
}

/// Runs one command in the worktree and returns its status and combined output.
function run(command, args, cwd) {
    const result = spawnSync(command, args, { cwd, encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 })
    return {
        code: result.status ?? 1,
        output: `${result.stdout ?? ''}${result.stderr ?? ''}`,
    }
}

function git(args, cwd) {
    return run('git', args, cwd)
}

/// The plan's own summary line, which every step prints; `(no [SUMMARY])` when the run never got
/// that far (a build failure, a missing binary).
function summaryOf(output) {
    const lines = output.split(/\r?\n/).filter((line) => line.includes('[SUMMARY] total:'))
    return lines.length ? lines[lines.length - 1].trim() : '(no [SUMMARY])'
}

/// One measurement: the plan through the wrapper, built, one job.
function measure(args, label) {
    const result = run(
        'pwsh',
        [
            '-NoProfile',
            '-File',
            args.wrapper,
            '-Path',
            args.path,
            '-Plan',
            args.plan,
            '-Tier',
            args.tier,
            '-Jobs',
            String(args.jobs),
        ],
        args.path,
    )
    const summary = summaryOf(result.output)
    console.log(`[bisect] ${label} exit=${result.code} ${summary}`)
    if (result.code !== 0 && /error:|BUILD_EXIT=1/.test(result.output)) {
        const failure = result.output
            .split(/\r?\n/)
            .filter((line) => /error:|BUILD_EXIT=1/.test(line))
            .slice(0, 3)
        for (const line of failure) {
            console.log(`[bisect]   build: ${line.trim()}`)
        }
    }
    return { green: result.code === 0, code: result.code, summary }
}

/// The merges to try, newest first, when the caller named none.
function mergesOf(args) {
    if (args.merges.length) {
        return args.merges.map((merge) => {
            if (merge.includes(' ')) {
                return merge
            }
            const subject = git(['log', '-1', '--format=%s', merge], args.path).output.trim()
            return `${merge} ${subject}`
        })
    }
    const log = git(
        ['log', '--merges', '--first-parent', args.start, '--not', args.base, '--format=%H%x09%s'],
        args.path,
    )
    if (log.code !== 0) {
        throw new Error(`git log failed: ${log.output.trim()}`)
    }
    return log.output
        .split(/\r?\n/)
        .filter((line) => line.trim() !== '')
        .map((line) => {
            const [sha, ...subject] = line.split('\t')
            return `${sha} ${subject.join('\t')}`
        })
}

function main() {
    const args = parseArgs(process.argv.slice(2))
    const status = git(['status', '--porcelain'], args.path)
    if (status.code !== 0) {
        throw new Error(`git status failed: ${status.output.trim()}`)
    }
    if (status.output.trim() !== '') {
        console.log('[bisect] the worktree has uncommitted changes; commit or stash them first')
        return 2
    }
    const checkout = git(['checkout', '-B', args.branch, args.start], args.path)
    if (checkout.code !== 0) {
        throw new Error(`git checkout failed: ${checkout.output.trim()}`)
    }
    const start = git(['rev-parse', '--short', 'HEAD'], args.path).output.trim()
    console.log(`[bisect] baseline ${start} (branch ${args.branch}), plan ${args.plan}`)

    const baseline = measure(args, `baseline ${start}`)
    if (baseline.green) {
        console.log('[bisect] baseline already green: nothing to revert, no culprit to name')
        return 0
    }

    // A merge cannot be reverted under the commits built on top of it: undo those first, in the
    // order the caller gives them (newest first), and start the series from what the merge left.
    for (const commit of args.before) {
        const revert = git(['revert', '--no-edit', commit], args.path)
        if (revert.code !== 0) {
            git(['revert', '--abort'], args.path)
            console.log(`[bisect] before-revert ${commit} CONFLICTED`)
            console.log(`[bisect]   ${revert.output.split(/\r?\n/).slice(0, 4).join('\n[bisect]   ')}`)
            return 3
        }
        const head = git(['rev-parse', '--short', 'HEAD'], args.path).output.trim()
        const subject = git(['log', '-1', '--format=%s', commit], args.path).output.trim()
        console.log(`[bisect] before-revert ${commit} ${subject} -> ${head}`)
    }

    const merges = mergesOf(args)
    if (merges.length === 0) {
        console.log(`[bisect] no merges between ${args.start} and ${args.base}: nothing to revert`)
        return 4
    }
    console.log(`[bisect] ${merges.length} merge(s) to try, one at a time`)

    let step = 0
    for (const merge of merges) {
        const [sha] = merge.split(' ')
        step += 1
        const revert = git(['revert', '-m', '1', '--no-edit', sha], args.path)
        if (revert.code !== 0) {
            git(['revert', '--abort'], args.path)
            console.log(`[bisect] step ${step}/${merges.length} revert ${merge} CONFLICTED`)
            console.log(`[bisect]   ${revert.output.split(/\r?\n/).slice(0, 4).join('\n[bisect]   ')}`)
            console.log('[bisect] the series stops here: resolve the revert by hand and re-run')
            return 3
        }
        const result = measure(args, `step ${step}/${merges.length} revert ${merge}`)
        if (result.green) {
            const head = git(['rev-parse', '--short', 'HEAD'], args.path).output.trim()
            console.log(`[bisect] CULPRIT ${merge}`)
            console.log(`[bisect] green at ${head} after ${step} revert(s); the plan's own reading:`)
            console.log(`[bisect]   ${result.summary}`)
            return 0
        }
    }
    console.log(
        `[bisect] every one of ${merges.length} merges was reverted one at a time and the plan stayed red: ` +
            'no single merge is the cause',
    )
    return 4
}

process.exitCode = main()