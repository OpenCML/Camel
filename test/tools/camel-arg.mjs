// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The `camel` binary a tool runs, named on the command line: one rule, in one place.
//
// The author's ruling of 2026-10-25 (docs/handover/reviews/questions-2026-10-24-evening.md,
// "stdlib-declaration-check-measures-a-foreign-install"): an override that names the binary is a
// command-line argument, `--camel <path>`, and not an environment variable -- `CAMEL_EXE` is gone.
// `CAMEL_HOME` is the install location's convention and stays: with no `--camel` the binary is
// `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`.
//
// A `--camel` with no value is a usage error: the caller's own `usage` prints (each tool owns its
// text) and the run exits 2. It never falls back to the default -- the run that was asked to
// measure one install and measured another is the defect this rule exists against.
//
// The stamp rule of a checker that reads an install's operator registry is `scripts/install-stamp.mjs`
// (`refuseInstallNotOfThisTree`), and it states its own default for the case it guards: this tree's
// `out/latest`. The two defaults are not merged here on purpose -- `CAMEL_HOME` is the install
// location's convention (the ruling keeps it), so a tool that honors it must not lose it to a
// helper whose subject is the stamp.

import path from 'node:path'

/** The install a tool measures with no `--camel`: `$CAMEL_HOME`, else this tree's `out/latest`. */
export function defaultCamel(repoRoot, env = process.env) {
    const home = env.CAMEL_HOME || path.join(repoRoot, 'out', 'latest')
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

/**
 * Take `--camel <path>` (or `--camel=<path>`) out of `argv` and resolve the binary it names.
 * Returns `{ camel, rest, named }`: `rest` holds every other argument in order, so a caller parses
 * its own arguments exactly as it did before, and `named` says whether the install was named here
 * (`perf-gate` must not force its own `CAMEL_HOME` on a run that named one). `usage(message)` prints
 * the tool's usage and exits 2; it is called only for a `--camel` with no value.
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