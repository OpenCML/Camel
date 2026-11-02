#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One ruleset import of `stdlib/vjp.cml`, dropped and put back.
//
// The library is `transpose(split(reinterp<jvp>(f)))`, so it imports two rulesets: `rulesets.jvp`
// for the forward walk and `rulesets.transpose` for the backward one. A program that calls `vjp`
// imports neither of them -- that is the point of the library carrying its own -- so no program's
// source can tell the two lines apart. This tool tells them apart the way a reader would: it makes
// a copy of the installed library with **one** of the two lines commented out, points `CAMEL_STD_LIB`
// at the copy, and runs the same program twice.
//
// Each half is a claim and each claim is read off a run:
//   dropped  -- the program exits 1 and the diagnostic stands on the library's own line
//               (`stdlib/vjp.cml:<line>:`), naming what is missing. The expected words differ per
//               line: without `rulesets.jvp` the forward walk has no rule for `:op/mul_d`, without
//               `rulesets.transpose` the backward walk has no family for `:op/add_d`.
//   restored -- the same program exits 0 and prints two 27.0 readings and the cotangent tuple
//               (27.0,) the pullback of a one-parameter function answers (author's ruling of
//               2026-11-01 14:30: the pullback always answers one cotangent per parameter).
//
// So this is a case per import line, and it goes red when the line it guards is gone from the
// library (nothing to drop, and the half that matters is the restored one), when the run stops
// somewhere else, or when a diagnostic no longer points into the library.
//
// Usage: node test/tools/check-vjp-ruleset.mjs --drop rulesets.jvp
//        node test/tools/check-vjp-ruleset.mjs --drop rulesets.transpose

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const library = path.join(home, 'stdlib', 'vjp.cml')
const program = path.join(ROOT, 'test', 'cases', 'compile', 'transforms', 'vjp_carries_its_ruleset.cml')

// What the walk says when the ruleset is not loaded. The first line is the diagnostic that names
// the missing rule; the second is the walk's own reading, which says which family was not found.
const EXPECTED = {
    'rulesets.jvp': {
        words: 'jvp has no forward rule for `:op/mul_d`',
        raised: 'note: raised here',
        reading: null,
        what: 'the forward walk asks for a rule it cannot find',
    },
    'rulesets.transpose': {
        words: 'transpose has no transpose for this function',
        raised: null,
        reading: 'skip=rule uri=:op/add_d reason=no-such-family',
        what: 'the backward walk asks for a family that is not loaded',
    },
}

function say(lines) {
    for (const line of lines) console.log(line)
}

const dropAt = process.argv.indexOf('--drop')
const drop = dropAt === -1 ? null : process.argv[dropAt + 1]
if (drop === null || !(drop in EXPECTED)) {
    say([
        'usage: node test/tools/check-vjp-ruleset.mjs --drop rulesets.jvp|rulesets.transpose',
        `  (got ${drop === null ? 'no --drop' : `--drop ${drop}`})`,
    ])
    process.exit(2)
}
const expected = EXPECTED[drop]

// 1. The line has to be there, or this case has nothing to read. That is also what makes the case
//    red when the library stops importing the ruleset it composes: the guard and the reading are
//    the same file.
const source = fs.readFileSync(library, 'utf8')
const lines = source.split('\n')
const importLine = `import ${drop}`
const at = lines.findIndex((line) => line.trim() === importLine)
if (at === -1) {
    say([
        `the library does not import ${drop}, so there is nothing to drop:`,
        `  ${library}: no line reads \`${importLine}\``,
        '  this case reads that line\'s weight, and the library has stopped carrying it',
    ])
    process.exit(1)
}
const importAt = at + 1

// 2. A copy of the installed library with that one line commented out. The comment keeps the line
//    count, so the library's own line numbers in the diagnostics are the ones a reader can open.
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-vjp-'))
const copyRoot = path.join(tmp, 'stdlib')
fs.cpSync(path.join(home, 'stdlib'), copyRoot, { recursive: true })
const copyLines = lines.slice()
copyLines[at] = `// ${importLine} -- dropped on purpose by test/tools/check-vjp-ruleset.mjs`
fs.writeFileSync(path.join(copyRoot, 'vjp.cml'), copyLines.join('\n'))

// The environment the compiler needs. An install built with a newer toolchain wants a newer
// `libstdc++` than the host's, and the test runner tells its children where to find one through
// `CAMEL_TEST_EXTRA_LIB_DIRS` (`:`-separated) rather than through `LD_LIBRARY_PATH`: joining it here
// is what `scripts/stdlib-reference.mjs` does for the same reason, and without it a run under the
// runner fails to start the binary at all and this case would report a wall that is not there.
function installEnvironment(extra = {}) {
    // `text` is the rich form: the one-line `short` form a run under the test runner gets
    // (`CAMEL_ERROR_FORMAT=short`) keeps the message and drops the `note: raised here` and the
    // `-->` locations, which are the two things this case reads. Asking for the form by name is
    // asking for the reading; it does not change what the walk decided.
    const env = { ...process.env, ...extra, CAMEL_HOME: home, CAMEL_ERROR_FORMAT: 'text' }
    const dirs = (process.env.CAMEL_TEST_EXTRA_LIB_DIRS || '').split(':').filter(Boolean)
    if (dirs.length > 0 && process.platform !== 'win32') {
        env.LD_LIBRARY_PATH = [...dirs, env.LD_LIBRARY_PATH].filter(Boolean).join(':')
    }
    return env
}

function run(extra) {
    const proc = spawnSync(camel, [program, 'std::nvm'], {
        cwd: ROOT,
        encoding: 'utf8',
        env: installEnvironment(extra),
        timeout: 120000,
    })
    return {
        status: proc.status,
        text: `${proc.stdout || ''}${proc.stderr || ''}`.replace(/\r/g, ''),
    }
}

const say1 = []
say1.push(`the library imports ${drop} (stdlib/vjp.cml:${importAt})`)
say1.push(`dropping that one line in a copy of the library, and running the same program against the copy:`)

const without = run({ CAMEL_STD_LIB: copyRoot })
const diagnostics = without.text.split('\n').filter((line) => /error\[|^note:|-->\s|^TRANSPOSE\s/.test(line))
const onLibrary = diagnostics.some((line) => /-->\s.*stdlib\/vjp\.cml:[0-9]+:[0-9]+/.test(line))
const named = diagnostics.some((line) => line.includes(expected.words))
const reading = expected.reading === null || diagnostics.some((line) => line.includes(expected.reading))
const raised = expected.raised === null || diagnostics.some((line) => line.includes(expected.raised))

say1.push(`  it exits ${without.status}, and its diagnostics are:`)
if (diagnostics.length === 0) {
    // A run that printed none of the shapes this case reads is a broken run, not a quiet one:
    // print what it did say, so the reader is not left with an empty list.
    for (const line of without.text.split('\n').slice(0, 3)) say1.push(`    ${line.trim()}`)
}
for (const line of diagnostics.slice(0, 6)) say1.push(`    ${line.trim()}`)

// 3. The other half: the same program, the library untouched. This is the half that goes red when
//    the import line is missing from the library, because the drop then proves nothing.
const restored = run({})
const numbers = restored.text.split('\n').filter((line) => /^27\.0$/.test(line.trim()))
const cotangents = restored.text
    .split('\n')
    .filter((line) => /^\(27\.0\)$/.test(line.trim()))
say1.push(
    `putting the line back, the same program exits ${restored.status} and prints ` +
        `${numbers.length === 0 ? 'no reading' : `${numbers.length} x 27.0`} + ${cotangents.length} x (27.0)`,
)

const failed = []
if (without.status === 0) {
    failed.push(`dropping ${drop} did not stop the program (exit 0): the line is not what makes it work`)
}
if (!named) failed.push(`the diagnostic does not say \`${expected.words}\``)
if (!reading) failed.push(`the walk's own reading does not say \`${expected.reading}\``)
if (!raised) failed.push(`the diagnostic does not carry \`${expected.raised}\``)
if (!onLibrary) {
    failed.push('the diagnostic does not stand on a `stdlib/vjp.cml:<line>:<column>`')
}
if (restored.status !== 0) failed.push(`with the line back the program exits ${restored.status}, not 0`)
if (numbers.length !== 2) failed.push(`with the line back the program prints ${numbers.length} x 27.0, not 2`)
if (cotangents.length !== 1) failed.push(`with the line back the program prints ${cotangents.length} x (27.0,), not 1`)

say(say1)
if (failed.length > 0) {
    say(['', `${drop} is not guarded the way this case claims:`])
    for (const line of failed) say([`  ${line}`])
    fs.rmSync(tmp, { recursive: true, force: true })
    process.exit(1)
}
say([
    `  ${drop} is load-bearing: dropped, ${expected.what} on the library's own line;`,
    '  back, the library answers.',
])
fs.rmSync(tmp, { recursive: true, force: true })
process.exit(0)
