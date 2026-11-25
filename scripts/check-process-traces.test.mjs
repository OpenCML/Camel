// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Every way a process trace has actually been written, one file each.
 *
 * The checker exists because a comment may say what the code does and why, not how the project got
 * there. When it read `src/` only it reported green while the same day's merges left work item
 * numbers in plans, lane codes in `known_fail_owner`, and dated rulings in sixty `desc` fields. So
 * each case here is a spelling that did occur, in the tree and in the file type where it occurred:
 * a C++ comment, a test plan's `desc`, a PowerShell comment, a case file's comment. A pattern that
 * stops matching one of them is a pattern that went quietly out of date, and this is the file that
 * notices.
 *
 * The last two cases are the other direction: a date or a hash that is *data* -- inside a string
 * literal, or in a plan's `program` text -- is not a comment, and refusing it would teach whoever
 * meets the check to spell the data differently.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const checker = path.join(path.dirname(fileURLToPath(import.meta.url)), 'check-process-traces.mjs');

// [tree, file name, file text] -- one trace each, written where that kind of trace was found.
const cases = [
    ['src', 'kernel/ops/note.h', '// The record is the operator\'s own (D-1123-78).\n'],
    ['src', 'kernel/ops/pending.h', '// 待审：this member is added, the declarations stand.\n'],
    ['src', 'compile/gir/builder.cpp', '// Both faces this lane reads carry such graphs.\n'],
    ['src', 'kernel/runtime/writings.h', '// The kernel only asks it for the choice (C-1123-17).\n'],
    ['scripts', 'some-tool.mjs', '// A lane code crept in here: lane-shapeext owned the measurement.\n'],
    ['scripts', 'other-tool.mjs', '// The tree is named by the caller (iron rule 17).\n'],
    ['internal/tools', 'merge-lane.ps1', '# The second seat came after `f26529338` broke the first.\n'],
    ['internal/tools', 'camel-cleanup.sh', '# Measured on 2026-11-15, and it held.\n'],
    ['test', 'plans/feat/mm/writings.plan.toml', '[[test]]\nname = "x"\ndesc = "The ruling of 2026-11-15 stopped asking it."\n'],
    ['test', 'plans/feat/mm/other.plan.toml', '[[test]]\nname = "y"\nknown_fail_owner = "lane-shapeext"\n'],
    ['test', 'plans/feat/mm/third.plan.toml', '[[test]]\nname = "z"\n# The author\'s ruling of 2026-11-23 is why.\n'],
    ['test', 'cases/compile/loop.cml', '// The notation the author gave on 2026-11-23.\n'],
    ['test', 'cases/compile/tuple.cml', '// It was 当时 read the other way round.\n'],
    ['test', 'cases/compile/branch.cml', '// Green today, measured 2026-11-05: exit 0.\n'],
    // The name is what the other entries cite and what a reader quotes, so a trace in one is refused
    // where it was written, not only in the `desc` beside it.
    ['test', 'plans/feat/mm/named.plan.toml', '[[tests]]\nname = "extract_D-1123-78"\ndesc = "Two writings of `a + 1` are one value."\n'],
];

const clean = [
    ['src', 'kernel/core/mm/mm.cpp', '// A coercion decides with 5.4: a rank-zero operand spreads as that value.\n'],
    ['test', 'plans/feat/mm/clean.plan.toml', '[[test]]\nname = "w"\ndesc = "Two writings of `a + 1` are one value."\nprogram = "let d = 2026-11-15"\n'],
];

function tree(files) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'traces-'));
    for (const [dir, name, text] of files) {
        const file = path.join(root, dir, name);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, text, 'utf8');
    }
    return root;
}

function run(root, extra = []) {
    try {
        const stdout = execFileSync(process.execPath, [checker, '--root', root, ...extra], { encoding: 'utf8' });
        return { status: 0, stdout };
    } catch (error) {
        return { status: error.status, stdout: `${error.stdout ?? ''}${error.stderr ?? ''}` };
    }
}

// The lines a report names, so a listing and a refusal can be compared line for line.
function named(stdout) {
    return stdout.split('\n').filter((line) => /^\s{2}\S.*:\d+: /.test(line));
}

test('every spelling that occurred is caught, in the tree and file type it occurred in', () => {
    for (const [dir, name, text] of cases) {
        const root = tree([[dir, name, text]]);
        const { status, stdout } = run(root);
        const rel = path.join(dir, name);
        assert.equal(status, 1, `${rel} should be refused, but the check passed: ${text.trim()}`);
        assert.ok(stdout.includes(rel), `${rel} should be named in the report, got: ${stdout}`);
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test('a date or a hash that is data is not a trace', () => {
    const root = tree(clean);
    const { status, stdout } = run(root);
    assert.equal(status, 0, `data is not a comment, but the check refused it: ${stdout}`);
    fs.rmSync(root, { recursive: true, force: true });
});

test('the GPU lane domain in the tiles compiler is refused on purpose, and the comment changed', () => {
    // One word, two domains. In the loop lowering of `src/stdlib/tiles/src/compiler.cpp#loop`
    // the sentence was "a legal graph outside the initial lane-local subset remains a CPU
    // region" -- GPU lane domains, two lines after a message about lane domains, with nothing to
    // do with a project lane. The pattern cannot tell that word from a project lane code, and the
    // two ways out are not equal: a whitelist for one path grows until nobody reads it, while
    // rewording says the same thing ("the subset the initial lanes handle") and leaves the
    // pattern sharp. The comment was rewritten; the pattern was not.
    //
    // This case is here so the next person who meets it meets the decision as well -- and does not
    // "fix" the checker by teaching it the exception. The word itself is in the data below, not in
    // this comment: a comment that spells it would be the very hit it explains.
    const root = tree([['src', 'stdlib/tiles/src/compiler.cpp', '// A legal graph outside the initial lane-local subset remains a CPU region.\n']]);
    const { status, stdout } = run(root);
    assert.equal(status, 1, 'the pattern still refuses it: it was the comment that changed, not the check');
    assert.ok(stdout.includes('lane-local'), `the report should name the word: ${stdout}`);
    fs.rmSync(root, { recursive: true, force: true });
});

test('a run that read nothing is red, and says so', () => {
    // 0 traces and 0 files read print the same otherwise, so a checker whose patterns stopped
    // matching anything would report green forever.
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'traces-empty-'));
    const { status, stdout } = run(root);
    assert.equal(status, 1, 'an empty tree proves nothing and must not pass');
    assert.ok(stdout.includes('not read'), `the report should say nothing was read: ${stdout}`);
    fs.rmSync(root, { recursive: true, force: true });
});

test('the trees read are the four that carry traces', () => {
    const source = fs.readFileSync(checker, 'utf8');
    const line = source.match(/^const dirs = \[(.+)\];$/m);
    assert.ok(line, 'the checker should name its directories on one line');
    for (const dir of ['src', 'scripts', 'internal/tools', 'test']) {
        assert.ok(line[1].includes(`'${dir}'`), `${dir} should be read`);
    }
});

test('--list names the same lines and judges nothing, and the same tree without it is refused', () => {
    // A tree that carries traces has to be read before it can be worked through, and the reading has
    // to be the one the refusal would print: two lists of the same tree that disagree are two
    // patterns, and the one that is edited later is whichever a reader happens to open.
    const root = tree(cases.slice(0, 4));
    const judged = run(root);
    const listed = run(root, ['--list']);
    assert.equal(judged.status, 1, `a trace must be refused without the flag: ${judged.stdout}`);
    assert.equal(listed.status, 0, `--list must list and not judge, and it exited ${listed.status}: ${listed.stdout}`);
    assert.deepEqual(named(listed.stdout), named(judged.stdout), 'a listing and a refusal name the same lines');
    assert.equal(named(listed.stdout).length, 4, 'every planted trace should be named');
    assert.match(listed.stdout, /nothing judged/, 'a listing has to say it judged nothing, or it reads as a pass');
    fs.rmSync(root, { recursive: true, force: true });
});

test('--list of a tree that read nothing is still red', () => {
    // The flag lifts the verdict on traces, not the rule that a run which read nothing checked
    // nothing: an empty listing is exactly what a broken pattern prints.
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'traces-empty-list-'));
    const { status, stdout } = run(root, ['--list']);
    assert.equal(status, 1, '--list on a tree that could not be read proves nothing and must not pass');
    assert.ok(stdout.includes('not read'), `the report should say nothing was read: ${stdout}`);
    fs.rmSync(root, { recursive: true, force: true });
});

test('a clean tree stays clean with --list', () => {
    const root = tree(clean);
    const { status, stdout } = run(root, ['--list']);
    assert.equal(status, 0, `a clean tree must pass with the flag too: ${stdout}`);
    assert.match(stdout, /no trace/, `the reading should be printed with the verdict: ${stdout}`);
    fs.rmSync(root, { recursive: true, force: true });
});