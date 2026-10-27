#!/usr/bin/env node
/**
 * The packages of the active environment are importable from the embedded interpreter.
 *
 * `VIRTUAL_ENV` names an environment whose packages live in `lib/python<major>.<minor>/site-packages`
 * (`Lib/site-packages` on Windows). The directory is made here, with a module in it, and a program
 * imports that module. Without the environment variable the import fails with a Python
 * exception, so the pass is the environment's doing.
 *
 * Usage: node python_site_packages.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
const CAMEL = path.join(HOME, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-site-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}`)
    if (!ok) {
        failed = true
        if (detail) console.log(`     ${String(detail).split('\n').join('\n     ')}`)
    }
}

function run(program, env) {
    const file = path.join(SCRATCH, `${program.name}.cml`)
    fs.writeFileSync(file, program.text)
    const merged = { ...process.env, CAMEL_HOME: HOME }
    delete merged.VIRTUAL_ENV
    delete merged.CONDA_PREFIX
    return spawnSync(CAMEL, [file], { env: { ...merged, ...env }, encoding: 'utf8', timeout: 60000 })
}

const probe = {
    name: 'probe',
    text: `import { import_module, attr, from_py, PyObject } from python

func main(): int sync {
    let value = match import_module("camel_site_probe") {
        case error e => -1
        case m => match m->attr<"VALUE"> {
            case error e => -2
            case v => from_py::<int>(v) !! -3
        }
    }
    println(value)
    return 0
}
`,
}

// The Python this binary embeds, asked of the interpreter itself.
const versionProbe = run({
    name: 'which_python',
    text: `import { eval, from_py, PyObject } from python

func main(): int sync {
    let major = match eval("__import__('sys').version_info[0]") {
        case error e => -1
        case v => from_py::<int>(v) !! -1
    }
    let minor = match eval("__import__('sys').version_info[1]") {
        case error e => -1
        case v => from_py::<int>(v) !! -1
    }
    println(major, minor)
    return 0
}
`,
})
const [major, minor] = versionProbe.stdout.trim().split(/\s+/)
check('the embedded Python reports its version', /^\d+$/.test(major) && /^\d+$/.test(minor), versionProbe.stdout + versionProbe.stderr)

const layout = process.platform === 'win32'
    ? path.join('Lib', 'site-packages')
    : path.join('lib', `python${major}.${minor}`, 'site-packages')
const env = path.join(SCRATCH, 'venv')
fs.mkdirSync(path.join(env, layout), { recursive: true })
fs.writeFileSync(path.join(env, layout, 'camel_site_probe.py'), 'VALUE = 42\n')

const without = run(probe, {})
check('without the environment the module is not found', without.stdout.trim() === '-1', without.stdout + without.stderr)

const withEnv = run(probe, { VIRTUAL_ENV: env })
check('with VIRTUAL_ENV its site-packages is on the path', withEnv.stdout.trim() === '42', withEnv.stdout + withEnv.stderr)

const withConda = run(probe, { CONDA_PREFIX: env })
check('with CONDA_PREFIX too', withConda.stdout.trim() === '42', withConda.stdout + withConda.stderr)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)
