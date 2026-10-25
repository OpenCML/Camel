#!/usr/bin/env node
/**
 * `unloadable.cmo` is a fixture, not a compiled module.
 *
 * The module plan needs a file the loader finds and refuses; a text file does that on every
 * platform (`dlopen`: invalid ELF header; `LoadLibraryW`: not a valid Win32 application), so the
 * fixture must stay text: readable in review, and accepted by no loader. Every compiled object
 * (ELF, Mach-O, PE) contains NUL bytes, so committing one in the fixture's place turns this case
 * red -- the loader-refusal case must not quietly start exercising a real module.
 *
 * Red direction: `cp out/latest/stdlib/statistics.cmo test/cases/modules/pass_not_found/unloadable.cmo`
 * fails here with "the file contains a NUL byte", before the plan can pass on a loadable file.
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const FIXTURE = path.join(HERE, 'unloadable.cmo')

const bytes = fs.readFileSync(FIXTURE)
const fail = (why) => {
    console.log(`FAIL unloadable.cmo is not text: ${why}`)
    process.exit(1)
}

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
console.log(`PASS unloadable.cmo is text: ${bytes.length} bytes, no NUL byte, valid UTF-8`)