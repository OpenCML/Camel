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
import { spawn, spawnSync } from 'node:child_process'
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
    const segments = relative.split('/')
    const first = segments[0]
    if (BUILD_TREES.has(first)) return true
    // `out_lr*` is gitignored as a pattern, and a run of the profiler build makes one.
    if (/^out_lr/.test(first)) return true
    /**
     * A `node_modules` at any depth, not only at the top of the checkout.
     *
     * `.gitignore` ignores `node_modules/` at every level and no file under one is tracked (checked
     * over the whole tree: 0 of 7,921), so `git status` can never name a path in one -- and the
     * criterion this function implements is "which paths can carry a finding". The gate checkout
     * carries 1,579 directories of an installed `editors/vscode/node_modules`, and the first-segment
     * rule above walked and watched every one of them for a path the verdict could not name.
     * Measured nine samples per shape, on that checkout: 2,392 directories watched
     * and a walk of 88.0 ms before this line, 818 and 29.4 ms after it, which moved the pre-test
     * pair from 119.5 ms to 54.2 ms and one launch's fixed cost from 267.0 ms to 201.5 ms (twelve
     * paired rounds on one commit). **The criterion is unchanged**; the set of paths that can carry
     * a finding shrinks to the set `git status` could ever mention. Checked both ways on the changed
     * tree: a file planted in `editors/vscode/node_modules` stays green, the same file planted in
     * `src` still fails the run and is still named with the test that wrote it.
     */
    if (segments.includes('node_modules')) return true
    if (segments.includes('__pycache__')) return true
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

const STATUS_ARGS = ['-c', 'core.quotepath=false', 'status', '--porcelain', '--untracked-files=normal']
const STATUS_TIMEOUT_MS = 120000

/** One finished `git status`, as this module's reading. Shared so both readings interpret alike. */
function interpretStatus({ error, status, stdout, stderr }) {
    if (error) {
        // `spawnSync git ENOENT` and `spawn git ENOENT` are the same state; the API's name in the
        // message is the only difference between the two shapes, and a reason is a reading too.
        const message = `${error.message}`.replace(/^spawn(Sync)?\s+/, '')
        return { ok: false, reason: `git could not be started: ${message}` }
    }
    if (status !== 0) {
        const detail = `${stderr || ''}`.trim().split('\n')[0] || `exit ${status}`
        return { ok: false, reason: `git status failed: ${detail}` }
    }
    return { ok: true, entries: parseStatus(stdout) }
}

/**
 * One `git status`, as the map of the paths it lists.
 *
 * `core.quotepath=false` so a path with a character outside ASCII prints as itself: git quotes it as
 * `"\345\255\220 ..."` by default, which is not a name anyone can act on. It changes the rendering
 * only; the set of paths and the verdict are the same either way.
 */
export function readTreeState(root) {
    const proc = spawnSync('git', STATUS_ARGS, {
        cwd: root,
        encoding: 'utf8',
        windowsHide: true,
        timeout: STATUS_TIMEOUT_MS
    })
    return interpretStatus(proc)
}

/**
 * The same reading, without blocking this process while git runs.
 *
 * Why the second shape exists: the run's pre-test window is two readings that do not depend on one
 * another -- this `git status` and the watcher's walk over the checkout (startTreeWatch) -- and the
 * runner used to take them one after the other, paying their sum before its first test could start.
 * Started together they cost the longer of the two instead. Measured 2026-11-16, nine samples of
 * each shape, on the two trees a run is made on:
 *
 *   the gate checkout (Linux, Node 20.20.2, 2,392 directories): `git status` 25.8 ms, the walk and
 *   its watches 88.0 ms, the pair 119.5 ms in sequence and 93.3 ms together -- 26.2 ms recovered,
 *   which is the whole 25.8 ms the reading costs on its own, because the walk is the longer side;
 *   this workstation (Windows, Node 24.21.0, 816 directories): 135.2 ms and 276.1 ms, the pair
 *   408.9 ms in sequence and 342.8 ms together, 66.1 ms of the 135.2 recovered.
 *
 * Why "recovered" is short of the reading's own cost on Windows: the walk and `git status` read the
 * same filesystem, so the pair runs slower than either alone. What it buys is measured on the launch
 * itself (`--selection-empty`, which reads the tree and starts no test, paired rounds on one commit
 * and one tree): 280.5 ms before the change and 254.0 ms after it on the gate host, 1,012 ms and
 * 934 ms on this workstation. A run's first test no longer waits for the reading.
 *
 * What is *not* done, and why -- the walk itself is the coverage and stays:
 *
 *   * `fs.watch(root, { recursive: true })` instead of the walk. On Windows the OS has a native
 *     subtree watcher, but it also delivers every event under the two trees this run writes into
 *     (`tmp/test`, `test/results`), and those bursts overflow the notification buffer: a measured
 *     8,000-write burst under `tmp/test` produced 5 events, and a file written beside it was never
 *     reported. Losing the moment a path appears is losing the attribution, which is the whole
 *     reason the watcher exists. On Linux the recursive option is a JavaScript shim that installs a
 *     watch per directory anyway: measured 263.7 ms for 818 directories against 33.2 ms for this
 *     walk, and 1,518.6 ms against 123.9 ms for a 2,416-directory checkout. Not on either platform.
 *   * Watching fewer directories. `isIgnoredByWatch` is the one list of what a run may write, and
 *     narrowing the coverage to make the number smaller would be a guard that no longer sees the
 *     path it exists to name. (For the reader who wonders why the gate checkout walks 2,392: 1,579
 *     of them are the gitignored `editors/vscode/node_modules`, which the list keeps because it
 *     drops `node_modules` by a tree's first path segment only. Dropping it is a change to the list,
 *     not to this function.)
 *
 * So the walk is still taken before the first test; only its cost stopped being added to the git
 * reading's cost. A reading taken this way is byte-for-byte the reading readTreeState returns.
 */
export function readTreeStateAsync(root) {
    return new Promise((resolve) => {
        let proc
        try {
            proc = spawn('git', STATUS_ARGS, { cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] })
        } catch (error) {
            resolve(interpretStatus({ error, status: null, stdout: '', stderr: '' }))
            return
        }
        let stdout = ''
        let stderr = ''
        let settled = false
        const done = (reading) => {
            if (settled) return
            settled = true
            clearTimeout(timer)
            resolve(reading)
        }
        const timer = setTimeout(() => {
            try {
                proc.kill()
            } catch {
                // Already gone: the reading below is the one that counts either way.
            }
            done({ ok: false, reason: `git status did not finish within ${STATUS_TIMEOUT_MS} ms` })
        }, STATUS_TIMEOUT_MS)
        proc.stdout.setEncoding('utf8')
        proc.stderr.setEncoding('utf8')
        proc.stdout.on('data', (chunk) => (stdout += chunk))
        proc.stderr.on('data', (chunk) => (stderr += chunk))
        proc.on('error', (error) => done(interpretStatus({ error, status: null, stdout, stderr })))
        proc.on('close', (status) => done(interpretStatus({ error: null, status, stdout, stderr })))
    })
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