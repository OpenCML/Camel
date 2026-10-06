#!/usr/bin/env node
/**
 * A `main` with parameters run without all of their values, under every scheduler: one
 * diagnostic RuntimeDiag::EntryArgumentMissing, stage `run`, with a source range, exit 1, and
 * the program does not run (it would print `hi`).
 *
 * Usage: node missing.mjs
 */

import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CAMEL = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }

let failed = false
for (const pass of [null, 'std::fvm', 'std::nvm', 'std::jit', 'std::tf']) {
    const args = ['--output-format', 'json', path.join(HERE, 'scalars.cml'), '--arg', 'n=3', '--arg', 'name=hi']
    if (pass) args.push(pass)
    const proc = spawnSync(CAMEL, args, { env, encoding: 'utf8' })
    const label = pass || 'default'
    let ok = false
    let why = ''
    try {
        const diags = JSON.parse(proc.stdout)
        const d = diags[0]
        ok =
            proc.status === 1 &&
            diags.length === 1 &&
            d.data.name === 'RuntimeDiag::EntryArgumentMissing' &&
            d.data.stage === 'run' &&
            d.range.start.line === 6 &&
            d.message.includes("'rate' and 'loud'") &&
            !proc.stdout.includes('hi 3')
        why = `exit ${proc.status}, ${diags.length} diagnostic(s), ${d.data.name} at stage ${d.data.stage}, line ${d.range.start.line}: ${d.message}`
    } catch (e) {
        why = `exit ${proc.status}, stdout is not a diagnostics array: ${proc.stdout} ${proc.stderr}`
    }
    console.log(ok ? `${label}: EntryArgumentMissing at main, stage run, nothing ran` : `${label}: FAIL ${why}`)
    failed ||= !ok
}
console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)
