// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Every tool that reads a diagnostic back out of `camel`'s output, and whether the run it starts asks
 * for the form it parses.
 *
 * `camel` prints a diagnostic in one of three forms, and the default is the rich one:
 *
 *   rich (default, `text`):  `error[SemanticDiag::Name]: message`, `--> path:line:col`, excerpt
 *   short (`-E short`):      `(module)path:line:col: [Error]: message suggestion (name=…, code=0x…)`
 *   json  (`-E json`):       the report as an object
 *
 * The form is chosen per run (`tools/camel-cli/config.cpp#applyDiagnosticOptions`): `-E` /
 * `--error-format` for `run`, `-O` / `--output-format` for `check`, else `CAMEL_ERROR_FORMAT`, else
 * `text`. The plan runner sets `CAMEL_ERROR_FORMAT=short` for every child it starts
 * (`test/tools/run-tests.mjs`), so a *test case* sees the one-line form by construction -- and a tool
 * that spawns `camel` itself, with the caller's environment, sees the rich one.
 *
 * That difference has gone wrong twice here, both times silently, because a reader that matches
 * nothing reads as "the thing is not there": that is a wrong answer, not an error. The autodiff
 * ledger looked for a line containing `: [Error]: `, matched nothing under the default form, filed
 * every failing case under `other`, and reded the ratchet on a tree where nothing had moved; and
 * before that it looked for `error[`, which the tree had stopped printing. So this checker names the
 * readers, says which form each parses, and fails when a reader and its run disagree:
 *
 *   * a reader of the one-line frame (`: [Error]: `) must ask for `short`;
 *   * a reader of the rich header (`error[`) must not ask for `short` -- the default already gives it
 *     that form, and asking for `short` would take it away;
 *   * a reader of both is fine either way, and is reported as such (`stdlib-reference.mjs` accepts
 *     `error[SemanticDiag::X]` and `name=SemanticDiag::X` in one expression, which is the pattern the
 *     others should copy).
 *
 * An exception is a reader known to parse the short form without asking for it: one entry here, with
 * what it costs and what closes it, printed on every run so the list is never silently empty. A new
 * one fails the check by name; an exception that no longer applies fails it too, because a list
 * longer than the defects hides the next one.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const SELF = fileURLToPath(import.meta.url)
const REPO_ROOT = path.resolve(path.dirname(SELF), '..', '..')

/// Where a tool that talks to `camel` lives.
const ROOTS = ['scripts', 'test/tools', 'exemplars']

/// The one-line frame, and the rich header. A tool may know either, or both.
const SHORT_FRAME = ': [Error]: '
const RICH_HEADER = /error\[/

/// How a tool asks for a form.
const ASKS_SHORT = [/CAMEL_ERROR_FORMAT\s*[:=]\s*['"]short['"]/, /-E\s+short/, /--error-format[=\s]+short/, /-O\s+short/]

/// How a tool starts `camel`.
const SPAWNS = [/\bspawn(Sync)?\s*\(/, /\bexecFile(Sync)?\s*\(/, /\bexecSync\s*\(/, /\brunProcess\s*\(/, /subprocess\.run\s*\(/]

/// Readers that parse the one-line frame without asking for it: a fact about this tree, not a
/// permission. The reason says what it costs; the repair says what closes it.
const EXCEPTIONS = [
    {
        file: 'scripts/autodiff-migration-ledger.mjs',
        why: 'its `runCase` takes the first line containing `: [Error]: ` out of a run it starts itself, and that run inherits the caller\'s environment, so under the default (rich) form the extraction is empty and every failing case\'s `first` column is blank; the ledger\'s own numbers are still right because the classification reads the case sources and the exit code, which is why nothing noticed',
        repair: 'read both shapes (`: [Error]: ` and `error[...]: `) in one expression, the way `scripts/stdlib-reference.mjs` does, or set `CAMEL_ERROR_FORMAT: \'short\'` in that spawn\'s environment; the file belongs to the merge lane, so it is not edited from here',
    },
]

function walk(dir, out) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            if (entry.name === 'node_modules' || entry.name.startsWith('.')) continue
            walk(full, out)
        } else if (/\.(mjs|js|cjs|py)$/.test(entry.name)) {
            out.push(full)
        }
    }
    return out
}

const exceptions = new Map(EXCEPTIONS.map((entry) => [entry.file, entry]))
const readers = []

for (const root of ROOTS) {
    const dir = path.join(REPO_ROOT, root)
    if (!fs.existsSync(dir)) continue
    for (const file of walk(dir, [])) {
        if (path.resolve(file) === path.resolve(SELF)) continue
        const relative = path.relative(REPO_ROOT, file).split(path.sep).join('/')
        const text = fs.readFileSync(file, 'utf8')
        const readsShort = text.includes(SHORT_FRAME)
        const readsRich = RICH_HEADER.test(text)
        if (!readsShort && !readsRich) continue
        if (!SPAWNS.some((one) => one.test(text))) continue
        readers.push({ file: relative, readsShort, readsRich, asksShort: ASKS_SHORT.some((one) => one.test(text)) })
    }
}

const stateOf = (reader) => {
    if (reader.readsShort && reader.readsRich) return 'reads both forms'
    if (reader.readsShort) return reader.asksShort ? 'reads the one-line form, and asks for it' : 'reads the one-line form and does NOT ask for it'
    return reader.asksShort ? 'reads the rich form but ASKS for the one-line one' : 'reads the rich form, which is the default'
}

for (const reader of readers) {
    console.log(`[DIAG-FORMAT] ${reader.file}: ${stateOf(reader)}`)
}
for (const entry of EXCEPTIONS) {
    console.log(`[DIAG-FORMAT] exception ${entry.file}: ${entry.why}`)
    console.log(`[DIAG-FORMAT] repair ${entry.file}: ${entry.repair}`)
}

const problems = []
if (readers.length === 0) {
    // A checker that scans nothing passes everything: say so instead of printing an empty list.
    problems.push('no tool reads a diagnostic back out of a run -- either the scan roots moved or the shapes did')
}
for (const reader of readers) {
    const exception = exceptions.get(reader.file)
    const misplaced =
        (reader.readsShort && !reader.readsRich && !reader.asksShort) ||
        (!reader.readsShort && reader.readsRich && reader.asksShort)
    if (misplaced && !exception) {
        problems.push(
            `${reader.file} ${stateOf(reader)}: a reader that matches nothing reads as "there is nothing " ` +
                `there. Read both shapes in one expression, or ask for the form it parses ` +
                `(CAMEL_ERROR_FORMAT='short' for the one-line frame, the default for the rich one); if it is ` +
                `meant to stay, add it to EXCEPTIONS with the reason and the repair.`,
        )
    }
    if (!misplaced && exception) {
        problems.push(
            `${reader.file} is in EXCEPTIONS but ${stateOf(reader)}: delete the exception, a list longer ` +
                `than the defects hides the next one.`,
        )
    }
}

if (problems.length > 0) {
    for (const problem of problems) console.error(`[DIAG-FORMAT] ${problem}`)
    process.exit(1)
}
console.log(
    `[DIAG-FORMAT] ok: ${readers.length} reader(s), ${readers.filter((one) => one.asksShort).length} ask for the ` +
        `one-line form, ${readers.filter((one) => one.readsShort && one.readsRich).length} read both, ` +
        `${EXCEPTIONS.length} recorded exception(s)`,
)