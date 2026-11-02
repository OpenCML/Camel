#!/usr/bin/env node
/**
 * `scripts/find-fix.mjs` -- asking "has somebody already fixed this?" before treating a red as a new
 * defect, and getting an answer that cannot be a stale yes.
 *
 * Twice in one night the branch was one `git log` away: `grad_array_sum` was about to get a probe
 * written for it while the fix sat on `origin/lane-lit2`, and a `GUARD` question was answered the
 * same way. The rule was prose in `agents/test.md`; this pins the tool that carries it.
 *
 * The readings this case takes are on a **throwaway repository**, so they do not depend on which
 * lanes happen to exist here next month:
 *
 *   * a branch that edited the case's entry shows in block 1 as `not merged`;
 *   * a commit that names the case and is already in `develop` shows in block 2 as `merged`;
 *   * a name with no fix anywhere prints three empty blocks and **exits 1** -- an empty reading is
 *     not a pass, and silence would read as "all clear";
 *   * a `fetch` that fails **exits 2**: a stale `refs/remotes` reports a false "no branch has it",
 *     which is exactly the failure that cost a night ("those four commits are on no remote branch"
 *     was true of the cache and false of the server);
 *   * `--offline` says the reading is stale instead of pretending it is fresh.
 *
 * Red when: any of those statuses flips, when a merged commit is labelled `not merged` (or the other
 * way round), when an empty reading exits 0, or when the tool writes to the repository.
 *
 * Run: node find_fix.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
const TOOL = path.join(REPO_ROOT, 'scripts', 'find-fix.mjs')
const SCRATCH =
    process.env.CAMEL_TEST_TMPDIR ||
    (fs.mkdirSync(path.join(REPO_ROOT, 'tmp'), { recursive: true }), fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'find-fix-')))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

function sh(cwd, args) {
    const result = spawnSync('git', args, { cwd, encoding: 'utf8' })
    if (result.status !== 0) throw new Error(`git ${args.join(' ')} failed: ${result.stderr || ''}`)
    return result.stdout.trim()
}

function tool(args) {
    const result = spawnSync(process.execPath, [TOOL, ...args], {
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH },
    })
    return { status: result.status, output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}

const PLAN = 'plans/sample.plan.toml'
function planText(extra = '') {
    return [
        'id = "probe.sample"',
        'tier = "fast"',
        '',
        '[[tests]]',
        'name = "delta_case.nvm"',
        'kind = "behavior"',
        'tool = "node"',
        'case = "../cases/delta.mjs"',
        'expect_exit = 0',
        extra,
        '',
    ].join('\n')
}

// ---- the throwaway repository ----------------------------------------------------------------
const work = path.join(SCRATCH, 'work')
const origin = path.join(SCRATCH, 'origin.git')
fs.rmSync(work, { recursive: true, force: true })
fs.rmSync(origin, { recursive: true, force: true })
fs.mkdirSync(path.join(work, 'plans'), { recursive: true })
sh(SCRATCH, ['init', '--quiet', '--bare', origin])
fs.writeFileSync(path.join(work, 'plans', 'sample.plan.toml'), planText())
fs.writeFileSync(path.join(work, 'README'), 'a tree\n')
sh(work, ['init', '--quiet', '--initial-branch=develop'])
sh(work, ['config', 'user.email', 'probe@example.invalid'])
sh(work, ['config', 'user.name', 'probe'])
sh(work, ['add', '-A'])
sh(work, ['commit', '--quiet', '-m', 'base: the sample plan'])
sh(work, ['remote', 'add', 'origin', origin])
sh(work, ['push', '--quiet', 'origin', 'develop'])

// A lane that edits this case's entry and is not merged.
sh(work, ['checkout', '--quiet', '-b', 'lane-open'])
fs.writeFileSync(path.join(work, 'plans', 'sample.plan.toml'), planText('expect_stdout_contains = ["42"]'))
sh(work, ['add', '-A'])
sh(work, ['commit', '--quiet', '-m', 'panels: give delta_case an output pin'])
sh(work, ['push', '--quiet', 'origin', 'lane-open'])
sh(work, ['checkout', '--quiet', 'develop'])

// A lane whose commit names the case, merged into develop before this reading.
sh(work, ['checkout', '--quiet', '-b', 'lane-merged'])
fs.writeFileSync(path.join(work, 'FIX'), 'the fix\n')
sh(work, ['add', '-A'])
sh(work, ['commit', '--quiet', '-m', 'fix: delta_case sums an array as an array (cotangent)'])
sh(work, ['push', '--quiet', 'origin', 'lane-merged'])
sh(work, ['checkout', '--quiet', 'develop'])
sh(work, ['merge', '--quiet', '--no-ff', '-m', 'Merge lane-merged: the delta_case fix', 'lane-merged'])
sh(work, ['push', '--quiet', 'origin', 'develop'])
sh(work, ['fetch', '--quiet', 'origin'])
sh(work, ['branch', '--quiet', '--set-upstream-to=origin/develop', 'develop'])

const before = sh(work, ['status', '--porcelain'])
const headBefore = sh(work, ['rev-parse', 'HEAD'])

// ---- the reading that finds a fix ------------------------------------------------------------
{
    const out = tool([`${PLAN}:delta_case.nvm,cotangent`, '--repo', work])
    check('a fix that exists is found (exit 0)', out.status === 0, `status ${out.status}\n${out.output}`)
    check('block 1 names the open lane that edits this entry', /\[1\][^\n]*: 1\b/.test(out.output) && /origin\/lane-open/.test(out.output), out.output)
    check('the open lane is not called merged', /lane-open[^\n]*not merged/.test(out.output), out.output)
    check('block 2 carries the merged fix', /fix: delta_case sums an array as an array \(cotangent\)/.test(out.output), out.output)
    const mergedLine = (out.output.split('\n').find((line) => line.includes('sums an array as an array')) || '')
    check('the merged commit is labelled merged, in the merged branch column', /merged \|/.test(mergedLine) && /origin\/lane-merged/.test(mergedLine), mergedLine)
    check('block 3 counts it as merged', /\[3\][^\n]*1 of 1 matching commit\(s\), 0 not merged/.test(out.output), out.output)
}

// ---- the reading that finds nothing -----------------------------------------------------------
{
    const out = tool([`${PLAN}:no_such_case_qqq_2026,zzzz_keyword`, '--repo', work])
    check('an empty reading exits 1, not 0', out.status === 1, `status ${out.status}\n${out.output}`)
    check('it says there are no candidates', /\[no candidates\]/.test(out.output), out.output)
    check('block 1 is empty', /\[1\][^\n]*: 0\b/.test(out.output), out.output)
    check('block 2 is empty', /\[2\][^\n]*: 0\b/.test(out.output), out.output)
    check('block 3 is empty', /\[3\][^\n]*0 of 0/.test(out.output), out.output)
}

// ---- a reading that cannot be trusted ---------------------------------------------------------
{
    const broken = path.join(SCRATCH, 'broken')
    fs.rmSync(broken, { recursive: true, force: true })
    fs.cpSync(work, broken, { recursive: true })
    sh(broken, ['remote', 'set-url', 'origin', path.join(SCRATCH, 'nowhere.git')])
    const out = tool([`${PLAN}:delta_case.nvm`, '--repo', broken])
    check('a failed fetch exits 2', out.status === 2, `status ${out.status}\n${out.output}`)
    check('it says the reading could be stale', /stale/i.test(out.output), out.output)
    check('it does not print a candidate block as if the reading were fresh', !/\[candidates\]/.test(out.output), out.output)
}

// ---- offline says so ---------------------------------------------------------------------------
{
    const out = tool([`${PLAN}:delta_case.nvm`, '--repo', work, '--offline'])
    check('--offline marks the reading stale and still reads', /\[stale\]/.test(out.output) && out.status === 0, `status ${out.status}\n${out.output}`)
}

// ---- it changed nothing ------------------------------------------------------------------------
{
    check('the repository is untouched (no index, worktree or HEAD change)', sh(work, ['status', '--porcelain']) === before && sh(work, ['rev-parse', 'HEAD']) === headBefore, 'the tool wrote to the repository')
}

// ---- bad usage is not a reading ----------------------------------------------------------------
{
    const out = tool(['plans/sample.plan.toml', '--repo', work])
    check('a target without :<case> exits 2 with usage', out.status === 2 && /\[usage\]/.test(out.output), `status ${out.status}\n${out.output}`)
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)