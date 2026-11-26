// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Four properties of how the runner starts an entry, each read from a real nested run of a fixture
// plan (the fixture lives in `test/results/`, which is gitignored, and `--camel` names an empty
// stand-in file because nothing here starts the compiler):
//   * a `skip_unless` probe that fails skips the entry at once, and it takes no card: a host whose
//     every card is busy still reports the probe's reason, not the device selection's;
//   * a probe that does not return within `skip_unless_timeout_ms` ends the entry with a failure
//     that names the probe, and the run goes on to the entries after it;
//   * `--entry` selects entries in an ordinary run, and a pattern that names nothing is a refusal;
//   * a plan file target runs all of its entries, and says so only when `--tier` was typed.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const WORK = path.join(ROOT, 'test', 'results', 'plan-launch-selftest')
const stubCamel = path.join(WORK, 'install', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const planPath = path.join(WORK, 'inner.plan.toml')
let checks = 0
let failed = 0

function check(name, ok, detail) {
    checks++
    if (ok) {
        console.log(`ok    ${name}`)
    } else {
        failed++
        console.log(`FAIL  ${name}${detail ? `: ${detail}` : ''}`)
    }
}

function entry(name, lines = []) {
    return [
        '[[tests]]',
        `name = "${name}"`,
        'kind = "behavior"',
        'tool = "node"',
        'args = ["-e", "console.log(\'ran\')"]',
        'expect_exit = 0',
        'timeout_ms = 30000',
        ...lines,
        '',
    ]
}

const FAILING_PROBE = 'skip_unless = ["node", "-e", "process.exit(3)"]'
const HANGING_PROBE = 'skip_unless = ["node", "-e", "setTimeout(() => {}, 60000)"]'

function run(plan, ...extra) {
    fs.rmSync(WORK, { recursive: true, force: true })
    fs.mkdirSync(path.dirname(stubCamel), { recursive: true })
    fs.writeFileSync(stubCamel, '')
    fs.writeFileSync(planPath, ['id = "self.plan_launch"', '', ...plan].join('\n'))
    const started = Date.now()
    const proc = spawnSync(
        process.execPath,
        [path.join('test', 'tools', 'run-tests.mjs'), planPath, '--jobs', '2', '--camel', stubCamel, ...extra],
        {
            cwd: ROOT,
            encoding: 'utf8',
            timeout: 120000,
            // Every card on the host is busy: a device selection that ran before the probe would
            // skip with its own reason.
            env: { ...process.env, CAMEL_TEST_GPU_CARDS: '0, GPU-a, 40000, 40960, busy card' },
        },
    )
    const out = `${proc.stdout || ''}${proc.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '')
    return { status: proc.status, out, wallMs: Date.now() - started }
}

let r = run([...entry('skipped', [FAILING_PROBE, 'cuda_free_device = true']), ...entry('after')])
check('a failing probe skips the entry and the run is green', r.status === 0 && /\[SKIP\] skipped/.test(r.out) && /skip: 1/.test(r.out), `exit ${r.status}`)
check('the skip names the probe, not a busy card', /precondition not met/.test(r.out) && !/no idle CUDA device/.test(r.out), r.out.slice(-500))

r = run([...entry('hung', [HANGING_PROBE, 'skip_unless_timeout_ms = 500']), ...entry('after')])
check('a probe past its limit fails the entry', r.status === 1 && /\[FAIL\] hung/.test(r.out), `exit ${r.status}`)
check('the failure names the probe and its limit', /precondition probe did not return within 500 ms/.test(r.out), r.out.slice(-500))
check('the entry after it still runs', /\[PASS\] after/.test(r.out))
check('the run does not wait for the probe', r.wallMs < 40000, `${r.wallMs} ms`)

r = run([...entry('a'), ...entry('b')], '--entry', 'a')
check('--entry selects one entry of an ordinary run', r.status === 0 && /total: 1, pass: 1/.test(r.out), r.out.slice(-400))
r = run([...entry('a'), ...entry('b')], '--entry', 'nothing_like_this')
check('--entry that names nothing refuses the run', r.status === 2 && /--entry matched no entry/.test(r.out), `exit ${r.status}`)

r = run([...entry('a')], '--tier', 'slow')
check('a plan file with an explicit tier says the tier does not filter it', /--tier slow does not filter/.test(r.out), r.out.slice(0, 600))
r = run([...entry('a')])
check('a plan file at the default tier says nothing about a tier', !/does not filter/.test(r.out))

fs.rmSync(WORK, { recursive: true, force: true })
console.log(`plan-launch: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 ? 0 : 1)
