// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The trees a plan run writes, and the retention the runner enforces on them.
 *
 * Neither tree is part of the checkout. `test/results` is the run's record (the captured output of
 * every test, a `meta.json` per test, a `run.json` per run, `summary.json` at the run root) and
 * `tmp/test` is the per-test scratch. Both are gitignored, so no `git clean` removes them, and
 * nothing in the repository reads an old one back: the tools that name them skip the walk (they are
 * not pages and not source), and the one consumer is a person reading the run they just made, while
 * it runs or just after it.
 *
 * They grow per run, not per test, and they grew without bound: the integration checkout held
 * 3,963 run directories under `test/results/log/<timestamp>_<pid>/`, 52,442 MB and 6,809,438
 * entries in all, and a bare walk of that tree cost the doc-form guard 41.4 s of its 70 s budget
 * (0.42 s in a checkout that had run the gate once) -- 99.4% of the guard, which pushed it past the
 * runner's `TIMEOUT_MARGIN` and turned a passing check red in three recorded runs. The same tree was
 * back to 660 run directories and 8.8 GB within four hours of being pruned to 20 by hand -- every one
 * of those directories written that morning -- which is what a policy in a person's hands buys.
 *
 * One run directory costs what it costs, measured on the largest of those (a `smoke` + `feat` fast
 * layer gate at `-Jobs 8`, 5,691 tests, written by the runner before the plan and case copies were
 * dropped): 43,898 entries, 245.0 MB of file content, 353 MB of disk blocks. Of that, the copy of the
 * plan and of the case was 11,382 files and 214.2 MB -- 87% of the bytes and 26% of the entries, the
 * same plan file rewritten once per test. The same gate on this branch (5,682 tests, all of them
 * runs): 32,461 entries, 31.0 MB of file content, 112 MB of disk blocks, and no file in it that the
 * runner did not write. See `docs/test-plans.md`, "Runner Output".
 *
 * So this module is the one definition of both facts: `isRunnerOutput` answers "did a run write
 * this path?" for anything that walks the checkout, and `pruneRunnerOutput` is the bound the runner
 * applies to the same two trees before it starts a run. A guard that has to know the name
 * `test/results` is one more place the next tree has to be added to; asking here is not.
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
export const REPO_ROOT = path.resolve(HERE, '..', '..')

/** The trees a run writes into, as repository-relative paths with `/` separators. */
export const RESULTS_DIR = 'test/results'
/** The run directories: one per run, `<timestamp>_<pid>`, holding `pass/`, `fail/`, `xfail/`. */
const LOG_DIR_NAME = 'log'
export const LOG_DIR = `${RESULTS_DIR}/${LOG_DIR_NAME}`
/** The per-test scratch roots, one `run-<pid>` per runner process. */
export const SCRATCH_DIR = 'tmp/test'
export const RUNNER_OUTPUT_DIRS = [RESULTS_DIR, SCRATCH_DIR]

/**
 * How many run directories `test/results/log` keeps, newest first, beside the current run and the
 * directories a live run still owns.
 *
 * A reader needs the run that is happening and the few that finished just before it: a failing
 * test's log is read while the run is on and just after it, and nothing reads yesterday's. 20 is
 * what the tree was pruned to by hand, and it is four times the number of runs this repository
 * starts at once in the worst case (the runner's own self-test starts one nested run at a time;
 * four host gates run side by side at most). One full `smoke` + `feat` fast layer run writes a
 * directory per selected plan and one per test -- about 5,700 status directories in the runs measured
 * below -- so the bound holds the tree at twenty gates instead of growing by a gate per run;
 * `docs/test-plans.md`, "Runner Output", has the measured figures.
 */
export const RUN_DIRS_KEPT = 20

/**
 * A scratch root `tmp/test/run-<pid>` outlives its process only as evidence: the runner removes its
 * own root when the run succeeded and keeps it when a test failed, for the files a person reads
 * right then. A root whose run is gone and that has not been touched for this long is neither being
 * written nor being read -- the self-test reads a kept nested root within seconds of the run that
 * kept it, so an hour is far beyond any reader, and it is short enough that a stale root does not
 * survive to the next day's walk.
 */
export const SCRATCH_ROOT_KEEP_MS = 60 * 60 * 1000

// The names the runner gives its own directories: `timestamp()_<pid>` (run-tests.mjs) and
// `run-<pid>`. A directory that matches neither was not written by this runner.
const RUN_DIR_NAME = /^\d{8}_\d{6}_(\d+)$/
const SCRATCH_ROOT_NAME = /^run-(\d+)$/

/**
 * Whether the process a directory is named for is still there. Signal 0 asks the operating system
 * without delivering anything; ESRCH means the process is gone, and EPERM means it exists and
 * belongs to someone else. A pid that is gone makes its run directory a candidate, which is exact
 * for "do not delete a directory a live run owns" -- including the nested runs the self-test starts,
 * whose directories are far older than the newest 20 by the time they matter.
 */
export function processAlive(pid) {
    if (!Number.isInteger(pid) || pid <= 0) return false
    try {
        process.kill(pid, 0)
        return true
    } catch (error) {
        return Boolean(error) && error.code === 'EPERM'
    }
}

/** The absolute path of one of the trees above (`RESULTS_DIR`, `LOG_DIR`, `SCRATCH_DIR`). */
export function runnerOutputPath(relative, repoRoot = REPO_ROOT) {
    return path.join(path.resolve(repoRoot), ...relative.split('/'))
}

/** Whether `target` (absolute, or relative to `repoRoot`) is inside a tree a run writes. */
export function isRunnerOutput(target, repoRoot = REPO_ROOT) {
    const full = path.resolve(repoRoot, target)
    return RUNNER_OUTPUT_DIRS.some((relative) => {
        const root = runnerOutputPath(relative, repoRoot)
        return full === root || full.startsWith(root + path.sep)
    })
}

function readdir(dir) {
    try {
        return fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return []
    }
}

function modifiedMs(full) {
    try {
        return fs.statSync(full).mtimeMs
    } catch {
        return 0
    }
}

/**
 * Entries and bytes under `dir`, the root itself not counted. Iterative, and one entry at a time:
 * a run's tree is deep enough that spreading a directory's whole subtree into one call overflows
 * the stack, and the walk is only ever asked about the directories being removed.
 */
function treeCost(dir) {
    let entries = 0
    let bytes = 0
    const stack = [dir]
    while (stack.length > 0) {
        const current = stack.pop()
        for (const child of readdir(current)) {
            entries++
            const full = path.join(current, child.name)
            if (child.isDirectory()) {
                stack.push(full)
            } else if (child.isFile()) {
                try {
                    bytes += fs.statSync(full).size
                } catch {
                    /* removed under us */
                }
            }
        }
    }
    return { entries, bytes }
}

function remove(full, report) {
    const cost = treeCost(full)
    try {
        fs.rmSync(full, { recursive: true, force: true })
    } catch (error) {
        report.errors.push(`${path.basename(full)}: ${error.message}`)
        return null
    }
    return cost
}

/**
 * Prune the run directories and the stale scratch roots of one checkout, before a run adds to
 * them. The policy, and why it is this one:
 *
 *   * `keep` newest run directories stay, ordered by the time each was last written -- for a
 *     finished run that is the moment its `summary.json` landed, so "newest" means "most recently
 *     read-relevant", not "started last".
 *   * a directory a live run owns is never removed, whatever its age or position: the self-test
 *     starts nested runs while an outer run is going, and they write to the same tree.
 *   * a directory this runner did not write (a name that does not match `timestamp_pid`) is left
 *     alone and reported instead: what this module does not understand is not its to delete.
 *   * a scratch root goes when its run is gone and the root has not been touched for
 *     `scratchKeepMs`, so the root of the run that just failed is still there to read.
 *
 * `currentRunDir` and `currentScratchRoot` name this process's own directories; they are protected
 * even before they exist (the runner prunes first, then creates them). `resultsRoot` is the record
 * tree this run writes into -- `test/results` unless `CAMEL_TEST_RESULTS_ROOT` moved it out of the
 * checkout -- so a run that writes elsewhere prunes what it is about to add a directory to, and not
 * the checkout's own tree. Everything removed is counted while it is removed, so the run prints what
 * the bound cost the tree rather than a number someone has to look up.
 */
export function pruneRunnerOutput(options = {}) {
    const repoRoot = options.repoRoot || REPO_ROOT
    const keep = options.keep === undefined ? RUN_DIRS_KEPT : options.keep
    if (!Number.isInteger(keep) || keep < 0) {
        throw new Error(`the retention bound needs a non-negative integer, got '${keep}'`)
    }
    const scratchKeepMs = options.scratchKeepMs === undefined ? SCRATCH_ROOT_KEEP_MS : options.scratchKeepMs
    const now = options.now === undefined ? Date.now() : options.now
    const isAlive = options.isAlive || processAlive
    const resultsRoot = options.resultsRoot ? path.resolve(options.resultsRoot) : runnerOutputPath(RESULTS_DIR, repoRoot)
    const logRoot = path.join(resultsRoot, LOG_DIR_NAME)
    const scratchRoot = runnerOutputPath(SCRATCH_DIR, repoRoot)
    const own = new Set(
        [options.currentRunDir, options.currentScratchRoot]
            .filter(Boolean)
            .map((entry) => path.resolve(entry))
    )

    const report = {
        keep,
        log_root: logRoot,
        scratch_root: scratchRoot,
        run_dirs: { scanned: 0, kept: 0, live: 0, foreign: 0, removed: 0, entries_removed: 0, bytes_removed: 0, removed_names: [], errors: [] },
        scratch_roots: { scanned: 0, kept: 0, live: 0, young: 0, foreign: 0, removed: 0, entries_removed: 0, bytes_removed: 0, removed_names: [], errors: [] },
    }

    const candidates = []
    for (const entry of readdir(logRoot)) {
        if (!entry.isDirectory()) continue
        report.run_dirs.scanned++
        const full = path.join(logRoot, entry.name)
        const name = entry.name.match(RUN_DIR_NAME)
        candidates.push({
            name: entry.name,
            full,
            pid: name ? Number(name[1]) : null,
            modified: modifiedMs(full),
        })
    }
    candidates.sort((lhs, rhs) => rhs.modified - lhs.modified || (lhs.name < rhs.name ? 1 : -1))
    for (const [index, candidate] of candidates.entries()) {
        if (own.has(candidate.full) || index < keep) {
            report.run_dirs.kept++
            continue
        }
        if (candidate.pid === null) {
            report.run_dirs.foreign++
            continue
        }
        if (isAlive(candidate.pid)) {
            report.run_dirs.live++
            continue
        }
        const cost = remove(candidate.full, report.run_dirs)
        if (!cost) continue
        report.run_dirs.removed++
        report.run_dirs.entries_removed += cost.entries
        report.run_dirs.bytes_removed += cost.bytes
        report.run_dirs.removed_names.push(candidate.name)
    }

    for (const entry of readdir(scratchRoot)) {
        if (!entry.isDirectory()) continue
        report.scratch_roots.scanned++
        const full = path.join(scratchRoot, entry.name)
        if (own.has(full)) {
            report.scratch_roots.kept++
            continue
        }
        const name = entry.name.match(SCRATCH_ROOT_NAME)
        if (!name) {
            report.scratch_roots.foreign++
            continue
        }
        if (isAlive(Number(name[1]))) {
            report.scratch_roots.live++
            continue
        }
        if (now - modifiedMs(full) < scratchKeepMs) {
            report.scratch_roots.young++
            continue
        }
        const cost = remove(full, report.scratch_roots)
        if (!cost) continue
        report.scratch_roots.removed++
        report.scratch_roots.entries_removed += cost.entries
        report.scratch_roots.bytes_removed += cost.bytes
        report.scratch_roots.removed_names.push(entry.name)
    }

    return report
}

/** The prune's receipt, one line, with the numbers it cost. */
export function formatRetention(report) {
    const runs = report.run_dirs
    const scratch = report.scratch_roots
    const megabytes = (bytes) => `${(bytes / (1024 * 1024)).toFixed(1)} MB`
    return `run dirs: ${runs.scanned} scanned, ${runs.removed} removed (${megabytes(runs.bytes_removed)}, `
        + `${runs.entries_removed} entries), ${runs.kept} kept of the newest ${report.keep}`
        + (runs.live > 0 ? ` plus ${runs.live} a live run owns` : '')
        + (runs.foreign > 0 ? `, ${runs.foreign} not written by the runner` : '')
        + `; scratch roots: ${scratch.removed} removed (${megabytes(scratch.bytes_removed)})`
        + (scratch.live + scratch.young > 0 ? `, ${scratch.live + scratch.young} kept` : '')
}