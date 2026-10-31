#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The scope of this guard, on its first line: every `getenv` call in `src/`, `modules/` and
 * `include/` -- a header is compiled into the program as much as a source file is, and the
 * environment does not care which of the three a read sits in.
 *
 * A run's behaviour is decided by the program, the command line and the build options, not by
 * the environment (design principles 9.3): reading the command line and the program has to be
 * enough to know what a run will do. What is left of the environment switches is a ledger, and
 * this guard keeps it from growing.
 *
 * A site is one `getenv` call under `src/`, `modules/` or `include/`, whichever name it reads:
 * the scan is by call, not by literal, because a helper that reads a name (`envString(name)`,
 * `envNumber(name, fallback)`) takes its variable from its caller and a scan for `getenv("CAMEL_`
 * would not see it. The Windows spelling `_dupenv_s` is the same reading in the other branch of
 * the same function, and that function is what the ledger allows, so only `getenv` is counted.
 *
 * A site whose name is not `CAMEL_*` is not a switch: `NO_COLOR` and `CUDA_PATH` are somebody
 * else's convention. A `CAMEL_*` site is allowed only by a line of `LEDGER`, and a line allows
 * exactly one site: the file, the variable it reads (`reads: null` when the helper reads a name
 * its caller chose), and one sentence saying why it is still there or who still owes it. A line
 * whose site is gone fails too, so clearing a switch means deleting its line, and the list can
 * only shrink towards the one line that stays -- `CAMEL_TILES_NVRTC`, where the CUDA toolchain
 * lives; `CAMEL_HOME` (the installation root) never needs a line at all.
 *
 * A reading can also be a pass parameter and still have no case that pins it, and the ledger says
 * so rather than leaving it to be forgotten: `std::profile variant_trace` prints only for a value
 * the collector relocates, so nothing pins it today, and pinning it belongs to the collector's
 * plan -- it is a known-open item, not a switch somebody stopped tracking.
 *
 * What the ledger is for, in one sentence: it does not record what is left to clear, it records
 * what is still there. A line that points at a call site that is gone is a fake asset in the
 * account -- it reads like work still owed when the work is done, and it hides the day a switch
 * comes back, because the line would quietly allow the new site.
 *
 *   node test/tools/check-env-switches.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { isRunnerOutput } from './runner-output.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')

// The roots whose code a run links: src/, modules/ and include/ -- a header is compiled into
// the program as much as a source file is, and the environment does not care which of the three a
// read sits in. The tool and the scripts are not: a tool's own switches are its command line.
const ROOTS = ['src', 'modules', 'include']
const SKIP_DIRS = new Set(['.git', 'out', 'node_modules', 'third_party', 'build', 'sdks'])
const SOURCE_EXTENSIONS = new Set(['.c', '.cc', '.cpp', '.h', '.hpp', '.hh'])

// The environment a run may still read: one line per site.
//
//   at     file:symbol -- the function the call is in, so a line can be found again
//   reads  the variable as a literal, or null when the helper takes its name from the caller
//   owner  the line that still owes the cleanup, so no line is an orphan
//   why    why it is still here
//
// A line is deleted with its site.
//
// The target, and the number this list is expected to reach: one line, and it is
// `CAMEL_TILES_NVRTC` (where the CUDA toolchain is installed: configuration, not behaviour).
// `CAMEL_HOME` needs no line at all: it names the installation the run was started from.
const LEDGER = [
    {
        at: 'src/core/mm/mm.cpp:envString',
        reads: null,
        owner: 'the environment-variable queue, core batch (lane-envsw)',
        why: 'reads the settings of the allocator (CAMEL_GC_*, CAMEL_SPACE_SEGMENT_KB) by name; still a switch',
    },
    {
        at: 'src/core/rtdata/collection_tree.cpp:envNumber',
        reads: null,
        owner: 'the environment-variable queue, core batch (lane-envsw)',
        why: 'reads the settings of the collection tree (CAMEL_COLL_*) by name; still a switch',
    },
    {
        at: 'include/camel/standalone/core.h:briefReports',
        reads: 'CAMEL_ERROR_FORMAT',
        owner: 'the environment-variable queue, core batch (lane-envsw)',
        why: 'a generated program reads whether its reports are one line: a diagnostic setting, and a switch still',
    },
    {
        at: 'include/camel/utils/env.h:getEnvIfSet',
        reads: null,
        owner: 'none: it is the process environment itself',
        why: 'the one place where the process environment is read: the names come from the platform and from PATH, not from this program',
    },
    {
        at: 'include/camel/utils/dll_path.h:setupLibrarySearchPathForHost',
        reads: null,
        owner: 'none: it is the process environment itself',
        why: 'reads the PATH entries a helper was handed; the names are the platform spelling, not a switch',
    },    {
        at: 'modules/tiles/src/cuda_backend.cpp:nvrtcPaths',
        reads: 'CAMEL_TILES_NVRTC',
        owner: 'none: it stays',
        why: 'where the CUDA toolchain is installed is configuration, not behaviour',
    },
    {
        at: 'modules/tiles/src/region_test_pass.cpp:RegionOutlineTestPass',
        reads: 'CAMEL_TILES_TEST_INVALIDATION',
        owner: 'the tiles line (its test pass)',
        why: 'a switch of the test pass itself',
    },
    {
        at: 'modules/tiles/src/region_test_pass.cpp:RegionOutlineTestPass',
        reads: 'CAMEL_TILES_TEST_I64',
        owner: 'the tiles line (its test pass)',
        why: 'a switch of the test pass itself',
    },
]

const CALL = /\bgetenv\s*\(/g

function* files(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            if (!SKIP_DIRS.has(entry.name) && !isRunnerOutput(full, REPO_ROOT)) {
                yield* files(full)
            }
        } else if (entry.isFile() && SOURCE_EXTENSIONS.has(path.extname(entry.name))) {
            yield full
        }
    }
}

// The argument of a `getenv` call as the file writes it: the variable, or null when it is a
// parameter.
function readName(text, from) {
    let i = from
    while (i < text.length && (text[i] === ' ' || text[i] === '\t')) {
        i++
    }
    if (text[i] === '"') {
        const end = text.indexOf('"', i + 1)
        return end < 0 ? null : text.slice(i + 1, end)
    }
    return null
}

const sites = []
for (const root of ROOTS) {
    for (const file of files(path.join(REPO_ROOT, root))) {
        let bytes
        try {
            bytes = fs.readFileSync(file)
        } catch {
            continue
        }
        if (bytes.includes(0)) {
            continue
        }
        const text = bytes.toString('utf8')
        if (!text.includes('getenv')) {
            continue
        }
        const relative = path.relative(REPO_ROOT, file).replaceAll(path.sep, '/')
        text.split(/\r?\n/).forEach((line, index) => {
            for (const match of line.matchAll(CALL)) {
                sites.push({ file: relative, line: index + 1, reads: readName(line, match.index + match[0].length) })
            }
        })
    }
}

const problems = []
// `CAMEL_HOME` names the installation the run was started from: configuration, not a switch, and
// a run could not find its own modules without it. It needs no line and no line may claim it.
const PERMANENT = new Set(['CAMEL_HOME'])
const allowed = sites.filter((site) => site.reads !== null && PERMANENT.has(site.reads))
const ignored = sites.filter(
    (site) => site.reads !== null && !site.reads.startsWith('CAMEL_') && !PERMANENT.has(site.reads)
)
const switches = sites.filter((site) => !ignored.includes(site) && !allowed.includes(site))

// One ledger line per site: same file, same variable (or both by name).
const unused = [...LEDGER]
for (const site of switches) {
    const index = unused.findIndex((entry) => {
        const split = entry.at.indexOf(':')
        const file = entry.at.slice(0, split)
        return file === site.file && entry.reads === site.reads
    })
    if (index >= 0) {
        unused.splice(index, 1)
        continue
    }
    const what = site.reads === null ? 'a name it was handed' : `'${site.reads}'`
    problems.push(`${site.file}:${site.line}: reads ${what} from the environment and no ledger line allows it`)
}
for (const entry of unused) {
    problems.push(
        `${entry.at}: the ledger line has no site left (${entry.owner}; the switch is gone: delete the line)`
    )
}

// A line's symbol has to be in its file: a renamed function must not leave a line that points at
// nothing while some other site quietly matches it by file and variable.
const textOf = new Map()
for (const entry of LEDGER) {
    const split = entry.at.indexOf(':')
    const file = entry.at.slice(0, split)
    const symbol = entry.at.slice(split + 1)
    if (!textOf.has(file)) {
        const full = path.join(REPO_ROOT, file)
        textOf.set(file, fs.existsSync(full) ? fs.readFileSync(full, 'utf8') : '')
    }
    if (!textOf.get(file).includes(symbol)) {
        problems.push(`${entry.at}: '${symbol}' is not in ${file}`)
    }
}

if (problems.length > 0) {
    for (const problem of problems) {
        console.log(`FAIL ${problem}`)
    }
    console.log(`FAIL environment switches: ${problems.length} problem(s)`)
    process.exit(1)
}

console.log(
    `PASS no environment switch outside the ledger: ${switches.length} CAMEL_ reading(s) accounted for, ` +
        `${LEDGER.length} line(s), ${allowed.length} reading(s) of CAMEL_HOME, ` +
        `${ignored.length} reading(s) of someone else's variable ignored`
)