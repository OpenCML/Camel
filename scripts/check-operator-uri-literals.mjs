#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * An operator's URI is spelled in its definition and in the registry, and nowhere else.
 *
 * The target architecture (section 7, "守护") writes the invariant as "an operator's semantics live
 * only in its definition", with the mechanical check "the literal of an operator URI may appear only
 * in the definition and in the implementation registry". A `":op/add_l"` written in a backend, a
 * pass or a module is a second place that knows an operator by name, and the second place is what
 * has to be edited when the operator moves: the URI is the operator's identity, and identity is
 * owned by the table (`OperatorTable`) and by the declarations that fill it.
 *
 * That page is not in this tree as of 2026-11-14: it is the design branch's (`origin/fsh-dev`), so
 * section 7 is quoted here rather than linked -- a link to a page this tree does not hold is a
 * broken link, and `scripts/check-doc-links.mjs --citations` says so.
 *
 * --- what "the definition or the registry" is here --------------------------
 * It is a rule about **files**, written down rather than a list of paths a person keeps in step by
 * hand, so that the next person can judge whether the boundary should move. A file is a definition
 * or a registry, and its URI literals are not findings, when
 *
 *   1. it sits under `src/builtin/` -- the built-in operators' definitions, the executors that run
 *      them, and the module registries that fill the table with them;
 *   2. it is `src/core/operator_table.cpp` or `src/core/operator_names.cpp` -- the table itself and
 *      the named-operator constants;
 *   3. it is `include/camel/core/operator_table.h` or `include/camel/core/operator_names.h` -- the
 *      declarations those two fill, including the well-known URI list the table is built from;
 *   4. its base name contains `traits` or `registry`, wherever it sits -- a traits table and a
 *      registry are the two other shapes a definition takes in this tree
 *      (`src/core/operator_traits.cpp`, `modules/tensor/ops/registry.cpp`).
 *
 * Everything else is a business file, and every built-in URI literal in it is reported once:
 * `path:line: ":op/..."`. A literal is a string whose first characters after the quote are `:`,
 * a name and `/`; that is the shape of the built-in namespaces (`:op/`, `:io/`, `:var/`, ...), and
 * a module's own namespace (`"tensor:matmul"`) is not one. Comments are not searched: a comment
 * that names a URI explains the code instead of depending on it.
 *
 * **This rule is mechanical, and it is the author's to change.** It says that the URI is spelled
 * where the operator is declared, not that every file listed above deserves the exemption: a
 * backend's spelling table (`src/passes/trans/cpp/operator_rules.cpp`), the bytecode's opcode map
 * (`src/passes/sched/fastvm/compile.cpp`) and a module's lowering table
 * (`modules/onnx/lowering.cpp`) are the biggest populations in the list, and whether one of them
 * *is* a registry is exactly the judgement this check asks a reader to make. If one is, add it to
 * the rule above with the reason beside it, and the list comes down by that much.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-operator-uri-literals.mjs                report-only (exit 0)
 *   node scripts/check-operator-uri-literals.mjs --enforce       exit 1 when the list is not empty
 *   node scripts/check-operator-uri-literals.mjs --path <p>      scan another file or directory
 *
 * The gate runs the bare form: this file prints the list and the count, and the count is the
 * baseline the list has to come down from. `--enforce` is the mode the gate entry becomes once the
 * list is empty.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const NAME = 'check-operator-uri-literals'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The directories the scan covers by default, the same set the other source checks read. */
const SCANNED = ['include', 'src', 'modules', 'tools']

const SOURCE = /\.(?:h|hpp|hh|hxx|cpp|cc|cxx)$/

/** A built-in operator URI literal: `":op/add_l"`, `":io/println"`, `":op/"` as a prefix. */
const URI_LITERAL = /"(:[A-Za-z_][A-Za-z0-9_]*\/[^"\n]*)"/g

/** The files the rule names outright, relative to the tree. */
const HOME_FILES = new Set([
    'src/core/operator_table.cpp',
    'src/core/operator_names.cpp',
    'include/camel/core/operator_table.h',
    'include/camel/core/operator_names.h',
])

/** A base name that says "traits table" or "registry" is a definition wherever it sits. */
const HOME_NAME = /(?:traits|registry)/i

/** The one directory whose whole tree is a definition: the built-in operators and their executors. */
const HOME_TREE = 'src/builtin/'

/** Replaces comments with spaces and leaves string literals alone, so offsets and lines survive. */
function stripComments(text) {
    const out = text.split('')
    let index = 0
    while (index < out.length) {
        const two = text.slice(index, index + 2)
        if (two === '//') {
            while (index < out.length && text[index] !== '\n') out[index++] = ' '
        } else if (two === '/*') {
            out[index++] = ' '
            out[index++] = ' '
            while (index < out.length && text.slice(index, index + 2) !== '*/') {
                if (text[index] !== '\n') out[index] = ' '
                index += 1
            }
            if (index < out.length) {
                out[index++] = ' '
                out[index++] = ' '
            }
        } else if (text[index] === '"' || text[index] === "'") {
            // A quote opens a literal: step over it so a `//` or `/*` inside is not a comment.
            const quote = text[index]
            index += 1
            while (index < out.length && text[index] !== quote) {
                if (text[index] === '\\') index += 1
                index += 1
            }
            if (index < out.length) index += 1
        } else {
            index += 1
        }
    }
    return out.join('')
}

function sourceFilesUnder(target) {
    const stat = fs.statSync(target)
    if (stat.isFile()) return SOURCE.test(target) ? [target] : []
    const files = []
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (entry.name.startsWith('.') || entry.name === 'node_modules' || entry.name === 'build') {
                continue
            }
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) walk(full)
            else if (SOURCE.test(entry.name)) files.push(full)
        }
    }
    walk(target)
    return files
}

const shown = (file) => path.relative(ROOT, file).split(path.sep).join('/')

/** Whether `file` is a definition or a registry, by the rule in the file comment. */
function isHome(file) {
    const relative = shown(file)
    if (relative.startsWith(HOME_TREE)) return true
    if (HOME_FILES.has(relative)) return true
    return HOME_NAME.test(path.basename(file))
}

const argv = process.argv.slice(2)
const enforce = argv.includes('--enforce')
const paths = []
for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index]
    if (arg === '--path') {
        const value = argv[index + 1]
        if (!value) {
            console.error(`${NAME}: --path needs a file or a directory`)
            process.exit(2)
        }
        paths.push(path.resolve(value))
        index += 1
    } else if (arg !== '--enforce' && !arg.startsWith('--')) {
        console.error(`${NAME}: unknown argument '${arg}'`)
        process.exit(2)
    }
}

const targets = paths.length > 0 ? paths : SCANNED.map((name) => path.join(ROOT, name))
const scanned = targets.flatMap((target) => sourceFilesUnder(target))
if (scanned.length === 0) {
    console.error(
        `${NAME}: ${targets.join(', ')} holds no C++ source, so this check matched nothing and ` +
            'proves nothing',
    )
    process.exit(2)
}

const findings = []
for (const file of scanned) {
    if (isHome(file)) continue
    const text = stripComments(fs.readFileSync(file, 'utf8'))
    for (const match of text.matchAll(URI_LITERAL)) {
        findings.push({
            file,
            line: text.slice(0, match.index).split('\n').length,
            uri: `"${match[1]}"`,
        })
    }
}
findings.sort(
    (left, right) =>
        left.file.localeCompare(right.file) || left.line - right.line || left.uri.localeCompare(right.uri),
)

for (const finding of findings) {
    console.log(
        `${shown(finding.file)}:${finding.line}: ${finding.uri} -- operator URI literal outside a ` +
            'definition or a registry',
    )
}

const files = new Set(findings.map((finding) => shown(finding.file)))
console.log(
    `${NAME}: ${findings.length} operator URI literal(s) outside a definition or a registry, in ` +
        `${files.size} file(s) of ${scanned.length} source file(s) scanned`,
)
console.log(
    `${NAME}: home is ${HOME_TREE}**, ${[...HOME_FILES].join(', ')}, and any *traits* / *registry* ` +
        'file (see the file comment)',
)
if (findings.length === 0) {
    console.log(`${NAME}: every operator URI is spelled where the operator is defined`)
}
if (enforce && findings.length > 0) {
    console.error(
        `${NAME}: a business file spells an operator's URI: the URI is the operator's identity and ` +
            'it is owned by the table and the declarations that fill it ' +
            '(the target architecture, section 7)',
    )
    process.exit(1)
}
process.exit(0)