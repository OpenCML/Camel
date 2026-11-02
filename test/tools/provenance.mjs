// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Where a run's numbers came from, on one line next to them.
 *
 * Four rewrites in one night came from a number without its provenance: `refs/remotes` that were one
 * fetch stale ("those four commits are on no branch"), a `build.ninja` read as if it were
 * `build-Release.ninja`, a lane's readings filed as `develop`'s, and a `-NoBuild` run's hollow exit
 * code read as a failure. Every one of them was a correct number attributed to the wrong artifact.
 *
 * So every run states, beside its `[SUMMARY]`:
 *
 *   [provenance] tip 4f1c... | binary built 2026-11-02T11:37:43+08:00 | tree /abs/path | tier fast | jobs 8
 *
 * Three rules that are the whole point:
 *
 * **`tip` is the tested tree's sha, not a branch's.** `git rev-parse HEAD` of the checkout the tests
 * ran in -- never `origin/develop`, which is a different artifact with a different commit on it.
 *
 * **A dirty tree has no tip.** `(dirty: 3 files)` instead of a sha: the sha names the commit, and the
 * code that ran is that commit plus three edits nobody recorded, so printing the sha would be
 * claiming a provenance that does not exist. This is the one field a reader is most likely to trust.
 *
 * **`binary built` is the binary's mtime, not the build's start or finish time.** "I ran the build"
 * is not "the binary is new": a link that failed, a stale `out/latest`, or a copy of another tree's
 * artifacts all leave the log looking right and the executable old. The file's own timestamp is what
 * the process that ran was.
 */

export function formatIsoTime(date) {
    if (!(date instanceof Date) || Number.isNaN(date.getTime())) return 'unknown'
    const pad = (value, width = 2) => String(value).padStart(width, '0')
    const offsetMin = -date.getTimezoneOffset()
    const sign = offsetMin >= 0 ? '+' : '-'
    const offset = `${sign}${pad(Math.floor(Math.abs(offsetMin) / 60))}:${pad(Math.abs(offsetMin) % 60)}`
    return (
        `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}` +
        `T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}${offset}`
    )
}

/**
 * The line, from what the runner measured. `sha` is what `git rev-parse HEAD` printed (a full 40
 * characters), or null when there is no repository or no commit; `dirty` counts the files
 * `git status --porcelain` lists. A dirty tree prints no sha at all -- see above.
 */
export function provenanceLine({ sha, dirty, binaryBuilt, tree, tier, jobs }) {
    const count = Number.isFinite(dirty) && dirty > 0 ? dirty : 0
    const tip = count > 0 ? `(dirty: ${count} files)` : sha || 'unknown'
    const built = binaryBuilt || 'unknown'
    const place = tree || 'unknown'
    const layer = tier || 'fast'
    const width = Number.isFinite(jobs) ? jobs : 'unknown'
    return `tip ${tip} | binary built ${built} | tree ${place} | tier ${layer} | jobs ${width}`
}