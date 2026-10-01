#!/usr/bin/env node
/**
 * Exemplar checker: re-runs every exemplar and checks what it claims.
 *
 * Each exemplar directory holds `program.cml`, a manifest `exemplar.toml`, its
 * expected outputs under `expected/` (graph dumps, program output), and
 * `run.log`, the record of the last recorded run. The manifest lists runs:
 *
 *   [[runs]]
 *   name = "dump"                     # names the run in reports and run.log
 *   args = ["std::argir"]             # after `camel program.cml`
 *   env = ["MODE=static", "ONNX_OUT={tmp}/model.onnx"]   # {tmp}: a fresh directory
 *   exit = 0
 *   stdout = "expected/dump.dot"      # exact (normalized) stdout
 *   output_contains = ["..."]         # substrings of stdout + stderr
 *   diagnostic = "RuntimeDiag::TensorDimensionMismatch"  # the reported diagnostic ...
 *   diagnostic_line = 11              # ... and its source position
 *   diagnostic_column = 25
 *   onnx = "{tmp}/model.onnx"         # a written model to inspect:
 *   onnx_ops = "Gather,Greater,If,Identity"               # top-level operators, in order
 *   onnx_inputs = "w1,b1,input1"
 *   onnx_outputs = "output"
 *
 * Usage:
 *   node exemplars/check.mjs [--update] [exemplar ...]   (a directory, or a name under exemplars/)
 * --update rewrites the expected stdout files and run.log from the current
 * build (review the diff); without it the exemplars are checked. Exits 1 when
 * a check fails.
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.dirname(HERE)
const { parseTomlFile } = await import(url.pathToFileURL(path.join(REPO_ROOT, 'test/tools/toml-lite.mjs')).href)
const { readOnnxModel } = await import(
    url.pathToFileURL(path.join(REPO_ROOT, 'test/cases/modules/onnx/onnx_ops.mjs')).href
)

const CAMEL = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')

function normalize(text) {
    const root = REPO_ROOT.replace(/\\/g, '/')
    return text
        .replace(/\x1b\[[0-9;]*m/g, '')
        .replace(/\r\n/g, '\n')
        .replace(/\\/g, '/')
        .replaceAll(root, '<ROOT>')
        .split('\n')
        .map((line) => line.replace(/\s+$/, ''))
        .join('\n')
}

/** The first diagnostic of a run: {name, line, column}. */
function diagnosticOf(output) {
    const m = output.match(/[.]cml:(\d+):(\d+): \[(?:Error|Warn)[^\]]*\]: [\s\S]*?\(name=([A-Za-z:]+), code=/)
    return m ? { line: Number(m[1]), column: Number(m[2]), name: m[3] } : null
}

function runOne(dir, run, update, log) {
    const failures = []
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-exemplar-'))
    const fill = (text) => String(text).replaceAll('{tmp}', tmp)
    const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }
    for (const entry of run.env || []) {
        const text = fill(entry)
        const eq = text.indexOf('=')
        env[text.slice(0, eq)] = text.slice(eq + 1)
    }
    const program = path.relative(REPO_ROOT, path.join(dir, 'program.cml')).replace(/\\/g, '/')
    const args = [program, ...(run.args || []).map(fill)]
    const proc = spawnSync(CAMEL, args, { cwd: REPO_ROOT, env, encoding: 'utf8', timeout: 120000 })
    const stdout = normalize(proc.stdout || '')
    const output = normalize((proc.stdout || '') + (proc.stderr || ''))
    log.push(`$ ${(run.env || []).map((e) => e.replaceAll('{tmp}', '<tmp>') + ' ').join('')}camel ${args.map((a) => a.replaceAll(tmp, '<tmp>')).join(' ')}`)
    log.push(`exit ${proc.status}`)
    log.push(output.replaceAll(normalize(tmp), '<tmp>').trimEnd(), '')

    if (typeof run.exit === 'number' && proc.status !== run.exit) {
        failures.push(`exit ${proc.status}, expected ${run.exit}`)
    }
    if (run.stdout) {
        const file = path.join(dir, run.stdout)
        if (update) {
            fs.mkdirSync(path.dirname(file), { recursive: true })
            fs.writeFileSync(file, stdout)
        } else if (!fs.existsSync(file)) {
            failures.push(`missing ${run.stdout} (run with --update)`)
        } else if (normalize(fs.readFileSync(file, 'utf8')) !== stdout) {
            failures.push(`stdout differs from ${run.stdout}`)
        }
    }
    for (const needle of run.output_contains || []) {
        if (!output.includes(needle)) failures.push(`output lacks '${needle}'`)
    }
    if (run.diagnostic) {
        const d = diagnosticOf(output)
        if (!d || d.name !== run.diagnostic) {
            failures.push(`expected diagnostic ${run.diagnostic}, got ${d ? d.name : 'none'}`)
        } else {
            if (typeof run.diagnostic_line === 'number' && d.line !== run.diagnostic_line) {
                failures.push(`diagnostic line ${d.line}, expected ${run.diagnostic_line}`)
            }
            if (typeof run.diagnostic_column === 'number' && d.column !== run.diagnostic_column) {
                failures.push(`diagnostic column ${d.column}, expected ${run.diagnostic_column}`)
            }
        }
    }
    if (run.onnx) {
        const file = fill(run.onnx)
        if (!fs.existsSync(file)) {
            failures.push(`no model written at ${run.onnx}`)
        } else {
            const model = readOnnxModel(file)
            for (const [key, actual] of [
                ['onnx_ops', model.ops],
                ['onnx_inputs', model.inputs],
                ['onnx_outputs', model.outputs],
            ]) {
                if (run[key] !== undefined && actual.join() !== run[key]) {
                    failures.push(`${key} ${actual.join()}, expected ${run[key]}`)
                }
            }
        }
    }
    fs.rmSync(tmp, { recursive: true, force: true })
    return failures
}

function checkExemplar(dir, update) {
    const manifest = parseTomlFile(path.join(dir, 'exemplar.toml'))
    const log = [`# ${manifest.title}`, `# recorded by exemplars/check.mjs --update`, '']
    let ok = true
    for (const run of manifest.runs || []) {
        const failures = runOne(dir, run, update, log)
        const label = `${path.basename(dir)}/${run.name}`
        if (failures.length) {
            ok = false
            console.log(`FAIL ${label}`)
            for (const f of failures) console.log(`     ${f}`)
        } else {
            console.log(`ok   ${label}`)
        }
    }
    if (update) fs.writeFileSync(path.join(dir, 'run.log'), log.join('\n') + '\n')
    return ok
}

const argv = process.argv.slice(2)
const update = argv.includes('--update')
const named = argv.filter((a) => a !== '--update')
const dirs = named.length
    ? named.map((d) => (fs.existsSync(path.join(HERE, d)) ? path.join(HERE, d) : path.resolve(d)))
    : fs
          .readdirSync(HERE)
          .map((d) => path.join(HERE, d))
          .filter((d) => fs.existsSync(path.join(d, 'exemplar.toml')))
          .sort()
let allOk = true
for (const dir of dirs) allOk = checkExemplar(dir, update) && allOk
console.log(allOk ? 'exemplars: all checks passed' : 'exemplars: FAILED')
process.exit(allOk ? 0 : 1)
