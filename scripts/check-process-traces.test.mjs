// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Every way a process trace has actually been written, one file each -- and the other direction.
 *
 * The checker exists because a comment may say what the code does and why, not how the project got
 * there. When it read `src/` only it reported green while the same day's merges left work item
 * numbers in plans, lane codes in `known_fail_owner`, and dated rulings in sixty `desc` fields. So
 * each case here is a spelling that did occur, in the tree and in the file type where it occurred:
 * a C++ comment, a test plan's `desc`, a PowerShell comment, a case file's comment. A pattern that
 * stops matching one of them is a pattern that went quietly out of date, and this is the file that
 * notices.
 *
 * The verdict is about a **diff**, so every case is written as one: a repository whose base commit
 * is clean, a commit that adds the case, and the checker asked what that commit adds. A tree that
 * already carries traces is the state the project is in, and refusing a push over it refuses the
 * author of the lines that are not being pushed -- the tests below hold both halves of that: an
 * inherited trace is not refused, and an added one is.
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
    // A work root under a concrete home directory is one host's directory, not the layout the tools
    // define: this is the row of that manifest as it was written on that machine.
    ['internal/tools', 'camel-cleanup.rows.sh', '# row  checkout\t/home/u/camel-work/tmp/lane-1/src\n'],
    ['test', 'plans/feat/mm/writings.plan.toml', '[[test]]\nname = "x"\ndesc = "The ruling of 2026-11-15 stopped asking it."\n'],
    ['test', 'plans/feat/mm/other.plan.toml', '[[test]]\nname = "y"\nknown_fail_owner = "lane-shapeext"\n'],
    ['test', 'plans/feat/mm/third.plan.toml', '[[test]]\nname = "z"\n# The author\'s ruling of 2026-11-23 is why.\n'],
    ['test', 'cases/compile/loop.cml', '// The notation the author gave on 2026-11-23.\n'],
    ['test', 'cases/compile/tuple.cml', '// It was 当时 read the other way round.\n'],
    ['test', 'cases/compile/branch.cml', '// Green today, measured 2026-11-05: exit 0.\n'],
    // The name is what the other entries cite and what a reader quotes, so a trace in one is refused
    // where it was written, not only in the `desc` beside it.
    ['test', 'plans/feat/mm/named.plan.toml', '[[tests]]\nname = "extract_D-1123-78"\ndesc = "Two writings of `a + 1` are one value."\n'],
    // A calendar date is the record of *when*, and the four ways a comment spends one -- each line
    // below is one of them, taken from the tree where it was found. A date is not refused because it
    // is a date: it is refused because the sentence around it says what the project did or measured
    // on that day, and a reader a year later cannot tell which tree the claim stood on. The next
    // reader who wants the count lower meets these four cases: they are here so that the change is
    // made in the comments, where the claim stays and the day goes, and not by teaching the pattern
    // that a date is not its business.
    //
    // A status that ended on the day.
    ['test', 'plans/feat/compile/ended.plan.toml', '[[test]]\nname = "k"\n# This case was marked known_fail until 2026-11-04, against the rule.\n'],
    // A reading taken on the day.
    ['test', 'cases/compile/loaded_host.cml', '// Measured 2026-11-04 on a loaded host (1 m load 20.8-21.0): value= 3.0.\n'],
    // A rule dated instead of stated -- the day stands where the rule's content belongs.
    ['test', 'cases/compile/dated_rule.cml', '// The transposed product is a Gemm instead of a MatMul beside a Transpose (rule of 2026-10-26).\n'],
    // An incident of the tooling on the day.
    ['internal/tools', 'row-drop.test.ps1', '# A bulk re-record on a tree missing seven `.cml` files deleted their rows on 2026-11-17.\n'],
    // A date that is a segment of a page name: the name is how a later comment cites the page, so
    // the day outlives the page.
    ['test', 'plans/feat/opt/page.plan.toml', '[[test]]\nname = "p"\ndesc = "Difference test for the page `2026-11-15-can-a-commit-depend-on-unrelated-writes`."\n'],
];

const clean = [
    ['src', 'kernel/core/mm/mm.cpp', '// A coercion decides with 5.4: a rank-zero operand spreads as that value.\n'],
    ['test', 'plans/feat/mm/clean.plan.toml', '[[test]]\nname = "w"\ndesc = "Two writings of `a + 1` are one value."\nprogram = "let d = 2026-11-15"\n'],
    // The work root the tools define, written the way they write it: rooted at `$HOME` or `~`, or
    // under `...` in a quoted placeholder. That is the layout this script works on, not a record of
    // where somebody worked; only a concrete `/home/<name>/` prefix is the record.
    ['internal/tools', 'camel-cleanup.laid-out.sh', '# The root is $HOME/camel-work/tmp and the bare is ~/camel-work/Camel.git.\n'],
    // The same two lines the date cases refuse, with the day gone and the claim kept: what a comment
    // looks like after the record is removed. A test that only plants traces never says what the
    // clean side of a date is, and the answer is not "a date that looks harmless" -- it is the day
    // deleted, which is why these two pass and the cases above do not.
    ['test', 'plans/feat/compile/rewritten.plan.toml', '[[test]]\nname = "k"\n# This case passes against the rule: it cannot pass with a mark, an owner and a reason.\n'],
    ['internal/tools', 'camel-cleanup.measured.sh', '# 48 compile jobs on a 32-core host (load 88).\n# Measured on a loaded host (1 m load 20.8-21.0): 47750 ms of CPU.\n'],
    // A date or a hash that is *data* -- inside a string literal, or in a plan's `program` text --
    // is not a comment, and refusing it would teach whoever meets the check to spell the data
    // differently.
    ['src', 'kernel/ops/data.h', 'const char* kStamp = "2026-11-15 D-1124-78";\n'],
];

function git(root, argv, env = {}) {
    return execFileSync('git', ['-C', root, ...argv], {
        encoding: 'utf8',
        env: { ...process.env, GIT_AUTHOR_NAME: 'test', GIT_AUTHOR_EMAIL: 'test@example.invalid',
            GIT_COMMITTER_NAME: 'test', GIT_COMMITTER_EMAIL: 'test@example.invalid', ...env },
    }).trim();
}

function write(root, dir, name, text) {
    const file = path.join(root, dir, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text, 'utf8');
}

/**
 * A repository with one base commit, and the commit that adds `files` on top of it.
 *
 * The base is what a branch inherits: `base` carries `inherited`, which is how a trace another
 * change wrote is put in front of the checker without being part of the diff.
 */
function repo({ inherited = [], files = [], message = 'change' } = {}) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'traces-'));
    git(root, ['init', '--quiet', '--initial-branch=trunk']);
    write(root, 'src', 'kernel/clean_base.h', '// The base commit of every case here.\n');
    for (const [dir, name, text] of inherited) write(root, dir, name, text);
    git(root, ['add', '-A']);
    git(root, ['commit', '--quiet', '-m', 'base']);
    const base = git(root, ['rev-parse', 'HEAD']);
    for (const [dir, name, text] of files) write(root, dir, name, text);
    git(root, ['add', '-A']);
    git(root, ['commit', '--quiet', '--allow-empty', '-m', message]);
    const head = git(root, ['rev-parse', 'HEAD']);
    return { root, base, head };
}

function run(root, extra = []) {
    try {
        const stdout = execFileSync(process.execPath, [checker, '--root', root, ...extra], { encoding: 'utf8' });
        return { status: 0, stdout };
    } catch (error) {
        return { status: error.status, stdout: `${error.stdout ?? ''}${error.stderr ?? ''}` };
    }
}

function judged({ root, base, head }) {
    return run(root, ['--added-from', base, '--head', head]);
}

// The lines a report names, so two readings of one tree can be compared line for line.
function named(stdout) {
    return stdout.split('\n').filter((line) => /^\s{2}\S.*:\d+: /.test(line));
}

test('every spelling that occurred is caught, in the tree and file type it occurred in', () => {
    for (const [dir, name, text] of cases) {
        const tree = repo({ files: [[dir, name, text]] });
        const { status, stdout } = judged(tree);
        const rel = path.join(dir, name).split(path.sep).join('/');
        assert.equal(status, 1, `${rel} should be refused, but the check passed: ${text.trim()}\n${stdout}`);
        assert.ok(stdout.includes(rel), `${rel} should be named in the report, got: ${stdout}`);
        fs.rmSync(tree.root, { recursive: true, force: true });
    }
});

test('a date or a hash that is data is not a trace', () => {
    const tree = repo({ files: clean });
    const { status, stdout } = judged(tree);
    assert.equal(status, 0, `data is not a comment, but the check refused it: ${stdout}`);
    fs.rmSync(tree.root, { recursive: true, force: true });
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
    const tree = repo({ files: [['src', 'stdlib/tiles/src/compiler.cpp', '// A legal graph outside the initial lane-local subset remains a CPU region.\n']] });
    const { status, stdout } = judged(tree);
    assert.equal(status, 1, 'the pattern still refuses it: it was the comment that changed, not the check');
    assert.ok(stdout.includes('lane-local'), `the report should name the word: ${stdout}`);
    fs.rmSync(tree.root, { recursive: true, force: true });
});

test('a trace the branch inherited is not refused, and a doc-only commit passes over it', () => {
    // The state the project is in: somebody else's trace is already in the tree, and this push
    // changes a document. Refusing it names a file its author never opened, and the reading that
    // catches the defect -- "the baseline only goes down" -- needs a second table that the people
    // refused by it cannot lower.
    const tree = repo({
        inherited: [['src', 'kernel/ops/older.h', '// The record is the operator\'s own (D-1123-78).\n']],
        files: [['docs', 'note.md', '# A document\n\nThe push changes nothing a comment check reads.\n']],
        message: 'docs: a document',
    });
    const { status, stdout } = judged(tree);
    assert.equal(status, 0, `an inherited trace is not this push's, but the check refused it: ${stdout}`);
    assert.match(stdout, /the lines this push adds carry none/, `the reading should say nothing was added: ${stdout}`);
    fs.rmSync(tree.root, { recursive: true, force: true });
});

test('a push that adds a trace of its own is refused, over the same inherited one', () => {
    // The other half of the pair above: the same inherited trace, and one line of this change
    // writes a new one. Two readings that agree here would mean the check reads nothing.
    const tree = repo({
        inherited: [['src', 'kernel/ops/older.h', '// The record is the operator\'s own (D-1123-78).\n']],
        files: [
            ['docs', 'note.md', '# A document\n'],
            ['src', 'kernel/ops/newer.h', '// Measured 2026-11-15, and it held (C-1126-1).\n'],
        ],
    });
    const { status, stdout } = judged(tree);
    assert.equal(status, 1, `a trace this push adds must be refused: ${stdout}`);
    assert.ok(stdout.includes('src/kernel/ops/newer.h'), `the added line should be named: ${stdout}`);
    assert.ok(!stdout.includes('src/kernel/ops/older.h'), `the inherited line must not be named: ${stdout}`);
    fs.rmSync(tree.root, { recursive: true, force: true });
});

test('a commit that adds no line is passed', () => {
    // Nothing between the tip and the head: there is no line for this push to be answerable for.
    const tree = repo({ inherited: [['src', 'kernel/ops/older.h', '// The record is the operator\'s own (D-1123-78).\n']] });
    const { status, stdout } = judged(tree);
    assert.equal(status, 0, `no added line means no verdict, but it refused: ${stdout}`);
    assert.match(stdout, /adds 0 line/, `the reading should say nothing was added: ${stdout}`);
    fs.rmSync(tree.root, { recursive: true, force: true });
});

test('a base this repository does not have is not a pass', () => {
    // A diff that could not be read judged nothing, and "nothing judged" must not print like a
    // clean tree: the caller has to fetch the tip and push again.
    const tree = repo({ files: clean });
    const { status, stdout } = run(tree.root, ['--added-from', '0123456789012345678901234567890123456789', '--head', tree.head]);
    assert.equal(status, 3, `an unreadable base is its own answer, got ${status}: ${stdout}`);
    assert.match(stdout, /could not be read/, `the reading should say the diff was not read: ${stdout}`);
    fs.rmSync(tree.root, { recursive: true, force: true });
});

test('the whole tree is listed and refuses no push', () => {
    // The report the work of taking the inherited debt down is read from. It names the same lines
    // the verdict would, and it says it decided nothing -- a report that read like a verdict is a
    // push refused for somebody else's file.
    const tree = repo({ inherited: cases.slice(0, 4) });
    const whole = run(tree.root);
    assert.equal(whole.status, 0, `the whole-tree report must not refuse: ${whole.stdout}`);
    assert.match(whole.stdout, /4 in the tree/, `every planted trace should be named: ${whole.stdout}`);
    assert.match(whole.stdout, /refuses no push/, 'the report has to say it decided nothing');
    assert.equal(named(whole.stdout).length, 4, 'every planted trace should be named on its own line');
    fs.rmSync(tree.root, { recursive: true, force: true });
});

test('a whole-tree run that read nothing is red, and says so', () => {
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