#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The 3a count and the kinds-to-oper count, as one command that cannot report a beautiful zero.
//
// The two numbers the lanes are judged by are counts of `git grep` matches:
//
//   3a            how many places still read `GCGraph` / `GraphDraft` directly, per directory
//   kinds-to-oper how many `Kind::Cast|Copy|Fill|Accs` references and `add*Node` builder calls
//
// Both were typed by hand into dispatch notes and backlog rows, and on 2026-11-24 a dispatch named
// `src/stdlib/std/onnx` and `src/stdlib/std/tiles` -- directories that do not exist; the real ones
// are `src/stdlib/onnx` and `src/stdlib/tiles`. `git grep -cE '...' <rev> -- <missing path>`
// prints nothing and exits 1. So does `git grep` finding no match. The two are opposite facts --
// "this path is not in the tree" and "this path has nothing left to count" -- and they read the
// same way: an empty output and the exit code 1. Three lanes came within one step of reporting
// "counted to zero" about a path that was never there.
//
// So this script asserts the paths before it counts them, and a path that is not in the revision,
// or that holds no source file, is exit 2 naming that path -- every such path, not just the first.
// A `git grep` that finds nothing is the one place where exit 1 is read as 0, and it is read there
// only after the path has been proven to exist.
//
// --- usage -----------------------------------------------------------------
//   node scripts/count-graph-readers.mjs [--rev <rev>] <directory>...
//   node scripts/count-graph-readers.mjs --self-test
//
// `--rev` defaults to `HEAD`; the repository root comes from `git rev-parse --show-toplevel`, so
// the script runs from the root of any checkout or linked worktree without a path written here.
//
// Output is one line per directory plus a total, and nothing else on stdout:
//
//   src/stdlib/onnx  graph=70  kind=7  addNode=0
//   src/stdlib/tiles  graph=71  kind=8  addNode=4
//   total  graph=141  kind=15  addNode=4  dirs=2  rev=e3c6e02933
//
// Exit codes: 0 counted; 2 a path, revision or argument is not usable (nothing was counted);
// 3 `git` itself failed in a way this script does not know how to read (this is not a zero).

import { spawnSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const NAME = 'count-graph-readers'
const SELF = fileURLToPath(import.meta.url)

/** The extensions that make a directory a place this count applies to. */
const SOURCE = /\.(?:c|cc|cpp|h|hpp|cml)$/i
const SOURCE_LIST = '.c/.cc/.cpp/.h/.hpp/.cml'

/** The three readings. A pattern is passed to `git grep -E` as one argument, unquoted. */
const GRAPH = String.raw`\bGCGraph\b|\bGraphDraft\b`
const KIND = String.raw`(NodeKind|GCNodeKind|Kind)::(Cast|Copy|Fill|Accs|CAST|COPY|FILL|ACCS)\b`
const ADD_NODE = String.raw`add(Cast|Copy|Fill|Accs)Node\b`

/** `git grep -c` writes one line per file; a directory of a few thousand files is still small. */
const MAX_BUFFER = 64 * 1024 * 1024

const USAGE = `usage: node scripts/${NAME}.mjs [--rev <rev>] <directory>...
       node scripts/${NAME}.mjs --self-test

  --rev <rev>   the revision to count in (default: HEAD)
  <directory>   a directory that must exist at <rev> and hold at least one of ${SOURCE_LIST}
  --self-test   prove the two readings this script exists for: a missing path is exit 2 and is
                named, and a directory with no match is exit 0 with zeros`

function git(root, args) {
    const result = spawnSync('git', args, {
        cwd: root,
        encoding: 'utf8',
        maxBuffer: MAX_BUFFER
    })
    if (result.error) {
        throw new Error(`git ${args.join(' ')}: ${result.error.message}`)
    }
    return {
        status: result.status,
        stdout: result.stdout ?? '',
        stderr: result.stderr ?? ''
    }
}

/** The root of the work tree the caller stands in, never a path written into this file. */
function findRoot() {
    const probe = spawnSync('git', ['rev-parse', '--show-toplevel'], {
        cwd: process.cwd(),
        encoding: 'utf8'
    })
    if (probe.error) throw new Error(`git rev-parse --show-toplevel: ${probe.error.message}`)
    if (probe.status !== 0) {
        throw new Error(
            `not inside a git work tree (${process.cwd()}): ${(probe.stderr ?? '').trim()}`
        )
    }
    return probe.stdout.trim()
}

/**
 * A directory argument as a path inside the revision. Absolute paths and `..` are resolved against
 * the caller's directory first, because a path that leaves the work tree is not a directory of the
 * revision however it is spelled.
 */
function toRepoPath(root, given) {
    const absolute = path.resolve(process.cwd(), given)
    const relative = path.relative(root, absolute)
    if (relative === '') return '.'
    if (path.isAbsolute(relative) || relative === '..' || relative.startsWith(`..${path.sep}`)) {
        return null
    }
    return relative.split(path.sep).join('/').replace(/\/+$/, '')
}

/** `git grep -c` lines are `<rev>:<path>:<count>`. The rev may carry no colon; the path may. */
function sumGitGrepCount(output) {
    let total = 0
    for (const line of output.split('\n')) {
        if (line === '') continue
        const cut = line.lastIndexOf(':')
        if (cut < 0) throw new Error(`unexpected git grep -c line: ${line}`)
        const count = Number(line.slice(cut + 1))
        if (!Number.isInteger(count) || count < 0) {
            throw new Error(`unexpected git grep -c count in: ${line}`)
        }
        total += count
    }
    return total
}

/**
 * One count over one proven directory. `git grep` exits 1 when it finds nothing, and that is the
 * legal zero -- but only here, after `inspectDirectory` has said the directory is in the revision.
 * An exit 1 that carries a message, or any other status, is a failure this script must not round
 * down to 0.
 */
function countMatches(root, rev, directory, pattern) {
    const result = git(root, ['grep', '-c', '--no-color', '-E', pattern, rev, '--', directory])
    if (result.status === 0) return sumGitGrepCount(result.stdout)
    if (result.status === 1 && result.stdout.trim() === '' && result.stderr.trim() === '') return 0
    const detail = result.stderr.trim() || result.stdout.trim()
    throw new Error(
        `git grep -c exited ${result.status} on ${directory}${detail ? `: ${detail}` : ''}`
    )
}

/**
 * Whether a directory may be counted: it exists at `rev` as a tree, and it holds at least one
 * source file. Both answers are facts about the revision and are read from the revision, not from
 * the checkout's working files.
 */
function inspectDirectory(root, rev, directory) {
    const type = git(root, ['cat-file', '-t', `${rev}:${directory}`])
    if (type.status !== 0) {
        return { ok: false, reason: `does not exist at ${rev}` }
    }
    const kind = type.stdout.trim()
    if (kind !== 'tree') {
        return { ok: false, reason: `is a ${kind || 'unknown object'}, not a directory` }
    }
    const listing = git(root, ['ls-tree', '-r', '--name-only', rev, '--', directory])
    if (listing.status !== 0) {
        throw new Error(
            `git ls-tree exited ${listing.status} on ${directory}: ${listing.stderr.trim()}`
        )
    }
    const files = listing.stdout.split('\n').filter(Boolean)
    const sources = files.filter((file) => SOURCE.test(file))
    if (sources.length === 0) {
        const others = files.length > 0 ? `; it holds ${files.length} other file(s)` : ''
        return { ok: false, reason: `holds no source file (${SOURCE_LIST})${others}` }
    }
    return { ok: true, sources: sources.length }
}

function parseArgs(argv) {
    const options = { rev: 'HEAD', selfTest: false, help: false, given: [] }
    let rest = false
    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i]
        if (rest) {
            options.given.push(arg)
            continue
        }
        if (arg === '--') {
            rest = true
            continue
        }
        if (arg === '--self-test') {
            options.selfTest = true
            continue
        }
        if (arg === '-h' || arg === '--help') {
            options.help = true
            continue
        }
        if (arg === '--rev') {
            i += 1
            if (i >= argv.length) throw new UsageError('--rev needs a revision')
            options.rev = argv[i]
            continue
        }
        if (arg.startsWith('--rev=')) {
            options.rev = arg.slice('--rev='.length)
            if (options.rev === '') throw new UsageError('--rev needs a revision')
            continue
        }
        if (arg.startsWith('-') && arg !== '-') {
            throw new UsageError(`unknown option: ${arg}`)
        }
        options.given.push(arg)
    }
    return options
}

class UsageError extends Error {}

function count(root, rev, directories) {
    const failures = []
    for (const directory of directories) {
        const inspected = inspectDirectory(root, rev, directory)
        if (!inspected.ok) failures.push(`${directory}: ${inspected.reason}`)
    }
    if (failures.length > 0) {
        // Every unusable directory is named, not just the first: a lane that fixes one path and
        // learns about the next one from a second run has been told half the truth twice.
        for (const failure of failures) {
            process.stderr.write(`${NAME}: ${failure}\n`)
        }
        process.stderr.write(
            `${NAME}: ${failures.length} of ${directories.length} path(s) cannot be counted; ` +
                'nothing was counted\n'
        )
        process.stderr.write(
            `${NAME}: a path that is not in the revision is not a zero: \`git grep\` exits 1 ` +
                'for it exactly as it does for no match\n'
        )
        return 2
    }

    const rows = []
    const total = { graph: 0, kind: 0, addNode: 0 }
    for (const directory of directories) {
        const graph = countMatches(root, rev, directory, GRAPH)
        const kind = countMatches(root, rev, directory, KIND)
        const addNode = countMatches(root, rev, directory, ADD_NODE)
        rows.push(`${directory}  graph=${graph}  kind=${kind}  addNode=${addNode}`)
        total.graph += graph
        total.kind += kind
        total.addNode += addNode
    }

    const short = git(root, ['rev-parse', '--short=12', rev]).stdout.trim()
    for (const row of rows) console.log(row)
    console.log(
        `total  graph=${total.graph}  kind=${total.kind}  addNode=${total.addNode}  ` +
            `dirs=${directories.length}  rev=${short}`
    )
    return 0
}

/**
 * Runs this file as a child process, so the self-test exercises the exit code and the stderr that
 * a caller sees.
 */
function runSelf(args, cwd) {
    const result = spawnSync(process.execPath, [SELF, ...args], {
        cwd,
        encoding: 'utf8',
        maxBuffer: MAX_BUFFER
    })
    return {
        status: result.status,
        stdout: result.stdout ?? '',
        stderr: result.stderr ?? ''
    }
}

function selfTest(root) {
    // The children run at the root, so the fixture paths below are the same wherever the self-test
    // is started from.
    const cwd = root
    const checks = []
    const check = (name, ok, detail) => {
        checks.push(ok)
        console.log(`self-test: ${ok ? 'PASS' : 'FAIL'} ${name}${detail ? ` -- ${detail}` : ''}`)
    }

    // 1. A directory that is not in the revision: exit 2, and stderr names it.
    const missing = 'src/__count-graph-readers-no-such-directory__'
    const missingRun = runSelf([missing], cwd)
    check(
        'a path that is not in the revision exits 2',
        missingRun.status === 2,
        `exit ${missingRun.status}`
    )
    check(
        '... and stderr names that path',
        missingRun.stderr.includes(missing),
        missingRun.stderr.split('\n').find((line) => line.includes(missing)) ?? 'not named'
    )
    check(
        '... and stdout is empty, so no zero is printed',
        missingRun.stdout.trim() === '',
        JSON.stringify(missingRun.stdout)
    )

    // 2. Two missing paths: both are named in one run, not just the first.
    const secondMissing = 'src/__count-graph-readers-also-no-such-directory__'
    const twoMissing = runSelf([missing, secondMissing], cwd)
    check(
        'two missing paths exit 2 with both named',
        twoMissing.status === 2 &&
            twoMissing.stderr.includes(missing) &&
            twoMissing.stderr.includes(secondMissing),
        `exit ${twoMissing.status}`
    )

    // 3. A directory that exists but holds no source file is refused, not reported as zero.
    const noSource = 'internal/plans'
    const noSourceRun = runSelf([noSource], cwd)
    check(
        `a directory without source files (${noSource}) exits 2`,
        noSourceRun.status === 2 && noSourceRun.stderr.includes(noSource),
        `exit ${noSourceRun.status}`
    )

    // 4. A real directory: exit 0, three counts and a total, in the one shape this prints.
    const realRun = runSelf(['src/kernel/graph'], cwd)
    const row = realRun.stdout.split('\n')[0] ?? ''
    const shape = new RegExp(
        '^src/kernel/graph {2}graph=\\d+ {2}kind=\\d+ {2}addNode=\\d+$'
    )
    check('a real directory exits 0', realRun.status === 0, `exit ${realRun.status}`)
    check('... with a readable row', shape.test(row), JSON.stringify(row))
    check(
        '... and one total line',
        /^total {2}graph=\d+ {2}kind=\d+ {2}addNode=\d+ {2}dirs=1 {2}rev=[0-9a-f]+$/.test(
            realRun.stdout.split('\n')[1] ?? ''
        ),
        JSON.stringify(realRun.stdout.split('\n')[1] ?? '')
    )

    // 5. A `git grep` with no match is a zero, and is read as one through the same path a caller
    // uses -- the case that must not be confused with a missing path.
    let noMatch = null
    try {
        noMatch = countMatches(root, 'HEAD', 'src/kernel/graph', 'zzz_no_such_symbol_zzz')
    } catch (error) {
        noMatch = error.message
    }
    check(
        'git grep finding nothing is read as 0',
        noMatch === 0,
        typeof noMatch === 'number' ? 'graph=0' : String(noMatch)
    )

    // 6. The `-c` reader: per-file counts are summed, and an empty output is 0.
    let summed = null
    try {
        summed = sumGitGrepCount('HEAD:a.cpp:3\nHEAD:b.h:2\n')
    } catch (error) {
        summed = error.message
    }
    check('per-file counts are summed', summed === 5, String(summed))
    check('empty output is 0', sumGitGrepCount('') === 0, String(sumGitGrepCount('')))

    const passed = checks.filter(Boolean).length
    console.log(`self-test: ${passed} of ${checks.length} checks passed`)
    return passed === checks.length ? 0 : 1
}

function main(argv) {
    let options
    try {
        options = parseArgs(argv)
    } catch (error) {
        if (!(error instanceof UsageError)) throw error
        process.stderr.write(`${NAME}: ${error.message}\n\n${USAGE}\n`)
        return 2
    }
    if (options.help) {
        console.log(USAGE)
        return 0
    }

    let root
    try {
        root = findRoot()
    } catch (error) {
        process.stderr.write(`${NAME}: ${error.message}\n`)
        return 2
    }

    if (options.selfTest) return selfTest(root)

    if (options.given.length === 0) {
        process.stderr.write(`${NAME}: no directory given\n\n${USAGE}\n`)
        return 2
    }

    const revision = git(root, ['rev-parse', '--verify', '--quiet', options.rev])
    if (revision.status !== 0 || revision.stdout.trim() === '') {
        process.stderr.write(`${NAME}: no such revision: ${options.rev}\n`)
        return 2
    }

    const directories = []
    const outside = []
    for (const given of options.given) {
        const relative = toRepoPath(root, given)
        if (relative === null) {
            outside.push(`${given}: is outside the work tree at ${root}`)
            continue
        }
        directories.push(relative)
    }
    if (outside.length > 0) {
        for (const failure of outside) process.stderr.write(`${NAME}: ${failure}\n`)
        process.stderr.write(`${NAME}: nothing was counted\n`)
        return 2
    }

    try {
        return count(root, options.rev, directories)
    } catch (error) {
        process.stderr.write(`${NAME}: ${error.message}\n`)
        return 3
    }
}

process.exit(main(process.argv.slice(2)))