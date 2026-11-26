// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What the push hook says about the process traces a push adds, run as git runs it: the real
 * `.husky/pre-push`, `scripts/hooks/pre-push` and `scripts/check-process-traces.mjs`, copied into a
 * throwaway repository, started by `sh -e` with the refs on stdin.
 *
 * The project's remote is recognised by its URL, so a push to the URL itself and a push under any
 * remote name are judged the same way. A push is judged on the lines it adds: a trace the tree
 * already held does not refuse it, a trace it writes does -- a date of any year, and a bare hash
 * that names a commit of the repository. Hex that names no commit and a constant written in code
 * are not traces. A push that only deletes a ref sends no commit and runs no checker.
 */
import { execFileSync, spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const project = 'git@github.com:OpenCML/Camel.git'
const elsewhere = 'ssh://build.example.invalid/srv/camel.git'
const zeros = '0'.repeat(40)

function gitShell() {
    const execPath = execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim()
    const gitRoot = path.dirname(path.dirname(path.dirname(execPath)))
    const sh = [
        path.join(gitRoot, 'usr', 'bin', 'sh.exe'),
        path.join(gitRoot, 'bin', 'sh.exe'),
        path.join(gitRoot, 'usr', 'bin', 'sh'),
        path.join(gitRoot, 'bin', 'sh'),
    ].find((one) => fs.existsSync(one))
    if (!sh) throw new Error(`no sh beside git (${execPath})`)
    return { sh, gitRoot }
}

const shell = gitShell()
const env = { ...process.env }
for (const key of Object.keys(env)) if (key.toUpperCase() === 'PATH') delete env[key]
env.PATH = [
    path.dirname(process.execPath),
    path.join(shell.gitRoot, 'usr', 'bin'),
    path.join(shell.gitRoot, 'mingw64', 'bin'),
    process.env.PATH ?? process.env.Path ?? '',
]
    .filter(Boolean)
    .join(path.delimiter)

function put(dir, rel, text) {
    const full = path.join(dir, rel)
    fs.mkdirSync(path.dirname(full), { recursive: true })
    fs.writeFileSync(full, text)
}

function git(dir, ...args) {
    return execFileSync('git', ['-C', dir, '-c', 'core.autocrlf=false', '-c', 'user.email=t@example.invalid', '-c', 'user.name=t', ...args], {
        encoding: 'utf8',
    }).trim()
}

/** A repository holding the real hook files and one commit; returns the directory and that commit. */
function fixture(baseSource) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'push-trace-range-'))
    git(dir, 'init', '--quiet', '--initial-branch=main')
    for (const rel of ['.husky/pre-push', 'scripts/hooks/pre-push', 'scripts/check-process-traces.mjs']) {
        put(dir, rel, fs.readFileSync(path.join(root, rel), 'utf8'))
    }
    put(dir, 'src/a.c', baseSource)
    git(dir, 'add', '-A')
    git(dir, 'commit', '--quiet', '-m', 'base')
    return { dir, base: git(dir, 'rev-parse', 'HEAD') }
}

/** Commits `source` as src/a.c and runs the hook for the push base -> that commit. */
function push(fx, source, remote, url, line = null) {
    put(fx.dir, 'src/a.c', source)
    git(fx.dir, 'add', '-A')
    git(fx.dir, 'commit', '--quiet', '-m', 'change')
    const head = git(fx.dir, 'rev-parse', 'HEAD')
    const refs = line ?? `refs/heads/x ${head} refs/heads/x ${fx.base}`
    const run = spawnSync(shell.sh, ['-e', '.husky/pre-push', remote, url], {
        cwd: fx.dir,
        env,
        input: `${refs}\n`,
        encoding: 'utf8',
        timeout: 60000,
    })
    return { status: run.status, text: `${run.stdout ?? ''}${run.stderr ?? ''}` }
}

let checks = 0
let failed = 0
function check(name, ok, detail = '') {
    checks += 1
    if (!ok) failed += 1
    process.stdout.write(`${ok ? 'ok  ' : 'FAIL'} ${name}${ok ? '' : `: ${detail}`}\n`)
}

const cleanBase = 'int main(void) { return 0; }\n'
// The fixtures' comments are built here and not written out, so that this file's own text carries no
// comment marker or date inside a string for the checker to read as a trace.
const note = (text) => `${'/'}${'/'} ${text}\n`
const stamp = (year) => `${year}-03-04`
const dir = []
function run(name, base, source, remote, url, expected, line = null, extra = () => true) {
    const fx = fixture(base)
    dir.push(fx.dir)
    const got = push(fx, typeof source === 'function' ? source(fx) : source, remote, url, line)
    check(name, got.status === expected && extra(got.text), `exit ${got.status}, expected ${expected}: ${got.text.trim().slice(0, 300)}`)
}

try {
    const dated = `${cleanBase}${note(`Measured on ${stamp(2027)} on the loaded host.`)}`
    run('a push by URL to the project remote that adds a dated comment is refused', cleanBase, dated, project, project, 1, null, (t) => /2027-03-04/.test(t))
    run('the same push under a remote named gh is refused', cleanBase, dated, 'gh', project, 1)
    run('the same push under origin is refused', cleanBase, dated, 'origin', project, 1)
    run('a date of any year is a trace (2031)', cleanBase, `${cleanBase}${note(`Held on ${stamp(2031)}.`)}`, 'gh', project, 1)
    run('the same comment pushed to another host is not read', cleanBase, dated, 'host', elsewhere, 0)
    run(
        'a trace the tree already held does not refuse a push that adds a clean line',
        `${cleanBase}${note(`Measured on ${stamp(2026)} on the loaded host.`)}`,
        `${cleanBase}${note(`Measured on ${stamp(2026)} on the loaded host.`)}int other(void) { return 1; }\n`,
        'gh',
        project,
        0,
    )
    run(
        'a bare hash of a commit in a comment is a trace',
        cleanBase,
        (fx) => `${cleanBase}${note(`Written after ${fx.base.slice(0, 9)} went in.`)}`,
        'gh',
        project,
        1,
        null,
        (t) => t.includes('Written after'),
    )
    run(
        'a hex constant in code and hex that names no commit are not',
        cleanBase,
        `${cleanBase}unsigned mask = 0x1a2b3c4d5e6f;\nunsigned wide = 0xdeadbeef1234;\n${note('the mask is 1a2b3c4d5e')}`,
        'gh',
        project,
        0,
    )
    run(
        'a push that only deletes a ref runs no checker',
        `${cleanBase}${note(`Measured on ${stamp(2026)} on the loaded host.`)}`,
        cleanBase,
        'gh',
        project,
        0,
        `(delete) ${zeros} refs/heads/x ${zeros}`,
        (t) => !/scanned|process traces/.test(t),
    )
} finally {
    for (const one of dir) fs.rmSync(one, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 })
}

process.stdout.write(`push-trace-range: ${checks} checks, ${failed} failed\n`)
process.exit(failed ? 1 : 0)
