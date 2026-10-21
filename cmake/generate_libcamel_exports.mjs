/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * Generates the .def lld-link is given for one configuration of libcamel (H36).
 *
 * The list is a build product, not a checked-in snapshot: it is the intersection between
 *   - the symbols the object files of every dependent target leave undefined, and
 *   - the symbols libcamel's own object files define,
 * plus the small manual list of exports that no build-time object can name
 * (cmake/libcamel_exports_manual.def: the standalone bridge).
 *
 * Compiling a dependent target's sources needs only the headers, so those objects already exist
 * when libcamel links. That is what lets the import set be known before the import library is
 * built, without a dependency cycle.
 *
 * Usage:
 *   node cmake/generate_libcamel_exports.mjs --nm <llvm-nm> --config <Debug|Release|...> \
 *       --core <manifest> --imports <manifest> --manual <def> --out <def>
 *
 * The manifests are written at configure time by the top-level CMakeLists.txt; they contain the
 * object paths, separated by semicolons or newlines, because one command line cannot carry them.
 */

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const IMPORT_CHUNK = 64

function fail(message) {
    console.error(`[exports] ${message}`)
    process.exit(1)
}

function parseArgs(argv) {
    const wanted = new Map([
        ['--nm', 'nm'],
        ['--config', 'config'],
        ['--core', 'core'],
        ['--imports', 'imports'],
        ['--manual', 'manual'],
        ['--out', 'out']
    ])
    const args = {}
    for (let i = 0; i < argv.length; i += 2) {
        const key = wanted.get(argv[i])
        if (!key) {
            fail(`unknown argument: ${argv[i]}`)
        }
        if (i + 1 >= argv.length) {
            fail(`missing value for ${argv[i]}`)
        }
        args[key] = argv[i + 1]
    }
    for (const key of wanted.values()) {
        if (!args[key]) {
            fail(`missing --${key}`)
        }
    }
    return args
}

/** Object paths of a manifest, one per semicolon or line. */
function readObjects(file) {
    const text = fs.readFileSync(file, 'utf8')
    return [...new Set(text.split(/[\r\n;]+/).map((s) => s.trim()).filter(Boolean))]
}

/**
 * The symbol names of a set of object files, in the portable `-P` format
 * (`name type value size`; `llvm-nm` prints a `path:` heading per file). Returns a Map from the
 * normalized symbol name to the object file that first named it, which is what makes an error
 * message actionable.
 *
 * COFF imports a `__declspec(dllimport)` declaration through a `__imp_` thunk; the export table
 * carries the undecorated name, so `__imp_` is stripped before matching.
 */
function symbolsOf(nm, objects, mode) {
    const symbols = new Map()
    const temporary = path.join(
        process.env.TEMP || process.env.TMP || '.',
        `libcamel-nm-${process.pid}-${mode.replace(/^-+/, '')}.txt`
    )
    try {
        for (let i = 0; i < objects.length; i += IMPORT_CHUNK) {
            const chunk = objects.slice(i, i + IMPORT_CHUNK)
            const fd = fs.openSync(temporary, 'w')
            const run = spawnSync(nm, ['-P', mode, '--extern-only', ...chunk], {
                stdio: ['ignore', fd, 'inherit']
            })
            fs.closeSync(fd)
            if (run.error) {
                fail(`cannot run ${nm}: ${run.error.message}`)
            }
            if (run.status !== 0) {
                fail(`${nm} exited ${run.status} on ${chunk[0]} ...`)
            }
            const text = fs.readFileSync(temporary, 'utf8')
            let object = ''
            for (const line of text.split(/\r?\n/)) {
                if (!line) {
                    continue
                }
                // A heading for one input file, printed because a chunk holds several objects.
                if (/:\s*$/.test(line) && !/^[^\s]+ [A-Za-z](?: |$)/.test(line)) {
                    object = line.replace(/:\s*$/, '')
                    continue
                }
                const match = /^(\S+) ([A-Za-z])(?: \S+)*$/.exec(line)
                if (!match) {
                    continue
                }
                const type = match[2]
                const undefined = type === 'U' || type === 'w'
                if (undefined !== (mode === '--undefined-only')) {
                    continue
                }
                let name = match[1]
                if (name.startsWith('__imp_')) {
                    name = name.slice('__imp_'.length)
                }
                if (name && !symbols.has(name)) {
                    symbols.set(name, object)
                }
            }
        }
    } finally {
        fs.rmSync(temporary, { force: true })
    }
    return symbols
}

/** The symbols of the manual list: `;` comments, a bare `EXPORTS` line, one name per line. */
function readManual(file) {
    const names = []
    for (const raw of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
        const line = raw.trim()
        if (!line || line.startsWith(';') || line.startsWith('#') || /^EXPORTS$/i.test(line)) {
            continue
        }
        names.push(line)
    }
    return names
}

function main() {
    const args = parseArgs(process.argv.slice(2))
    const coreObjects = readObjects(args.core)
    const importObjects = readObjects(args.imports)
    if (coreObjects.length === 0) {
        fail(`${args.core} names no libcamel object files`)
    }

    const defined = symbolsOf(args.nm, coreObjects, '--defined-only')
    const imported = symbolsOf(args.nm, importObjects, '--undefined-only')

    const manual = readManual(args.manual)
    const missing = manual.filter((name) => !defined.has(name))
    if (missing.length > 0) {
        fail(
            `the manual export list names ${missing.length} symbol(s) libcamel does not define in ` +
                `${args.config}:\n  ${missing.join('\n  ')}`
        )
    }

    const exports = new Set(manual)
    for (const name of imported.keys()) {
        if (defined.has(name)) {
            exports.add(name)
        }
    }

    const sorted = [...exports].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))
    const content =
        `; Generated by cmake/generate_libcamel_exports.mjs for ${args.config}. Do not edit.\n` +
        `; It is the intersection of the symbols libcamel's object files define with the symbols\n` +
        `; the modules' and tools' object files import (${sorted.length - manual.length} derived),\n` +
        `; plus ${manual.length} manual entr${manual.length === 1 ? 'y' : 'ies'} for code no object file\n` +
        `; names (${args.manual.replace(/^.*[\\/]/, '')}).\n` +
        `EXPORTS\n` +
        sorted.map((name) => `    ${name}\n`).join('')

    let previous = null
    if (fs.existsSync(args.out)) {
        previous = fs.readFileSync(args.out, 'utf8')
    }
    if (previous !== content) {
        fs.writeFileSync(args.out, content)
        console.log(
            `[exports] ${args.config}: ${sorted.length} exported symbols ` +
                `(${sorted.length - manual.length} derived from ${importObjects.length} objects, ` +
                `${manual.length} manual)`
        )
    }
}

main()