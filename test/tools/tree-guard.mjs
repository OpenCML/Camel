// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What a run left in the checkout it tested, and which test left it.
 *
 * A plan run is supposed to write inside the two trees the runner owns (`test/results`, `tmp/test`,
 * both gitignored: test/tools/runner-output.mjs) and nowhere else. It did not hold:
 * `test/plans/feat/tools/plan_refs_sweep.plan.toml` starts one `camel` per case in the tree, and the
 * cases it handed that compiler were run from the checkout root. `export_model<out: 'affine.onnx'>`
 * writes the model to the process's current directory and `env_paths.cml` writes
 * `数据 文件 🐫.txt` beside its own cwd, so one gate left twelve `.onnx` files, `子 目录/嵌套 文件.txt`
 * and `数据 文件 🐫.txt` in the repository root. Three costs, and only the first two are cosmetic:
 * `[provenance]` read `(dirty: 14 files)` on every run after it, so the one line that says which tree
 * a reading belongs to stopped distinguishing anything; `camel-remote.ps1` carries the untracked
 * files to the host on every run; and the `-Changed` file set was polluted with files nobody changed.
 * That producer is fixed where it was: the sweep gives each case it runs a directory of its own
 * (scripts/check-plan-refs.mjs). This module is the general answer, for the tests nobody has written
 * yet.
 *
 * So the end of every run answers the same question the reader would ask by hand:
 *
 *   [TREE] clean: nothing appeared under the checkout during this run
 *
 * **The verdict is `git status --porcelain --untracked-files=normal` read twice, once before the
 * first test and once after the last one, and the failure is the set difference.** Git decides what
 * is dirt -- the ignored trees are ignored because .gitignore says so, not because this module keeps
 * a list -- and a run that leaves nothing behind fails nothing. Two rules keep it from being a check
 * that cannot fail:
 *
 *   * a `git status` that cannot be read is a failure, not a clean tree: `asked and there are none`
 *     and `could not ask` are different states that both print as no paths;
 *   * what was already there when the run started is reported in full and never counted as this
 *     run's, because a lane that edits and then runs `--changed` would otherwise red every run it
 *     makes. It is printed rather than dropped: a path left by the run before this one has to be
 *     visible somewhere, and this line is where.
 *
 * **The attribution is a watcher, and it is not what decides the verdict.** The runner keeps the set
 * of tests whose child is alive (run-tests.mjs's `inFlight`); a watcher on the checkout records the
 * first moment a path appears together with that set. A path the verdict names is therefore printed
 * next to the tests that could have written it -- normally one, at `-Jobs 8` a handful. A watcher
 * that cannot be established, or a path that appeared while no test was running, costs the
 * attribution and never the verdict: the line says the attribution is missing instead of inventing
 * a name.
 *
 * The ignored trees are asked from runner-output.mjs rather than listed again here, for the reason
 * that module gives for itself: a guard that has to know the name `test/results` is one more place
 * the next tree has to be added to.
 */
import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { RUNNER_OUTPUT_DIRS, isRunnerOutput } from './runner-output.mjs'

/**
 * The trees a build writes, which a test may start (a plan of `tool = "camel"` cases that build, a
 * case that runs `npm`), and which are gitignored: watching them would be watching the build, and
 * the verdict never reads them anyway. Everything else under the checkout is watched.
 */
const BUILD_TREES = new Set([
    '.git',
    '.ccache',
    '.venv',
    'build',
    'cmake-build-debug',
    'cmake-build-release',
    'dist',
    'legacy',
    'node_modules',
    'out',
    'out_lr',
    'out_lr0',
    'out_lr1',
    'out_lr2',
    'vendor'
])

/** Whether a repository-relative path (with `/` separators) is a tree the runner or a build owns. */
export function isIgnoredByWatch(relative) {
    if (relative === '' || relative === '.') return true
    const first = relative.split('/')[0]
    if (BUILD_TREES.has(first)) return true
    // `out_lr*` is gitignored as a pattern, and a run of the profiler build makes one.
    if (/^out_lr/.test(first)) return true
    if (relative.split('/').includes('__pycache__')) return true
    return isRunnerOutput(relative)
}

/**
 * One `git status`, as the map of the paths it lists.
 *
 * `core.quotepath=false` so a path with a character outside ASCII prints as itself: git quotes it as
 * `"\345\255\220 ..."` by default, which is not a name anyone can act on. It changes the rendering
 * only; the set of paths and the verdict are the same either way.
 */
export function readTreeState(root) {
    const proc = spawnSync('git', ['-c', 'core.quotepath=false', 'status', '--porcelain', '--untracked-files=normal'], {
        cwd: root,
        encoding: 'utf8',
        windowsHide: true,
        timeout: 120000
    })
    if (proc.error) {
        return { ok: false, reason: `git could not be started: ${proc.error.message}` }
    }
    if (proc.status !== 0) {
        const detail = `${proc.stderr || ''}`.trim().split('\n')[0] || `exit ${proc.status}`
        return { ok: false, reason: `git status failed: ${detail}` }
    }
    const entries = new Map()
    for (const line of proc.stdout.split('\n')) {
        if (line.trim() === '') continue
        const status = line.slice(0, 2)
        let rel = line.slice(3)
        // A path git still had to quote (a control character, a newline) is kept as git wrote it:
        // unquoting it here would produce a name that `git status` could not be compared against.
        if (rel.startsWith('"') && rel.endsWith('"') && rel.length >= 2) rel = rel.slice(1, -1)
        // A rename is one line naming both paths; the one that exists after the run is the second.
        const arrow = rel.indexOf(' -> ')
        if (arrow >= 0) rel = rel.slice(arrow + 4)
        // `-u normal` names a whole new directory as `name/`; the watcher sees it as `name`, and the
        // trailing slash is git's way of saying it is a directory, not part of the name.
        rel = rel.replace(/\/+$/, '')
        if (rel !== '') entries.set(rel, status)
    }
    return { ok: true, entries }
}

/**
 * A watcher over the checkout, one `fs.watch` per directory and none on the trees a run owns.
 *
 * Not `{ recursive: true }`: on Linux that installs a watch per subdirectory anyway, including the
 * thousands under `tmp/test` and `test/results` that this run creates as it goes, which is both the
 * inotify budget and the work the run is measured on. Walking the tree once and skipping those
 * subtrees is the same coverage for a walk of about a thousand `readdir` calls.
 *
 * A directory that appears during the run is watched from that moment, so a file written one level
 * below a new directory is still attributed to the test that wrote it.
 */
export function startTreeWatch(root, { onPath, ignored = isIgnoredByWatch } = {}) {
    const watchers = new Map()
    let failure = null

    const handle = (dir, name) => {
        if (!name) return
        const full = path.join(dir, String(name))
        const relative = path.relative(root, full).split(path.sep).join('/')
        if (relative === '' || relative.startsWith('..') || ignored(relative)) return
        if (onPath) onPath(relative)
        let isDirectory = false
        try {
            isDirectory = fs.statSync(full).isDirectory()
        } catch {
            // Removed again between the event and this call: nothing to walk, and the path has
            // already been handed over as the one to attribute.
        }
        if (isDirectory) walk(full)
    }

    const watch = (dir) => {
        if (watchers.has(dir)) return
        let watcher
        try {
            watcher = fs.watch(dir, (event, name) => handle(dir, name))
        } catch (error) {
            failure = failure || `${path.relative(root, dir) || '.'}: ${error && error.message ? error.message : error}`
            return
        }
        watcher.on('error', (error) => {
            failure = failure || `${path.relative(root, dir) || '.'}: ${error && error.message ? error.message : error}`
        })
        watchers.set(dir, watcher)
    }

    const walk = (dir) => {
        watch(dir)
        let entries
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true })
        } catch {
            return
        }
        for (const entry of entries) {
            if (!entry.isDirectory()) continue
            const full = path.join(dir, entry.name)
            const relative = path.relative(root, full).split(path.sep).join('/')
            if (ignored(relative)) continue
            walk(full)
        }
    }

    walk(root)

    return {
        /** How many directories are watched: the reading that says the walk found the tree. */
        watched: () => watchers.size,
        /** Why the watcher is not whole, or null. Never the verdict: only the attribution. */
        failure: () => failure,
        stop() {
            for (const watcher of watchers.values()) {
                try {
                    watcher.close()
                } catch {
                    // A directory the run removed took its watcher with it.
                }
            }
            watchers.clear()
        }
    }
}

/**
 * The verdict, from the two readings and the watcher's record.
 *
 * `appeared` is the difference and it is the only thing that fails; `preexisting` is printed with
 * the same weight and fails nothing (see the header). `updateGolden` is the one run that is allowed
 * to rewrite the tree, and it says so instead of passing: a run that rewrites goldens cannot answer
 * whether it left something behind, and printing it as clean would be the default pass this whole
 * module exists to avoid. **A reading that could not be taken is a failure**, with `judged: false`
 * and the reason on the line: `could not ask` must never print as `there is nothing`.
 */
export function judgeTree({ before, after, firstSeen = new Map(), updateGolden = false, failure = null, watched = 0 }) {
    if (updateGolden) {
        return {
            judged: false,
            failed: false,
            reason: '--update-golden rewrites tracked files, so this run cannot say what it left behind'
        }
    }
    if (!before || !before.ok) {
        return {
            judged: false,
            failed: true,
            reason: `the tree could not be read before the run: ${before ? before.reason : 'no reading'}`
        }
    }
    if (!after || !after.ok) {
        return {
            judged: false,
            failed: true,
            reason: `the tree could not be read after the run: ${after ? after.reason : 'no reading'}`
        }
    }
    const appeared = []
    for (const [rel, status] of after.entries) {
        if (before.entries.has(rel)) continue
        const seen = firstSeen.get(rel) || null
        appeared.push({ path: rel, status, at: seen ? seen.at : null, running: seen ? seen.running : [] })
    }
    appeared.sort((left, right) => left.path.localeCompare(right.path))
    const preexisting = [...before.entries.keys()].sort((left, right) => left.localeCompare(right))
    const transient = []
    for (const [rel, seen] of firstSeen) {
        if (!after.entries.has(rel) && !before.entries.has(rel)) transient.push({ path: rel, at: seen.at, running: seen.running })
    }
    transient.sort((left, right) => left.path.localeCompare(right.path))
    return {
        judged: true,
        failed: appeared.length > 0,
        beforeCount: before.entries.size,
        afterCount: after.entries.size,
        appeared,
        preexisting,
        transient,
        failure,
        watched
    }
}