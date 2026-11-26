// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { test } from 'node:test'
import { execFileSync } from 'node:child_process'
import { changedRanges, formatWithPin, majorOf } from './clang-format-pin.js'

// A stub formatter: reports `version` for --version and appends a marker line for -i.
function makeStub(dir, version) {
    const js = path.join(dir, 'stub.mjs')
    fs.writeFileSync(
        js,
        `import fs from 'fs'
const a = process.argv.slice(2)
if (a[0] === '--version') console.log('clang-format version ${version}')
else if (a[0] === '-i') {
  fs.appendFileSync(a[a.length - 1], '// formatted' + String.fromCharCode(10))
  if (process.env.STUB_LOG) fs.appendFileSync(process.env.STUB_LOG, a.slice(1, -1).join(' ') + String.fromCharCode(10))
}
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
        rangesOf: () => [[1, 1]],
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
    const r = run(makeStub(dir, '22.1.0'), 22)
    assert.equal(r.formatted, true)
    assert.equal(r.content, 'int x;\n// formatted\n')
    assert.match(r.lines[0], /^Formatted: /)
})

test('a formatter of another major leaves files untouched and says so', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fmtstub-'))
    const r = run(makeStub(dir, '19.1.0'), 22)
    assert.equal(r.formatted, false)
    assert.equal(r.content, 'int x;\n')
    assert.equal(r.lines.length, 1)
    assert.match(r.lines[0], /found clang-format 19, the repository pins clang-format 22.*left as they are/)
})

test('no formatter leaves files untouched and says so', () => {
    const r = run(null, 22)
    assert.equal(r.formatted, false)
    assert.equal(r.content, 'int x;\n')
    assert.equal(r.lines.length, 1)
    assert.match(r.lines[0], /found no usable clang-format, the repository pins clang-format 22/)
})

test('changedRanges keeps the new-side range of each hunk and skips pure deletions', () => {
    const diff = ['@@ -2,0 +3,2 @@', '+a', '+b', '@@ -10 +12 @@', '-x', '+y', '@@ -20,3 +21,0 @@', '-p'].join('\n')
    assert.deepEqual(changedRanges(diff), [
        [3, 4],
        [12, 12],
    ])
})

// A repository in which a file is staged after an edit; returns what the stub formatter was given.
function stagedRun(oldLines, newLines) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'fmtrepo-'))
    const git = (...a) => execFileSync('git', ['-C', dir, ...a], { encoding: 'utf-8' })
    git('init', '-q')
    git('config', 'user.email', 't@example.com')
    git('config', 'user.name', 't')
    const file = path.join(dir, 'a.cpp')
    fs.writeFileSync(file, oldLines.join('\n') + '\n')
    git('add', 'a.cpp')
    git('commit', '-q', '-m', 'base')
    fs.writeFileSync(file, newLines.join('\n') + '\n')
    git('add', 'a.cpp')
    const log = path.join(dir, 'stub.log')
    process.env.STUB_LOG = log
    try {
        const bin = makeStub(dir, '22.1.0')
        const formatted = formatWithPin([file], {
            bin,
            pin: 22,
            rangesOf: () => changedRanges(git('diff', '--cached', '-U0', 'HEAD', '--', 'a.cpp')),
            log: () => {},
            logError: () => {},
        })
        return { formatted, args: fs.existsSync(log) ? fs.readFileSync(log, 'utf-8').trim().split('\n') : [] }
    } finally {
        delete process.env.STUB_LOG
    }
}

const base = Array.from({ length: 12 }, (_, i) => `int v${i};`)

test('two separate changed regions give exactly two --lines ranges', () => {
    const next = [...base]
    next[1] = 'int  changed1;'
    next[8] = 'int  changed8;'
    const r = stagedRun(base, next)
    assert.deepEqual(r.args, ['--lines=2:2 --lines=9:9'])
})

test('a pure deletion passes no range', () => {
    const next = base.filter((_, i) => i !== 5)
    const r = stagedRun(base, next)
    assert.deepEqual(r.args, [])
})

test('an unchanged file is not formatted', () => {
    const r = stagedRun(base, base)
    assert.deepEqual(r.args, [])
})
