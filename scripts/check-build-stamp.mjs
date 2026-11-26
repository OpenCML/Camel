#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The build stamp's two directions, measured without a compiler:
 *
 *   node scripts/check-build-stamp.mjs
 *
 * `BUILD_FOOTPRINT` is the only name a reading carries for the binary that produced it, so it is
 * worth exactly what can be compared with it. It used to be minted from the clock and stored in
 * `build/footprint.json`, which made it a name for the *moment a configure happened*: the same
 * content stamped one way in a checkout that had been rebuilt and another way in a checkout that had
 * never seen the first build, while nothing in the string named the content at all. Two runs could
 * therefore carry stamps that were equal without saying "the same build", or unequal without saying
 * "different content".
 *
 * This script states the properties instead, on a tree small enough to write here, one line each:
 *
 *   * the same build stamps the same -- twice in one checkout, and once again with no pin to reuse,
 *     which is the reading a lane quotes when it runs a plan again after a configure;
 *   * a different build stamps differently: one byte of a file the build reads moves the tag, and so
 *     does another configuration or another option set, since the tag is the build key (all three
 *     hashed together) rather than the source hash alone.
 *
 * One more line is the negative control: a file the build does not read does not move the stamp, or
 * every document edit would recompile the two translation units that carry it into the binary.
 *
 * Everything happens under `os.tmpdir()` and is deleted; nothing here reads or writes the checkout,
 * so the check answers in about a second and can run on any machine, not only the build host.
 */
import fs from 'fs'
import os from 'os'
import path from 'path'
import { pinnedFootprint, sourceFingerprint } from './common.js'

// The paths the build reads, one per fingerprint root that a one-byte change can reach, plus the
// two single files at the repository root. `sourceFingerprint` walks these by name, so a root that
// silently drops out of `BUILD_INFO_ROOTS` is a red here rather than a stamp that stops moving.
const READ = {
    'src/kernel/a.cpp': 'int a() { return 1; }\n',
    'include/camel/b.h': '#pragma once\n',
    'modules/m/body.cml': 'func main() => int { return 0 }\n',
    'tools/t/main.cpp': 'int main() { return 0; }\n',
    'stdlib/std.cml': '// the standard library\n',
    'third_party/p/one.txt': 'third party\n',
    'cmake/Options.cmake': 'option(CAMEL_X "" OFF)\n',
    'CMakeLists.txt': 'project(camel CXX)\n',
    'conanfile.txt': '[requires]\n',
}
// Read by a person or a test, not by the build: `docs/`, `test/`, `scripts/` and the markdown files
// are outside `BUILD_INFO_ROOTS` on purpose (`scripts/common.js` says why), and a stamp that moved
// on them would recompile the stamp units for a comment.
const UNREAD = {
    'docs/notes.md': '# notes\n',
    'test/cases/x.cml': 'func main() => int { return 0 }\n',
}

const problems = []
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : `: ${detail}`}`)
    if (!ok) problems.push(`${label}: ${detail}`)
}

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-stamp-'))
function write(rel, text) {
    const full = path.join(root, rel)
    fs.mkdirSync(path.dirname(full), { recursive: true })
    fs.writeFileSync(full, text)
}
/** Add one byte to a file and put it back, so every measurement starts from the same tree. */
function withExtraByte(rel, measure) {
    const full = path.join(root, rel)
    const original = fs.readFileSync(full)
    fs.writeFileSync(full, Buffer.concat([original, Buffer.from('\n')]))
    try {
        return measure()
    } finally {
        fs.writeFileSync(full, original)
    }
}

try {
    for (const [rel, text] of Object.entries({ ...READ, ...UNREAD })) write(rel, text)

    // --- the fingerprint --------------------------------------------------------------------
    const first = sourceFingerprint(root)
    check('the fingerprint covers the files the build reads', first.files === Object.keys(READ).length,
        `${first.files} file(s), expected ${Object.keys(READ).length}`)
    check('the same content fingerprints the same', sourceFingerprint(root).hash === first.hash,
        `${sourceFingerprint(root).hash} vs ${first.hash}`)

    for (const rel of Object.keys(READ)) {
        const moved = withExtraByte(rel, () => sourceFingerprint(root).hash) !== first.hash
        check(`one byte in ${rel} moves the fingerprint`, moved, 'the fingerprint ignored the file')
    }
    for (const rel of Object.keys(UNREAD)) {
        const moved = withExtraByte(rel, () => sourceFingerprint(root).hash) !== first.hash
        check(`one byte in ${rel} leaves the fingerprint alone`, !moved,
            'the file is not read by the build, so a stamp that moves on it recompiles for nothing')
    }

    // --- the stamp --------------------------------------------------------------------------
    const gitVersion = 'abc1234'
    const stamp = (optionFlags = '-DCAMEL_X=OFF') => pinnedFootprint('Release', optionFlags, gitVersion, root)
    const stamped = stamp()
    const contentTag = (value) => value.split('_')[0]
    check('the stamp tags the build with 12 hex digits of its content', /^[0-9a-f]{12}$/.test(contentTag(stamped)),
        `${stamped} does not begin with a 12-digit content tag`)

    // A second configure of the same build in the same checkout: the pin is reused.
    check('the same build stamps the same on a reconfigure', stamp() === stamped, `${stamp()} vs ${stamped}`)

    // And in a checkout that has never seen the first build: no `build/`, no pin. The wait is what
    // makes this a measurement rather than a coin flip -- a stamp minted from the clock is stable
    // within one second, so only a second boundary can show that it is the clock and not the build.
    fs.rmSync(path.join(root, 'build'), { recursive: true, force: true })
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 1100)
    const fresh = stamp()
    check('the same build stamps the same in a fresh configure', fresh === stamped, `${fresh} vs ${stamped}`)

    // One byte of build-read content: a different build, and the string says which content it is --
    // the tag before the git marker, so two stamps can be compared without the tree that made them.
    // No wait here on purpose: a wall clock at second resolution is the case that made two different
    // contents share a stamp, and this line is exactly that case.
    const changedRel = 'stdlib/std.cml'
    const changed = withExtraByte(changedRel, () => stamp())
    check(`one byte in ${changedRel} stamps differently`, changed !== stamped, `both are ${stamped}`)
    check('and the two stamps name different content', contentTag(changed) !== contentTag(stamped),
        `${changed} vs ${stamped}`)

    // The configuration and the option flags are part of the build too: two installs built from one
    // tree with different `-D` values carry different binaries, so they may not share a name. This
    // is the property the pin's key had and a stamp made from the source hash alone would have lost.
    const otherOptions = stamp('-DCAMEL_X=ON')
    check('a different option set stamps differently', otherOptions !== stamped, `both are ${stamped}`)
    const otherConfig = pinnedFootprint('Debug', '-DCAMEL_X=OFF', gitVersion, root)
    check('a different configuration stamps differently', otherConfig !== stamped, `both are ${stamped}`)
} finally {
    fs.rmSync(root, { recursive: true, force: true })
}

if (problems.length > 0) {
    console.error(problems.join('\n'))
    process.exit(1)
}
console.log('check-build-stamp: the same content stamps the same, and one byte moves it')