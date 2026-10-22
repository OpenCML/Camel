#!/usr/bin/env node
/**
 * A `--timeit` run that fails names the iteration that failed. The case divides by zero on its
 * third execution (`timeit_failure.cml` counts executions in a file); the JSON report must carry
 * `"iteration": 3`, and the text report must print the same number in the `[timeit] 3/5 ... failed`
 * line, so both say which iteration produced the failure.
 *
 * Usage: node timeit_failure.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CAMEL = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const program = path.join(HERE, 'timeit_failure.cml')
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-timeit-failure-'))
const counter = path.join(scratch, 'count')
const env = {
    ...process.env,
    CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest'),
    CAMEL_TIMEIT_COUNTER: counter,
}
const reset = () => fs.rmSync(counter, { force: true })
let failed = false

try {
    {
        reset()
        const proc = spawnSync(CAMEL, ['--output-format', 'json', program, '--timeit', 'std::nvm/5'], {
            env,
            encoding: 'utf8',
        })
        let report = null
        try {
            report = JSON.parse(proc.stdout.slice(proc.stdout.indexOf('{')))
        } catch (e) {
            report = null
        }
        const output = proc.stdout + proc.stderr
        const ok =
            proc.status === 1 &&
            report !== null &&
            report.kind === 'timeit' &&
            report.failed === true &&
            report.iteration === 3 &&
            Array.isArray(report.diagnostics) &&
            report.diagnostics.some((d) => d.data && d.data.name === 'RuntimeDiag::DivisionByZero')
        console.log(ok ? 'timeit failure json: iteration 3 division by zero' : `FAIL timeit failure json: exit ${proc.status}: ${output}`)
        failed ||= !ok
    }
    {
        reset()
        const proc = spawnSync(CAMEL, [program, '--timeit', 'std::nvm/5'], { env, encoding: 'utf8' })
        const output = proc.stdout + proc.stderr
        const ok =
            proc.status === 1 &&
            output.includes('[timeit] 1/5') &&
            output.includes('[timeit] 2/5') &&
            output.includes('[timeit] 3/5 pass=std::nvm failed') &&
            output.includes('RuntimeDiag::DivisionByZero')
        console.log(ok ? 'timeit failure text: same iteration 3' : `FAIL timeit failure text: exit ${proc.status}: ${output}`)
        failed ||= !ok
    }
} finally {
    fs.rmSync(scratch, { recursive: true, force: true })
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)