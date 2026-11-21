#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The `[TREE]` verdict, proved without a checkout: the guard can be shown to go red on demand.
 *
 * On 2026-11-16 the gate was proved the expensive way -- a probe case wrote `tree-leak-probe.txt`
 * into the checkout, the run printed `[FAIL] [TREE] 1 path appeared ...` and exited 1, and the probe
 * was then deleted. That proof lived in a report and could not be re-run: from the next day on,
 * anyone asking "does this gate actually fail?" had only the report's word for it, which is the same
 * shape as a check that never runs. So the verdict is a pure function of the two readings
 * (`judgeTree` in test/tools/tree-guard.mjs) and of the `git status` parser (`parseStatus`), and this
 * case pins both: an artificial difference must fail **and name the path**, an identical pair must
 * pass, and the two readings that cannot be taken must not read as clean.
 *
 * The other half of the same rule is pinned here too: a run started inside another run's window
 * defers instead of judging, so one phenomenon gets one verdict. That is a rule about which run
 * judges, and it is measured at the two ends that could silence the gate -- a nested run must not
 * fail, and a run nobody marked must still judge.
 *
 * Nothing here starts a process or writes a file, so it cannot leave anything behind itself.
 *
 * Run: node tree_guard.mjs
 */

import { TREE_GUARD_ROLE_ENV, judgeTree, parseStatus, treeGuardRole } from '../../tools/tree-guard.mjs'

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// ---- the two readings ---------------------------------------------------------------------------
const clean = { ok: true, entries: new Map() }
const withFile = (path, status = '??') => ({ ok: true, entries: new Map([[path, status]]) })
const unreadable = (reason) => ({ ok: false, reason })

// ---- an identical pair passes -------------------------------------------------------------------
{
    const verdict = judgeTree({ before: clean, after: clean })
    check('nothing on either side is judged and passes', verdict.judged === true && verdict.failed === false, JSON.stringify(verdict))
    check('and it says so with no paths in it', verdict.appeared.length === 0 && verdict.preexisting.length === 0, JSON.stringify(verdict))

    const same = judgeTree({ before: withFile('docs/page.md', ' M'), after: withFile('docs/page.md', ' M') })
    check('a path dirty on both sides is not this run\'s', same.failed === false && same.preexisting.includes('docs/page.md'), JSON.stringify(same))
}

// ---- one added path fails, and the line it is printed from names it ------------------------------
{
    const verdict = judgeTree({
        before: clean,
        after: withFile('affine.onnx'),
        firstSeen: new Map([['affine.onnx', { at: 220000, running: [{ plan: 'feat.tools.sweep', name: 'sweep.run', case: 'test/cases/x.cml' }] }]])
    })
    check('a path that appeared fails the run', verdict.failed === true, JSON.stringify(verdict))
    check('the verdict names the path', verdict.appeared.length === 1 && verdict.appeared[0].path === 'affine.onnx', JSON.stringify(verdict.appeared))
    check('and carries the status git gave it', verdict.appeared[0].status === '??', JSON.stringify(verdict.appeared))
    check('and the test that was running when it appeared', verdict.appeared[0].at === 220000 && verdict.appeared[0].running[0].name === 'sweep.run', JSON.stringify(verdict.appeared))
}

// ---- the same difference with no attribution is still a failure ---------------------------------
{
    const verdict = judgeTree({ before: clean, after: withFile('leak.txt') })
    check('a path the watcher did not see still fails', verdict.failed === true, JSON.stringify(verdict))
    check('and says the attribution is missing rather than inventing one', verdict.appeared[0].at === null && verdict.appeared[0].running.length === 0, JSON.stringify(verdict.appeared))
}

// ---- a path that appeared and went again is reported and does not fail --------------------------
{
    const seen = new Map([['planted-page.md', { at: 40, running: [{ plan: 'p', name: 'n', case: 'c.mjs' }] }]])
    const verdict = judgeTree({ before: clean, after: clean, firstSeen: seen })
    check('a path gone by the end does not fail the run', verdict.failed === false, JSON.stringify(verdict))
    check('it is still reported, with the test that had it', verdict.transient.length === 1 && verdict.transient[0].path === 'planted-page.md', JSON.stringify(verdict.transient))
}

// ---- one phenomenon, one verdict: a nested run defers to the outer one --------------------------
{
    // The reading that failed a gate twice: a sibling test wrote `test/.provenance-probe-*.tmp` and
    // removed it again, so the nested run's window closed with it there and the outer run's closed
    // after it was gone. The nested run must not answer, and the outer run must still fail on a file
    // that stayed -- the deferral moves the verdict, it does not remove it.
    const nested = judgeTree({ before: clean, after: withFile('test/.provenance-probe-1.tmp'), role: 'nested' })
    check('a nested run does not judge the tree', nested.judged === false && nested.failed === false, JSON.stringify(nested))
    check('and says the verdict went to the outer run', nested.deferred === 'nested' && /deferred to the outer run/.test(nested.reason || ''), `${nested.reason}`)

    const outer = judgeTree({ before: clean, after: withFile('test/.provenance-probe-1.tmp') })
    check('the same readings are still a failure for the outer run', outer.judged === true && outer.failed === true, JSON.stringify(outer))

    // Ordering: a nested run that rewrote goldens is a nested writer like any other, and what it
    // left is the outer run's reading -- `not judged` here would be a second exception on the same
    // phenomenon.
    const rewriting = judgeTree({ before: clean, after: clean, updateGolden: true, role: 'nested' })
    check('a nested --update-golden run defers rather than excusing itself', rewriting.deferred === 'nested' && rewriting.judged === false, JSON.stringify(rewriting))
}

// ---- which run judges, read from the environment ------------------------------------------------
{
    check('a run nobody marked is the outer run', treeGuardRole({}) === 'outer', treeGuardRole({}))
    check('a child of a run is the nested one', treeGuardRole({ [TREE_GUARD_ROLE_ENV]: 'nested' }) === 'nested', treeGuardRole({ [TREE_GUARD_ROLE_ENV]: 'nested' }))
    // The wrong default here would be a gate that is off and still prints: an unrecognised value
    // must resolve to the judging role, never to the silence.
    check('an unrecognised value judges rather than silencing the gate', treeGuardRole({ [TREE_GUARD_ROLE_ENV]: 'unknown' }) === 'outer', treeGuardRole({ [TREE_GUARD_ROLE_ENV]: 'unknown' }))
}

// ---- "could not ask" is never "there is nothing" ------------------------------------------------
{
    const before = judgeTree({ before: unreadable('git could not be started: ENOENT'), after: clean })
    check('an unreadable tree before the run is a failure', before.judged === false && before.failed === true, JSON.stringify(before))
    check('and the reason is on the line', /git could not be started/.test(before.reason), `${before.reason}`)

    const after = judgeTree({ before: clean, after: unreadable('git status failed: not a repository') })
    check('an unreadable tree after the run is a failure', after.judged === false && after.failed === true, JSON.stringify(after))

    const missing = judgeTree({ before: null, after: clean })
    check('a missing reading is a failure too', missing.judged === false && missing.failed === true, JSON.stringify(missing))

    const golden = judgeTree({ before: clean, after: clean, updateGolden: true })
    check('--update-golden is not judged and does not fail', golden.judged === false && golden.failed === false, JSON.stringify(golden))
    check('--update-golden gives its reason instead of a green line', /update-golden/.test(golden.reason), `${golden.reason}`)
}

// ---- the parser: the three shapes a character-per-line split gets wrong -------------------------
{
    const plain = parseStatus(' M docs/page.md\n?? a.onnx\n!! ignored.txt\n')
    check('a status line and a path become one entry', plain.get('docs/page.md') === ' M' && plain.get('a.onnx') === '??', [...plain].join(' '))
    check('blank lines are not entries', parseStatus('\n\n').size === 0, `${parseStatus('\n\n').size}`)

    const dir = parseStatus('?? 子 目录/\n')
    check('a new directory keeps its name and loses git\'s slash', dir.has('子 目录') && dir.get('子 目录') === '??', [...dir.keys()].join(' '))

    const renamed = parseStatus('R  old/path.md -> new/path.md\n')
    check('a rename is the path that exists after, not both', renamed.has('new/path.md') && renamed.size === 1, [...renamed.keys()].join(' '))

    const quoted = parseStatus('?? "a\\tb.txt"\n')
    check('a quoted path is kept as git wrote it', quoted.size === 1 && [...quoted.keys()][0] === 'a\\tb.txt', [...quoted.keys()].join(' '))
}

// ---- the two readings parsed from real git text ------------------------------------------------
{
    // The reading above is what the run's own before/after pair looks like, run through the parser
    // the same way `readTreeState` runs it: the 12 models and the directory a gate left in 2026-11.
    const before = parseStatus('')
    const after = parseStatus(
        ['?? affine.onnx', '?? checked.onnx', '?? 子 目录/', '?? 数据 文件 🐫.txt'].join('\n') + '\n'
    )
    const verdict = judgeTree({ before: { ok: true, entries: before }, after: { ok: true, entries: after } })
    check('the fourteen-file defect of 2026-11-16 reads as a failure here', verdict.failed === true && verdict.appeared.length === 4, JSON.stringify(verdict.appeared.map((item) => item.path)))
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)