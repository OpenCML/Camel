#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One reading per claim of `scripts/vjp-parity.mjs`, and all three come off a run of the gate.
//
// The gate says one thing: a difference between the two derivative tables that the exemption list
// does not hold is a debt. That claim is only worth what the three readings below are worth, and a
// gate that has never been red on purpose is a gate nobody has seen work:
//
//   the real tree          -- `0 new` **and** `stale=0`, read with `--strict`: every difference
//                             the two tables have is written down with an owner and a reason, and
//                             no row in the list has outlived the difference it holds. This is the
//                             reading the plan pins, and `stale=0` is what makes it bite: before
//                             2026-11-14 the gate was silent about a row with no difference, and
//                             the list had grown to 80 rows for 56 differences -- a dead row reads
//                             exactly like a live one, so nothing named the difference.
//   a law with no rule     -- a copy of the rulesets with **one** rule added (`fake_parity_op_t`,
//                             an operator whose name appears in neither table) and the engine's
//                             table untouched: the gate must name that operator and exit 1. The
//                             copy is a temp root with `src/` and `modules/` linked to the real
//                             tree, so the engine side of the comparison is the tree's own and the
//                             only thing that changed is the law.
//   the same law, exempted -- the same copy with the row the list would need: the gate must exit 0
//                             and must not name the operator. That is what says the list is what
//                             turns the report off, rather than the report being blind to the copy.
//
// A gate that answered `0 new` for the second reading is the failure this file exists for: it would
// be green on the day a law is filed with no engine rule behind it, which is exactly the debt
// `pick_at`, `sum_stated`, `take_stated` and `mean_axis_stated` each were.
//
// Usage: node test/tools/check-vjp-parity.mjs
//        node test/tools/check-vjp-parity.mjs --keep   (leaves the temp root for inspection)

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const GATE = path.join(ROOT, 'scripts', 'vjp-parity.mjs')
const EXEMPTIONS = path.join(ROOT, 'scripts', 'vjp-parity-exemptions.tsv')
const FAKE = 'fake_parity_op'
const KEEP = process.argv.includes('--keep')

const failures = []

function fail(what, detail) {
    failures.push({ what, detail })
}

/// The gate, run the way the plan runs it: as a program, so its exit code is the claim. Output goes
/// through a file rather than a pipe, because a pipe is not available under every sandbox this runs
/// in, and a checker that cannot report is worse than one that reports slowly.
function runGate(args) {
    const log = path.join(os.tmpdir(), `vjp-parity-${process.pid}-${Math.random().toString(16).slice(2)}.log`)
    const fd = fs.openSync(log, 'w')
    const run = spawnSync(process.execPath, [GATE, ...args], { stdio: ['ignore', fd, fd] })
    fs.closeSync(fd)
    const output = fs.readFileSync(log, 'utf8')
    fs.rmSync(log, { force: true })
    if (run.error) {
        return { code: null, output: `${output}\n${run.error.message}` }
    }
    return { code: run.status, output }
}

function report(name, ok, detail) {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail === undefined ? '' : `: ${detail}`}`)
    if (!ok) {
        fail(name, detail)
    }
}

// ---- the real tree ---------------------------------------------------------------------------
//
// `--strict` on purpose: the default reading prints `stale=<n>` and stays green, so a reading taken
// without the flag would go green on a list that is mostly dead rows. This is the reading the plan
// pins, and it is the one that says the account and the fact are the same length.

const real = runGate(['--strict'])
const realOk = real.code === 0 && /0 new/.test(real.output) && /stale=0\b/.test(real.output)
report(
    'the real tree has no difference the list does not hold, and no row without a difference',
    realOk,
    real.code === 0
        ? /stale=0\b/.test(real.output)
            ? 'exit 0, 0 new, stale=0'
            : `exit 0, 0 new, but the count is not zero: ${real.output.trim().split('\n').slice(-1)}`
        : `exit ${real.code}: ${real.output.trim().split('\n').slice(-3).join(' | ')}`
)

// ---- a copy of the rulesets with one law nothing answers --------------------------------------
//
// The copy holds the two rulesets and links the engine's own sources, so the comparison is the
// gate's real one: the same scanner, the same key derivation, the same engine table.

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'vjp-parity-copy-'))

function link(name) {
    const target = path.join(ROOT, name)
    const link_ = path.join(scratch, name)
    try {
        fs.symlinkSync(target, link_, process.platform === 'win32' ? 'junction' : 'dir')
    } catch {
        fs.cpSync(target, link_, { recursive: true })
    }
}

function buildCopy() {
    fs.mkdirSync(path.join(scratch, 'stdlib', 'rulesets'), { recursive: true })
    for (const name of ['transpose.cml', 'jvp.cml']) {
        fs.copyFileSync(
            path.join(ROOT, 'stdlib', 'rulesets', name),
            path.join(scratch, 'stdlib', 'rulesets', name)
        )
    }
    for (const name of ['src', 'modules']) {
        link(name)
    }
    // One law, filed under a name neither table has: the shape of `pick_at` on the day the library
    // answered it and the engine's table had no row.
    const file = path.join(scratch, 'stdlib', 'rulesets', 'transpose.cml')
    fs.appendFileSync(
        file,
        `\n// written by test/tools/check-vjp-parity.mjs: a law nothing answers.\n` +
            `export rule ${FAKE}_t::<T, X, A>(t: X, at: A) when is_float::<T>() {\n` +
            `    ${FAKE}(t, at) => (ct: T) => spread_over(ct, at)\n` +
            `}\n`
    )
}

buildCopy()

const unlisted = runGate(['--root', scratch, '--exempt', EXEMPTIONS])
report(
    'a law with no engine rule is reported',
    unlisted.code === 1 && unlisted.output.includes(FAKE),
    unlisted.code === 1
        ? `exit 1 and the report names '${FAKE}'`
        : `exit ${unlisted.code} and the report ${unlisted.output.includes(FAKE) ? 'names' : 'does not name'} '${FAKE}'`
)

// ---- the same law, written down -----------------------------------------------------------------
//
// The row the list would need, appended to a copy of it: the gate must go quiet about the operator
// and stay quiet about everything else. The copy is of the list, not of the tree, so the reading is
// "the list turns this report off" and not "the list turns the gate off".

const exemptionCopy = path.join(scratch, 'exemptions.tsv')
fs.writeFileSync(
    exemptionCopy,
    `${fs.readFileSync(EXEMPTIONS, 'utf8')}\n` +
        `${FAKE}\tcheck-vjp-parity\ta law this checker wrote, to be caught by the run above\n`
)

const listed = runGate(['--root', scratch, '--exempt', exemptionCopy])
report(
    'the same law is not reported once the list holds it',
    listed.code === 0 && !listed.output.includes(FAKE),
    listed.code === 0
        ? 'exit 0 and the report does not name it'
        : `exit ${listed.code}: ${listed.output.trim().split('\n').slice(-3).join(' | ')}`
)

if (!KEEP) {
    fs.rmSync(scratch, { recursive: true, force: true })
} else {
    console.log(`note the copy is left at ${scratch}`)
}

if (failures.length > 0) {
    console.log(`\nFAIL ${failures.length} of 3 reading(s) of the parity gate`)
    process.exit(1)
}
console.log('\nPASS the parity gate answers the three readings it exists for')
process.exit(0)