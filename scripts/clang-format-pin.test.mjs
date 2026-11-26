// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { test } from 'node:test'
import { formatWithPin, majorOf } from './clang-format-pin.js'

// A stub formatter: reports `version` for --version and appends a marker line for -i.
function makeStub(dir, version) {
    const js = path.join(dir, 'stub.mjs')
    fs.writeFileSync(
        js,
        `import fs from 'fs'
const a = process.argv.slice(2)
if (a[0] === '--version') console.log('clang-format version ${version}')
else if (a[0] === '-i') fs.appendFileSync(a[1], '// formatted' + String.fromCharCode(10))
`
    )
    const node = process.execPath
    if (process.platform === 'win32') {
        const bin = path.join(dir, `stub-${version.split('.')[0]}.cmd`)
        fs.writeFileSync(bin, `@"${node}" "${js}" %*\r\n`)
        return bin
    }
    const bin = path.join(dir, `stub-${version.split('.')[0]}`)
    fs.writeFileSync(bin, `#!/bin/sh\nexec "${node}" "${js}" "$@"\n`)
    fs.chmodSync(bin, 0o755)
    return bin
}

function run(bin, pin) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fmtpin-'))
    const file = path.join(dir, 'a.cpp')
    fs.writeFileSync(file, 'int x;\n')
    const lines = []
    const formatted = formatWithPin([file], {
        bin: bin ?? path.join(dir, 'no-such-formatter'),
        pin,
        log: (l) => lines.push(l),
        logError: (l) => lines.push(l),
    })
    return { formatted, lines, content: fs.readFileSync(file, 'utf-8'), dir }
}

test('majorOf reads the major version from the banner', () => {
    assert.equal(majorOf('clang-format version 20.1.8'), 20)
    assert.equal(majorOf('Ubuntu clang-format version 18.1.3 (1ubuntu1)'), 18)
    assert.equal(majorOf('not a version'), null)
})

test('a formatter of the pinned major formats', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fmtstub-'))
    const r = run(makeStub(dir, '20.1.8'), 20)
    assert.equal(r.formatted, true)
    assert.equal(r.content, 'int x;\n// formatted\n')
    assert.match(r.lines[0], /^Formatted: /)
})

test('a formatter of another major leaves files untouched and says so', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fmtstub-'))
    const r = run(makeStub(dir, '22.1.0'), 20)
    assert.equal(r.formatted, false)
    assert.equal(r.content, 'int x;\n')
    assert.equal(r.lines.length, 1)
    assert.match(r.lines[0], /found clang-format 22, the repository pins clang-format 20.*left as they are/)
})

test('no formatter leaves files untouched and says so', () => {
    const r = run(null, 20)
    assert.equal(r.formatted, false)
    assert.equal(r.content, 'int x;\n')
    assert.equal(r.lines.length, 1)
    assert.match(r.lines[0], /found no usable clang-format, the repository pins clang-format 20/)
})
