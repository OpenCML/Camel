// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

//
// One migration of the pack spelling: `..` in a pack position becomes `...` (design principles §5,
// the author's ruling of 2026-11-04 05:45 -- a pack is written `...`, and `..` keeps intervals,
// composition and stream types). Written down as rules rather than as a one-off replacement, so that
// this migration can be read, checked and run again.
//
//   node scripts/migrate-pack-dots.mjs --check   list what would change, write nothing
//   node scripts/migrate-pack-dots.mjs --write   rewrite the files
//
// The rule: a `..` is a pack exactly when nothing is written to its left, because an interval
// (`a..b`), a composition (`f..g`) and a stream type (`T..`) all need a left operand and a pack never
// has one. So the character before the dots, ignoring spaces and newlines, decides it:
//
//   (   `(..xs: T)`, `(..ParamsOf<F>)`, `(..ElementOf<A>)`, `(..A)[]`, `(...)` in a tuple type
//   <   `::<..A>`, `with <..ws: T>`, `<..WithOf<F>>`, `f<..ws>(..xs)`
//   [   `T[..]`: any rank in a dimension list
//   ,   `f(t, ..ts)`, `f32[n, ..rest]`, `(W, ..Rest)`
//   when  `when ..is_array::<A>()`, the predicate every member has to satisfy
//
// Anything else -- a digit, a name, a `)` or a `]` -- leaves the dots alone. That is what keeps
// `1..2` an interval: the `1` is written to its left. A counter-example the rule refuses on purpose:
// `x..+1` and `x as int..5` are intervals too, and the `x`/`int` before them is exactly the operand
// that makes them one.
//
// A line that writes the *grammar* is not a program and is never rewritten (`GRAMMAR_POSITION`
// below): the parser's own productions, the precedence table and the operator list name `..` as a
// token, and a `..` there is the composition operator whatever its neighbour is. This migration got
// one of those lines wrong -- `compExpr : annoExpr ('..' annoExpr)*` -- and the reason is worth
// keeping: the first version of this rule put the quotes in `LEFT`, so a `..` *between quotes* was
// rewritten as if it were a pack. The criterion was "the text has `..`" instead of "this `..` stands
// where a pack stands". The quotes are out of `LEFT` now, and the grammar positions are named here
// as well, because a production can put the dots right after a `(`.
//
// Files: the standard library, the cases, the language and design documents. `docs/handover/reviews/`
// is a record of what was written at the time and is never rewritten (author, 18:35); the pages under
// `docs/handover/questions/` are proposals, and only the one about taking a type pack apart names the
// spelling, so that page is edited by hand rather than by this script.

import { readFileSync, writeFileSync } from 'node:fs';
import { readdirSync, statSync } from 'node:fs';
import path from 'node:path';

const ROOTS = [
    'stdlib',
    'test/cases',
    // A plan pins what the compiler prints, and a printed type carries the spelling
    // (`float[...]` for any rank). Leaving the plans out leaves the expectations on the old
    // spelling and turns every one of them red for the reason the migration fixed.
    'test/plans',
    // The programs a reader runs. They carried the old spelling too, and the parser refuses it:
    // a migration that leaves them out leaves the shipped examples unable to compile.
    'examples',
    'showcase',
    'benchmarks',
    'exemplars',
    'docs',
    'scripts',
    'test/tools',
    // The declarations the diagnostics are generated from: the message text quotes the spelling
    // it is about, and the example beside it is a program.
    'diagnostics',
    // A diagnostic quotes the spelling it is about, so the sentences move with the rule. In a
    // message a pack is always quoted (`'...e'`), which the left context `'` used to cover; a
    // quote is no longer a left context on its own (see the path rule below), so the message
    // roots are listed here and their sentences were migrated by the first run.
    'src/core/error/diagnostics/messages',
    'src/parse',
];
const EXTENSIONS = new Set(['.cml', '.md', '.toml', '.cpp', '.h', '.mjs', '.js', '.py', '.tsv']);
// The characters a pack can follow. A quote is deliberately absent: `'a'..'b'` is the interval
// between two one-character strings and `'..'` in a sentence names the composition operator,
// so a quote says nothing about whether the dots are a pack.
const LEFT = new Set(['(', '<', '[', ',', ':', '=']);
// A `..` that continues a path is never a pack: the relative links of a Markdown page and the
// relative names of an import both climb to a parent. The recorded readings spell that climb after
// a parenthesised name, so what decides is the character before the dots, not the sentence.
const PATH_CONTINUATION = new Set(['/', '\\']);

// A line that writes the grammar or the operator table rather than a program: a `..` there is the
// composition operator, and one of these lines is the one the first version of the rule got wrong
// (`compExpr : annoExpr ('..' annoExpr)*`). The real defence is that `LEFT` holds no quote and a
// table cell has a space on its left; naming the positions costs four lines and says why.
const GRAMMAR_POSITION = [
    // The parser's production block: `compExpr : annoExpr ('..' annoExpr)*` and the lines around it.
    /^\s*(?:\/\/\/\s*)?(?:dataExpr|waitExpr|ctrlExpr|assignExpr|logicalOrExpr|logicalAndExpr|equalityExpr|relationalExpr|additiveExpr|multiplicativeExpr|powerExpr|nullableExpr|unaryExpr|linkExpr|negExpr|negPower|compExpr|annoExpr|accessExpr|typeExpr)\s*:/,
    // The precedence table: `17  compExpr            ..            repeat, left`.
    /^\s*\d+\s+[A-Za-z]+\s/,
    // The operator list of the lexical page and its mirrors.
    /^\*\*Operators and separators\*\*/,
];
const CWD = process.cwd();

function* walk(dir) {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        // `test/cases/probe/` holds the cases that must keep the *old* spelling: they are the
        // evidence that a `..` in a pack position is refused. A migration that rewrites its own
        // counter-examples erases the thing it is measured by.
        if (full.replaceAll('\\', '/').endsWith('test/cases/probe')) {
            continue;
        }
        // `docs/handover/` is the record of what was written at the time -- reports, reviews and
        // questions. It is read as history, so the spelling in it stays the one that was written.
        if (full.replaceAll('\\', '/').endsWith('docs/handover')) {
            continue;
        }
        // This file quotes the old spelling on purpose: its header is the table of the positions
        // the rule looks at, so rewriting it would delete the examples that explain the rule.
        if (full.replaceAll('\\', '/').endsWith('scripts/migrate-pack-dots.mjs')) {
            continue;
        }
        if (entry.isDirectory()) {
            yield* walk(full);
        } else if (EXTENSIONS.has(path.extname(entry.name))) {
            yield full;
        }
    }
}

/// The index of the last character before `at` that is not a space, a tab or a newline.
function leftOf(text, at) {
    let i = at - 1;
    while (i >= 0 && (text[i] === ' ' || text[i] === '\t' || text[i] === '\r' || text[i] === '\n')) {
        --i;
    }
    return i;
}

/// The word that ends at `i`, or '' when a non-name character is there.
function wordEndingAt(text, i) {
    let j = i;
    let word = '';
    while (j >= 0 && /[A-Za-z_]/.test(text[j])) {
        word = text[j] + word;
        --j;
    }
    return word;
}

/// Rewrites the pack dots of one text: `text` and the number of changes.
export function migrate(text) {
    let out = '';
    let changed = 0;
    // Line by line, so a line that writes the grammar can be left entirely alone. See the header.
    const lines = text.split('\n');
    for (let n = 0; n < lines.length; ++n) {
        if (n > 0) out += '\n';
        const line = lines[n];
        if (GRAMMAR_POSITION.some((pattern) => pattern.test(line))) {
            out += line;
            continue;
        }
        const part = migrateLine(line);
        out += part.text;
        changed += part.changed;
    }
    return { text: out, changed };
}

/// The same rule for one line; the count is left on `.changes` so the split above stays readable.
function migrateLine(text) {
    let out = '';
    let changed = 0;
    for (let i = 0; i < text.length; ++i) {
        // Exactly two dots, and not the first two of three.
        if (text[i] === '.' && text[i + 1] === '.' && text[i + 2] !== '.') {
            const left = leftOf(text, i);
            // A path never carries a pack: `../x`, `../../x`. The dots continue a name.
            const continuesPath = PATH_CONTINUATION.has(text[i + 2]);
            const isPack =
                !continuesPath &&
                ((left >= 0 && LEFT.has(text[left])) ||
                    (left >= 0 && wordEndingAt(text, left) === 'when'));
            if (isPack) {
                out += '...';
                ++changed;
                i += 1;
                continue;
            }
        }
        out += text[i];
    }
    return { text: out, changed };
}

const write = process.argv.includes('--write');
let files = 0;
let changes = 0;
for (const root of ROOTS) {
    const full = path.join(CWD, root);
    let stat;
    try {
        stat = statSync(full);
    } catch {
        continue;
    }
    const entries = stat.isDirectory() ? [...walk(full)] : [full];
    for (const file of entries) {
        const before = readFileSync(file, 'utf8');
        const after = migrate(before);
        if (after.changed === 0) {
            continue;
        }
        ++files;
        changes += after.changed;
        console.log(`${path.relative(CWD, file).replaceAll('\\', '/')}: ${after.changed}`);
        if (write) {
            writeFileSync(file, after.text, 'utf8');
        }
    }
}
console.log(
    `${write ? 'rewrote' : 'would rewrite'} ${changes} pack spelling(s) in ${files} file(s)`,
);
