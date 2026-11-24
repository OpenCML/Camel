// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A change that touches only documents still has to select a plan.
//
// The failure this pins down is not a slow selection, it is an absent one: `internal/backlog.md`
// and `internal/plans/` matched no rule, so `plansForChanges` returned null for a change made of
// those files and `--changed` widened to the whole fast layer while the merge side reported that
// the merge selected no plan. A documentation merge could then be run against a layer three
// hundred plans wide, or not measured at all -- and a merge that cannot be measured cannot be
// advanced. The two rules that close it name `feat/docs_links`, the plan whose `--citations` case
// reads every page and the paths those pages mention.
//
// The assertions are written so that the next reader who wants to relax this rule meets the
// reason: the selected plan must be docs_links, no file of the change may be unmatched, and the
// selection must not be the fast layer wearing a plan's name.
//
//   node --test test/tools/selection-docs-only.test.mjs

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { plansForChanges } from '../runner.mjs'

const docsOnly = ['internal/backlog.md', 'internal/plans/2026-11-24-daily-review.cn.md']

test('a diff of only backlog rows and plan pages selects feat/docs_links', () => {
    const selection = plansForChanges(docsOnly)
    assert.ok(selection, 'null here is what becomes the whole fast layer: a docs change must select a plan')
    const selected = [...selection.names, ...selection.targets].join(' ')
    assert.ok(
        selected.includes('feat/docs_links'),
        `docs_links is the plan that reads these files, and ${selected || '(nothing)'} is not it`
    )
})

test('no file of the change is left unmatched', () => {
    // `unmatched` is the hole the rules closed. It is asserted separately from the plan name: a
    // rule that matched one file and not the other still reports a plan, and the other file would
    // keep falling back.
    const selection = plansForChanges(docsOnly)
    assert.ok(selection, 'a docs-only change must select a plan at all')
    assert.deepEqual(selection.unmatched, [], 'every changed file is read by the rule that selected it')
})

test('the selection is the plan, not the fast layer under another name', () => {
    // The widening path is the thing being refused here: if a future edit turns these rules into
    // "run everything", the assertion above still passes on the name while the cost is a layer.
    const selection = plansForChanges(docsOnly)
    assert.ok(selection, 'a docs-only change must select a plan at all')
    const names = [...selection.names, ...selection.targets]
    assert.ok(names.length <= 2, `a two-file documentation change selected ${names.length} plans: ${names.join(', ')}`)
})