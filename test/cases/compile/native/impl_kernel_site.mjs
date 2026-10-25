#!/usr/bin/env node
/**
 * Where a `#impl` kernel can be declared (design principles 10.4: everything the language defines
 * itself is written in Camel, so a module's kernel and the Camel definition whose semantics it
 * implements belong to the module that owns the operator). The standard library's kernels go beside
 * the functions they implement, which is what this case pins down:
 *
 *   A. `#op('tensor:log_softmax')` in a module that is not `tensor` is refused with
 *      NativeDeclarationForeignOperator, and not with NativeDeclarationNotLoaded: the operator is
 *      registered and the module imports it, so what refuses the declaration is ownership. A module
 *      writes its own signature, and no other module's.
 *   B. The same for `#op('nn:adam')` (Adam's kernel is `nn`'s). The ownership rule is the only
 *      refusal: the shape of an Adam over a parameter tree is legal, and the kernel's presence on a
 *      generic function is not what refuses the declaration.
 *   C. The module that does own `tensor:log_softmax` is a declaration module (a `.cml` beside a
 *      `.cmo`): a copy of the pair in a scratch directory in which the kernel is declared and the
 *      definition keeps its Camel body is accepted, and the module runs.
 *
 * Usage: node impl_kernel_site.mjs
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
const env = { ...process.env, CAMEL_HOME: HOME }

let failed = false
const failures = []
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) {
        failed = true
        failures.push(`${label}: ${detail}`)
    }
}

function summarize(tag, result) {
    for (const d of result.diagnostics) {
        const name = d.data && d.data.name
        const line = d.range && d.range.start.line
        const column = d.range && d.range.start.column
        console.log(`DIAG ${tag} ${d.code} ${name} at ${line}:${column} :: ${String(d.message).slice(0, 90)}`)
    }
}

function run(name, files, entry) {
    const dir = path.join(SCRATCH, name)
    fs.mkdirSync(dir, { recursive: true })
    for (const [file, text] of Object.entries(files)) fs.writeFileSync(path.join(dir, file), text)
    const proc = spawnSync(CAMEL, ['--output-format', 'json', path.join(dir, entry)], {
        env,
        encoding: 'utf8',
        cwd: dir,
        timeout: 120000,
    })
    let diagnostics = []
    // A run that compiles prints the diagnostics array; one that also runs the program prints
    // what the program prints, and an accepted program that prints nothing prints nothing.
    const text = proc.stdout.trim()
    if (text.length > 0 && text.startsWith('[')) {
        try {
            diagnostics = JSON.parse(text)
        } catch {
            check(`${name}: the run prints a diagnostics array`, false, `${proc.stdout} ${proc.stderr}`)
        }
    }
    const names = diagnostics.map((d) => d.data && d.data.name)
    console.log(`--- ${name}: exit ${proc.status}, diagnostics ${JSON.stringify(names)}`)
    return { status: proc.status, diagnostics, names, stdout: proc.stdout }
}

// ---- A: the tensor operator cannot be declared by a module that is not `tensor` ----
const a = run(
    'foreign_tensor_kernel',
    {
        'site_a.cml': `module kernel_site_a
import { Tensor } from tensor

#op('tensor:log_softmax')
func log_softmax_kernel(x: Tensor<f32>, axis: int): Tensor<f32>

#impl(log_softmax_kernel)
func log_softmax_here(x: Tensor<f32>, axis: int): Tensor<f32> {
    return x
}

func main(): int sync {
    return 0
}
`,
    },
    'site_a.cml',
)
check('A. refused', a.status === 1, String(a.status))
check(
    'A. the refusal is the ownership rule',
    a.names.includes('SemanticDiag::NativeDeclarationForeignOperator'),
    JSON.stringify(a.names),
)
check(
    'A. the operator itself is registered (not a missing implementation)',
    !a.names.includes('SemanticDiag::NativeDeclarationNotLoaded'),
    JSON.stringify(a.names),
)
const aFound = a.diagnostics.find((d) => d.data && d.data.name === 'SemanticDiag::NativeDeclarationForeignOperator')
if (aFound) {
    check('A. code is the catalog code', aFound.code === '0x03000181', String(aFound.code))
    check('A. the message names the operator', aFound.message.includes('tensor:log_softmax'), aFound.message)
    check('A. located in the file that declared it', path.basename(aFound.data.modulePath || '') === 'site_a.cml', String(aFound.data.modulePath))
    // The origin of a declaration is the `#op` tag that names the operator, 0-based as the runner
    // reports it (the tag is line 4 of the file, the declaration's `func` line is 5).
    check('A. located at the tag', Boolean(aFound.range) && aFound.range.start.line === 3, JSON.stringify(aFound.range))
}

// ---- B: Adam's kernel is `nn`'s; the generic function it is written on is legal now ----
const b = run(
    'foreign_adam_kernel',
    {
        'site_b.cml': `module kernel_site_b
import nn

#op('nn:adam')
func adam_kernel::<P>(params: P, grads: P): P

#impl(adam_kernel)
func adam_here::<P>(params: P, grads: P): P {
    return params
}

func main(): int sync {
    return 0
}
`,
    },
    'site_b.cml',
)
check('B. refused', b.status === 1, String(b.status))
check(
    'B. the ownership rule refuses the `nn:adam` declaration',
    b.names.includes('SemanticDiag::NativeDeclarationForeignOperator'),
    JSON.stringify(b.names),
)
check(
    'B. the ownership rule is the only refusal',
    !b.names.some((name) => name === 'SemanticDiag::ImplSignatureMismatch'),
    JSON.stringify(b.names),
)
const bFound = b.diagnostics.find((d) => d.data && d.data.name === 'SemanticDiag::NativeDeclarationForeignOperator')
if (bFound) check('B. the refusal is the catalog code', bFound.code === '0x03000181', String(bFound.code))

// ---- C: the tensor module's own file declares the kernel and holds the body ----
const cFiles = {
    'tensor.cml': `module tensor

#op('tensor:log_softmax')
func log_softmax_kernel(x: Tensor<f32>, axis: int): Tensor<f32>

#impl(log_softmax_kernel)
export func log_softmax(x: Tensor<f32>, axis: int): Tensor<f32> {
    return x
}
`,
    'main.cml': `import tensor
import { Tensor } from tensor

func main(): int sync {
    return 0
}
`,
}
const scratchC = path.join(SCRATCH, 'tensor_declaration_module')
fs.mkdirSync(scratchC, { recursive: true })
fs.copyFileSync(path.join(HOME, 'stdlib', 'tensor.cmo'), path.join(scratchC, 'tensor.cmo'))
const c = run('tensor_declaration_module', cFiles, 'main.cml')
check('C. accepted', c.status === 0, `${c.status}: ${c.stdout}`)
check(
    'C. the kernel declaration and the body in the tensor module`s own file are both accepted',
    c.names.length === 0,
    JSON.stringify(c.names),
)

console.log(failed ? `FAILURES: ${JSON.stringify(failures)}` : 'FAILURES: none')
summarize('A', a)
summarize('B', b)
summarize('C', c)
console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)