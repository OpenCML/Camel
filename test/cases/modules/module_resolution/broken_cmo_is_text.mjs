#!/usr/bin/env node
/**
 * The two `.cmo` fixtures of this directory are fixtures, not compiled modules.
 *
 * Each plan needs a file the loader finds and refuses; a text file does that on every platform
 * (`dlopen`: invalid ELF header; `LoadLibraryW`: not a valid Win32 application), so the fixtures
 * must stay text: readable in review, and accepted by no loader. Every compiled object (ELF,
 * Mach-O, PE) contains NUL bytes, so committing one in a fixture's place turns the cases red --
 * the "found and refused" cases must not quietly start exercising a real module.
 *
 * Red direction: `cp out/latest/stdlib/statistics.cmo <fixture>` fails here with "the file
 * contains a NUL byte", before a plan can pass on a loadable file.
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const FIXTURES = [
    path.join(HERE, 'cmo_hit_stops', 'level', 'foo.cmo'),
    path.join(HERE, 'only_cml_and_cmo', 'cmo_beside_cml', 'thing.cmo'),
]

for (const fixture of FIXTURES) {
    const where = path.relative(HERE, fixture).split(path.sep).join('/')
    const fail = (why) => {
        console.log(`FAIL ${where} is not text: ${why}`)
        process.exit(1)
    }
    const bytes = fs.readFileSync(fixture)
    if (bytes.length === 0) {
        fail('the file is empty')
    }
    if (bytes.includes(0)) {
        fail('the file contains a NUL byte, which a compiled object does')
    }
    try {
        new TextDecoder('utf-8', { fatal: true }).decode(bytes)
    } catch {
        fail('the file is not valid UTF-8')
    }
    console.log(`PASS ${where} is text: ${bytes.length} bytes, no NUL byte, valid UTF-8`)
}