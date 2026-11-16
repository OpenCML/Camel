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
 *
 * **One phenomenon has one owner.** A test may start a run of this runner (`feat/tools` does: the
 * layer-accounting and provenance cases drive two or three nested runs each), and that nested run's
 * window lies entirely inside the outer one's -- the outer run read the tree before its first test
 * and after its last, so every moment the nested run could have written something is covered by that
 * pair of readings. Judging it again is a second verdict on the same phenomenon, and the two
 * criteria differ: the nested window closes while a file a *sibling* test wrote is still there (a
 * leak, red), and that file is gone when the outer window closes (`appeared and were gone again`, an
 * annotation). Measured on 2026-11-16: `test/.provenance-probe-3367882.tmp`, written and removed by
 * `runner.provenance_line_names_the_artifact`, failed the nested run started by
 * `runner.tier_declared_against_started` and was an annotation in the outer run -- one gate, one
 * file, two verdicts. So the role travels in `CAMEL_TREE_GUARD`, which every run sets to `nested` for
 * its children: a nested run reads no tree, starts no watcher, and says on its own line that the
 * outer run owns the verdict. An unset or unrecognised value is the outer run, because the failure
 * mode of the wrong default here is a gate that is silently off.
 */
import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { RUNNER_OUTPUT_DIRS, isRunnerOutput } from './runner-output.mjs'

/**
 * The variable that says which run owns the verdict. The runner sets it for every process it starts,
 * so a run started inside another run inherits it with the rest of the environment.
 */
export const TREE_GUARD_ROLE_ENV = 'CAMEL_TREE_GUARD'

/** The one value that means "inside another run's window". */
const NESTED_ROLE = 'nested'

/**
 * Which run judges: `outer`, unless a run above this one said otherwise.
 *
 * The default is the judging role on purpose. A typo in the variable, a value written by some future
 * run, or a process started by hand must all land on "judge", never on the silence this module exists
 * to avoid: a gate whose off-switch is reached by accident is worse than no gate, because it still
 * prints a line.
 */
export function treeGuardRole(env = process.env) {
    return env[TREE_GUARD_ROLE_ENV] === NESTED_ROLE ? NESTED_ROLE : 'outer'
}

/**
 * Mark the processes this run starts as nested, so any runner among them defers instead of judging.
 *
 * Called once, on the environment every child the runner starts spreads -- the test program, a
 * `skip_unless` probe, a verification script -- rather than at each of the places that build one: a
 * fourth spawn site that forgot the mark would be a second verdict again, and it would look like an
 * ordinary omission rather than a defect. Idempotent, so a nested run re-marking its own children
 * (which are nested too) changes nothing.
 */
export function markChildrenNested(env) {
    env[TREE_GUARD_ROLE_ENV] = NESTED_ROLE
    return env
}

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
 * One `git status --porcelain --untracked-files=normal` output, as the map of the paths it lists.
 *
 * Pure, and exported for that reason: it is half of the verdict, and a fixture can hand it the
 * shapes git writes that a plain split gets wrong -- a whole new directory (`name/`), a rename
 * (`old -> new`), a quoted name. test/cases/tools/tree_guard.mjs pins all of them, so the parser is
 * measured without a checkout and without a run.
 */
export function parseStatus(stdout) {
    const entries = new Map()
    for (const line of `${stdout}`.split('\n')) {
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
    return entries
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
    return { ok: true, entries: parseStatus(proc.stdout) }
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
 *
 * `role` is which run is asking: a `nested` run answers before either reading is looked at, because
 * the outer run already owns this phenomenon and the nested verdict would only be a second opinion
 * read against a window that closed too early. That branch comes first, ahead of `updateGolden`: a
 * nested run that rewrote goldens is a nested writer like any other, and the reading of what it left
 * is the outer run's.
 */
export function judgeTree({
    before,
    after,
    firstSeen = new Map(),
    updateGolden = false,
    failure = null,
    watched = 0,
    role = 'outer'
}) {
    if (role === NESTED_ROLE) {
        return {
            judged: false,
            failed: false,
            deferred: NESTED_ROLE,
            reason:
                'nested: tree guard deferred to the outer run -- this run started inside another ' +
                "run's window, and the outer run read the whole tree before and after it"
        }
    }
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