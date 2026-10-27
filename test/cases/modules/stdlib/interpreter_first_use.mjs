#!/usr/bin/env node
/**
 * `import` executes no code (design principles 3.1; 
 * in answer to `docs/technical/50_pyplot_cpython_init.md`): importing `pyplot` or
 * `python` must not create the embedded CPython, and the first operator call must create it.
 *
 * The strongest honest proof is the environment, not a clock. CPython cannot start with a
 * `PYTHONHOME` that is an empty directory: it prints its path configuration, pybind11 throws, and
 * the process ends with 1. `CAMEL_PYTHON_INHERIT_HOST_ENV=1` keeps Camel from hiding the host's
 * `PYTHONHOME`, so an interpreter created *at import* could not exit 0. That gives the pins:
 *
 *   1. `import pyplot` and `import python`, alone, under that environment exit 0 and print
 *      nothing — reintroducing the initialisation at import makes these read exit 1 with CPython's
 *       path configuration, which is the measurement's first observable (§3.1);
 *    2. `camel inspect --ops` on the import-only program exits 0 and lists `pyplot:plot`;
 *      measured exit 1 and no listing;
 *   3. one `plot` / one `exec` under the same environment exits 1, and the diagnostic is a
 *      `RuntimeError` at that call's source position that names the first use — the failure moved,
 *      it did not vanish, and its kind and exit code are the ones it had at import;
 *   4. with a healthy environment: `python` first and then `pyplot`, and `pyplot` first and then
 *      `python`, both work; a program that plots twice around a Python call works (one interpreter,
 *      not one per call); and a program that plots and then imports a C extension module (`_ssl`)
 *      succeeds — whichever module created the interpreter, libpython was promoted to the global
 *       namespace (: `pyplot` first used to make `_ssl` fail). The `_ssl` legs are skipped,
 *      with the reason printed, when a python-first program cannot import it in this environment.
 *
 * Usage: node interpreter_first_use.mjs
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
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-init-'))
const BROKEN_HOME = path.join(SCRATCH, 'broken-python-home')
fs.mkdirSync(BROKEN_HOME, { recursive: true })

const programs = {
    import_pyplot: `import pyplot

func main(): int {
    return 0
}
`,
    import_python: `import python

func main(): int {
    return 0
}
`,
    use_pyplot: `import { plot } from pyplot

func main(): int sync {
    plot([1.0, 2.0, 3.0], "${SCRATCH}/use-pyplot.svg")
    return 0
}
`,
    use_python: `import { exec } from python

func main(): int sync {
    let _ = exec("x = 1")
    return 0
}
`,
    python_then_plot: `import { import_module } from python
import { plot } from pyplot

func main(): int sync {
    let failed = match import_module("_ssl") {
        case error e => 1
        case v => 0
    }
    plot([1.0, 2.0, 3.0], "${SCRATCH}/python-then-plot.svg")
    return failed
}
`,
    plot_then_python: `import { plot } from pyplot
import { import_module } from python

func main(): int sync {
    plot([1.0, 2.0, 3.0], "${SCRATCH}/plot-then-python.svg")
    let failed = match import_module("_ssl") {
        case error e => 1
        case v => 0
    }
    return failed
}
`,
    twice: `import { plot } from pyplot
import { exec } from python

func main(): int sync {
    plot([1.0, 2.0, 3.0], "${SCRATCH}/twice-1.svg")
    let _ = exec("x = 1")
    plot([3.0, 2.0, 1.0], "${SCRATCH}/twice-2.svg")
    return 0
}
`,
}
for (const [name, text] of Object.entries(programs)) {
    fs.writeFileSync(path.join(SCRATCH, `${name}.cml`), text)
}

// The environment in which CPython cannot start. `CAMEL_PYTHON_INHERIT_HOST_ENV` is what makes it
// reach Python: without it Camel removes the host's PYTHONHOME before initialising.
const HOSTILE = {
    CAMEL_PYTHON_INHERIT_HOST_ENV: '1',
    PYTHONHOME: BROKEN_HOME,
}

function camel(args, env = {}) {
    return spawnSync(CAMEL, args, {
        env: { ...process.env, CAMEL_HOME: HOME, ...env },
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
    })
}

function runProgram(name, env = {}) {
    return camel([path.join(SCRATCH, `${name}.cml`), 'std::nvm'], env)
}

let failed = false
let skipped = 0
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}
function skip(label, why) {
    console.log(`skip ${label}: ${why}`)
    skipped++
}

// 1. Import alone, in an environment where creating the interpreter cannot succeed.
for (const name of ['import_pyplot', 'import_python']) {
    const r = runProgram(name, HOSTILE)
    check(
        `\`import ${name.slice('import_'.length)}\` creates no interpreter (exit 0 under a broken PYTHONHOME)`,
        r.status === 0,
        `exit ${r.status}; stdout=${JSON.stringify(r.stdout)} stderr=${JSON.stringify(r.stderr)}`,
    )
    check(
        `\`import ${name.slice('import_'.length)}\` prints nothing about Python`,
        !r.stdout.includes('Python') && !r.stderr.includes('Python'),
        `stdout=${JSON.stringify(r.stdout)} stderr=${JSON.stringify(r.stderr)}`,
    )
}

//  2. The tool's observable from: the listing no longer depends on an interpreter.
{
    const r = camel(['inspect', '--ops', path.join(SCRATCH, 'import_pyplot.cml')], HOSTILE)
    check('`inspect --ops` on the import-only program exits 0', r.status === 0, `exit ${r.status}: ${r.stderr}`)
    check(
        '`inspect --ops` lists `pyplot:plot` without an interpreter',
        r.stdout.includes('"uri":"pyplot:plot"'),
        r.stdout.slice(0, 400),
    )
}

// 3. The failure is at the call, with the kind and exit code it had at import.
for (const [name, message] of [
    ['use_pyplot', "Failed to initialise pyplot's embedded Python interpreter at its first use"],
    ['use_python', "Failed to initialise python's embedded Python interpreter at its first use"],
]) {
    const r = runProgram(name, HOSTILE)
    check(`\`${name}\` under a broken PYTHONHOME exits 1`, r.status === 1, `exit ${r.status}`)
    check(
        `\`${name}\` diagnoses the first use, not the import`,
        r.stdout.includes('RuntimeDiag::RuntimeError') && r.stdout.includes(message),
        r.stdout.slice(0, 600),
    )
    check(
        `\`${name}\` places the diagnostic at the call`,
        new RegExp(`${name}\\.cml:\\d+:\\d+`).test(r.stdout),
        r.stdout.slice(0, 600),
    )
    check(
        `\`${name}\` reports CPython's own reason`,
        (r.stdout + r.stderr).includes('Python path configuration') ||
            (r.stdout + r.stderr).includes('failed to get the Python codec'),
        (r.stdout + r.stderr).slice(-600),
    )
}

// A probe: does this environment let a python-first program import `_ssl` at all? Where it does
// not, the promote legs say so instead of failing for the environment's reason.
const probe = runProgram('python_then_plot')
const sslUsable = probe.status === 0

// 4. Both orders, and one interpreter across several calls.
{
    const twice = runProgram('twice')
    check('two `plot` calls around a Python call work', twice.status === 0, `${twice.status}: ${twice.stderr}`)
    check(
        'both plot calls wrote their image',
        fs.existsSync(path.join(SCRATCH, 'twice-1.svg')) && fs.existsSync(path.join(SCRATCH, 'twice-2.svg')),
        SCRATCH,
    )
}
if (sslUsable) {
    check('`python` first then `pyplot`, and `_ssl` imports', probe.status === 0, `${probe.status}: ${probe.stderr}`)
    const reverse = runProgram('plot_then_python')
    check(
        '`pyplot` first then `python`, and `_ssl` imports (the first initializer promotes libpython)',
        reverse.status === 0,
        `${reverse.status}: ${reverse.stdout}${reverse.stderr}`,
    )
    check(
        'the plot wrote its image in the pyplot-first order',
        fs.existsSync(path.join(SCRATCH, 'plot-then-python.svg')),
        SCRATCH,
    )
} else {
    skip(
        'the `_ssl` promote legs',
        `a python-first program cannot import \`_ssl\` here: ${probe.stdout}${probe.stderr}`,
    )
}

console.log(failed ? 'FAIL' : `PASS${skipped > 0 ? ` (${skipped} skipped)` : ''}`)
process.exit(failed ? 1 : 0)