#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Looks for the leftovers of an unfinished merge in every tracked text file: the hunk head, the
// diff3 base block, the separator and the tail.
//
// Why this exists beside the one-liner we used to type: on 2026-11-02 a merge that had been aborted
// was committed as 407a7ee9f, and test/plans/feat/trans/cml.plan.toml went into the tree carrying
// the diff3 base block `||||||| bbc9b2b65`. The gate we ran was `git grep -n '^<<<<<<< '`, which
// cannot see that marker, nor a trailing separator or tail: the half of a conflict below the base is
// the half nobody looks at, and a plan file with a base block in it still parses far enough to be
// reported as a case. A bare `=======` is not a marker by itself -- in Markdown it is a setext
// heading underline -- so it counts only where a hunk head or a base block precedes it in the same
// block.
//
// The check reads the tree's own files, and the tree is not still while it is read: the runner gives
// every test a directory under `tmp/test/run-<pid>/` and removes it as the test ends (that is
// `test/tools/runner-output.mjs`'s scratch root, git-ignored, so nothing under it can be committed).
// On 2026-11-22 the `linux-x64-nojit` leg of CI 44978093664 failed here with `ENOENT: no such file
// or directory, scandir '.../tmp/test/run-5046/tools.plan_refs_sweep/...'`: another plan's test had
// just removed its own directory between this walk listing the parent and entering the child, and
// the throw left stdout empty -- a gate that said "a merge was left unfinished" about a tree with no
// merge in it. `tmp/` is therefore skipped like `out/` and `test/results/`, and a directory or file
// that is gone when the walk reaches it is read as gone rather than as a failure.
//
//   node scripts/conflict-markers.mjs             check the tree, print every marker it finds
//   node scripts/conflict-markers.mjs --self-test on a probe file with all four forms, catch four
//                                                lines; the old one-liner catches one; on a
//                                                miniature tree, skip `tmp/` and read the file beside
//                                                it; and read a directory that is gone as gone

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..');
// `tmp` is the runner's scratch root (`tmp/test/run-<pid>/<plan>/<test>`, one directory per test,
// removed as the test ends) and is git-ignored: a marker written there is not a merge left in the
// tree, and the directory it stands in is the one that can vanish under the walk. `--self-test`
// writes its probe there too, through CAMEL_TEST_TMPDIR, and the four forms of that probe are
// exactly what this check reports.
const skipDirectories = new Set([
    '.git', 'out', 'node_modules', 'build', '.cache', 'test/results', 'tmp',
]);
const textExtensions = new Set([
    '.cml', '.cpp', '.h', '.hpp', '.c', '.mjs', '.js', '.toml', '.md', '.yml', '.yaml', '.json',
    '.txt', '.cmake', '.py', '.ps1', '.sh', '.in', '.cfg',
]);

// A path that is not there when the walk reaches it was never part of the tree this check reads.
// Only the two codes that mean "gone" are read that way -- a file this check may not open is still
// a failure, because a marker in an unreadable file is a marker nobody saw.
function gone(error) {
    return error.code === 'ENOENT' || error.code === 'ENOTDIR';
}

function walk(directory, files, base = root) {
    let entries;
    try {
        entries = fs.readdirSync(directory, { withFileTypes: true });
    } catch (error) {
        if (gone(error)) return;
        throw error;
    }
    for (const entry of entries) {
        const full = path.join(directory, entry.name);
        const relative = path.relative(base, full).split(path.sep).join('/');
        if (entry.isDirectory()) {
            if (skipDirectories.has(entry.name) || skipDirectories.has(relative)) continue;
            walk(full, files, base);
            continue;
        }
        if (!textExtensions.has(path.extname(entry.name))) continue;
        files.push({ full, relative });
    }
}

function markers(visited) {
    const problems = [];
    for (const file of visited) {
        let text;
        try {
            text = fs.readFileSync(file.full, 'utf8');
        } catch (error) {
            if (gone(error)) continue;
            throw error;
        }
        if (!text.includes('<<<<<<<') && !text.includes('|||||||') && !text.includes('>>>>>>>')) {
            // A file may still carry a stray `=======`; it is only a marker if a hunk head precedes
            // it, and without any `<<<<<<<` or `|||||||` in the file there is no hunk.
            continue;
        }
        const lines = text.split('\n');
        let recentHunkHead = false;
        for (let i = 0; i < lines.length; i += 1) {
            const line = lines[i];
            if (/^<<<<<<< /.test(line)) {
                problems.push(`${file.relative}:${i + 1}: conflict head: ${line}`);
                recentHunkHead = true;
                continue;
            }
            if (/^\|\|\|\|\|\|\| /.test(line)) {
                problems.push(`${file.relative}:${i + 1}: diff3 base block: ${line}`);
                recentHunkHead = true;
                continue;
            }
            if (/^>>>>>>> /.test(line)) {
                problems.push(`${file.relative}:${i + 1}: conflict tail: ${line}`);
                recentHunkHead = true;
                continue;
            }
            if (/^=======$/.test(line) && recentHunkHead) {
                problems.push(`${file.relative}:${i + 1}: conflict separator`);
            }
        }
    }
    return problems;
}

const visited = [];
walk(root, visited);
const problems = markers(visited);

if (process.argv.includes('--self-test')) {
    // The reverse reading: build a file that carries each of the four forms, prove this script finds
    // them, and prove the old one-liner finds only the first. The probe is written under the system
    // temp root so a run of the gate never sees it, in a directory of its own: a fixed name there
    // was shared by every run of the gate on a host at once, and a write or a removal by one run
    // landing between another's write and its read came back as a truncated or vanished probe --
    // `catches 0 line(s)` or `catches 1 line(s)`, red on a leg where nothing was wrong with the
    // rule. The directory is per run, so the reading is this run's own, as `CAMEL_TEST_TMPDIR` is
    // for every other case that writes a file.
    const base = process.env.CAMEL_TEST_TMPDIR
        ? path.resolve(process.env.CAMEL_TEST_TMPDIR)
        : os.tmpdir();
    fs.mkdirSync(base, { recursive: true });
    const scratch = fs.mkdtempSync(path.join(base, 'camel-conflict-probe-'));
    const probe = path.join(scratch, 'probe.txt');
    fs.writeFileSync(
        probe,
        ['clean line', '<<<<<<< HEAD', 'ours', '||||||| bbc9b2b65', 'base', '=======', 'theirs', '>>>>>>> theirs', 'clean tail', ''].join('\n'),
        'utf8',
    );
    const probeLines = fs.readFileSync(probe, 'utf8').split('\n');
    const caught = [];
    let head = false;
    probeLines.forEach((line, i) => {
        if (/^<<<<<<< /.test(line)) { caught.push(`head:${i + 1}`); head = true; }
        else if (/^\|\|\|\|\|\|\| /.test(line)) caught.push(`base:${i + 1}`);
        else if (/^>>>>>>> /.test(line)) caught.push(`tail:${i + 1}`);
        else if (/^=======$/.test(line) && head) caught.push(`separator:${i + 1}`);
    });
    const oldCaught = probeLines.filter((line) => /^<<<<<<< /.test(line)).length;
    console.log(`self-test: this rule catches ${caught.length} line(s) [${caught.join(', ')}], the old one-liner catches ${oldCaught}`);

    // What the walk of *this* repository does, on a miniature tree: the same four forms under
    // `tmp/test/run-1/` are not reported (that directory is the runner's scratch, and the 2026-11-22
    // CI failure was a walk that entered one as its test removed it), and the same text written into
    // a directory of the tree -- `docs` here, a plain file beside the scratch -- is reported, all
    // four lines. The claim is about the walk and not about a Markdown page, so the file is `.txt`.
    const four = ['clean line', '<<<<<<< HEAD', 'ours', '||||||| bbc9b2b65', 'base', '=======', 'theirs', '>>>>>>> theirs', 'clean tail', ''].join('\n');
    const branch = path.join(scratch, 'tree');
    const inTree = path.join(branch, 'docs', 'draft.txt');
    fs.mkdirSync(path.join(branch, 'tmp', 'test', 'run-1', 'plan-refs'), { recursive: true });
    fs.mkdirSync(path.dirname(inTree), { recursive: true });
    fs.writeFileSync(path.join(branch, 'tmp', 'test', 'run-1', 'plan-refs', 'probe.txt'), four, 'utf8');
    fs.writeFileSync(inTree, four, 'utf8');
    const inBranch = [];
    walk(branch, inBranch, branch);
    const branchProblems = markers(inBranch);
    const reported = 'docs/draft.txt';
    const scratchRead = branchProblems.length === 4
        && branchProblems.every((problem) => problem.startsWith(`${reported}:`));
    console.log(`self-test: on a miniature tree the walk reports ${branchProblems.length} line(s), `
        + `all in ${reported} and none under tmp/test/run-1/: ${scratchRead}`);

    // A directory that is gone when the walk reaches it: the shape of the 2026-11-22 failure, which
    // was a throw and an empty stdout rather than a finding. It must read as nothing found.
    let vanishedOk = true;
    const vanished = [];
    try {
        walk(path.join(branch, 'gone'), vanished, branch);
    } catch (error) {
        vanishedOk = false;
    }
    const vanishedRead = vanishedOk && vanished.length === 0;
    console.log(`self-test: a directory that is gone is read as gone, not as a failure: ${vanishedRead}`);

    fs.rmSync(scratch, { recursive: true, force: true });
    const ok = caught.length === 4 && oldCaught === 1 && scratchRead && vanishedRead;
    console.log(ok ? 'self-test: the old pattern misses three of the four forms' : 'self-test: FAILED');
    process.exit(ok ? 0 : 1);
}

if (problems.length > 0) {
    for (const problem of problems) console.log(problem);
    console.log(`${problems.length} conflict marker(s) in ${visited.length} file(s): a merge was left unfinished`);
    process.exit(1);
}
console.log(`no conflict markers: none of ${visited.length} text file(s) carries a hunk head, a base block, a separator or a tail`);