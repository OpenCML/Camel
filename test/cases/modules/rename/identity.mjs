// The graph identity of a module rename (handover H114): `std::jgir` of a sample program is
// compared after the module name is replaced in every string of the document, so the graph may
// differ from the graph before the rename in that name and in nothing else. The expected digest
// was recorded from the same sample on `origin/develop` before the rename.
//
// The sample programs hold literals only and `std::jgir` is a translation pass: no environment
// read, hash or machine query is executed, so the graph is the same on every machine.
//
// A `verify_script_path` is a stand-alone Node program: it reads the test's JSON payload from
// its first argument and exits non-zero when an assertion fails.

import crypto from 'node:crypto'
import fs from 'node:fs'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const CASES = {
    // test name -> the old spelling, the spelling now, and the digest of the graph with the
    // module name normalized away.
    env: {
        old: 'sys',
        now: 'env',
        expected: 'e25ec979452a20c9cc3eaaa82733755c84c855ae93edf92908e5999050807eab',
    },
    process: {
        old: 'os',
        now: 'process',
        expected: '01e668a034a724c249cdbf147322958c7a425cd8dbb38a73583d50e436b0d758',
    },
    hash: {
        old: 'crypto',
        now: 'hash',
        expected: '29ae6c2ed66a698bcaeeb3113261c57b556b58cdaafa9b2968f05ce615c453a2',
    },
}

// A sweep copy is named `<sweep>:<source plan>:<test name>` (H120), so the last dot-segment is
// the test name whether this runs from its own plan (`rename.env_graph_identity`) or from a copy
// of it (`poison:modules.rename_identity:rename.env_graph_identity`).
const key = input.test_name.split('.').pop().split('_')[0]
const kase = CASES[key]
if (!kase) throw new Error(`no identity case for test ${input.test_name}`)

let document
try {
    document = JSON.parse(input.normalized.stdout)
} catch (error) {
    throw new Error(`std::jgir did not print JSON: ${error.message}`)
}

const marker = 'MODULE'
const rewrite = (value) => {
    if (typeof value === 'string') {
        // The source path is where the sample sits in this checkout; it says nothing about the
        // graph, so it is replaced with a marker too (the same digest on any machine).
        return value
            .split(input.case_path)
            .join('CASE')
            .replace(new RegExp(`\\b${kase.now}:`, 'g'), `${marker}:`)
    }
    if (Array.isArray(value)) return value.map(rewrite)
    if (value && typeof value === 'object') {
        // origin is an opaque per-build identifier for a source position, not part of the graph:
        // it is renumbered whenever any other source file changes, which would make this digest a
        // snapshot of the whole tree instead of a check on the rename.
        return Object.fromEntries(
            Object.entries(value)
                .filter(([k]) => k !== 'origin')
                .map(([k, v]) => [k, rewrite(v)]),
        )
    }
    return value
}
const normalized = JSON.stringify(rewrite(document))
const digest = crypto.createHash('sha256').update(normalized).digest('hex')

if (kase.expected === 'PENDING') {
    throw new Error(`normalized graph sha256 ${digest} (record it as the expected value)`)
}

assert(
    digest === kase.expected,
    `the graph changed beyond the module name: normalized sha256 ${digest}, expected ${kase.expected}`,
)
assert(
    input.normalized.stdout.includes(`${kase.now}:`),
    `the graph has no ${kase.now}: operator, so it does not exercise the renamed module`,
)
assert(
    !input.normalized.stdout.includes(`${kase.old}:`),
    `the graph still has ${kase.old}: operator URIs`,
)