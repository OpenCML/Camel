#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Fails when a reader-facing page teaches a pass invocation the command line refuses.
 *
 * A pass parameter is a `key=value` word after the pass name (design principles 9.3). The
 * rule that wrote that down deleted three other spellings at once: `std::opt(safe_fp=true)`,
 * the colon spelling `safe_fp: true`, and positional parameters. A page that still shows one is
 * a defect by the same reasoning: the reader copies a spelling the tool refuses with
 * `RuntimeDiag::InvalidPassParameter`.
 *
 *   node test/tools/check-doc-pass-forms.mjs
 *   node test/tools/check-doc-pass-forms.mjs --list      print every invocation with its page
 *   node test/tools/check-doc-pass-forms.mjs --selftest  plant a tree and check the walk itself
 *                                                        (no camel binary and no pages needed)
 *
 * What it checks, and where each rule comes from:
 *
 *   * A page's `camel …` command line is split the way the command line splits itself:
 *     `splitPassList` for a `--passes` list and `groupPassWords` for the words after the
 *     program (`src/execute/pass_spec.cpp`). Each specification that comes out is then read by
 *     the parser's own rule, `parseSpec` in the same file: the name may contain no `(`, and
 *     every word after the name is a `key=value` word whose key is a bare name and whose value
 *     is not empty. A command line is a fenced block line (`camel …`, optionally after a `//`
 *     comment mark), a `// camel …` line inside a fence, or an inline `` `camel …` `` span.
 *   * A word after the program that is not a pass name is reported when it is unmistakably a
 *     parameter: `safe_fp:` is the colon spelling, and a bare literal (`true`, `2`) directly
 *     after a pass name is a positional parameter. Both are refused, and both are exact shapes.
 *   * A pass name immediately followed by `(` is refused as well, anywhere in the scanned
 *     pages, run or only quoted in prose. That is how the one defect found by hand read:
 *     `stdlib/rules.cml` described `simplify_strict` with `std::opt(safe_fp=true)`.
 *   * Each distinct specification is then run through the **real parser**. The invocation is
 *     followed by a pass name no build registers, so the pipeline is resolved and refused
 *     before any pass runs: a spelling the parser rejects is `RuntimeDiag::InvalidPassParameter`,
 *     while a well-formed specification reaches the unknown name
 *     (`RuntimeDiag::UnrecognizedGraphPass`) or a parameter the declaration does not accept
 *     (`SemanticDiag::ParamArgument*`). An invocation the real parser accepts is one a reader
 *     can copy. This stage reads the value's text as the parser reads it, so it catches what no
 *     rule here mirrors: a value a constant parameter refuses (a `bool`, `int` or `float`
 *     parameter takes a constant expression; a `string` parameter takes the word as written).
 *
 * What it cannot catch, and why:
 *
 *   * A pass name the build does not declare (a stale name such as the `std::tiles` a design
 *     note still writes) is not a spelling problem: `UnrecognizedGraphPass` is also what a page
 *     shows on purpose (`docs/cli.md` uses `std::no_such_pass` as its example). Names are not
 *     checked here. A rule for them is possible and exact — a `std::` name in a page must be a
 *     declaration of `stdlib/std.cml` or an alias of the core — but it needs the deliberate
 *     unknown-name examples excepted, so it is not part of this guard.
 *   * The colon and positional spellings are reported on a `camel` command line, and the paren
 *     spelling anywhere; a page that teaches the colon spelling in prose alone is not caught.
 *      `safe_fp: true` is also how Camel source writes a named argument, which the rule kept,
 *     so away from a command line the two cannot be told apart.
 *   * A positional value that is not a bare literal (`std::opt model.onnx`) is not reported: a
 *     stray word cannot be told apart from a placeholder (`<file>`) or a redirection
 *     (`2>/dev/null`). The command line still refuses it, as an unknown pass name.
 *   * A parameter name or type the declaration does not accept is the other half of the same
 *     machinery (`SemanticDiag::ParamArgument*`), not a spelling: the real parser reports it and
 *     the run accepts it, because checking the name against every `module::pass` declaration
 *     needs the module search paths, which this guard does not read.
 *   * A specification written in Camel source (`f->std::opt<safe_fp: true>`) is the source
 *      spelling of a named argument, which the rule kept; only the command line changed.
 *   * `changes.log` and the record directories are history: an entry states the release it
 *     belongs to, and `docs/handover/` quotes the old spelling to say what happened. They are not
 *     scanned, and every entry of that list is asserted to still be in the tree, so a class cannot
 *     keep excusing pages it no longer describes.
 *
 * A line the guard would report is excused when it says why it is not a defect: the removed form
 * is written there in order to be called removed. The exceptions are listed in EXCUSED with their
 * words, and each is checked to still match a line, so none can outlive what it was written for.
 * An exception for a decision and the parser disagreeing is only needed while they disagree: the
 * two that excused design principles 9.3 (`out=model.onnx` as a string value written as it is)
 * were deleted when the parser took a `string` parameter as written, and that was found by the
 * check that every exception still matches a line, not by reading the pages.
 * A test plan's `args` and `envs` are pins, not pages: a refusal test must contain the spelling it
 * pins (`error.positional` in `test/plans/feat/opt/pass_params.plan.toml`), and a test that has a
 * guard plant a page must name the spelling in the environment it passes. Their lines, and the
 * executable fixtures `verify_script` / `verify_script_path` / `verify_js`, are not read for the
 * removed form; every other line of a plan, comments included, is.
 *
 * A test plan's `desc` is not a page of the documentation: it is the plan's own label, read in a
 * plan listing and not by a reader looking for a command to copy. What a plan does teach is the
 * rest of the file — the comments that say what it pins and the `args` it pins — and that is read
 * as a page. A `camel …` command quoted inside a `desc` is therefore observed and not counted:
 * the guard reports how many of them the labels showed, so the definition cannot go quietly
 * blind, and an invocation there moves no population and no pin. A plan's description that
 * quoted a command was what moved the count by one invocation with nothing in the reader's world
 * changing; the number is a measurement of the pages, and a label is not one.
 *
 * A check of nothing is refused (exit 2), and the property a green run states is that every
 * distinct specification was read by the parser and accepted by it: the guard fails when the number
 * that reached the probe is not the number of distinct specifications, so a batch whose answer
 * never arrived is red. The numbers — pages walked, invocations found, distinct specifications —
 * are printed as a measurement of the pages; `test/plans/feat/opt/pass_params.plan.toml` checks
 * the shape of that line and the property behind it, and a page added or removed moves no
 * assertion.
 *
 * What it costs, and what it does not read:
 *
 *   * The walk skips the trees a run writes into, and asks the one definition of which trees
 *     those are: `isRunnerOutput` (`test/tools/runner-output.mjs`), the module that sits beside
 *     the runner's retention bound and answers the same question for `check-no-algo.mjs`,
 *     `scripts/check-doc-links.mjs`, `scripts/headers.mjs --root` and `scripts/clean.js`. A guard
 *     that hard-codes `test/results` is one more place the next tree has to be remembered.
 *   * What a run directory holds is the run's own record and nothing of the checkout: `run.json`
 *     at the run root, and per test its captured output and a `meta.json` naming `plan_path` and
 *     `case_path`. The plan and the case used to be copied into it, one copy of the same plan
 *     file per test, and that copy was 87% of a run directory's bytes; the runner stopped writing
 *     it, so no `.md` and no plan of the checkout is there to be skipped. No page of the
 *     documentation can be lost by the skip, and that is asserted rather than argued: the walk
 *     yields every tracked `*.md` and every tracked plan file (checked against `git ls-files`),
 *     and the `--selftest` recipe plants one page inside the runner's tree and the same page
 *     outside it and requires the walk to count only the second. A tree that is not the runner's
 *     (`tmp` is the repository's gitignored scratch root; the runner's own scratch is `tmp/test`,
 *     one `run-<pid>` per run) is skipped by the guard's own deliberate choice, and the recipe
 *     asserts that difference too, so a narrowed skip is red here rather than silent.
 *   * Skipping them is also the whole cost of this guard on a checkout that has run the gate many
 *     times: the integration checkout held 6.8 M entries and 51 GB under those two trees, and a
 *     bare walk of it measured 41.4 s against 0.42 s in a checkout that had run the gate once
 *     (the runner's own accounting, `docs/test-plans.md`, "Runner Output"), while the parser work
 *     is a fraction of a second. That put the guard past the runner's margin — a passing test is
 *     red above a quarter of its `timeout_ms` (`TIMEOUT_MARGIN` in `test/tools/run-tests.mjs`) —
 *     so it went red on a busy host while every check of it passed, which is a red that says
 *     nothing about the pages. (That checkout has since been pruned to 6.65 GB and 0.86 M entries,
 *     where the guard as it was costs 10.4 s and passes: the pruning hides the cost, the skip
 *     removes it.)
 *     `tmp` is skipped as a whole rather than as `tmp/test` alone, which is the narrower tree the
 *     runner owns: both are gitignored, `tmp` holds no tracked file at all (asserted), and a page
 *     written into either one is a run's scratch, not a page of the tree.
 *   * One parser invocation decides several specifications: `requireKnownPasses`
 *     (`src/execute/pass.cpp`) resolves every name of a pipeline, and refuses the pipeline,
 *     before the first pass runs, so a pipeline of specifications followed by the probe reads
 *     all of them and runs nothing. The pipeline is refused at the first name it cannot resolve,
 *     so a batch is taken as proof of all its members only when the complaint names the probe;
 *     anything else — an earlier member this build does not register, a parameter a declaration
 *     does not accept — decides none of them, and that batch is then decided specification by
 *     specification, exactly as every specification was decided before. Narrowing an aggregate is
 *     not retrying a check: a refusal is reported with its own words, and the number of
 *     specifications the parser read is unchanged. (Measured: one invocation carrying all 38
 *     specifications of that checkout is refused at `mod::pass`, and the specification the parser
 *     refuses is reported by its own run exactly as it is with one specification per invocation.)
 *   * The first line of a red is the reason: the page, the line, the invocation, and what the
 *     parser said (or that the invocation did not come back within the documented limit). The
 *     runner echoes only the last 12 lines of a failed test and clips each at 240 characters, so
 *     the report is a few short lines, and `CAMEL_DOC_FORM_REPORT_ALL=1` prints every problem
 *     rather than the first three. Those lines are echoed when `CAMEL_TEST_SHOW_OUTPUT` or `CI`
 *     is set, which `tools/camel-remote.ps1` always does; without it the runner names the
 *     scratch directory it kept, whose `stdout.txt` holds the same lines.
 *
 *   --camel PATH               the binary to run the specifications through: `--camel <path>`
 *                              names it, else this tree's `out/latest/bin/camel`
 *   CAMEL_DOC_FORM_JOBS        parser invocations in parallel (default 8)
 *   CAMEL_DOC_FORM_BATCH       specifications decided by one invocation (default 8)
 *   CAMEL_DOC_FORM_TIMEOUT_MS  the documented limit for one invocation (default 30000); a
 *                              specification is decided in tens of milliseconds, so only a wedged
 *                              parser or a broken install reaches it
 *   CAMEL_DOC_FORM_REPORT_ALL  1 prints every problem instead of the first three
 *   CAMEL_DOC_FORM_PLANT_PAGE  a `camel …` invocation to plant as a page in this process before
 *                              the walk, so a check can require this guard to go red on a page it
 *                              wrote (the page is removed however the run ends); a check has to ask
 *                              for it here because a verification script cannot read back a guard
 *                              it starts itself
 */

import crypto from 'node:crypto'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { isRunnerOutput } from './runner-output.mjs'
import { takeCamel } from './camel-arg.mjs'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')
const JOBS = Math.max(1, Number(process.env.CAMEL_DOC_FORM_JOBS || 8))
const BATCH = Math.max(1, Number(process.env.CAMEL_DOC_FORM_BATCH || 8))
const TIMEOUT_MS = Number(process.env.CAMEL_DOC_FORM_TIMEOUT_MS || 30000)
const REPORT_ALL = Boolean(process.env.CAMEL_DOC_FORM_REPORT_ALL)
const STARTED_AT = Date.now()

function usage(message) {
    if (message) console.error(`check-doc-pass-forms: ${message}`)
    console.error('usage: node test/tools/check-doc-pass-forms.mjs [--list] [--selftest] [--camel PATH]')
    process.exit(2)
}

// The compiler every specification is run through: `--camel <path>` names it, else this tree's
// `out/latest/bin/camel`. One rule, in one place: test/tools/camel-arg.mjs.
const { camel: CAMEL, rest: ARGV } = takeCamel(process.argv.slice(2), REPO_ROOT, usage)
const LIST = ARGV.includes('--list')

// A name no build registers: the pipeline is refused on it before any pass runs, so the
// specifications before it are parsed and never executed.
const PROBE = 'std::doc_form_probe'
// The one text that proves an invocation was refused *on the probe*, and therefore that every
// specification written before it resolved: the pass listing an unknown name prints.
const PROBE_REFUSED = new RegExp(`Pass <${PROBE}> not found`)

// Not pages: a release log states the release it belongs to, and the handover directory quotes the
// old spelling to say what happened. The list is asserted, not assumed: an entry that is not in
// the tree any more is reported, so a class cannot keep excusing pages it no longer describes.
// (`docs/reviews/` was a third entry until the directory was removed with the ledgers it held.)
const NOT_A_PAGE = ['changes.log', 'docs/handover/']
// Written by a script, never by hand: their text comes from the pages and the sources scanned
// here, so a hit in them is fixed where it is written.
const GENERATED = ['docs/diagnostics.md', 'docs/stdlib-reference.md']
const SKIP_DIRS = new Set(['.git', 'out', 'build', 'node_modules', 'third_party', 'sdks'])

// The trees a run writes into are the runner's own question, and it answers it in one place:
// `isRunnerOutput` (`test/tools/runner-output.mjs`), beside the retention bound that prunes the
// same trees and asked by every other tool that walks the checkout. `tmp` is not that tree — the
// runner's scratch is `tmp/test`, one `run-<pid>` per run — and it is skipped anyway: it is the
// repository's gitignored scratch root, it holds no tracked file at all (asserted below), and a
// page written there is a run's scratch rather than a page of the tree. `tmp` is the guard's own
// deliberate choice; `test/results` and `tmp/test` are the runner's, and both facts are asserted
// by the `--selftest` recipe so a narrower or wider skip is red here instead of silent.
const DELIBERATE_SKIP = ['tmp']

// A text file that is a page of the documentation a reader is meant to read. `desc` in a test
// plan is not one of them: it is the plan's own label (see the header).
function isPage(rel) {
    if (NOT_A_PAGE.some((entry) => rel === entry.replace(/\/$/, '') || rel.startsWith(entry))) return false
    if (GENERATED.includes(rel)) return false
    return (
        rel.endsWith('.md') ||
        (rel.startsWith('stdlib/') && rel.endsWith('.cml')) ||
        (rel.startsWith('test/plans/') && rel.endsWith('.toml'))
    )
}

// A line the guard does not report, with the words that make it an exception. Every entry is
// checked to still match a line, so an exception cannot outlive what it was written for.
//
// Each shows a removed form while saying it was removed. A decision and the parser no longer
// disagree about anything this guard reads: design principles 9.3 writes `out=model.onnx` and
// says a string value is taken as written, and the parser now takes a `string` parameter as
// written (`onnx::export out=model.onnx` really writes that file), so the two entries that used
// to excuse that sentence are gone and the sentence needs no exception.
const EXCUSED = [
    {
        file: 'docs/cli.md',
        pattern: /The parenthesised form \(`std::opt\(safe_fp=true\)`\), the colon spelling/,
        reason: 'says the three spellings are gone and how the parameter is written now',
    },
    {
        file: 'docs/cli.cn.md',
        pattern: /括号形式（`std::opt\(safe_fp=true\)`）、命令行上的冒号写法/,
        reason: '同 docs/cli.md：说明三种写法已删除，并给出现在的写法',
    },
    {
        file: 'docs/design/gpu/plan.cn.md',
        pattern: /没有 `tiles::compile\(device=\)`/,
        reason: 'quotes the review question and answers it in the negative: only `--devices` stays',
    },
]

const problems = []
const excusedUsed = new Map()
const invocations = []
// The commands a plan's `desc` shows: observed, reported, and not part of the population (see
// the header). Their count is printed with the pass and pinned by the plan beside its siblings,
// so the definition cannot go blind.
const labels = []
const pageLines = new Map()

// Report a problem against a page line, unless that line says why it is not one.
function report(rel, line, message) {
    const text = (pageLines.get(rel) || [])[line - 1] || ''
    const entry = EXCUSED.find(
        (candidate) => candidate.file === rel && candidate.pattern.test(text)
    )
    if (entry) {
        excusedUsed.set(entry, (excusedUsed.get(entry) || 0) + 1)
        return
    }
    problems.push(`${rel}:${line}: ${message}`)
}

function read(file) {
    return fs.readFileSync(path.join(REPO_ROOT, file), 'utf8')
}

function rel_exists(rel) {
    return fs.existsSync(path.join(REPO_ROOT, ...rel.split('/')))
}

function relative(abs) {
    return path.relative(REPO_ROOT, abs).replace(/\\/g, '/')
}

function* walk(root, dir = root, rel = '') {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        if (SKIP_DIRS.has(entry.name)) continue
        const child = rel === '' ? entry.name : `${rel}/${entry.name}`
        if (DELIBERATE_SKIP.some((skip) => child === skip || child.startsWith(`${skip}/`))) continue
        const full = path.join(dir, entry.name)
        // The runner's trees are wherever a run writes, not wherever this guard remembers they
        // were: the question is the module's, and this is the one place that asks it.
        if (isRunnerOutput(full, root)) continue
        if (entry.isDirectory()) yield* walk(root, full, child)
        else yield full
    }
}

/**
 * The pages of one checkout, in path order: the files a reader is meant to read, with the trees a
 * run writes into and the guard's own scratch skip left out. `root` is a checkout root, which is
 * what makes the rule testable over a planted tree as well as over this one.
 */
function discoverPages(root) {
    return [...walk(root)]
        .map((abs) => path.relative(root, abs).replace(/\\/g, '/'))
        .filter(isPage)
        .sort()
}

/** Everything under `dir`, counted one entry at a time, so a planted tree can be compared. */
function countFiles(dir) {
    let count = 0
    const stack = [dir]
    while (stack.length > 0) {
        const current = stack.pop()
        let children
        try {
            children = fs.readdirSync(current, { withFileTypes: true })
        } catch {
            continue
        }
        for (const child of children) {
            if (child.isDirectory()) stack.push(path.join(current, child.name))
            else count++
        }
    }
    return count
}

// ---- The walk's own recipe, over a planted tree ------------------------------------------------
// The skip is asserted, not argued: the same page planted inside the runner's tree and outside it
// must be counted once. The tree is planted outside the checkout (in `CAMEL_TEST_TMPDIR` when a run
// gives one, else in the OS temporary directory) and removed here; nothing of this checkout is
// written. Every run fails if a page inside a runner tree is counted, or if the page outside it is
// not, so the narrower skip this guard had (two hand-kept names) cannot come back unnoticed.
function selftest() {
    const parent = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'doc-form-walk-'))
    const root = path.join(parent, `doc-form-walk-${process.pid}`)
    const planted = [
        'docs/planted-page.md',
        'test/plans/planted.plan.toml',
        'test/results/log/planted-run/pass/planted.plan/planted.test/planted-page.md',
        'tmp/test/run-planted/planted.plan/planted.test/planted-page.md',
    ]
    const expected = ['docs/planted-page.md', 'test/plans/planted.plan.toml']
    const failures = []
    try {
        for (const rel of planted) {
            const full = path.join(root, ...rel.split('/'))
            fs.mkdirSync(path.dirname(full), { recursive: true })
            fs.writeFileSync(full, '# a planted page\n')
        }
        const found = discoverPages(root)
        if (found.join(',') !== expected.join(',')) {
            failures.push(
                `the walk of a planted checkout found ${found.length} page(s): ${found.join(', ') || 'none'}; ` +
                    `expected ${expected.join(', ')}`
            )
        }
        if (!isRunnerOutput(path.join(root, 'test', 'results'), root)) {
            failures.push('the runner tree `test/results` of the planted checkout does not read as one')
        }
        if (!isRunnerOutput(path.join(root, 'tmp', 'test'), root)) {
            failures.push("the runner's scratch `tmp/test` does not read as a tree a run writes")
        }
        if (isRunnerOutput(path.join(root, 'tmp'), root)) {
            failures.push(
                'the guard skips `tmp`, and `tmp` is no tree the runner writes: the deliberate ' +
                    'skip and the runner\'s own tree are different names, and the header says which'
            )
        }
        if (countFiles(path.join(root, 'test', 'results')) === 0) {
            failures.push('the planted runner tree holds no file, so the recipe could not tell a skip from an empty tree')
        }
    } finally {
        fs.rmSync(root, { recursive: true, force: true })
    }
    if (failures.length > 0) {
        for (const failure of failures) console.log(`FAIL ${failure}`)
        console.log(`FAIL the walk counts a page it must not, or misses one it must: ${failures.length} check(s)`)
        process.exit(1)
    }
    console.log(
        `PASS doc pass forms walk: one page planted inside the runner's tree and the same page ` +
            `outside it; the walk counted 1 of the 2, and the planted tree is gone`
    )
}

if (ARGV.includes('--selftest')) {
    selftest()
    process.exit(0)
}

// ---- A planted page, when a check asks for one ------------------------------------------------
// The check that the guard is not vacuous cannot plant the page itself: it would have to start a
// second guard, and a verification script on this host cannot capture the output of the process it
// starts (the runner starts this guard, and a guard started from a verification script cannot be
// read back). So the ask travels as an environment variable and the plant happens in this process,
// before the walk, and is removed however the run ends. A planted page is a page, so it is read by
// the same rule as any other: this changes what is on disk, never what the guard counts.
const PLANT_PAGE = process.env.CAMEL_DOC_FORM_PLANT_PAGE
// The name is unique per run, not per process: two of these runs can be told the same pid — an
// earlier run's guard can still be reading its page when this one starts — and a name that
// collided would have one run removing the other's page mid-walk.
const PLANTED_PAGE = PLANT_PAGE
    ? path.join(
          REPO_ROOT,
          `planted-page-${process.pid}-${crypto.randomUUID().slice(0, 8)}.md`
      )
    : null
if (PLANTED_PAGE) fs.writeFileSync(PLANTED_PAGE, `# a page a check planted\n\n\`${PLANT_PAGE}\`\n`)
process.on('exit', () => {
    // Only the file this run created, by its whole path: a `force` removal of a path another run
    // owns is exactly the collision above.
    if (PLANTED_PAGE) fs.rmSync(PLANTED_PAGE, { force: true })
})

// ---- The pass names the command line knows ------------------------------------------------

const PASS_NAMES = new Set()

// The core's passes are the declarations of stdlib/std.cml, and the alias table of
// src/execute/pass.cpp names the spellings that resolve to them.
{
    let namespace = []
    for (const line of read('stdlib/std.cml').split(/\r?\n/)) {
        const opened = line.trim().match(/^namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{$/)
        if (opened) {
            namespace.push(opened[1])
            continue
        }
        if (line.trim() === '}') {
            namespace.pop()
            continue
        }
        const func = line.trim().match(/^export\s+func\s+([A-Za-z_][A-Za-z0-9_]*)::<F>/)
        if (func) PASS_NAMES.add('std::' + [...namespace, func[1]].join('::'))
    }
    const text = read('src/execute/pass.cpp')
    const start = text.indexOf('const std::map<std::string, std::string> passAliases = {')
    const end = start < 0 ? -1 : text.indexOf('\n};', start)
    if (start < 0 || end < 0) {
        problems.push('src/execute/pass.cpp: the core alias table is not where the guard reads it')
    } else {
        for (const match of text.slice(start, end).matchAll(/\{"(std::[^"]+)",/g)) {
            PASS_NAMES.add(match[1])
        }
    }
}

// A module's Camel passes: the declarations of stdlib/<module>.cml, and the names the module
// registers (the loop below).
//
// What a pass is, and why the shape is tested rather than the head alone: a pass is a function of
// the graph, so its declaration takes the graph function as its value and says so —
// `export func expand::<F>(f: F): F! when is_function::<F>()`. The head `::<T>` alone does not
// make one: every generic function of the module is written that way, and reading the head alone
// put `array::reverse`, `array::sum`, `random::choice`, `optim::init` and `tensor::show` among the
// passes of their modules. The explicit-generic spelling the design gives a tensor signature
// (`export func to_bytes::<T>(t: T[..]) when is_numeric::<T>()`, design principles 4.1) joined that
// set, and `tensor::to_bytes` is then a "pass name followed by `(`" on the page that names the
// operator (`docs/design/training/bytes.cn.md`), which the command line has never read as a pass:
// only the head-plus-shape reading keeps the rule about passes and stops reporting operators.
{
    for (const entry of fs.readdirSync(path.join(REPO_ROOT, 'stdlib'))) {
        if (!entry.endsWith('.cml') || entry === 'std.cml') continue
        const module = entry.slice(0, -4)
        for (const match of read(`stdlib/${entry}`).matchAll(
            /^\s*export\s+func\s+([A-Za-z_][A-Za-z0-9_]*)\s*::\s*<([A-Za-z_][A-Za-z0-9_]*)>\s*\([^\n]*when\s+is_function::<\2>\(\)/gm
        )) {
            PASS_NAMES.add(`${module}::${match[1]}`)
        }
    }
    for (const file of walk(path.join(REPO_ROOT, 'modules'), 'modules')) {
        if (!file.endsWith('.cpp')) continue
        const text = fs.readFileSync(file, 'utf8')
        const constants = new Map()
        for (const match of text.matchAll(
            /constexpr\s+const\s+char\s+\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]+)"/g
        )) {
            constants.set(match[1], match[2])
        }
        for (const match of text.matchAll(
            /registerModulePass\(\s*([A-Za-z_][A-Za-z0-9_]*|"[^"]+")/g
        )) {
            let value = match[1]
            if (value.startsWith('"')) value = value.slice(1, -1)
            else if (constants.has(value)) value = constants.get(value)
            else continue // `module + "::" + function`: a Camel pass, named at run time
            PASS_NAMES.add(value)
        }
        for (const match of text.matchAll(/registerPassAlias\(\s*"([^"]+)"/g)) {
            PASS_NAMES.add(match[1])
        }
    }
}

const BY_LENGTH = [...PASS_NAMES].sort((a, b) => b.length - a.length)
const escape = (name) => name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
// A known pass name immediately followed by `(`: the spelling the rule deleted.
const PAREN_FORM = new RegExp(`(${BY_LENGTH.map(escape).join('|')})\\(`)
// A word that can be a specification's name: a qualified pass name, the shape a module's pass
// has whether or not this build registers it (`tiles::compile`, `mymod::report`).
const QUALIFIED = /^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+/

function knownName(text) {
    return BY_LENGTH.find((name) => text === name || text.startsWith(`${name}(`))
}

// ---- Splitting a command line, the way the command line splits itself ----------------------

// A shell word splitter: a quote is a delimiter, its content one word (`src/execute/pass_spec.cpp`
// reads what the shell left, so the quoting of a page is undone here the same way).
function shellSplit(text) {
    const tokens = []
    let current = ''
    let quote = 0
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (quote) {
            if (ch === '\\' && i + 1 < text.length) {
                current += ch + text[++i]
                continue
            }
            if (ch === quote) {
                quote = 0
                continue
            }
            current += ch
            continue
        }
        if (ch === "'" || ch === '"') {
            quote = ch
            continue
        }
        if (/\s/.test(ch)) {
            if (current !== '') {
                tokens.push(current)
                current = ''
            }
            continue
        }
        current += ch
    }
    if (current !== '') tokens.push(current)
    return tokens
}

// `splitPassList` (src/execute/pass_spec.cpp): commas outside quotes separate passes. The quote
// characters stay, because the parser reads them as part of a value (`stats=""`).
function splitPassList(text) {
    const out = []
    let current = ''
    let quote = 0
    const add = () => {
        const trimmed = current.trim()
        if (trimmed !== '') out.push(trimmed)
        current = ''
    }
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (quote) {
            current += ch
            if (ch === '\\' && i + 1 < text.length) current += text[++i]
            else if (ch === quote) quote = 0
            continue
        }
        if (ch === '"' || ch === "'") quote = ch
        else if (ch === ',') {
            add()
            continue
        }
        current += ch
    }
    add()
    return out
}

// `groupPassWords` (src/execute/pass_spec.cpp): a word whose text before `=` is a bare name
// continues the specification before it; every other word starts one.
function groupPassWords(words, first = 0) {
    const out = []
    for (let i = first; i < words.length; i++) {
        const word = words[i]
        const equals = word.indexOf('=')
        const parameter =
            equals > 0 && [...word.slice(0, equals)].every((c) => /[A-Za-z0-9_]/.test(c))
        if (parameter && out.length > 0) out[out.length - 1] += ' ' + word
        else out.push(word)
    }
    return out
}

// `parseSpec` (src/execute/pass_spec.cpp): the name ends at the first space, the name may hold
// no `(`, and every word after it is a `key=value` word whose key is a bare name with a value.
// The word scanner is the parser's: a quote is part of the word it delimits, so `stats=""` is a
// parameter whose value is the empty string literal, not a parameter with no value.
function readSpec(spec) {
    const first = spec.search(/\S/)
    if (first < 0) return 'the pass specification is empty'
    let nameEnd = first
    while (nameEnd < spec.length && !/\s/.test(spec[nameEnd])) nameEnd++
    const name = spec.slice(first, nameEnd)
    if (name.includes('(')) {
        return `'${name}': a parameter is written key=value after the pass name, not in parentheses`
    }
    let cursor = nameEnd
    for (;;) {
        while (cursor < spec.length && /\s/.test(spec[cursor])) cursor++
        if (cursor >= spec.length) return null
        let word = ''
        let quote = 0
        while (cursor < spec.length) {
            const ch = spec[cursor]
            if (quote) {
                word += ch
                cursor++
                if (ch === '\\' && cursor < spec.length) word += spec[cursor++]
                else if (ch === quote) quote = 0
                continue
            }
            if (ch === '"' || ch === "'") {
                quote = ch
                word += ch
                cursor++
                continue
            }
            if (/\s/.test(ch)) break
            word += ch
            cursor++
        }
        if (quote) return `'${word}': the string is not closed`
        const equals = word.indexOf('=')
        if (equals < 0) {
            if (word.includes(':') && !word.includes('::')) {
                return `'${word}': a pass parameter is written key=value on the command line; key: value is the spelling of a named argument in Camel source`
            }
            return `'${word}' is not a parameter: a parameter is written key=value`
        }
        const key = word.slice(0, equals)
        if (key === '' || !/^[A-Za-z0-9_]+$/.test(key)) return `'${key}' is not a parameter name`
        if (word.slice(equals + 1) === '') return `the parameter '${key}' has no value`
    }
}

function isInvocationName(word) {
    if (knownName(word)) return true
    return QUALIFIED.test(word) || word.includes('(')
}

// `safe_fp:` is the colon spelling the command line refuses; `true` after a pass name is the
// positional parameter it refuses. Both are exact shapes, so neither can catch a redirection.
const COLON_PARAMETER = /^[A-Za-z0-9_]+:$/
const BARE_LITERAL = /^(?:true|false|-?\d+(?:\.\d+)?)$/

// ---- Finding the command lines on a page ---------------------------------------------------

const FENCE = /^(\s*)(```+|~~~+)\s*([A-Za-z0-9_+#-]*)(.*)$/
const SPAN = /`([^`\n]+)`/g
const CAMEL_COMMAND = /^(?:[^\s]*[\\/])?camel(?:\.exe)?$/

function commandLines(text) {
    const lines = text.split(/\r?\n/)
    const found = []
    let insideFence = false
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i]
        const fence = FENCE.exec(line)
        if (fence) {
            insideFence = !insideFence
            continue
        }
        if (insideFence) {
            const stripped = line
                .replace(/^\s*(?:\/\/|#)\s*/, '')
                .replace(/\s+#.*$/, '')
                .trim()
            if (CAMEL_COMMAND.test(shellSplit(stripped)[0] || '')) {
                // A command continued on the next line with a trailing backslash.
                let whole = stripped
                let j = i
                while (/\\$/.test(whole) && j + 1 < lines.length) {
                    whole = whole.replace(/\\$/, '') + ' ' + lines[++j].trim()
                }
                found.push({ line: i + 1, text: whole })
            }
        }
        for (const span of line.matchAll(SPAN)) {
            const words = shellSplit(span[1])
            if (words.length > 0 && CAMEL_COMMAND.test(words[0])) {
                found.push({ line: i + 1, text: span[1] })
            }
        }
    }
    return found
}

// The specifications of one command line: the program and the options are not passes, and
// `groupPassWords` decides where one specification ends (`--passes` is a list of its own).
function specsOfCommand(text) {
    const tokens = shellSplit(text)
    const specs = []
    let i = 1 // the program name of the command
    let file = null
    while (i < tokens.length) {
        const token = tokens[i]
        if (token === '--passes') {
            const list = tokens[i + 1] ?? ''
            specs.push(...groupPassWords(splitPassList(list), 0))
            i += 2
            continue
        }
        if (['--arg', '-I', '--include', '--input', '--timeit'].includes(token)) {
            if (token === '--input') file = tokens[i + 1] ?? null
            i += 2
            continue
        }
        if (/^--(?:arg|include|input)=/.test(token)) {
            if (token.startsWith('--input=')) file = token.slice('--input='.length)
            i++
            continue
        }
        if (token.startsWith('-')) {
            i++
            // A value that is not a specification and does not look like one belongs to the option.
            if (i < tokens.length && !tokens[i].startsWith('-') && !isInvocationName(tokens[i])) i++
            continue
        }
        if (file === null && /\.cml$/i.test(token)) {
            file = token
            i++
            continue
        }
        // Every remaining word is grouped the way the command line groups it: the first word
        // that is not a `key=value` parameter opens a specification.
        specs.push(...groupPassWords(tokens, i))
        break
    }
    return specs
}

// ---- Scanning the pages --------------------------------------------------------------------

const files = discoverPages(REPO_ROOT)

if (files.length === 0) {
    console.log('no reader-facing page in the tree: nothing was checked')
    process.exit(2)
}

// The walk counts every page of the checkout and nothing else. Both halves are stated here
// because a page silently lost is a rule that stopped matching: every tracked `*.md` that is
// meant to be read must be in the population — the ones left out are exactly the two classes
// above, and each is named — and the trees a run writes must hold no tracked file the skip could
// lose.
{
    const listed = spawnSync('git', ['ls-files', '*.md'], { cwd: REPO_ROOT, encoding: 'utf8' })
    const trackedPages = (listed.stdout || '').split(/\r?\n/).filter(Boolean)
    // The same two rules the walk applies, so "left out" means the class and not the walk.
    const meantToBeRead = (rel) =>
        !NOT_A_PAGE.some((entry) => rel === entry.replace(/\/$/, '') || rel.startsWith(entry)) &&
        !GENERATED.includes(rel)
    const missing = trackedPages.filter((rel) => meantToBeRead(rel) && !files.includes(rel))
    const leftOut = trackedPages.filter((rel) => !meantToBeRead(rel))
    if (!listed.error && missing.length > 0) {
        problems.push(
            `the walk left ${missing.length} tracked page(s) behind: ${missing.slice(0, 3).join(', ')}` +
                `${missing.length > 3 ? ', ...' : ''}`
        )
    }
    for (const entry of [...NOT_A_PAGE, ...GENERATED]) {
        const base = entry.replace(/\/$/, '')
        // An exclusion is a directory or a single page. A directory still holding a page is the
        // only shape `NOT_A_PAGE` and `GENERATED` have; a name that is not there would make its
        // class look applied when the walk is skipping nothing.
        const known = entry.endsWith('/')
            ? trackedPages.some((rel) => rel.startsWith(entry))
            : rel_exists(base)
        if (!listed.error && !known) {
            problems.push(
                `'${entry}' is excluded from the pages and the tree holds nothing of that name: ` +
                    `the exclusion outlived what it was written for`
            )
        }
    }
    if (!listed.error && !rel_exists('changes.log')) {
        problems.push('changes.log is excluded from the pages and is not in the tree any more')
    }
    if (!listed.error && trackedPages.length === 0) {
        problems.push('the tree has no tracked page at all: the walk was checked over nothing')
    }
    if (!listed.error) {
        if (leftOut.length === 0) {
            problems.push('every tracked page was read, so the exclusions above excuse nothing')
        }
    }
    const scratch = ['test/results', 'tmp']
    for (const rel of scratch) {
        const listed = spawnSync('git', ['ls-files', '--', rel], { cwd: REPO_ROOT, encoding: 'utf8' })
        const entries = (listed.stdout || '').split(/\r?\n/).filter(Boolean)
        if (!listed.error && entries.length > 0) {
            problems.push(
                `'${rel}' is skipped by the walk and holds ${entries.length} tracked file(s): ` +
                    `${entries.slice(0, 3).join(', ')}; it is no longer scratch`
            )
        }
    }
}

// A test plan is read for what a reader would take from it, and four of its keys are executable
// fixtures rather than prose: `args` is the pin a refusal test must contain, `envs` is the same
// pin for a test that drives a tool through the environment (a guard is told to plant a page that
// spells the removed form), and `verify_script` / `verify_script_path` / the older `verify_js` are
// a check's own code. Their lines are therefore not read for the removed form; every other line of
// a plan, comments included, is.
function pinnedLines(lines) {
    const pinned = new Set()
    const KEYS = ['args', 'envs', 'verify_script', 'verify_script_path', 'verify_js']
    let open = null
    lines.forEach((line, index) => {
        if (open === null) {
            const key = line.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*/)
            if (!key || !KEYS.includes(key[1])) return
            const rest = line.slice(key[0].length)
            const block = rest.match(/^("""|''')/)
            if (block) {
                pinned.add(index)
                // A `"""` block may open and close on one line, and then it is pinned whole.
                open = rest.slice(3).includes(block[1]) ? null : block[1]
                return
            }
            if (!['args', 'envs'].includes(key[1])) return
            pinned.add(index)
            if (!/\]/.test(rest)) open = ']'
            return
        }
        pinned.add(index)
        if (open === ']' ? /^\s*\]/.test(line) : line.includes(open)) open = null
    })
    return pinned
}

// A plan's `desc` is its own label, not a page (the header says why): a line of it is observed
// and not counted. Both spellings a plan uses are read — the one-line string and the `"""` block
// — because a rule that matches one of them would count some labels and not others.
function labelLines(lines) {
    const labels = new Set()
    for (let index = 0; index < lines.length; index++) {
        const opened = lines[index].match(/^\s*desc\s*=\s*(.*)$/)
        if (!opened) continue
        labels.add(index)
        const rest = opened[1]
        const block = rest.match(/^("""|''')/)
        if (!block) continue
        let closed = rest.slice(3).includes(block[1])
        for (let cursor = index + 1; cursor < lines.length && !closed; cursor++) {
            labels.add(cursor)
            if (lines[cursor].includes(block[1])) closed = true
        }
    }
    return labels
}

for (const rel of files) {
    const text = read(rel)
    const lines = text.split(/\r?\n/)
    pageLines.set(rel, lines)
    const pinned = rel.startsWith('test/plans/') ? pinnedLines(lines) : new Set()
    const inLabel = rel.startsWith('test/plans/') ? labelLines(lines) : new Set()

    // A pass name immediately followed by `(` is the deleted spelling wherever it is written.
    lines.forEach((line, index) => {
        if (pinned.has(index)) return
        if (!PAREN_FORM.test(line)) return
        report(
            rel,
            index + 1,
            `a pass name followed by '(' is the removed form: ${line.trim().slice(0, 120)}`
        )
    })

    // A `camel …` command line is an invocation a reader copies and runs. Every word after the
    // program is a pass specification to the command line, so a word that is not a pass name is
    // reported when it is unmistakably a parameter: `key:` is the colon spelling and a bare
    // literal is a positional parameter. A word that is neither (`<file>`, `2>/dev/null`) is
    // left alone: it cannot be told apart from a placeholder or a redirection.
    //
    // A command line inside a plan's `desc` is the plan's own label and not a page: it is counted
    // in its own bucket, reported, and left out of the population. Every rule above still applies
    // to it, so a label that teaches a spelling the parser refuses is still red.
    for (const command of commandLines(text)) {
        const label = inLabel.has(command.line - 1)
        let afterAPass = false
        for (const spec of specsOfCommand(command.text)) {
            const name = shellSplit(spec)[0] ?? ''
            if (isInvocationName(name)) {
                if (label) labels.push({ rel, line: command.line, spec })
                else invocations.push({ rel, line: command.line, spec })
                afterAPass = true
                continue
            }
            if (COLON_PARAMETER.test(spec)) {
                report(
                    rel,
                    command.line,
                    `'${spec}' is the removed \`key: value\` spelling: a pass parameter is ` +
                        `written key=value on the command line`
                )
            } else if (afterAPass && BARE_LITERAL.test(spec)) {
                report(
                    rel,
                    command.line,
                    `'${spec}' is the removed positional parameter: a parameter is written ` +
                        `key=value after the pass name`
                )
            }
            afterAPass = false
        }
    }
}

// The parser's own rule, applied to every invocation the pages show. A plan's label is read by
// the same rule even though it is not counted: a label that teaches a spelling the parser refuses
// is a defect of the command it names, whatever a reader does with it.
const seen = new Map()
for (const { rel, line, spec } of invocations) {
    const refusal = readSpec(spec)
    if (refusal) report(rel, line, `the command line refuses '${spec}': ${refusal}`)
    if (!seen.has(spec)) seen.set(spec, [])
    seen.get(spec).push({ rel, line })
    if (LIST) console.log(`invocation ${rel}:${line}: ${spec}`)
}
for (const { rel, line, spec } of labels) {
    const refusal = readSpec(spec)
    if (refusal) report(rel, line, `the plan's label shows '${spec}' and ${refusal}`)
    if (LIST) console.log(`label ${rel}:${line}: ${spec}`)
}
const distinct = [...seen.entries()].map(([spec, occurrences]) => ({ spec, occurrences }))

// ---- The real parser ----------------------------------------------------------------------

function runCamel(args, cwd) {
    return new Promise((resolve) => {
        const child = spawn(CAMEL, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
        let output = ''
        let timedOut = false
        const timer = setTimeout(() => {
            timedOut = true
            child.kill('SIGKILL')
        }, TIMEOUT_MS)
        child.stdout.on('data', (chunk) => (output += chunk))
        child.stderr.on('data', (chunk) => (output += chunk))
        child.on('error', (error) => {
            clearTimeout(timer)
            // A spawn that never started — a missing executable, or a library the loader refuses —
            // is not a page defect: it is reported with the error, and when no invocation started
            // at all the guard refuses to report a pass rather than blame every page.
            resolve({ unstarted: true, output: error.message, timedOut })
        })
        child.on('close', () => {
            clearTimeout(timer)
            resolve({ unstarted: false, output, timedOut })
        })
    })
}

if (!fs.existsSync(CAMEL)) {
    for (const problem of problems) console.log(`FAIL ${problem}`)
    console.log(
        `no camel binary at ${CAMEL}: the ${distinct.length} invocation(s) were not run through ` +
            `the parser; refusing to report a pass`
    )
    process.exit(2)
}

const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-pass-form-'))
const program = path.join(tempDir, 'entry.cml')
fs.writeFileSync(program, 'func main(): int sync {\n    return 0\n}\n')

// The pipeline is resolved and refused before any pass runs, so the invocation never executes.
// Reaching the probe means the parser read the specification; a name this build does not
// register is refused as unknown, and a parameter the declaration does not accept is one of the
// `SemanticDiag::ParamArgument*` diagnostics — neither is a spelling problem.
const RESOLVED =
    /UnrecognizedGraphPass|PassNotProvidedByModule|PassNotExportedByModule|ParamArgumentUnknown|ParamArgumentDuplicate|ParamArgumentWrongType|ParamArgumentMissing|ParamArgumentValue/

// The parser's own words for a refused specification: the one line that names the diagnostic,
// short enough for the first line of a red (the runner clips an echoed line at 240 characters).
function complaintOf(output) {
    const line = output
        .split(/\r?\n/)
        .map((text) => text.trim())
        .find((text) => text.includes('InvalidPassParameter'))
    return (line || output.trim().split(/\r?\n/)[0] || 'no diagnostic').slice(0, 110)
}

function timeoutReport(spec, together) {
    return (
        `'${spec}' did not come back in the documented ${TIMEOUT_MS} ms limit` +
        (together > 0 ? ` (it shared the invocation with ${together} other specification(s))` : '') +
        `; tens of milliseconds is normal, so the host is loaded or the parser is wedged`
    )
}

// Every page that shows a specification is named, so one defect is not reported once and hidden
// on the other pages.
let parsed = 0
let unstarted = 0
let unstartedMessage = ''
let invocationsRun = 0

// One specification, one invocation: the whole cost this guard used to pay, and the path a batch
// the parser refuses falls back to. The verdict of a specification is its own run's, never a
// batch's, so a refusal is reported with its own words.
async function decideOne(spec, occurrences) {
    const result = await runCamel([program, spec, PROBE], tempDir)
    invocationsRun++
    if (result.unstarted) {
        unstarted += 1
        unstartedMessage = unstartedMessage || result.output.trim()
        for (const { rel, line } of occurrences) {
            report(
                rel,
                line,
                `the parser could not be started for '${spec}': ${result.output.trim().slice(0, 110)}`
            )
        }
        return
    }
    if (result.timedOut) {
        for (const { rel, line } of occurrences) report(rel, line, timeoutReport(spec, 0))
        return
    }
    if (/InvalidPassParameter/.test(result.output)) {
        for (const { rel, line } of occurrences) {
            report(rel, line, `the parser refuses '${spec}': ${complaintOf(result.output)}`)
        }
        return
    }
    // The specification was read and the pipeline went on to the probe: either the probe's
    // unknown name, or the pass name itself when this build does not register it.
    if (!RESOLVED.test(result.output)) {
        const seenOutput = result.output.trim().split(/\r?\n/).slice(0, 2).join(' / ')
        for (const { rel, line } of occurrences) {
            report(
                rel,
                line,
                `'${spec}' neither parsed nor reached the probe: ${seenOutput.slice(0, 130)}`
            )
        }
        return
    }
    parsed++
}

// One invocation decides several specifications. `requireKnownPasses` resolves every name of a
// pipeline, and refuses the pipeline, before the first pass runs, so a pipeline of
// specifications followed by the probe reads each of them with the real parser and runs nothing.
//
// The pipeline is refused at the **first** name it cannot resolve, so only an invocation whose
// complaint names the probe proves that every specification in it resolved: a complaint about an
// earlier member — a name this build does not register, a parameter a declaration does not accept
// — may leave a specification after it unread, and a spelling the parser would refuse there would
// be hidden. Anything else than the probe's own name therefore decides none of the members, and
// they are decided one by one above, which is the evidence this guard collected for every
// specification before the batch existed. (Measured: a single invocation carrying all 37
// specifications of the tree is refused at `mod::pass` — and it does mention the probe, in the
// `PassesNeverRun` warning that quotes every pass written after the first graph-consuming one,
// which is why the check is for the probe's own complaint and not for its name. The batches that
// hold `std::no_such_pass`, `mymod::fn` and `std::opt safe_fpp=true` are refused at those members
// and are decided one specification at a time; a page that adds a spelling the parser refuses,
// `camel train.cml std::opt(safe_fp=true)`, is reported by its own run with the same problem list
// whether the specifications are batched, one per invocation, or all in one.)
async function decideBatch(batch) {
    // A batch of one is the old path exactly: one specification, one invocation, its own verdict
    // (a specification whose own name is unknown is read by `decideOne` as reaching the probe,
    // which is the verdict this guard has always given it).
    if (batch.length === 1) return decideOne(batch[0].spec, batch[0].occurrences)
    const result = await runCamel([program, ...batch.map((item) => item.spec), PROBE], tempDir)
    invocationsRun++
    const together = batch.length - 1
    if (result.unstarted) {
        unstarted += batch.length
        unstartedMessage = unstartedMessage || result.output.trim()
        for (const { spec, occurrences } of batch) {
            for (const { rel, line } of occurrences) {
                report(
                    rel,
                    line,
                    `the parser could not be started for '${spec}': ` +
                        `${result.output.trim().slice(0, 110)}`
                )
            }
        }
        return
    }
    if (result.timedOut) {
        // A batch that never answered is reported against every specification it held, and is not
        // re-run: a silent retry would hide the one thing the red has to say.
        for (const { spec, occurrences } of batch) {
            for (const { rel, line } of occurrences) report(rel, line, timeoutReport(spec, together))
        }
        return
    }
    // The probe's own complaint, not any mention of its name. The pipeline issues a warning that
    // quotes every pass written after the first graph-consuming one (`PassesNeverRun`), and the
    // probe is one of them, so a bare `includes(PROBE)` would accept a batch the parser never
    // resolved that far — measured: one invocation carrying all 38 specifications mentions the
    // probe in that warning while it is in fact refused at `mod::pass`, and the specification the
    // parser refuses was then not reported at all.
    if (PROBE_REFUSED.test(result.output) && !/InvalidPassParameter/.test(result.output)) {
        parsed += batch.length
        return
    }
    for (const { spec, occurrences } of batch) await decideOne(spec, occurrences)
}

const batches = []
for (let i = 0; i < distinct.length; i += BATCH) {
    batches.push(distinct.slice(i, i + BATCH))
}

let cursor = 0
async function worker() {
    while (cursor < batches.length) {
        await decideBatch(batches[cursor++])
    }
}
try {
    await Promise.all(Array.from({ length: Math.min(JOBS, batches.length || 1) }, worker))
} finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
}

// An exception that no longer excuses anything is a sentence that changed: report it, so the
// list cannot outlive what it was written for.
let excused = 0
for (const entry of EXCUSED) {
    const used = excusedUsed.get(entry) || 0
    excused += used
    if (used === 0) {
        problems.push(
            `${entry.file}: the exception '${entry.reason}' no longer matches a line; the ` +
                `sentence it excuses was rewritten or removed`
        )
    }
}

if (invocations.length === 0) {
    console.log(
        `no pass invocation in ${files.length} reader-facing page(s): nothing was checked; the ` +
            `extraction stopped matching the pages`
    )
    process.exit(2)
}
if (distinct.length === 0) {
    console.log(
        `the ${invocations.length} invocation(s) of ${files.length} page(s) yielded no pass ` +
            `specification: the split stopped matching the command lines`
    )
    process.exit(2)
}

// An install that cannot run the parser at all is not a page: every specification was refused by
// the same broken invocation, so no page is blamed and the pass is not reported.
if (distinct.length > 0 && unstarted === distinct.length) {
    console.log(`FAIL no specification could be run through the parser: ${CAMEL}`)
    console.log(`FAIL ${unstartedMessage.slice(0, 200)}`)
    console.log(
        `FAIL the ${distinct.length} specification(s) in ${files.length} page(s) were not read; ` +
            `refusing to report a pass`
    )
    process.exit(2)
}

// The first line of a red is the reason. The runner echoes only the last 12 lines of a failed
// test's output, so the report is kept to a few short lines and the first one carries the page,
// the line, the invocation and what the parser said; every problem is still counted, and
// CAMEL_DOC_FORM_REPORT_ALL=1 prints them all.
const shown = REPORT_ALL ? problems.length : Math.min(problems.length, 3)
for (let i = 0; i < shown; i++) console.log(`FAIL ${problems[i]}`)
if (problems.length > shown) {
    console.log(
        `FAIL ... and ${problems.length - shown} more problem(s): CAMEL_DOC_FORM_REPORT_ALL=1 ` +
            `prints every one`
    )
}
if (problems.length > 0) {
    console.log(
        `FAIL the pages teach a pass spelling the command line refuses: ${problems.length} ` +
            `problem(s) in ${files.length} page(s); the first is the line above`
    )
    process.exit(1)
}
// The property, not the count: a green run has read every distinct specification through the real
// parser and heard it reach the probe. A specification the parser refused is reported, so this is
// the statement that nothing was decided by a batch that never resolved it.
if (parsed !== distinct.length) {
    console.log(
        `FAIL the parser decided ${parsed} of ${distinct.length} specification(s): ` +
            `${distinct.length - parsed} were neither refused nor read through the probe`
    )
    process.exit(1)
}

console.log(
    `PASS doc pass forms: ${invocations.length} pass invocation(s) in ${files.length} page(s), ` +
        `${distinct.length} distinct, each read by the parser's own rule and each accepted by the ` +
        `real parser (${parsed} reached the probe); ${labels.length} invocation(s) in a plan's own ` +
        `desc are reported and not counted; ${excused} page line(s) are excused (the form is said ` +
        `to be removed, or a decision and the parser disagree); ` +
        `${invocationsRun} parser invocation(s) for ${distinct.length} specification(s), ` +
        `${((Date.now() - STARTED_AT) / 1000).toFixed(1)} s`
)