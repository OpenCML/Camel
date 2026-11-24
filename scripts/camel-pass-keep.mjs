// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The case-script pipelines that are decided and not patched, each with the reason.
 *
 * `scripts/camel-pass-scan.mjs` finds the pipelines that end in a translating pass: they hand the
 * graph on, so the default scheduling pass runs the program after the dump. The `std::nop` patch
 * script reaches the plans only -- a pipeline written in a case script is left for a person, because
 * what the script reads is not visible in its pass list. These are those lines.
 *
 * Every entry says **why it is not patched**, in terms of what that line's stdout carries, and the
 * entries are keyed by `file` and `line` so that an entry which no longer names a pipeline is
 * reported as stale and refused by `--check` (`node scripts/camel-pass-scan.mjs --check`): a keep
 * list that cannot rot into permission. A bare path would be a decision nobody can read, which is
 * why `why` is a field rather than a comment.
 *
 * The measurements below were taken with `out/latest/bin/camel` of this tree: the same case under
 * the typed pipeline and with a trailing `std::nop`, comparing the exit code and the bytes of
 * stdout. They say what the run contributes to the case's observation; whether the case should stop
 * reading it is the case's own change (each of these scripts builds several pipelines, so a nop on
 * one line does not finish any of them).
 */
export const KEPT_CASE_SCRIPTS = [
    {
        file: 'test/cases/compile/unused_values/repeat_compile.py',
        line: 30,
        why:
            'compiles `train_step.cml`, whose `main` has three open parameters: the run the dump hands the graph to ' +
            'stops at EntryArgumentMissing (measured `std::gir` exit 1, `std::gir std::nop` exit 0), so the exit code ' +
            'and stderr this case asserts come from the run, not from the dump',
    },
    {
        file: 'test/cases/trans/canon_order.py',
        line: 309,
        why:
            'the `passes=` default of the dump helper every comparison of this script is built through: the run after ' +
            'the dump decides the exit code the helper reports (measured: the script fails at "the program did not ' +
            'compile", which is the run exiting non-zero with an empty stderr)',
    },
    {
        file: 'test/cases/trans/canon_order.py',
        line: 628,
        why:
            'a caller of that helper with its own tuple (`std::jgir canonical=true`): same measurement as the default ' +
            'above -- the document is compared byte for byte, and the run after it is what the process exit code ' +
            'reports',
    },
    {
        file: 'test/cases/trans/canon_order.py',
        line: 631,
        why:
            'the right-hand program of the same comparison, through the same helper: the two documents must agree, ' +
            'and both runs contribute their exit code',
    },
    {
        file: 'test/cases/trans/print_determinism.py',
        line: 40,
        why:
            'the case hashes the whole stdout of the pipeline over twenty runs and asserts its line count: measured ' +
            '23465 bytes under `std::gir` against 23449 with a trailing `std::nop`, so the program output is inside ' +
            'what it hashes',
    },
    {
        file: 'test/cases/trans/spelling/canonical_pairing.py',
        line: 116,
        why:
            'the `run()` helper every comparison of this file goes through (`+ ["std::jgir", "canonical=true"]`): ' +
            'measured "camel exited 42 before the pass ran" -- the exit code that reaches the case is the run after ' +
            'the dump',
    },
    {
        file: 'test/cases/trans/canonical_opt_export.py',
        line: 36,
        why:
            'the file\'s only pipeline (`std::opt std::jgir canonical=true`): measured the program exit 7 arrives as ' +
            'the process exit code while the case asserts 0, so what the case reads includes the run',
    },
    {
        file: 'test/cases/trans/cml_canon_contract.py',
        line: 57,
        why:
            'the `canonical()` helper (`*passes` then `"std::jgir canonical=true"`): measured `json.loads` fails with ' +
            '"Extra data" on the program output that follows the document -- the run is inside the stdout it parses',
    },
    {
        file: 'test/cases/trans/cml_canon_contract.py',
        line: 165,
        why:
            'a direct `invoke(EXE, LOOPS, "std::jgir")`: same measurement as the helper -- "Extra data" where the run ' +
            'writes after the version 1 document',
    },
    {
        file: 'test/cases/trans/cml_canon_contract.py',
        line: 369,
        why:
            '`std::cml` prints the program source and the printed text is written back as a program: the run after ' +
            'the printed source is what the same "Extra data" measurement picks up',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 128,
        why:
            'the `canonical()` helper (`"std::jgir canonical=true"`): measured `linear.cml` exits 23 after the ' +
            'document and the program writes `bonus = 24 ...` behind it, so the run is both the exit code and the ' +
            'trailing text of what the case parses',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 139,
        why:
            'the `print_source()` helper (`std::cml`): the printed source is the case\'s subject and the run after it ' +
            'contributes the exit code the case checks',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 161,
        why:
            'the version 1 `std::jgir` dump of a source: same measurement as `canonical()` -- the document, then the ' +
            'run',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 312,
        why:
            '`std::cml max_literal_bytes=64`: the parameter belongs to the translating pass, and what follows the ' +
            'printed source is the run',
    },
    {
        file: 'test/cases/trans/cml_contract.py',
        line: 353,
        why:
            'a `std::jgir canonical=true` call on a rewritten source: same measurement -- the run after the document ' +
            'is inside the stdout the case reads',
    },
    {
        file: 'test/cases/trans/jgir_contract.py',
        line: 67,
        why:
            '`jgir_duplicate.cml` under `std::jgir`: measured exit 4 and `json.loads` "Extra data" at line 1979 -- ' +
            'the run writes behind the document; this script has more `subprocess.run` pipelines of the same shape, ' +
            'so the decision belongs to the file',
    },
]