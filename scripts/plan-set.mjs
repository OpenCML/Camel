// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A plan set, named and hashed -- one definition, because two programs have to agree on it.
 *
 * `merge-readings.mjs` keys a baseline by the plan set it was measured under and writes the same set
 * into the comparison it produces (`diff.json`); `merge-record.mjs` reads that set back out of the
 * document, recomputes its hash to prove the document says which plans were read, and stores the
 * pair in the record a merge leaves -- which the pre-push hook then checks against itself, so the
 * same function answers at both ends of the record. Those are all one value about one set, so they
 * are one function. A second copy of "how a plan set is
 * hashed" would agree on the day it was written and disagree on the day one of them learned about,
 * say, trimming a name -- and both would still look right in a passing record.
 *
 * What the hash is over: the *names* the caller handed in, and nothing else. It is order-independent
 * and content-sensitive, so the same plans listed in another order are the same set and one plan
 * added or removed is a different one. It is not a digest of the plans' contents: it says which
 * plans were read, and `diff.json` carries the baseline whose sha (and whose rows) say what they read.
 *
 * What this file decides and, more importantly, what it does NOT decide:
 *
 *   - It does not select plans. It reads no rule file, and in particular nothing in
 *     `test/plan-selection.json`; the caller's list *is* the set. `merge-lane.ps1` gets its list from
 *     `camel-remote.ps1 -Changed` and widens it to the whole fast layer when a change names no plan;
 *     those decisions are already made before a name reaches here, and none of them is re-made or
 *     second-guessed below.
 *   - It has no notion of a tier. `test/plans/smoke` and `test/plans/feat` are two directory names
 *     here, not the `fast` layer; a caller that wants the fast layer names both, as
 *     `internal/tools/merge-lane.ps1#Test-FastLayerBaseline` does. A plan set that happens to equal
 *     the fast layer and one that happens to equal a single plan hash differently, which is the point.
 *   - It does not expand a target. A directory and the plan files under it are different names, so
 *     `test/plans/feat` and `test/plans/feat/a.plan.toml` hash as two different sets even where they
 *     run the same cases -- because the hash answers "which targets were named", and expanding one
 *     into the other would need a reader of the plan tree that this file deliberately does not have.
 *     The comparison in `merge-readings.mjs` is what handles containment (`targetCovers`), because
 *     that is a question about which rows answer which target and not about the set's identity.
 *
 * Two names that differ only around the edges (` a.plan.toml` and `a.plan.toml`) are one name here:
 * the leading and trailing space is trimmed, an empty line is not a name, and a name listed twice is
 * still one plan. A cache key and a record that disagreed about that would be disagreeing about the
 * same set.
 */
import fs from 'node:fs'

/** The names `--plans` names: a file of one name per line, or a comma list. */
export function planSetNames(list) {
    return fs.existsSync(list) ? fs.readFileSync(list, 'utf8').split('\n') : list.split(',')
}

/**
 * The hash of a plan set: `<count>-<hash>`, order-independent, one name added or removed differs.
 *
 * `<count>` is of the trimmed, de-duplicated names, so it is the number of plans the set says were
 * read rather than the number of lines the caller wrote -- a caller that lists one plan twice gets
 * `1-...`, because one plan was read.
 */
export function planSetHash(names) {
    const list = [...new Set(names.map((n) => n.trim()).filter(Boolean))].sort()
    let h = 0
    for (const ch of list.join('\n')) h = (h * 31 + ch.codePointAt(0)) >>> 0
    return `${list.length}-${h.toString(16)}`
}