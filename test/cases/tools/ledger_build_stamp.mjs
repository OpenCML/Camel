#!/usr/bin/env node
/**
 * `--check-ledger` must not read numbers from a binary it cannot prove is this tree's (H213).
 *
 * The checker splits its claims in two. The declaration modules, declared names and
 * declarations, computed names and signature files come from the tree, so a fresh worktree with no
 * build still checks them. The registered numbers come from `camel inspect --ops`, and the checker
 * may read them only when `build-info.json` names HEAD and this tree's sources. Before H213 a
 * stale `out/latest` made the checker print 38 page problems on a correct tree, and a missing
 * build made it print nothing useful; both taught people to ignore it.
 *
 * Four runs, each against a scratch install named by `CAMEL_EXE` (never the real `out/latest`):
 *
 *   1. no install: exit 0, the nine build-dependent claims are listed as skipped, the
 *      tree-decidable half is checked, and the run is not called stale;
 *   2. a stamp naming another commit: exit 4, one line naming that commit, HEAD and
 *      `camel-remote.ps1`;
 *   3. the same stamp with `--allow-stale-build`: the warning names both commits and the run
 *      continues (it then fails to run the non-existent scratch binary, which is fine: the
 *      assertion is the warning and the attempt);
 *   4. no install with `--require-build` (what the plan uses): exit 5, so the plan cannot pass
 *      over a missing build and silently skip the binary half.
 *
 * Red proof for the tree half: set 26 -> 27 in `docs/technical/48_declaration_migration.md` §2.1
 * and run `node scripts/stdlib-declarations.mjs --check-ledger` with no build; it exits 1 with
 * `48_declaration_migration.md:42: declaration modules: page says 27, tree has 26`. The scratch
 * installs keep that proof independent of whatever `out/latest` this machine happens to have.
 *
 * Usage: node ledger_build_stamp.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
const SCRIPT = path.join(REPO_ROOT, 'scripts', 'stdlib-declarations.mjs')
const HEAD = (spawnSync('git', ['-C', REPO_ROOT, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).stdout || '').trim()
const OTHER = 'deadbeef'.repeat(5)

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

function run(installDir, extra = []) {
    const camel = path.join(installDir, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
    return spawnSync(process.execPath, [SCRIPT, '--check-ledger', ...extra], {
        env: { ...process.env, CAMEL_EXE: camel },
        encoding: 'utf8',
        timeout: 120000,
    })
}

function stamp(installDir, revision, dirty) {
    fs.mkdirSync(path.join(installDir, 'bin'), { recursive: true })
    const info = {
        schema: 1,
        config: 'Release',
        tag: 'latest',
        builtAt: '2026-10-22T00:00:00.000Z',
        revision,
        dirty,
        dirtyCount: dirty ? 1 : 0,
        dirtyPaths: [],
        git: true,
        fingerprint: { version: 1, roots: ['src'], files: 1, hash: '0'.repeat(64), fileHashes: {} },
    }
    fs.writeFileSync(path.join(installDir, 'build-info.json'), JSON.stringify(info))
}

const BASE = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-ledger-build-'))
try {
    // 1. A tree with no build of this commit: the tree half is still checked, the binary half is
    //    announced as skipped, and the run is not refused for a build it does not have. The exit
    //    code is the tree half's own answer, so a stale *page* in the repo under test is still a
    //    failure here; what must never appear is a refusal about the build.
    const none = path.join(BASE, 'none')
    const noBuild = run(none)
    check(
        'no build is not refused for a build it does not have',
        !noBuild.stdout.includes('refusing the build-dependent numbers') && !noBuild.stdout.includes('using a stale build'),
        `exit ${noBuild.status}` + "\n" + noBuild.stdout + noBuild.stderr,
    )
    check(
        'no build announces the skipped build-dependent claims',
        noBuild.stdout.includes('the build-dependent claims are skipped'),
        noBuild.stdout,
    )
    check(
        'no build names all nine skipped claims',
        noBuild.stdout.includes('skipped 9 build-dependent claim(s)'),
        noBuild.stdout,
    )
    check(
        'no build still checks the tree-decidable half',
        noBuild.stdout.includes("the ledgers' tree-decidable numbers agree with the tree"),
        noBuild.stdout,
    )
    check('no build is not called stale', !noBuild.stdout.includes('the ledgers are stale'), noBuild.stdout)

    // 2. A stamp at another commit is refused, naming both commits and the command that rebuilds.
    const stale = path.join(BASE, 'stale')
    stamp(stale, OTHER, false)
    const refused = run(stale)
    check('a stamp at another commit exits 4', refused.status === 4, `exit ${refused.status}\n${refused.stdout}`)
    check('the refusal names the build commit', refused.stdout.includes(OTHER.slice(0, 10)), refused.stdout)
    check('the refusal names HEAD', HEAD.length === 40 && refused.stdout.includes(HEAD.slice(0, 10)), refused.stdout)
    check('the refusal names camel-remote.ps1', refused.stdout.includes('camel-remote.ps1'), refused.stdout)

    // 3. The explicit override warns with both commits and continues (the scratch binary does not
    //    exist, so the run then reports that it cannot inspect --ops; the warning is the point).
    const allowed = run(stale, ['--allow-stale-build'])
    check('--allow-stale-build warns', allowed.stdout.includes('using a stale build'), allowed.stdout)
    check(
        'the override warning names both commits',
        allowed.stdout.includes(OTHER.slice(0, 10)) && allowed.stdout.includes(HEAD.slice(0, 10)),
        allowed.stdout,
    )
    check('the override continues into the binary half', allowed.status === 1, `exit ${allowed.status}\n${allowed.stdout}`)

    // 4. --require-build is the plan's guard: a missing build is exit 5, not a silent pass over the
    //    claims that need one.
    const required = run(none, ['--require-build'])
    check('--require-build without a build exits 5', required.status === 5, `exit ${required.status}\n${required.stdout}`)
    check(
        '--require-build says no build of this tree',
        required.stdout.includes('--require-build: no build of this tree'),
        required.stdout,
    )
} finally {
    fs.rmSync(BASE, { recursive: true, force: true })
}

if (failed) process.exit(1)
console.log('PASS the ledger checker refuses a stale build, skips one it does not have, and keeps the tree half')
