// Two-line header: name, purpose.
// conflict-markers: no file carries the leftovers of an unfinished merge.
//
// Why this exists beside the one-liner we used to type: on 2026-11-02 a merge that had been aborted
// was committed as 407a7ee9f, and test/plans/feat/trans/cml.plan.toml went into the tree carrying
// the diff3 base block `||||||| bbc9b2b65`. The gate we ran was `git grep -n '^<<<<<<< '`, which
// cannot see that marker, nor a trailing `=======` or `>>>>>>>`: the half of a conflict that sits
// below the base is exactly the half nobody looks at, and a plan file with a base block in it still
// parses far enough to be reported as a case. So all four forms are checked here, and the reason a
// bare `=======` needs care is that in Markdown it is a valid setext heading underline -- hence the
// rule that a `=======` counts only when a `<<<<<<<` or `|||||||` line is inside the same hunk, i.e.
// within the previous few lines.

import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..');
const skipDirectories = new Set(['.git', 'out', 'node_modules', 'build', '.cache', 'test/results']);
const textExtensions = new Set([
    '.cml', '.cpp', '.h', '.hpp', '.c', '.mjs', '.js', '.toml', '.md', '.yml', '.yaml', '.json',
    '.txt', '.cmake', '.py', '.ps1', '.sh', '.in', '.cfg',
]);

function walk(directory, files) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const full = path.join(directory, entry.name);
        const relative = path.relative(root, full).split(path.sep).join('/');
        if (entry.isDirectory()) {
            if (skipDirectories.has(entry.name) || skipDirectories.has(relative)) continue;
            walk(full, files);
            continue;
        }
        if (!textExtensions.has(path.extname(entry.name))) continue;
        files.push({ full, relative });
    }
}

const problems = [];
const visited = [];
walk(root, visited);
for (const file of visited) {
    const text = fs.readFileSync(file.full, 'utf8');
    if (!text.includes('<<<<<<<') && !text.includes('|||||||') && !text.includes('>>>>>>>')) {
        // A file may still carry a stray `=======`; it is only a marker if a hunk head precedes it,
        // and without any `<<<<<<<` or `|||||||` in the file there is no hunk.
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

if (process.argv.includes('--self-test')) {
    // The reverse reading: build a file that carries each of the four forms, prove this script finds
    // them, and prove the old one-liner finds only the first. The temporary file is written under
    // the system temp directory so a run of the gate never sees it.
    const probe = path.join(process.env.TEMP ?? '/tmp', 'camel-conflict-probe.txt');
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
    fs.rmSync(probe, { force: true });
    const ok = caught.length === 4 && oldCaught === 1;
    console.log(ok ? 'self-test: the old pattern misses three of the four forms' : 'self-test: FAILED');
    process.exit(ok ? 0 : 1);
}

if (problems.length > 0) {
    for (const problem of problems) console.log(problem);
    console.log(`${problems.length} conflict marker(s) in ${visited.length} file(s): a merge was left unfinished`);
    process.exit(1);
}
console.log(`no conflict markers: none of ${visited.length} text file(s) carries a hunk head, a base block, a separator or a tail`);