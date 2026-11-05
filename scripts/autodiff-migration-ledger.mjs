#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The autodiff migration ledger, as a command instead of a morning's arithmetic.
//
// The question the ledger answers is narrow: **how many of the old autodiff cases can run on the
// `vjp` library today, and what stops the rest?** The page that holds the answer
// (docs/handover/m4-autodiff-migration-table.cn.md) is written by hand, and every time it was
// recounted by hand the count came out wrong -- once by 1 (a probe rewrote a whole import line and
// took the helper names with it), once with a blind spot (measuring every case by exit 0, when 30 of
// them guard an exit 1 on purpose). So the counting is here, and the page reads the number.
//
//   node scripts/autodiff-migration-ledger.mjs              the table and the one-line summary
//   node scripts/autodiff-migration-ledger.mjs --quiet       only the `[LEDGER]` line
//   node scripts/autodiff-migration-ledger.mjs --case NAME   one case, with its diagnostic
//
// **It reads, it does not write.** The page is a human's; this prints the numbers the human then
// writes down. Nothing here edits a case, a plan or a document.
//
// How a case is measured -- three steps, each of which is where the hand count went wrong before:
//
// 1. The case is copied to a probe beside itself and its `import { ... } from autodiff` is **split in
//    two**: the transform names the library owns (`vjp`, `grad`, `with_grad`) come from `vjp`, and
//    every other name stays on `autodiff`, which still has them (`add_tangent`, `zero_tangent`, ...).
//    Rewriting the whole line to `from vjp` is what turned `transform_rules` into a false failure.
// 2. The case is run with the pass its plan runs it with, and **passes when the exit code is the one
//    its own plan expects** -- not when it exits 0. `with_grad_old_name` guards that `value_and_grad`
//    does not exist: its expectation is exit 1, so exit 0 would be the failure. The plan that is
//    read is the one that actually declares the case, wherever under `test/plans` it lives -- see
//    "where the expectation comes from", below.
// 3. The three non-`pass` columns are counted **from the case sources and the rule files**, never
//    from the wording of a diagnostic. `array` is a case whose source spells an array or tensor
//    type (`[]`, `[...]`, `f32`, `f16`, `bf16`, `Model`, `Plain`); `with` is one whose source spells
//    a function type carrying a `with` parameter (`<x: float> () => float`); `other` is the rest.
//    **A comment is not a spelling**: both comment forms are stripped before either pattern is looked
//    for, so an explanation that mentions a type no longer reclassifies the case (2026-11-05).
//    The rule files are counted the same way and printed with `file:line` anchors beside the table.
//
//    **What this contract is, and what it is not.** The columns classify all 118 library cases, so
//    they sum to 118 whatever the run does; `pass` is a separate runtime column and the three do not
//    add up with it. That is deliberate: a structure count and a run count answer different
//    questions, and mixing them is how the previous version read 0/0/0.
//
//    Until 2026-11-02 the split was made from **the text of the first diagnostic** -- `[]`/`f32` or
//    `<x: float>` appearing in the message. When the front-end transform table and its call-site
//    refusal were withdrawn, the refusal point moved, the wording stopped matching, and all four
//    columns collapsed into `other` (`pass=0 with=0 array=0 other=118`). A ruler that reads wording
//    can only measure wording; that classifier is deleted, not tuned.
//
//    **`pass` depends on the binary it runs.** It is measured by running `out/latest/bin/camel`, so
//    the same tree reads differently under a different build of that file (the same tree read
//    `pass=6` against the 2026-11-01 13:44 binary and `pass=0` where no binary was present). That
//    column is not comparable across machines; the three source columns are, because they never run
//    anything.
//
//    **`array` is no longer this column's business.** What the array step needs is a program that
//    differentiates: `grad(f)([3.0, 2.0])` printing `[2.0, 3.0]`, and `skip=body` gone. Read those
//    two, not the `array` column; the column only counts how many cases are *written* with an array
//    type in them, which does not move when the work lands.
//
// Where the expectation comes from (2026-11-04). It is the `[[tests]]` table that names the case, and
// those tables are not all under `test/plans/feat/modules`: 114 of the 132 cases are declared there,
// 14 elsewhere (`feat/linear/var_reference` 8, `feat/compile/rules_angle_args` 4, `feat/mm/gc` 1,
// `feat/mm/process_state` 1), and 4 by no plan at all. Reading one directory was a ruler measuring
// the wrong table for the 14: see the record below for what it cost and how the count moved when the
// scan was widened to the whole plan tree.
//
// The plan files are read with a small bracket scanner, not a TOML library: the repository has no
// dependency for that, and the five keys this needs (`case`, `args`, `expect_exit`, and the test's
// name to prefer the `nvm` run) sit in flat `[[tests]]` tables.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const CASE_DIR = path.join(ROOT, 'test', 'cases', 'modules', 'autodiff')
// Where a case's expectation is read from. **Every** plan under `test/plans`, not one directory:
// the migration cases' home is `feat/modules`, but 14 of the 132 are declared somewhere else
// entirely (`feat/linear`, `feat/mm`, `feat/compile`), and a hard-coded list of directories cannot
// stay complete -- it did not, and the cost was 5 cases measured against an expectation nobody wrote
// (see the record below).
const PLAN_ROOT = path.join(ROOT, 'test', 'plans')
// The one directory whose plans are read first, so the cases it already covers keep the exact test
// they had before this scan widened: the test to use is "the first one seen, unless a later one is
// named `*.nvm`", and reordering the scan would silently repick tests.
const HOME_PLAN_DIR = path.join(PLAN_ROOT, 'feat', 'modules')
const LIBRARY_NAMES = new Set(['vjp', 'grad', 'with_grad'])
const OFF_PATH_DIR = 'jax_names'

const args = process.argv.slice(2)
const quiet = args.includes('--quiet')
const ledgerCheck = args.includes('--ledger-check')
const onlyCase = args.includes('--case') ? args[args.indexOf('--case') + 1] : undefined
let loaderFailure = ''

// The ledger's own record, kept the way `diagnostics-catalog.mjs` keeps its name coverage: a constant
// in the file, refused to move the wrong way. Two kinds of number live here, so the check reads them
// in two directions --
//
//   `pass` may only go **up**: a case that starts doing what its plan expects is progress, and one
//   that stops is a regression however the other lines move. It is a run of `out/latest/bin/camel`
//   and therefore moves with that binary (see the header): it is not comparable across machines.
//   `array`, `with` and `other` may only go **down**: they count, from the sources, how many cases
//   are still written in a shape the library does not take. `total` and `library` are the
//   denominator and may not move at all: a case appearing or leaving changes what the ledger means,
//   which is a decision for a person, not a side effect.
//
// **What `pass` counts, and what it does not.** It counts cases that do what their own plan asks --
// and some of them guard a refusal the library is supposed to make (`err_vjp_annotation`, the
// `grad_function_value_*` family, `with_grad_old_name`). So this records "how many cases are green on
// the library", **not** "how many cases can be differentiated".
//
// The record was re-read on 2026-11-02, after the withdrawal of the front-end transform table and its
// call-site refusal. The three source columns were recounted from the sources rather than from
// diagnostic wording (54/46/6 became 44/67/7 -- a different question asked of the same 118). `pass`
// did **not** move: it is 12 before and after the withdrawal, and the first reading of it here said 6
// for a reason that belongs in this file: the shell that ran the ledger had no `LD_LIBRARY_PATH`, so
// the compiler could not load `libstdc++`, every run exited 1 with no diagnostic, and six cases read as
// failing that pass when the binary can start. That is why the run now refuses to report a number when
// the binary does not start (see `loaderFailure` below): a ruler that cannot see must say so, not print
// a smaller number.
// Re-read on 2026-11-03, after the transpose lanes added three library cases. `total`, `library` and
// `array` moved by the same 3, which is the shape that says the corpus grew and nothing was
// reclassified: the three new programs are all `vjp`/`grad` cases written with array types.
//
//   * `test/cases/compile/transforms/transpose_array_product.cml` and
//     `.../jvp_vjp_array_product_cross.cml` (`fd964b750`, the array product's transpose needing the
//     array sum's law), and
//   * `test/cases/compile/transforms/transpose_product_fixed_shape.cml` (`910f49400`, the shape
//     clause held from both sides).
//
// `array` is a ratchet and it went **up**, which the ratchet exists to notice: it is recorded here
// rather than left to glare because the denominators are decisions, not side effects, and a
// denominator that cannot follow its corpus makes every later reading meaningless. What it must not
// do is hide a case that *stopped* being written with an array -- none did: `with` and `other` are
// unchanged at 45 and 7, so the 3 are additions and not a shift between classes.
//
// `pass` stays at 12 although the run read 14: it is the one column that follows the host binary and
// the file says it is not comparable across machines. Recording 14 here would fail the ledger on a
// host where the same three cases do not pass, which is a red about the host and not about the
// migration.
//
// The record moved a second time on `integ2/rules-batch2`, the batch of rules lanes merged onto
// `c4391db57`: `total` 131 -> 140, `library` 122 -> 131, `with` 45 -> 50, `array` 70 -> 72,
// `other` 7 -> 9. Eight of those nine are cases the batch added under `test/cases/modules/autodiff/`
// -- seven `has_aux_*` (five `with`, two `other`) and `reshape_adjoint_shape` (`array`) -- and the
// ninth was behind the record before the batch reached it: the same run on `545b24d92` reads
// `total=132 library=123 with=45 array=71 other=7`, one `array` case more than this file recorded,
// and no lane in the batch edited that case. So one of the nine is a debt being paid and not a case
// arriving, and `with`/`array`/`other` moving up here is the corpus growing, which the file has to
// follow or every later reading of it means nothing.
//
//
// **2026-11-04: the expectation was read from the wrong table, and `pass` moves because of that.**
// This is not a recount and no case, plan, rule or binary changed. The ledger used to read a case's
// expectation from `test/plans/feat/modules/*.plan.toml` and nowhere else, so 14 of the 132 cases
// were measured against a default `expect_exit = 0` that nobody had written:
//
//   * 5 of the 14 pin `expect_exit = 1` -- they guard a refusal, they exit 1 on purpose, and the
//     ledger booked them as failing *for exiting 1*. Their own plans live elsewhere:
//     `transpose_needs_parameter_shape` in `feat/compile/rules_angle_args.plan.toml` (it is the
//     negative of `rules_angle_args_mean_transpose`, and the 2026-11-04 entry above is the case it
//     guards), and `var_tensor_local`, `var_tensor_read_after_call`, `var_tensor_read_after_write`,
//     `var_tensor_read_in_callee` in `feat/linear/var_reference.plan.toml`;
//   * the other 9 pin exit 0, so the default happened to match -- the number was right and the reason
//     was wrong, which is the same defect seen from its quiet side.
//
// Measured on one binary (`out/latest/bin/camel`, the 2026-11-03 build, host `88zjwei`), running the
// before and the after of this file over the same tree: `pass=14` before, `pass=19` after, and the
// per-case table differs on exactly 9 rows -- the 5 above turn from "not passing" to "passing"
// because the expectation compared against is now the one their plan writes, and 4
// (`probe_library_grad`, `vjp_lib`, `vjp_user_a`, `vjp_user_b`) stop being compared against an
// invented zero and print `want = none` instead. No case's exit code moved: what moved is what it was
// compared to.
//
// `probe_library_grad` is the one that was worth finding: it has a `func main`, no plan names it
// anywhere, and it used to be counted as passing on an exit code nobody had asked for. The other
// three have no `main` -- `vjp_cross_module` imports them and they are its modules -- so they were
// never judged alone. A case nobody declares an expectation for is now printed with `want = none`
// and is **not** counted as passing; a ledger that guesses an expectation to keep its count up is
// the defect this whole column exists to avoid.
//
// No case carries its own expectation, so the plan is the only place it is written down: `expect_exit`,
// `expect_stdout`, `@expect` and `vitest` appear 0 times across `test/cases/modules/autodiff`, and the
// strings `expect_output_contains` names are read from the same `[[tests]]` table and are part of the
// match (a case that exits 1 for some other reason does not pass).
//
// The fix is in `PLAN_ROOT`/`expectations`, not in these numbers: the scan is every `*.plan.toml`
// under `test/plans`, home directory first, so the cases it already covered keep the exact test they
// had (measured: 0 of them repicked) and a case declared by any plan is found (measured: 128 `.cml`
// cases named by a plan, 128 found; two more entries name a `.py` twin with `tool = "python"`, and
// those are keys with no row). There is no directory list left for a case to fall out of.
//
// The record carries 17 rather than the 19 this host reads, by the same rule the 12 was carried
// under: `pass` follows the host binary, and the two readings above the old floor of 12 were the
// host's. 17 = 12 + the 5 this fix turns over, and those 5 are the ruler's, so they are required
// everywhere; the 2 that ride on the binary are not.
// **2026-11-04, second move: three cases arrived and they are all `with`.** `total` 141 -> 144,
// `library` 132 -> 135, `with` 50 -> 53; `array` and `other` do not move. The three are
// `second_order_nest.cml`, `second_order_refused_captured.cml` and `second_order_refused_vjp.cml`
// (`test/plans/feat/modules/autodiff.plan.toml`), added by the second-derivative lane: they ask what a
// nested `jvp`/`vjp` can and cannot be handed, and each is written with a `with` parameter, which is
// the class they land in. `pass` does not move: the record holds 23 and the host reads 23.
// **2026-11-05, one case arrived and it is `with`. ** `total` 143 -> 144, `library` 134 -> 135,
// `with` 53 -> 54; `array` and `other` do not move. The one is
// `test/cases/modules/autodiff/has_aux_library_composition.cml`: it names `vjp` (the library's
// `has_aux` it is about) and `with_grad` (the engine leg it is compared against), which is `library`,
// and it is written with `with` parameters for that comparison -- `with <w: float> func loss_engine()`
// is the engine's shape, and comparing the two legs is the whole point of the case, so the `with`
// class rising here is arithmetic and not a case falling back. `pass` is a floor and does not move.
// **2026-11-05, no case arrived and four changed class: a comment is not a spelling.** Both comment
// forms are now stripped before the two patterns are looked for, and four cases were being counted by
// the words of their explanation rather than by what they are written as: `array` 72 -> 69,
// `with` 54 -> 55, `other` 9 -> 11. The three that leave `array` spell their array only in a doc
// comment -- `jax_vmap.cml` (`T[]`), `loop_grad_iterable_length.cml` (`float[]`) and
// `loop_grad_refuse_no_element_tangent.cml` (`string[]`) -- and the last two are `with` cases in code
// (`with <w: float>`), which is where they land; `jax_vmap` lands in `other`, which is where the other
// JAX cases are. The fourth, `has_aux_vjp_named.cml`, was `with` on the strength of a `<...>` in the
// signature its comment quotes, and is `other` in code. **This is a measurement correction, not debt
// moving**: the ratchet's "only down" is about a case being rewritten to a shape the library takes, and
// none of these four was rewritten -- the ruler was reading the wrong sentence, so the record is
// restated here (the same rule the denominators are restated under) rather than left to drift.
// `total`, `library` and `pass` do not move.
//
// **Why the four are a correction and not a moved target: the spelling is not in their code.** One
// reading each -- the line that carried the spelling, and what the file says once both comment forms are
// cut out. A case whose spelling *is* in its code would make this paragraph a disguised retune, so the
// reading is written down where a reviewer can repeat it:
//
//   - `jax_vmap.cml`: the comment says "the leading axis of a `T[]` is its elements"; the code part has
//     no `[]`, no `[...]` and no `f32`/`f16`/`bf16`/`Model`/`Plain`, and no `with` spelling either.
//   - `loop_grad_iterable_length.cml`: the comment says "only `float[]` is"; the code part has no array
//     spelling, and has `with <w: float>` -- which is why the case is `with` and not `array`.
//   - `loop_grad_refuse_no_element_tangent.cml`: the comment says "`string[]` has none"; the code part
//     has no array spelling, and has `with <w: float>`, so it is `with`.
//   - `has_aux_vjp_named.cml`: the comment quotes the instantiated signature
//     `(f: F) => <__pack_0: ...WithOf<F>> (...)`; the code part has neither the `<...>` the class was
//     read from nor an array spelling, so the case is `other`.
//
// So the old reading counted a comment: `array` was overstated by 3, `with` by 1, and `other` understated
// by 2 -- which is the whole of the movement above.
const LEDGER_RECORDED = {
    total: 144,
    library: 135,
    pass: 22,
    with: 55,
    array: 69,
    other: 11,
}

function camelBinary() {
    const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
    const exe = process.platform === 'win32' ? 'camel.exe' : 'camel'
    return path.join(home, 'bin', exe)
}

// --- reading the expectations out of the plans ---------------------------------------------------
//
// Each `[[tests]]` table is a flat run of `key = value` lines; `args` is the only list. A test's pass
// is the first word of `args` that is not the case itself, with `${run_nvm}` and friends spelled out.
const PASS_WORDS = {
    '${run_nvm}': 'std::nvm',
    '${run_fvm}': 'std::fvm',
    '${run_jit}': 'std::jit',
    '${run_prl}': 'std::prl',
    '${run_macro}': 'std::macro',
}

function readTests(file) {
    const text = fs.readFileSync(file, 'utf8')
    const tests = []
    for (const block of text.split(/^\[\[tests\]\]$/m).slice(1)) {
        const test = { name: '', case: '', args: [], expectExit: 0, contains: [] }
        const name = /^name = "([^"]*)"/m.exec(block)
        if (name) test.name = name[1]
        const kase = /^case = "([^"]*)"/m.exec(block)
        if (!kase) continue
        test.case = kase[1]
        const run = /^args = \[(.*)\]/m.exec(block)
        if (run) {
            test.args = [...run[1].matchAll(/"([^"]*)"/g)].map((m) => m[1].replace('${case}', test.case))
        }
        const exit = /^expect_exit = (-?\d+)/m.exec(block)
        if (exit) test.expectExit = Number(exit[1])
        const contains = /^expect_output_contains = \[(.*)\]/m.exec(block)
        test.contains = contains ? [...contains[1].matchAll(/"([^"]*)"/g)].map((m) => m[1]) : []
        tests.push(test)
    }
    return tests
}

// Every case file the plan tree names, mapped to the test that describes it, together with the plan
// file it came from (the provenance is printed for the cases whose expectation is not in the home
// directory, so a reader can check that the reading is the case's own). A case with several tests
// (one per backend) is represented by its `nvm` test when it has one -- that is the run this ledger
// makes; when it has none, by its first test, so a case whose only reading is a `macro` run is still
// measured the way its plan measures it.
const planFiles = []
function collectPlans(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) collectPlans(full)
        else if (entry.name.endsWith('.plan.toml')) planFiles.push(full)
    }
}
collectPlans(PLAN_ROOT)
const homePlans = planFiles.filter((file) => path.dirname(file) === HOME_PLAN_DIR)
const otherPlans = planFiles
    .filter((file) => path.dirname(file) !== HOME_PLAN_DIR)
    .sort()
// Read the home directory first, in readdir order (this keeps every pick it already made), then the
// rest by path. The scan is the whole tree, so a case declared by a plan anywhere is found; there is
// no directory list left for a new case to fall out of.
const orderedPlans = [...homePlans, ...otherPlans]

const expectations = new Map()
for (const file of orderedPlans) {
    for (const test of readTests(file)) {
        const target = path.resolve(path.dirname(file), test.case)
        const seen = expectations.get(target)
        if (!seen) expectations.set(target, { test, plan: file })
        else if (test.name.endsWith('.nvm') && !seen.test.name.endsWith('.nvm'))
            expectations.set(target, { test, plan: file })
    }
}

function passOf(test) {
    for (const word of test.args.slice(1)) {
        if (PASS_WORDS[word]) return PASS_WORDS[word]
        if (word.startsWith('std::')) return word
    }
    return 'std::nvm'
}

// --- the probe --------------------------------------------------------------------------------
//
// The two-line import is the whole rewrite; a case that names none of the library's transforms (the
// six that only use helpers, the JAX ones) is run untouched, because there is nothing to migrate.
function rewriteImports(source) {
    const line = /^import\s*\{([^}]*)\}\s*from\s*autodiff\s*$/m.exec(source)
    if (!line) return { source, migrated: false }
    const names = line[1]
        .split(',')
        .map((n) => n.trim())
        .filter(Boolean)
    const mine = names.filter((n) => LIBRARY_NAMES.has(n))
    const theirs = names.filter((n) => !LIBRARY_NAMES.has(n))
    const replacement = []
    if (mine.length) replacement.push(`import { ${mine.join(', ')} } from vjp`)
    if (theirs.length) replacement.push(`import { ${theirs.join(', ')} } from autodiff`)
    return { source: source.replace(line[0], replacement.join('\n')), migrated: mine.length > 0 }
}

function runCase(file, test) {
    const source = fs.readFileSync(file, 'utf8')
    const { source: rewritten } = rewriteImports(source)
    const probe = file.replace(/\.cml$/, '.ledger-probe.cml')
    fs.writeFileSync(probe, rewritten, 'utf8')
    if (!fs.existsSync(camelBinary())) {
        loaderFailure = `no binary at ${camelBinary()}`;
    }
    try {
        const result = spawnSync(camelBinary(), [probe, passOf(test)], {
            cwd: ROOT,
            encoding: 'utf8',
            timeout: 120000,
            env: { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest') },
        })
        const text = `${result.stdout || ''}${result.stderr || ''}`
        const first = text.split('\n').find((l) => l.includes(': [Error]: ')) || ''
        // A binary that cannot start is not a case that failed. On this host the compiler needs the
        // newer `libstdc++` on `LD_LIBRARY_PATH`; without it every run exits 1 with a loader message
        // and no diagnostic, and the ledger would report a pass count that measures the shell, not
        // the tree. One such run marks the whole measure unusable.
        if (/error while loading shared libraries|GLIBCXX_|version `GLIBC/.test(text)) {
            loaderFailure = text.split('\n').find((l) => /error while loading shared libraries|GLIBCXX_/.test(l)) || text.split('\n')[0]
        }
        return { exit: result.status === null ? -1 : result.status, first: first.trim(), text }
    } finally {
        fs.rmSync(probe, { force: true })
    }
}

// --- the classes ------------------------------------------------------------------------------
//
// Counted from the source, not from a diagnostic: see the header. `array` is an array or tensor type
// spelled anywhere in the case; `with` is a function type carrying a `with` parameter; `other` is
// what is left. The same two spellings are counted in the rule files, where the hits are printed with
// `file:line` anchors so a reader can go and look.
const ARRAY_SPELLING = /\[\]|\[\.\.\.\]|\bf32\b|\bf16\b|\bbf16\b|\bModel\b|\bPlain\b/
const WITH_SPELLING = /<[A-Za-z_]\w*\s*:\s*[^>]+>/

// **A comment is not a spelling.** The class is read from the source text, and it used to read the
// explanation as readily as the code: `has_aux_vjp_pair.cml` said "a `float` or an `f32`" in its block
// comment, the case moved from `other` to `array`, and the ratchet -- right to notice a
// reclassification -- went red on `array=73 other=8` (`05e1145a9`). The sentence and the page both had
// to be reworded to get the reading back, which is the ruler bending the thing it measures.
//
// So both comment forms are cut out before the two spellings are looked for. The stripper follows
// string literals, because a `//` inside one is not a comment (`"http://..."`) and a type named in a
// literal is a spelling of it. Newlines are kept, so a `file:line` anchor still points at the line the
// reader should open.
function stripComments(source) {
    let out = ''
    let quote = null
    for (let i = 0; i < source.length; ) {
        const ch = source[i]
        const next = source[i + 1]
        if (quote !== null) {
            out += ch
            if (ch === '\\' && i + 1 < source.length) {
                out += next
                i += 2
                continue
            }
            if (ch === quote) quote = null
            i += 1
            continue
        }
        if (ch === '"' || ch === "'") {
            quote = ch
            out += ch
            i += 1
            continue
        }
        if (ch === '/' && next === '/') {
            while (i < source.length && source[i] !== '\n') i += 1
            continue
        }
        if (ch === '/' && next === '*') {
            i += 2
            while (i < source.length && !(source[i] === '*' && source[i + 1] === '/')) {
                if (source[i] === '\n') out += '\n'
                i += 1
            }
            i = Math.min(i + 2, source.length)
            continue
        }
        out += ch
        i += 1
    }
    return out
}

function classifySource(source) {
    const text = stripComments(source)
    if (ARRAY_SPELLING.test(text)) return 'array'
    if (WITH_SPELLING.test(text)) return 'with'
    return 'other'
}

const RULE_DIR = path.join(ROOT, 'stdlib', 'rulesets')
function ruleSpellings() {
    const hits = { array: [], with: [] }
    let files = []
    try {
        files = fs.readdirSync(RULE_DIR).filter((f) => f.endsWith('.cml')).sort()
    } catch {
        return hits
    }
    for (const name of files) {
        const lines = stripComments(fs.readFileSync(path.join(RULE_DIR, name), 'utf8')).split('\n')
        lines.forEach((line, index) => {
            const anchor = `stdlib/rulesets/${name}:${index + 1}`
            if (ARRAY_SPELLING.test(line)) hits.array.push(anchor)
            if (WITH_SPELLING.test(line)) hits.with.push(anchor)
        })
    }
    return hits
}

// --- the run ----------------------------------------------------------------------------------
const files = []
function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walk(full)
        else if (entry.name.endsWith('.cml') && !entry.name.endsWith('.probe.cml')) files.push(full)
    }
}
walk(CASE_DIR)
files.sort()

const rows = []
for (const file of files) {
    const relative = path.relative(ROOT, file).replace(/\\/g, '/')
    const entry = expectations.get(file)
    const test = entry ? entry.test : undefined
    const planFile = entry ? path.relative(ROOT, entry.plan).replace(/\\/g, '/') : null
    const offPath = path.relative(CASE_DIR, file).split(path.sep)[0] === OFF_PATH_DIR
    if (onlyCase && !relative.includes(onlyCase)) continue
    const source = fs.readFileSync(file, 'utf8')
    const migrated = rewriteImports(source).migrated
    // A case with no `func main` is a library user, run through another case that imports it. Run
    // alone it reports `EntryModuleMissingMain`, which would be this tool's own noise reported as the
    // case's failure, so it is named for what it is.
    // A case that expects exit 1 is run whether or not it has a main: rr_vjp_annotation fails
    // at the parse, so it has none, and its refusal is exactly what it guards.
    const standalone = /^func\s+main\s*\(/m.test(source) || (test ? test.expectExit !== 0 : false)
    const { exit, first, text } = standalone
        ? runCase(file, test || { args: ['${case}'], expectExit: 0, contains: [] })
        : { exit: null, first: 'no main: this case is run through another one, so it cannot be judged alone', text: '' }
    // `null`, not 0: a case no plan names has no expectation, and reading it against a zero written
    // here is how this ledger used to invent one -- `probe_library_grad` has a `main`, is named by no
    // plan, and used to pass on an exit code nobody had asked for.
    const expected = test ? test.expectExit : null
    // The exit code is the least a case can ask for. When its plan also names strings the run has to
    // print, a refusal that exits 1 for some other reason is not that case passing -- the same
    // distinction the plan itself draws.
    const missing = (test ? test.contains : []).filter((s) => !text.includes(s))
    const matched = Boolean(test) && exit !== null && exit === expected && missing.length === 0
    rows.push({
        relative,
        offPath,
        expected,
        exit,
        pass: matched,
        kind: classifySource(source),
        missing,
        migrated,
        first,
        pass_: test ? passOf(test) : 'std::nvm',
        planFile,
        planInHome: entry ? path.dirname(entry.plan) === HOME_PLAN_DIR : false,
        declared: Boolean(test),
    })
}

const library = rows.filter((r) => !r.offPath)
// Two different questions, deliberately kept apart: `pass` is what the run did, the three classes are
// what the sources are written as. The classes cover all 118 library cases, so they sum to 118
// whether or not a binary is present.
// Two different questions, deliberately kept apart: `pass` is what the run did, the three classes are
// what the sources are written as. The classes cover all 118 library cases, so they sum to 118
// whether or not a binary is present.
const countKind = (kind) => library.filter((r) => r.kind === kind).length
const passed = library.filter((r) => r.pass).length

// A run of nothing is not a run where nothing passes, and a dead binary looks exactly like a clean
// refusal -- six cases that expect exit 1 and pin no output "pass" against a compiler that never
// started (measured 2026-11-02, a shell without LD_LIBRARY_PATH). So the detector is not the count:
// if no case produced an exit code, and if any run failed to start the binary, the instrument is not
// moving, and an instrument that is not moving does not report a number. `pass` is printed as `n/a`,
// the reason is said out loud, and the process exits non-zero so a gate cannot carry the number on.
const ran = rows.filter((r) => r.exit !== null && r.exit !== -1).length
const blind = ran === 0 || Boolean(loaderFailure)
const passCell = blind ? 'n/a' : String(passed)
if (blind) {
    console.error(
        'ledger: the instrument is not running, so `pass` is printed as `n/a` rather than a number. ' +
            (loaderFailure
                ? `The binary did not start: ${loaderFailure}. `
                : `Nothing ran at all: there is no built tree at ` +
                  `${path.relative(ROOT, path.dirname(camelBinary())).replace(/\\/g, '/') || 'out/latest'}. `) +
            'A run of nothing is not a run where nothing passes, and a binary that cannot start reads ' +
            'like a clean refusal. The three classes above are unaffected: they are counted from the sources.',
    )
    process.exitCode = 1
}

const summary =
    `[LEDGER] total=${rows.length} library=${library.length} pass=${passCell} ` +
    `with=${countKind('with')} array=${countKind('array')} other=${countKind('other')}`

if (!quiet) {
    const width = Math.max(...rows.map((r) => r.relative.length), 10)
    console.log(`${'case'.padEnd(width)}  exit  want  class   first diagnostic`)
    for (const row of rows) {
        const line = [
            row.relative.padEnd(width),
            (row.exit === null ? '-' : String(row.exit)).padStart(4),
            (row.expected === null ? 'none' : String(row.expected)).padStart(5),
            row.offPath ? 'off' : row.kind.padEnd(6),
            row.first.slice(0, 110),
        ].join('  ')
        console.log(line)
    }
    const off = rows.length - library.length
    console.log('')
    // Where each expectation came from, and what has none. A widened scan is only worth trusting if
    // it says, out loud, which cases it found somewhere other than the home directory and which it
    // found nowhere -- the second kind used to be read against an invented `expect_exit = 0` and
    // counted as passing, which is a number that answers nobody's question.
    const fromElsewhere = rows.filter((r) => r.declared && !r.planInHome)
    if (fromElsewhere.length > 0) {
        console.log(
            `${fromElsewhere.length} cases have their expectation outside ${path.relative(ROOT, HOME_PLAN_DIR).replace(/\\/g, '/')}:`,
        )
        for (const row of fromElsewhere) console.log(`  ${row.relative} -> ${row.planFile} want=${row.expected}`)
    }
    const undeclared = rows.filter((r) => !r.declared)
    if (undeclared.length > 0) {
        console.log(
            `${undeclared.length} cases are named by no [[tests]] table under ` +
                `${path.relative(ROOT, PLAN_ROOT).replace(/\\/g, '/')}: they are listed with \`want = none\`, ` +
                'they are **not** counted as passing, and a case with no `func main` among them is run ' +
                'through another case rather than alone:',
        )
        for (const row of undeclared)
            console.log(`  ${row.relative}  exit=${row.exit === null ? '-' : row.exit}`)
    }
    console.log(
        `${library.length} cases name the library's transforms; ${off} more live under ` +
            `${OFF_PATH_DIR}/ or name nothing the library owns and are counted in total only.`,
    )
    // The nine under `jax_names/` are not skipped, and this says so where a reader can see the
    // count: they are walked like the rest, they print a row, and they are left out of the four
    // classes because they exercise the prelude's `vmap`, not this library's `vjp` -- counting them
    // as "not passing on the library" would be counting the wrong thing.
    const offPathRows = rows.filter((r) => r.offPath)
    if (offPathRows.length > 0) {
        console.log(
            `Of the ${off}, ${offPathRows.length} live under ${OFF_PATH_DIR}/ and are run and listed, ` +
                'but they are off this lane: they test the prelude, so they count in total only.',
        )
    }
    const noNames = library.filter((r) => !r.migrated)
    console.log(
        'A case passes when its exit code is the one its own plan expects and it prints every string ' +
            `the plan names; ${noNames.length} of the ${library.length} name none of the library's ` +
            'transforms, so they pass without it.',
    )
    const binary = camelBinary()
    let stamp = 'absent'
    try {
        stamp = fs.statSync(binary).mtime.toISOString()
    } catch {
        stamp = 'absent'
    }
    console.log(
        `pass is a run of ${path.relative(ROOT, binary).replace(/\\/g, '/')} (${stamp}): it moves with ` +
            'that binary and is not comparable across machines. The three classes above are counted ' +
            'from the sources and never run anything.',
    )
    const spells = ruleSpellings()
    if (spells.array.length > 0 || spells.with.length > 0) {
        console.log(
            `Rule files, same two spellings: array ${spells.array.length} site(s)` +
                (spells.array.length > 0 ? ` (${spells.array.slice(0, 6).join(', ')})` : '') +
                `, with ${spells.with.length} site(s)` +
                (spells.with.length > 0 ? ` (${spells.with.slice(0, 6).join(', ')})` : '') +
                '.',
        )
    }
    console.log(
        'array is not the array step\'s acceptance: read `grad(f)([3.0, 2.0])` printing `[2.0, 3.0]` ' +
            'and `skip=body` gone. This column only counts cases written with an array type.',
    )
}
console.log(summary)

if (ledgerCheck && blind) {
    console.log('')
    console.log('[LEDGER-CHECK] not run: the instrument is not running (see above), so `pass` is not ' +
        'compared against its recorded 12 -- a number from a run of nothing would send a reader hunting ' +
        'for a fall that happened because nothing started.')
    process.exit(1)
}

if (ledgerCheck) {
    const problems = []
    const measured = {
        total: rows.length,
        library: library.length,
        pass: passed,
        with: countKind('with'),
        array: countKind('array'),
        other: countKind('other'),
    }
    const move = (key, direction, wording) => {
        const recorded = LEDGER_RECORDED[key]
        const got = measured[key]
        if (direction === 'up' ? got < recorded : got > recorded) {
            problems.push(
                `${key} 记录值 ${recorded} 但实测 ${got}（${wording}）：` +
                    `改了就该在同一个提交里改下 LEDGER_RECORDED 的那个数，并说明读数为什么动了`,
            )
        }
    }
    if (measured.total !== LEDGER_RECORDED.total) {
        problems.push(
            `total 记录值 ${LEDGER_RECORDED.total} 但实测 ${measured.total}：分母是决定，不是副作用`,
        )
    }
    if (measured.library !== LEDGER_RECORDED.library) {
        problems.push(
            `library 记录值 ${LEDGER_RECORDED.library} 但实测 ${measured.library}：分母是决定，不是副作用`,
        )
    }
    move('pass', 'up', '只能升')
    move('with', 'down', '只能降')
    move('array', 'down', '只能降')
    move('other', 'down', '只能降')
    if (problems.length > 0) {
        console.log('')
        console.log('[LEDGER-CHECK] the ledger moved the wrong way:')
        for (const problem of problems) console.log(`  ${problem}`)
        process.exitCode = 1
    } else {
        console.log('')
        console.log(
            `[LEDGER-CHECK] ok: total=${measured.total} library=${measured.library} ` +
                `pass=${measured.pass} with=${measured.with} array=${measured.array} ` +
                `other=${measured.other} -- pass 没有降，三类与记录的读数一致`,
        )
    }
}
