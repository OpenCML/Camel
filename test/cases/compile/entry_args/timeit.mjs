#!/usr/bin/env node
/**
 * `--timeit` with a `main` that takes parameters: the arguments are bound for every fresh
 * execution, and a missing one is reported like on a plain run.
 *
 * Usage: node timeit.mjs
 */

import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CAMEL = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }
const program = path.join(HERE, 'scalars.cml')

let failed = false
{
    const proc = spawnSync(
        CAMEL,
        ['--output-format', 'json', program, '--arg', 'n=3', '--arg', 'rate=0.5', '--arg', 'name=hi', '--arg', 'loud=true', '--timeit', 'std::fvm/2'],
        { env, encoding: 'utf8' },
    )
    let samples = -1
    try {
        const start = proc.stdout.indexOf('{')
        const report = JSON.parse(proc.stdout.slice(start))
        samples = (report.samples_ms || report.samples || []).length
    } catch (e) {
        samples = -1
    }
    const ok = proc.status !== null && !/Error|EntryArgument/.test(proc.stdout + proc.stderr) && samples === 2
    console.log(ok ? 'timeit with arguments: 2 runs' : `FAIL timeit with arguments: exit ${proc.status}, ${samples} samples: ${proc.stdout} ${proc.stderr}`)
    failed ||= !ok
}
{
    const proc = spawnSync(CAMEL, [program, '--arg', 'n=3', '--timeit', 'std::fvm/2'], { env, encoding: 'utf8' })
    const out = proc.stdout + proc.stderr
    const ok =
        proc.status === 1 &&
        out.includes('RuntimeDiag::EntryArgumentMissing') &&
        out.includes("'rate', 'name' and 'loud'") &&
        !out.includes('no diagnostics were collected')
    console.log(ok ? 'timeit without: EntryArgumentMissing at main' : `FAIL timeit without: exit ${proc.status}: ${out}`)
    failed ||= !ok
}
console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)
