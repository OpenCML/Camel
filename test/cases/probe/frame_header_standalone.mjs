#!/usr/bin/env node
/**
 * A frame's header on its own: one translation unit that includes nothing but
 * `camel/core/context/frame.h` must compile, and the header's closure must not name
 * `camel/runtime/graph.h` any more.
 *
 * Why it is needed: `frame.h` used to include the graph's header only so that the frame could read
 * five values off the graph. Those values now live in `camel/runtime/frame_layout.h`
 * (`FrameLayout`), and the claim "the frame's header no longer needs the graph's definition" is
 * exactly what this reading takes: a TU with one include, built with the flags the build itself
 * uses for `frame.cpp` (the compiler and the whole flag line come from
 * `build/compile_commands.json`, so this cannot drift from the build by a flag someone forgot to
 * copy).
 *
 * Two readings from the same header, and they must differ:
 *   · the standalone unit compiles -- exit 0;
 *   · a unit that calls the removed `Frame::hasRuntimeGraph` is rejected -- exit non-zero, and the
 *     diagnostics name it. One green and one red from the same header is what says the check is
 *     reading the header and not only the compiler.
 *
 * And one reading of the closure, taken by the compiler itself: `-M` on the standalone unit prints
 * every header it read, and `camel/runtime/graph.h` is not among them. The frame's header could
 * still compile while forwarding the graph's, so the compile alone would not answer the question.
 *
 * A reading that cannot be taken is not a green one: no database, no compile line for `frame.cpp`,
 * or a compiler that is not there exits 2.
 *
 * Run: node frame_header_standalone.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import { spawnSync } from 'node:child_process'

const ROOT = process.cwd()
const DB = path.join(ROOT, 'build', 'compile_commands.json')

function refuse(why) {
    console.error(`cannot take the reading: ${why}`)
    process.exit(2)
}

if (!fs.existsSync(DB)) refuse(`no compile database at ${DB}`)
const database = JSON.parse(fs.readFileSync(DB, 'utf8'))
const wanted = 'src/kernel/core/context/frame.cpp'
const entry = database.find((e) => path.resolve(ROOT, e.directory ?? '', e.file) === path.join(ROOT, wanted))
if (entry === undefined) refuse(`the database has no compile line for ${wanted}`)

/// The command as an argument list: CMake writes both forms, and the array form needs no unquoting.
function argumentList(compileEntry) {
    if (Array.isArray(compileEntry.arguments)) return [...compileEntry.arguments]
    const words = []
    let current = ''
    let quoted = false
    for (const ch of compileEntry.command) {
        if (ch === '"') {
            quoted = !quoted
        } else if (/\s/.test(ch) && !quoted) {
            if (current !== '') words.push(current)
            current = ''
        } else {
            current += ch
        }
    }
    if (current !== '') words.push(current)
    return words
}

/// The build's own line, minus the parts that produce an object file: what is left is the flags.
const words = argumentList(entry)
const compiler = words[0]
const flags = []
for (let i = 1; i < words.length; ++i) {
    const word = words[i]
    if (word === '-o' || word === '-MF' || word === '-MT' || word === '-MQ') {
        ++i // and its value
        continue
    }
    if (word === '-c' || word === '-MD' || word === '-MMD') continue
    if (path.resolve(entry.directory ?? ROOT, word) === path.join(ROOT, wanted)) continue
    if (word === wanted) continue
    flags.push(word)
}

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-frame-header-'))
const standalone = path.join(temporary, 'frame_header_only.cpp')
fs.writeFileSync(
    standalone,
    [
        '// The one include this reading is about.',
        '#include "camel/core/context/frame.h"',
        '',
        '// A use that needs the class, not a forward declaration.',
        'size_t camel_frame_first_word_offset() {',
        '    return camel::core::context::Frame::layoutOffset();',
        '}',
        '',
    ].join('\n'))

const deleted = path.join(temporary, 'frame_deleted_accessor.cpp')
fs.writeFileSync(
    deleted,
    [
        '#include "camel/core/context/frame.h"',
        '',
        'bool camel_frame_has_runtime_graph(camel::core::context::Frame &frame) {',
        '    return frame.hasRuntimeGraph();',
        '}',
        '',
    ].join('\n'))

function run(args) {
    const command = [compiler, ...flags, ...args]
    const result = spawnSync(compiler, [...flags, ...args], { encoding: 'utf8' })
    return { command, ...result }
}

function print(result) {
    console.log(`$ ${result.command.join(' ')}`)
    const said = `${result.stdout ?? ''}${result.stderr ?? ''}`.trim()
    if (said !== '') console.log(said)
}

let problems = 0
function check(what, ok, detail) {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${what}${detail === undefined ? '' : `: ${detail}`}`)
    if (!ok) ++problems
}

// ① the standalone unit compiles
const compiled = run(['-fsyntax-only', standalone])
print(compiled)
check('the header stands alone', compiled.status === 0, `exit ${compiled.status}`)

// ② the compiler's own closure for that unit names no graph header
const dependencies = run(['-M', standalone])
const closure = (dependencies.stdout ?? '').split(/\s+/).filter((word) => word.endsWith('.h'))
const graphHeader = closure.filter((header) => header.endsWith('runtime/graph.h'))
check(
    'the closure reaches no runtime/graph.h',
    dependencies.status === 0 && graphHeader.length === 0,
    `${closure.length} header(s) read, ${graphHeader.length} of them runtime/graph.h`)

// ③ the removed accessor is rejected by the same header
const removed = run(['-fsyntax-only', deleted])
print(removed)
const said = `${removed.stdout ?? ''}${removed.stderr ?? ''}`
check(
    'the removed accessor is rejected',
    removed.status !== 0 && said.includes('hasRuntimeGraph'),
    `exit ${removed.status}`)

fs.rmSync(temporary, { recursive: true, force: true })
console.log(`PASS frame header standalone: ${problems === 0 ? 'all readings taken' : `${problems} reading(s) failed`}`)
process.exit(problems === 0 ? 0 : 1)