// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which merge made a plan red, asked in whichever direction the tree allows.
 *
 * Who wants the two directions to cross-check has to give a unit that can be judged first: a step
 * is only a step when it builds and the plan runs on it. Weaker than that, the plan never runs and
 * the step answers nothing (exit 7) -- and the two directions then reach different granularities,
 * because a merge inside a train can be reverted on its own but cannot be applied on its own when
 * the commits that complete it landed after it.
 *
 * Two rules for a unit, before anything else:
 *
 *   1. it must build: a merge that lands on a tree the compiler refuses is not a step, and the
 *      script says so and stops (exit 7) rather than calling the plan red there;
 *   2. the plan must run on it: a step whose reading has no `[SUMMARY]` was never measured, and a
 *      measurement that did not happen is not evidence either way;
 *   3. a harmless unit (`--harmless`) must have no ancestor relation to the units after it. When it
 *      contains one, `git merge` answers "Already up to date", that unit is never applied, and the
 *      series reads green because nothing was asked -- refused before anything is built (exit 8).
 *
 * Two directions, one judge (the plan's own exit code):
 *
 *   * `--revert` (default): the tree is already red. Revert its merges one at a time from the tip
 *     until the plan goes green; the revert that does it names the merge. One build per step, and
 *     no hand resolution unless a merge conflicts -- which is why this direction is the cheaper
 *     one, and why `--before` exists: a merge cannot be reverted under the commits built on top of
 *     it, so those are undone first, newest first.
 *   * `--forward`: a sha that is green. Merge the candidates one at a time, in the order given, and
 *     the first merge that turns the plan red names the culprit. This is the direction that also
 *     says whether the red needs one lane or two (a tree that stays green until the last merge was
 *     red because of the pair, and this mode says so instead of blaming the last one).
 *
 * The two directions do not always reach the same granularity. A merge inside a train can be
 * reverted on its own but cannot be applied on its own when the commits that complete it landed
 * after it: merging it into the green base lands on a tree that does not build, and the plan never
 * runs there. The forward series then has to name that unit with `--group` (the merge plus the
 * commits that complete it) or start from the train's tip, and the answer it gives is the unit it
 * was given -- which is a fact about the history, not about the plan.
 *
 * A reading only counts when it came from the tree the script thinks it measured. The wrapper
 * syncs the workstation tree to the host before it builds, so a stale host is repaired by the run
 * itself -- but the reading is checked afterwards: `git rev-parse HEAD` on the host must equal the
 * worktree's, and when it does not the step is refused (exit 5) with both shas printed, because a
 * number measured on another tree is exactly the false green this check exists for.
 *
 *   node scripts/bisect-merge.mjs --path D:/Projects/Camel-wt/h-guard --start 4fbdc4e3a \
 *        --plan test/plans/feat/consistency/runner.plan.toml
 *
 *   node scripts/bisect-merge.mjs --path D:/Projects/Camel-wt/h-guard --forward --start 77b962cc9 \
 *        --plan test/plans/feat/consistency/runner.plan.toml --merge 85664d056 --merge c697a08f6
 *
 *   * `--merge` may be repeated, in the order to try; without it the candidates are read from git
 *     (`--revert`: merges between the start and `--base`, newest first; `--forward`: merges in
 *     `--start..--base`, oldest first).
 *   * every step prints the plan's own `[SUMMARY]` line and its exit code, so the log says what was
 *     measured and not only what was concluded.
 *   * exit 0: a culprit was found (or the tree answered "already green" / "already red, forward
 *     asks no question"); 3: a revert or merge conflicted and the series stops there; 4: the series
 *     finished without an answer; 5: a reading came from a tree that is not this one; 6: forward
 *     was asked of a red baseline.
 */
import { spawnSync } from 'child_process'
import path from 'path'

function parseArgs(argv) {
    const args = {
        path: null,
        start: null,
        plan: null,
        base: 'origin/develop',
        branch: null,
        jobs: 1,
        tier: 'fast',
        wrapper: null,
        host: '88zjwei',
        hostRoot: null,
        forward: false,
        merges: [],
        groups: [],
        harmless: [],
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
            case '--host':
                args.host = value
                i += 1
                break
            case '--host-root':
                args.hostRoot = value
                i += 1
                break
            case '--merge':
                args.merges.push(value)
                i += 1
                break
            case '--harmless':
                args.harmless.push(value)
                i += 1
                break
            case '--group':
                args.groups.push(value)
                i += 1
                break
            case '--before':
                args.before.push(value)
                i += 1
                break
            case '--forward':
                args.forward = true
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
    args.hostRoot ??= `$HOME/camel-work/${path.basename(args.path)}`
    args.branch ??= args.forward ? 'probe/bisect-forward' : 'probe/bisect-revert'
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

/// The sha the host built from: the reading is taken on the host, so this is the tree it came from.
function hostHead(args) {
    const result = run(
        'ssh',
        ['-o', 'BatchMode=yes', args.host, `cd ${args.hostRoot} && git rev-parse HEAD`],
        args.path,
    )
    const sha = result.output.trim().split(/\s+/).pop()
    return { code: result.code, sha: sha ?? '' }
}

/// One measurement: the plan through the wrapper, built, one job -- and the tree it was measured on.
function measure(args, tag, label) {
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
    const local = git(['rev-parse', 'HEAD'], args.path).output.trim()
    const host = hostHead(args)
    if (host.code !== 0 || host.sha !== local) {
        console.log(`${tag} ${label} exit=${result.code} ${summary}`)
        console.log(`[bisect] REFUSED: the reading came from another tree`)
        console.log(`[bisect]   worktree ${local}`)
        console.log(`[bisect]   host     ${host.sha === '' ? '(unreadable)' : host.sha}`)
        return { green: false, code: 5, summary, refused: true }
    }
    console.log(`${tag} ${label} exit=${result.code} ${summary} (tree ${local.slice(0, 9)})`)
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

/// The candidates the caller named, or the merges git knows about in the direction being asked.
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
    const range = args.forward
        ? ['--merges', '--reverse', '--first-parent', `${args.start}..${args.base}`]
        : ['--merges', '--first-parent', args.start, '--not', args.base]
    const log = git(['log', ...range, '--format=%H%x09%s'], args.path)
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

/// Reverts a plain commit that a merge cannot be reverted under: newest first, no hand resolution.
function revertBefore(args) {
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
    return 0
}

/// The series the caller asked for, and the one relation that makes it a wrong question.
function planSteps(args) {
    // A step is one unit of history: a single merge, or a group of commits the caller names
    // because the unit only builds once all of them are applied (a merge whose follow-up commits
    // land after it is such a unit). `--harmless` names units applied before the series, to prove
    // that an order does not change the answer.
    const steps = args.groups.length
        ? args.groups.map((group) => group.split(',').map((sha) => sha.trim()))
        : mergesOf(args).map((merge) => [merge.split(' ')[0]])
    if (args.harmless.length === 0) {
        return { steps }
    }
    // A harmless unit that contains a later one swallows it: `git merge` answers "Already up to
    // date", the later unit is never applied, and a series that reads green is green because
    // nothing was asked. That is a wrong question, not a measurement, so it is refused here.
    const all = [...args.harmless, ...steps.flat()]
    for (const harmless of args.harmless) {
        for (const other of all) {
            if (other === harmless) {
                continue
            }
            const contained = git(['merge-base', '--is-ancestor', other, harmless], args.path).code === 0
            const equal =
                git(['rev-parse', harmless], args.path).output.trim() ===
                git(['rev-parse', other], args.path).output.trim()
            if (contained || equal) {
                console.log(
                    `[bisect] REFUSED: the harmless unit ${harmless} already contains ${other}; ` +
                        "git merge answers 'Already up to date' and swallows it, so the series " +
                        'would read green because nothing after it was applied',
                )
                return { refused: true }
            }
        }
    }
    return { steps: [...args.harmless.map((sha) => [sha]), ...steps] }
}

function forward(args, baseline, steps) {
    if (!baseline.green) {
        console.log(
            '[bisect] baseline already red: forward asks which merge turns a green tree red, and ' +
                'this tree is red before the first one is applied -- nothing to learn',
        )
        return 6
    }
    if (steps.length === 0) {
        console.log(`[bisect] no merges between ${args.start} and ${args.base}: nothing to apply`)
        return 4
    }
    console.log(`[bisect] ${steps.length} unit(s) to apply, one at a time`)
    let step = 0
    for (const shas of steps) {
        step += 1
        const subject = git(['log', '-1', '--format=%s', shas[shas.length - 1]], args.path)
            .output.trim()
        const label =
            shas.length === 1 ? `${shas[0]} ${subject}` : `group ${shas.join(',')} ${subject}`
        for (const sha of shas) {
            const applied = git(['merge', '--no-ff', '--no-edit', sha], args.path)
            if (applied.code !== 0) {
                git(['merge', '--abort'], args.path)
                console.log(`[bisect] step ${step}/${steps.length} apply ${label} CONFLICTED at ${sha}`)
                console.log(
                    `[bisect]   ${applied.output.split(/\r?\n/).slice(0, 4).join('\n[bisect]   ')}`,
                )
                console.log('[bisect] the series stops here: resolve the merge by hand and re-run')
                return 3
            }
        }
        const result = measure(args, '[forward]', `step ${step}/${steps.length} apply ${label}`)
        if (result.refused) {
            return 5
        }
        if (result.summary === '(no [SUMMARY])') {
            const head = git(['rev-parse', '--short', 'HEAD'], args.path).output.trim()
            console.log(
                `[bisect] step ${step}/${steps.length} did not build (at ${head}): forward can only ` +
                    'judge a state the plan ran on -- this unit needs more of its history before it ' +
                    'is a unit, and `--group` is how the caller says so',
            )
            return 7
        }
        if (!result.green) {
            const head = git(['rev-parse', '--short', 'HEAD'], args.path).output.trim()
            console.log(`[bisect] CULPRIT ${shas.join(',')} ${subject}`)
            console.log(`[bisect] red at ${head} after ${step} step(s); the plan's own reading:`)
            console.log(`[bisect]   ${result.summary}`)
            return 0
        }
    }
    console.log(
        `[bisect] every one of ${steps.length} unit(s) was applied and the plan stayed green: ` +
            'no single unit of this list turns it red',
    )
    return 4
}

function revert(args, baseline) {
    if (baseline.green) {
        console.log('[bisect] baseline already green: nothing to revert, no culprit to name')
        return 0
    }
    const before = revertBefore(args)
    if (before !== 0) {
        return before
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
        const reverted = git(['revert', '-m', '1', '--no-edit', sha], args.path)
        if (reverted.code !== 0) {
            git(['revert', '--abort'], args.path)
            console.log(`[bisect] step ${step}/${merges.length} revert ${merge} CONFLICTED`)
            console.log(
                `[bisect]   ${reverted.output.split(/\r?\n/).slice(0, 4).join('\n[bisect]   ')}`,
            )
            console.log(
                '[bisect] the series stops here: resolve the revert by hand, or undo the commits ' +
                    'built on this merge with --before, and re-run',
            )
            return 3
        }
        const result = measure(args, '[revert]', `step ${step}/${merges.length} revert ${merge}`)
        if (result.refused) {
            return 5
        }
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
    console.log(
        `[bisect] baseline ${start} (branch ${args.branch}), ${args.forward ? 'forward' : 'revert'}, plan ${args.plan}`,
    )
    const stale = hostHead(args)
    const local = git(['rev-parse', 'HEAD'], args.path).output.trim()
    if (stale.code === 0 && stale.sha !== '' && stale.sha !== local) {
        console.log(
            `[bisect] host sits on ${stale.sha.slice(0, 9)} before the run; the wrapper checks this ` +
                'tree out before it builds, and the reading is refused if it did not',
        )
    }

    // The series is checked before anything is built: a wrong question costs no build.
    const planned = args.forward ? planSteps(args) : null
    if (planned?.refused) {
        return 8
    }
    const baseline = measure(args, '[baseline]', start)
    if (baseline.refused) {
        return 5
    }
    return args.forward ? forward(args, baseline, planned.steps) : revert(args, baseline)
}

process.exitCode = main()