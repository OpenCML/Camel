// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The second reader of `time_sensitive`, against the file format and against the tree.
 *
 * `time-sensitive-reader.mjs` exists so the gate's reading of a plan's `time_sensitive` declaration
 * can be compared with something. A second reader that is itself unverified is not a comparison --
 * it is one more opinion -- so this file pins what it answers on the shapes a plan can write, and
 * holds it to the whole plan tree.
 *
 * Three readings, and each one fails in its own direction:
 *
 *   the boundary shapes        what the reader says about one declaration: an empty value, a quoted
 *                              one, mixed case, whitespace, a repeated key, a key inside a table, a
 *                              key inside a string. These are the shapes where the gate and this
 *                              reader are known to disagree, so the answers here are what keeps the
 *                              disagreement *about the format* instead of about the vocabulary.
 *   the top-level shape        every plan file's top-level keys and table names, as this reader sees
 *                              them, against `toml-lite.mjs` -- the reader the runner loads plans
 *                              with. A scanner that silently folds a line of a triple-quoted string
 *                              into its key list reds here, one file at a time, before it can make
 *                              the cross-check say "the gate and the reader agree" for the wrong
 *                              reason.
 *   the declaration, per file   the `time_sensitive` answer against the runner's own document, on
 *                              every plan file the runner's reader accepts. This is the one answer
 *                              the gate acts on, so it is the one answer worth comparing with the
 *                              program that decides whether the plan runs at all.
 *
 * `toml-lite.mjs` is the oracle here and not a dependency of the reader: the reader imports nothing
 * from this tree, which is what makes it a second reading rather than a wrapper.
 */
import { test } from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'

import { readTimeSensitiveDeclaration, scanDocument } from './time-sensitive-reader.mjs'
import { parseTomlFile, parseTomlString } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')

// Every `*.plan.toml` in the tree, in one order, skipping the directories that hold another tree's
// copy of the same names.
function planFiles(dir = ROOT, out = []) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (entry.name === '.git' || entry.name === 'node_modules' || entry.name === 'out') continue
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) planFiles(full, out)
        else if (entry.name.endsWith('.plan.toml')) out.push(full)
    }
    return out.sort()
}

const read = (text) => readTimeSensitiveDeclaration(text)

// One row per shape: what the declaration is, what this reader must say about it, and why. `gate` is
// the only thing the gate reads; `state` and `problem` are what this reader knows beyond it.
const SHAPES = [
    {
        name: 'empty value',
        text: 'id = "x"\ntime_sensitive =\n',
        gate: false,
        state: 'invalid',
        problem: /does not read as a boolean/,
    },
    {
        name: 'quoted value: a string is not a boolean',
        text: 'time_sensitive = "true"\n',
        gate: false,
        state: 'invalid',
        problem: /does not read as a boolean/,
    },
    {
        name: 'mixed case: TOML spells the boolean lowercase only',
        text: 'time_sensitive = True\n',
        gate: false,
        state: 'invalid',
        problem: /does not read as a boolean/,
    },
    {
        name: 'surrounding and inner whitespace',
        text: '   time_sensitive   =   true   \n',
        gate: true,
        state: 'true',
        problem: null,
    },
    {
        name: 'a repeated key',
        text: 'time_sensitive = true\ntime_sensitive = true\n',
        gate: true,
        state: 'true',
        problem: /written 2 times/,
    },
    {
        name: 'a line starting with [ inside a triple-quoted string',
        text: 'desc = """\n[note]\n"""\ntime_sensitive = true\n',
        gate: true,
        state: 'true',
        problem: null,
    },
    {
        name: 'inside a table',
        text: '[defaults]\ntime_sensitive = true\n',
        gate: false,
        state: 'absent',
        problem: /inside \[defaults\]/,
    },
    {
        name: 'a quoted key is the same key',
        text: '"time_sensitive" = true\n',
        gate: true,
        state: 'true',
        problem: null,
    },
    {
        name: 'the words inside a string are not a declaration',
        text: 'desc = """\ntime_sensitive = true\n"""\n',
        gate: false,
        state: 'absent',
        problem: null,
    },
    {
        name: 'a declaration after a nested array',
        text: 'ids = [\n  [1, 2],\n]\ntime_sensitive = true\n',
        gate: true,
        state: 'true',
        problem: null,
    },
    {
        name: 'the plain declaration',
        text: 'time_sensitive = true\n',
        gate: true,
        state: 'true',
        problem: null,
    },
    {
        name: 'the other boolean',
        text: 'time_sensitive = false\n',
        gate: false,
        state: 'false',
        problem: null,
    },
    {
        name: 'no declaration',
        text: 'id = "x"\n',
        gate: false,
        state: 'absent',
        problem: null,
    },
    {
        name: 'a comment after the value',
        text: 'time_sensitive = true # durations\n',
        gate: true,
        state: 'true',
        problem: null,
    },
    {
        name: 'no spaces around the =',
        text: 'time_sensitive=true\n',
        gate: true,
        state: 'true',
        problem: null,
    },
]

test('a declaration is read the way the file format reads it', () => {
    for (const shape of SHAPES) {
        const reading = read(shape.text)
        assert.equal(reading.gate, shape.gate, `${shape.name}: gate`)
        assert.equal(reading.state, shape.state, `${shape.name}: state`)
        if (shape.problem === null) {
            assert.equal(reading.problem, null, `${shape.name}: problem`)
        } else {
            assert.match(reading.problem ?? '', shape.problem, `${shape.name}: problem`)
        }
    }
})

test('the line a declaration is reported at is the line it is written on', () => {
    assert.equal(read('time_sensitive = true\n').line, 1)
    assert.equal(read('id = "x"\n\n# a comment\ntime_sensitive = true\n').line, 4)
    // Inside a multi-line string the line numbers keep counting: a reader that lost count there
    // would name the wrong place for every declaration that follows one.
    assert.equal(read('desc = """\na\nb\n"""\ntime_sensitive = true\n').line, 5)
    assert.equal(read('id = "x"\n').line, 0, 'no declaration has no line')
})

test("every plan file's top level is the one the runner's reader sees", () => {
    const files = planFiles()
    assert.ok(files.length > 0, 'the tree has no plan files: this reading examined nothing')
    const wrong = []
    for (const file of files) {
        const text = fs.readFileSync(file, 'utf8')
        const scanned = scanDocument(text)
        const ours = new Set([...scanned.top.map((entry) => entry.key), ...scanned.tables.map((table) => table.name)])
        const threads = new Set(Object.keys(parseTomlString(text)))
        const missing = [...threads].filter((key) => !ours.has(key))
        const extra = [...ours].filter((key) => !threads.has(key))
        if (missing.length > 0 || extra.length > 0) {
            wrong.push(`${path.relative(ROOT, file)}: only the runner sees ${JSON.stringify(missing)}, only this reader sees ${JSON.stringify(extra)}`)
        }
        if (scanned.problems.length > 0) {
            wrong.push(`${path.relative(ROOT, file)}: ${scanned.problems.join('; ')}`)
        }
    }
    assert.deepEqual(wrong, [], `${wrong.length} of ${files.length} plan file(s) disagree about the top level`)
})

test('the declaration agrees with the runner on every plan file it can read', () => {
    const files = planFiles()
    const disagreements = []
    let accepted = 0
    for (const file of files) {
        let doc
        try {
            doc = parseTomlFile(file)
        } catch {
            // A file the runner refuses is not a file it can have a reading about; the refusal is
            // its own reading and this test is not that one.
            continue
        }
        accepted += 1
        const gate = read(fs.readFileSync(file, 'utf8')).gate
        const runner = doc.time_sensitive === true
        if (gate !== runner) {
            disagreements.push(`${path.relative(ROOT, file)}: reader gate=${gate}, runner time_sensitive=${JSON.stringify(doc.time_sensitive)}`)
        }
    }
    assert.ok(accepted > 0, 'the runner accepted no plan file: this reading examined nothing')
    assert.deepEqual(disagreements, [])
})

test('the tree declares the plan whose header says why, and the reader finds it', () => {
    // The anchor: without it, a reader that answered "false" to everything would pass every
    // agreement above. It names the plan and does not pin the population -- a change that marks
    // another plan time sensitive is a change to which runs own the host, and the gate already
    // prints that list on every run (`[camel-remote] time sensitive: ...`), so nothing here has to
    // red for it.
    const declaring = planFiles()
        .map((file) => path.relative(ROOT, file).split(path.sep).join('/'))
        .filter((rel) => read(fs.readFileSync(path.join(ROOT, rel), 'utf8')).gate)
    assert.ok(declaring.includes('test/plans/perf/fib.plan.toml'), `the declaring plan is gone: ${declaring.join(', ') || 'none'}`)
    assert.ok(declaring.length >= 1, 'no plan declares itself time sensitive: this reading examined nothing')
})

test('a fixture directory written to disk reads the same as the text it was written from', () => {
    // The command-line reader (`--files-from`) is what the cross-check calls, so the path it takes
    // from the disk to the answer is checked here once, rather than trusted.
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ts-reader-'))
    try {
        for (const [index, shape] of SHAPES.entries()) {
            fs.writeFileSync(path.join(dir, `s${index}.plan.toml`), shape.text)
        }
        for (const [index, shape] of SHAPES.entries()) {
            const reading = readTimeSensitiveDeclaration(fs.readFileSync(path.join(dir, `s${index}.plan.toml`), 'utf8'))
            assert.equal(reading.gate, shape.gate, `${shape.name}: gate through the disk`)
        }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
})

test('an empty document is a reading and not an error', () => {
    // A caller that enumerates and then reads is two moments, and the answer for a document with no
    // declaration has to be "absent", never a thrown error that stops a whole tree's reading.
    const reading = readTimeSensitiveDeclaration('')
    assert.equal(reading.gate, false)
    assert.equal(reading.state, 'absent')
    assert.equal(reading.problem, null)
})