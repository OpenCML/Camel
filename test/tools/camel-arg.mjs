// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The `camel` binary a tool runs is named on the command line: one rule, in one place.
//
// `--camel <path>` (or `--camel=<path>`) names the install a run measures; with none it is this
// tree's `out/latest/bin/camel`. The default is this tree's own build on purpose: `CAMEL_HOME` is
// the install location's convention -- what the `camel` binary and an SDK user read to find the
// runtime library and the standard library -- and a tool that let it pick the binary would be one
// run measuring whichever install the shell happened to export. A tool sets `CAMEL_HOME` for the
// children it starts, to the root of the install it measured; it never takes it from the
// environment.
//
// A `--camel` with no value is a usage error: the caller's own `usage` prints (each tool owns its
// text) and the run exits 2. It never falls back to the default -- the run that was asked to
// measure one install and measured another is the defect this rule exists against.
//
// The stamp rule for a checker that reads an install's operator registry is
// `scripts/install-stamp.mjs` (`refuseInstallNotOfThisTree`), and it states the same default this
// module does.

import path from 'node:path'

/** The install a tool measures with no `--camel`: this tree's `out/latest`. */
export function defaultCamel(repoRoot) {
    return path.join(repoRoot, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

/**
 * Take `--camel <path>` (or `--camel=<path>`) out of `argv` and resolve the binary it names.
 * Returns `{ camel, rest, named }`: `rest` holds every other argument in order, so a caller parses
 * its own arguments exactly as it did before, and `named` says whether the install was named here
 * (a caller that sets `CAMEL_HOME` for its children still does, to the root of `camel`).
 * `usage(message)` prints the tool's usage and exits 2; it is called only for a `--camel` with no
 * value.
 */
export function takeCamel(argv, repoRoot, usage) {
    const rest = []
    let named = null
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--camel') {
            if (i + 1 >= argv.length) usage('--camel needs the path of the install to measure')
            named = argv[++i]
        } else if (arg.startsWith('--camel=')) {
            named = arg.slice('--camel='.length)
            if (named === '') usage('--camel needs the path of the install to measure')
        } else {
            rest.push(arg)
        }
    }
    return { camel: named === null ? defaultCamel(repoRoot) : path.resolve(named), rest, named: named !== null }
}