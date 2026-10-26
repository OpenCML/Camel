// The graph identity of a module rename: `std::jgir` of a sample program is
// compared after the module name is replaced in every string of the document, so the graph may
// differ from the graph before the rename in that name and in nothing else. The expected digest
// was recorded from the same sample on `origin/develop` before the rename; recorded the
// values below again because `std::jgir` prints each operator's resources and these samples call
// `println`, whose resource was renamed from `io:stdout` to `stdout` (the module rename itself
// still changes only the module name; the samples differ from their `_old` twins in that only).
// `std::cml`'s first step made `std::jgir` print each node's source name (`let path`), so the
// digests were recorded once more for that field: deleting the node `name` entries from the
// document reproduces the previous values exactly.
//
// Recorded once more when the samples stopped declaring `module <name>`: the digest covers each
// node's span, and the samples' only change on this branch is that deleted declaration (one line
// each), so their nodes stand on the line above the one they stood on. The three digests below are
// the documents the runs of the current samples produce, normalized the way this script normalizes
// them: `module` and the checkout path replaced, `origin` dropped.
//
// The sample programs hold literals only and `std::jgir` is a translation pass: no environment
// read, hash or machine query is executed, so the graph is the same on every machine.
//
// A `verify_script_path` is a stand-alone Node program: it reads the test's JSON payload from
// its first argument and exits non-zero when an assertion fails.
//
// The jgir document names each node's source file by absolute path. Where the checkout sits is
// not part of the graph, so that path is replaced with a marker before hashing; a digest that
// still contains it is only valid in the checkout that recorded it.

import crypto from 'node:crypto'
import fs from 'node:fs'
import path from 'node:path'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const CASES = {
    // test name -> the old spelling, the spelling now, the case's file name, and the digest of
    // the graph with the module name and the checkout path normalized away.
    env: {
        old: 'sys',
        now: 'env',
        file: 'env_identity.cml',
        expected: 'b7340fb18b74240ed13bcfbe7a9b056ed3692f7f745fffda0796cde952f2c75b',
    },
    process: {
        old: 'os',
        now: 'process',
        file: 'process_identity.cml',
        expected: '92a32a088572ece27eed08a59553056120f93fb86ce2d153ef1fbbe4f42f5b0a',
    },
    hash: {
        old: 'crypto',
        now: 'hash',
        file: 'hash_identity.cml',
        expected: '392a26ad1011fd25d73956cc276aafefb3968a50136a222522babee6bc691c28',
    },
}

// A sweep copy is named `<sweep>:<source plan>:<test name>`, so the last dot-segment is
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
const caseMarker = 'CASE'
const caseFile = kase.file

// The runner passes the resolved case path as `case_path` (docs/test-plans.md); it is the exact
// string `std::jgir` writes into every node's `source.file`. The file name is enough to find the
// path in a payload that lacks the key, because an absolute path that ends with this file name
// has only the checkout as its prefix.
const casePath =
    typeof input.case_path === 'string' && path.isAbsolute(input.case_path) ? input.case_path : null
const casePathMarker = `${caseMarker}/${caseFile}`
const escapeRegExp = (text) => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
// An absolute root (POSIX or Windows) followed by this case's file name, with any character that
// can sit before it in a document string. The prefix is the checkout and is replaced; the file
// name is kept, so a node that moved to another source file still changes the digest.
const checkoutPrefix = new RegExp(
    `(^|[^A-Za-z0-9._\\\\/-])(?:[A-Za-z]:[\\\\/]|/)[^"']*?[\\\\/]${escapeRegExp(caseFile)}`,
    'g',
)

const normalizeSourcePath = (text) => {
    const exact = casePath ? text.split(casePath).join(casePathMarker) : text
    return exact.replace(checkoutPrefix, (_match, before) => `${before}${casePathMarker}`)
}

const rewrite = (value) => {
    if (typeof value === 'string') {
        const normalized = normalizeSourcePath(value)
        // The digest must not depend on where the checkout sits. A path that survived
        // normalization means the payload had no usable `case_path` and the fallback did not
        // recognize the source path: fail loudly instead of hashing an absolute path.
        if (path.isAbsolute(normalized)) {
            throw new Error(
                `the payload has no usable case_path and ${JSON.stringify(normalized)} does not ` +
                    `end with ${caseFile}; refusing to hash a checkout-dependent digest`,
            )
        }
        return normalized.replace(new RegExp(`\\b${kase.now}:`, 'g'), `${marker}:`)
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