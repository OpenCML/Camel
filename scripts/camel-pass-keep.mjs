// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The case-script pipelines that are decided and not patched, each with the reason.
 *
 * `scripts/camel-pass-scan.mjs` finds the pipelines that end in a translating pass: they hand the
 * graph on, so the default scheduling pass runs the program after the dump. These are the lines no
 * `std::nop` was added to, and why. The patch that added `std::nop` to the other case-script
 * pipelines (compile/unused_values/repeat_compile.py, trans/canon_order.py, trans/canonical_opt_export.py,
 * trans/spelling/canonical_pairing.py, trans/jgir_contract.py, tools/roundtrip-compare.mjs) is the
 * one that made this list short: a pipeline whose case reads the dump stops saying what happens
 * after it, and a pipeline whose case reads the *run* keeps the run.
 *
 * Every entry says **why it is not patched**, in terms of what that line's stdout carries, and the
 * entries are keyed by `file` and `line` so that an entry which no longer names a pipeline is
 * reported as stale and refused by `--check` (`node scripts/camel-pass-scan.mjs --check`): a keep
 * list that cannot rot into permission. A bare path would be a decision nobody can read, which is
 * why `why` is a field rather than a comment.
 *
 * A pipeline that is decided on its own line says so there instead
 * (`# camel-pass-scan: keep -- ...`), which is how `trans/canon_order.py` records the three pass
 * tuples its `jgir()` helper is called with: those lines are not command lines at all, and the
 * helper adds `std::nop` where it builds the command.
 *
 * The measurements below were taken with `out/latest/bin/camel` of the tree that carries this file:
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
    {
        file: 'test/cases/trans/cml_canon_contract.py',
        line: 57,
        why:
            'the `canonical()` helper (`*passes` then `"std::jgir canonical=true"`): the run the fallback hands the ' +
            'graph to writes behind the document (measured `json.loads` "Extra data"). Not patched with the others ' +
            'because this plan is red on the baseline for a reason of its own (`var_param` and its printed program ' +
            'are not equivalent), so a `std::nop` here would only move the reading past that defect; the `std::nop` ' +
            'belongs with the fix for it',
    },
    {
        file: 'test/cases/trans/cml_canon_contract.py',
        line: 165,
        why:
            'a direct `invoke(EXE, LOOPS, "std::jgir")`: the version 1 document, then the run behind it, which is the ' +
            'same "Extra data"; not patched for the same reason as the helper above -- the plan fails earlier on a ' +
            'defect that is not the run',
    },
    {
        file: 'test/cases/trans/cml_canon_contract.py',
        line: 369,
        why:
            '`std::cml` prints the program source and that text is written back as a program: the run after the ' +
            'printed source is the same "Extra data"; not patched for the same reason as the helper above',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 128,
        why:
            'the `canonical()` helper (`"std::jgir canonical=true"`): measured `linear.cml` exits 23 after the ' +
            'document and the program writes `bonus = 24 ...` behind it, so the run is both the exit code and the ' +
            'trailing text of what the case parses. Not patched with the others because this plan is red on the ' +
            'baseline for a reason of its own (`arm_write` before and after the print are not equivalent); the ' +
            '`std::nop` belongs with the fix for it',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 139,
        why:
            'the `print_source()` helper (`std::cml`): the printed source is the case\'s subject and the run after ' +
            'it appends the program\'s own stdout to it, which is what makes the recompiled copy fail; not patched ' +
            'for the same reason as the helper above',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 161,
        why:
            'the version 1 `std::jgir` dump of a source: the document, then the run, as in `canonical()`; not ' +
            'patched for the same reason',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 312,
        why:
            '`std::cml max_literal_bytes=64` on a program whose constant is over the limit: the pass refuses it ' +
            '(`CmlBigConstant`), so nothing is handed on and no run follows -- what this line asserts is the ' +
            'refusal and the limit it names, not a dump and not the program',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 353,
        why:
            '`std::jgir canonical=true` on the printed program with its `type` line removed: the pipeline stops at ' +
            'the compile (`UnresolvedTypeReference`), so no run follows and a `std::nop` would say nothing -- the ' +
            'case asserts the refusal',
    },
]