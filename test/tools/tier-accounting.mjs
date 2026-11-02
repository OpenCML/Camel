// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Two numbers a tier run has to report side by side, and the judgement between them.
 *
 *   declared -- the test entries the plan files name for the layer being run, counted through the
 *               runner's own loaders, one per entry;
 *   started  -- the tests this run actually launches.
 *
 * **Declared smaller than started is normal**: a run expands a test that names `backends` into one
 * test per back end, and a `compare_to` companion joins the run without being declared for the
 * layer. On 2026-11-02 the milestone layer was `3330 declared / 3404 started`, and nobody had ever
 * printed the pair, so 3330 was read as "the layer" while a run of it was 3404.
 *
 * **Declared larger than started is a defect**, and the one this module exists for: an entry the
 * file declares for this layer that the run does not launch is neither run nor skipped, so it
 * appears in no summary and no count -- it is the selector dropping a test, which is how a plan
 * starts proving nothing while looking green. It is reported per plan, by name, with both numbers.
 *
 * The started count is **counted, never computed**: multiplying by `backends.length` gives 7339 for
 * a layer that starts 3404 (a swept copy carries the source's `backends` list and would be
 * multiplied twice). A number that a check rests on is measured.
 */

export function tierAccountingLine(tier, rows) {
    const declared = rows.reduce((sum, row) => sum + row.declared, 0)
    const started = rows.reduce((sum, row) => sum + row.started, 0)
    return `${tier}: ${rows.length} plan file(s), ${declared} declared test entries, ${started} started`
}

/** The plans whose declared entries the run did not all start. One message each, naming the plan. */
export function tierAccountingProblems(rows) {
    return rows
        .filter((row) => row.declared > row.started)
        .map(
            (row) =>
                `${row.plan}: declares ${row.declared} test entries of this layer and the run started ` +
                `${row.started} -- ${row.declared - row.started} declared entry(s) neither ran nor were ` +
                'skipped, which is the selector dropping a test rather than a plan that proves nothing',
        )
}