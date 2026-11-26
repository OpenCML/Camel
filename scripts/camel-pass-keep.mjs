// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The case-script pipelines that are decided and not patched, each with the reason.
 *
 * `scripts/camel-pass-scan.mjs` finds the pipelines that end in a translating pass: they hand the
 * graph on, so the default scheduling pass runs the program after the dump. These are the lines no
 * `std::nop` was added to, and why. The patches that added `std::nop` to the other case-script and
 * tool pipelines are the ones that made this list short: a pipeline whose case reads the dump (or
 * the printed source, or the exported document) stops saying what happens after it, and a pipeline
 * whose case reads the *run* keeps the run.
 *
 * Every entry says **why it is not patched**, in terms of what that line's stdout carries, and the
 * entries are keyed by `file` and `line` so that an entry which no longer names a pipeline is
 * reported as stale and refused by `--check` (`node scripts/camel-pass-scan.mjs --check`). That
 * refusal is that command's own answer and reaches nobody by itself: the scan is not a row of the
 * gate's `$checkers`, so the list stands only as long as a person keeps running it. A bare path
 * would be a decision nobody can read, which is why `why` is a field rather than a comment.
 *
 * A pipeline that is decided on its own line says so there instead
 * (`# camel-pass-scan: keep -- ...`), which is how `trans/canon_order.py` records the three pass
 * tuples its `jgir()` helper is called with: those lines are not command lines at all, and the
 * helper adds `std::nop` where it builds the command.
 *
 * Two shapes are *not* on this list because they need no decision: a pipeline that ends in a
 * scheduling pass (`std::nvm` runs the program, and the case says so), and one whose pass refuses
 * the program before anything is handed on (`CmlBigConstant`, `UnresolvedTypeReference`,
 * `EntryModuleMissingMain`), where the refusal is the assertion.
 *
 * The measurement below was taken with `out/latest/bin/camel` of the tree that carries this file:
 * the same case under the typed pipeline and with a trailing `std::nop`, comparing the exit code and
 * the bytes of stdout.
 */
export const KEPT_CASE_SCRIPTS = [
    {
        file: 'test/cases/trans/print_determinism.py',
        line: 40,
        why:
            'the case hashes the whole stdout of the pipeline over twenty runs and asserts its line count: measured ' +
            '23465 bytes under `std::gir` against 23449 with a trailing `std::nop`, so the program output is inside ' +
            'what it hashes -- the case is green either way, and patching it would narrow what it reads',
    },
]