#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A Python test that imports a CUDA-aware library can reach a card without ever naming one, and
 * then dies or passes by who else is on the host: `torch.optim.Adam(...).step()` asks for the
 * current accelerator and its current stream, which retains the primary context of every card,
 * and `cuDevicePrimaryCtxRetain` returns `CUDA_ERROR_UNKNOWN` (999) while another tenant holds
 * one (docs/test-plans.md, "CUDA visibility"). The runner hides the machine's
 * devices from every Python child that did not ask for them (`cuda-visibility.mjs`), so such a
 * test needs no declaration to be safe. This guard checks that each one is nevertheless in a state
 * someone chose:
 *
 *   * it sees no card, because the runner's default covers the Python entry point it uses, or
 *     because the plan pins `CUDA_VISIBLE_DEVICES=-1`/empty itself, or because the case script
 *     hides the devices from the processes it starts; or
 *   * it declares `cuda_devices = true`, which is the one way to say "this test wants the cards",
 *     and then the runner leaves the machine's value alone.
 *
 * Anything else is a failure by name: a test that names a card in its `envs` without declaring
 * that it wants one, or that reaches Python in a way the runner does not wrap (a `.py` case under
 * another tool, an interpreter named in `args`), so that no rule decides its visibility. The
 * default is what makes a *new* plan safe before anyone runs this guard; this guard is what keeps
 * the plan text honest about the ones that are here.
 *
 * What it cannot see, and does not claim to: a library reached through a module the case imports
 * (a script that pins the variable inside a shared helper); a CUDA-aware import assembled at run
 * time; a program a case starts that is not a Python file it names. It also does not read the perf
 * layer: those plans are the manual measuring layer, are never selected by `--changed`, and are not
 * part of this rule (docs/test-plans.md, "Performance Measurement").
 *
 *   node test/tools/check-cuda-visibility.mjs [--root <checkout>]
 *
 * The runner's self-test (`test/tools/selftest/run.mjs`) calls the same audit on the real tree and
 * on the fixture next to it, so the guard cannot stop examining anything without a red run.
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

import { parseTomlFile } from './toml-lite.mjs'
import {
    CUDA_VISIBLE_DEVICES,
    declaredVisibility,
    declaresDevices,
    hidesEveryDevice,
    pythonEntryPoints,
} from './cuda-visibility.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')

/**
 * The libraries whose import can make a process enumerate devices or retain a context without
 * the program asking for one. `numba` is on the list although only its `.cuda` half touches a
 * driver: reading the import is what this guard can do, and a false positive costs one line in
 * the plan, while a false negative is a test that answers to the host's other tenants.
 */
const CUDA_AWARE_MODULES = [
    'torch', 'torchvision', 'torchaudio', 'onnxruntime', 'tensorflow', 'jax',
    'paddle', 'cupy', 'numba', 'pycuda', 'pyopencl', 'triton', 'tensorrt', 'vllm', 'taichi',
]

/**
 * How many tests the audit examined on the tree it was written against. The numbers are pinned
 * because a guard that examines nothing always passes: a change that makes this guard see fewer
 * tests (a plan that stops naming its case, a scan that stops reading one) is red here, and so is
 * a new CUDA-aware Python test, which is the moment to decide its visibility on purpose. The
 * second number is the Python tests of the plans tagged `gpu`, the layer that must keep the cards.
 *
 * 31 -> 32 on 2026-10-26: `ort_core_slice` joined, the ONNX Runtime probe of the core's own slice
 * (`feat/modules/onnx.plan.toml`). It is a CPU-only program -- it exports a model and compares
 * values -- so it names no device and the runner hides the machine's cards from it, like every
 * other ONNX Runtime probe in that plan.
 * 32 -> 33 on 2026-10-26: `ort_core_slice_grad` joined, the ONNX Runtime probe of that slice's
 * adjoint in the same plan. It is CPU-only too -- `providers=["CPUExecutionProvider"]` on a model
 * it exports and compares -- so it names no device either, and the runner's default hides the
 * machine's cards from it as it does for `ort_core_slice` beside it. The lane that added it left
 * this pin at 32, and the pin is what makes that visible: the runner's self-test reads the same
 * audit, so the stale number was red there before anyone ran the guard alone.
 * 33 -> 34: `ort_slice_grad_stride` joined, the ONNX Runtime probe of a slice whose step is not 1
 * in the same plan. CPU-only in the same way, so it names no device and the runner's default
 * hides the machine's cards from it.
 * 34 -> 35 on 2026-10-27: `ort_grad_matmul_batched` joined, the ONNX Runtime probe of a batched
 * matmul's gradient in the same plan (`test/cases/modules/onnx/ort_grad_matmul_batched.py`). It is
 * CPU-only too -- `providers=["CPUExecutionProvider"]` on a model it exports and compares -- so it
 * names no device either, and the runner's default hides the machine's cards from it.
 */
export const EXPECTED_EXAMINED = 35
export const EXPECTED_DEVICE_LAYER = 17

function walkPlanFiles(dir, out = []) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return out
    }
    for (const entry of entries) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walkPlanFiles(full, out)
        else if (entry.isFile() && entry.name.endsWith('.plan.toml')) out.push(full)
    }
    return out
}

/** The libraries this text imports, from `import x`/`from x import y`/`__import__('x')`. */
export function cudaAwareImports(text) {
    const found = new Set()
    for (const raw of String(text).split(/\r?\n/)) {
        const line = raw.trim()
        if (!/^(import|from)\s/.test(line) && !/__import__\s*\(/.test(line)) continue
        for (const name of CUDA_AWARE_MODULES) {
            const pattern = new RegExp(`(^|[^\\w.])${name}([^\\w.]|$)`)
            if (pattern.test(line)) found.add(name)
        }
    }
    return [...found]
}

/**
 * The Python files a case names, so that a case which starts another Python program is read too:
 * the guard's own unpinned fixture does it through a node harness, and `showcase/`'s compare
 * scripts do it through an interpreter.
 */
function namedPythonFiles(text, baseDir) {
    const out = []
    for (const match of String(text).matchAll(/["']([^"'\n]+\.py)["']/g)) {
        const candidate = path.resolve(baseDir, match[1])
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) out.push(candidate)
    }
    return out
}

/**
 * The value a case script writes to `CUDA_VISIBLE_DEVICES` for the processes it starts, or null.
 * Three shapes exist in this tree: `env["CUDA_VISIBLE_DEVICES"] = ""`,
 * `env['CUDA_VISIBLE_DEVICES'] = '-1'` and `dict(os.environ, CUDA_VISIBLE_DEVICES="-1")`.
 */
export function scriptVisibility(text) {
    const source = String(text)
        .split(/\r?\n/)
        .filter((line) => !line.trim().startsWith('#'))
        .join('\n')
    const assignment = source.match(
        new RegExp(`${CUDA_VISIBLE_DEVICES}["']?\\s*\\]?\\s*=\\s*("[^"]*"|'[^']*'|[^\\s,#)]+)`)
    )
    if (assignment) return assignment[1].replace(/^["']|["']$/g, '')
    const setdefault = source.match(
        new RegExp(`${CUDA_VISIBLE_DEVICES}["']\\s*,\\s*("[^"]*"|'[^']*')`)
    )
    if (setdefault) return setdefault[1].replace(/^["']|["']$/g, '')
    return null
}

/**
 * Every test of the tree that reaches Python with a CUDA-aware import, with the reason the guard
 * accepts or rejects it, and every Python test of the plans tagged `gpu`, which must declare that
 * it wants the cards. `expectations` carries the pinned population of the tree being audited; a
 * caller reading another tree (the fixture next to the runner's self-test) states its own.
 */
export function cudaVisibilityAudit(root = REPO_ROOT, expectations = {}) {
    // `expectations === false` reads a tree whose population is not the pinned one (the fixture
    // next to the runner's self-test): the population checks are the caller's business then, and
    // "an empty population fails" stays on for every tree.
    const checkPopulation = expectations !== false
    const expectedExamined = checkPopulation ? expectations.examined ?? EXPECTED_EXAMINED : null
    const expectedDeviceLayer = checkPopulation ? expectations.deviceLayer ?? EXPECTED_DEVICE_LAYER : null
    const problems = []
    const examined = []
    const deviceLayer = []
    let runnerDefault = 0
    let planPinned = 0
    let scriptPinned = 0
    let declared = 0
    const plansRoot = path.join(root, 'test', 'plans')
    for (const planFile of walkPlanFiles(plansRoot).sort()) {
        const relativePlan = path.relative(root, planFile).split(path.sep).join('/')
        // The perf layer is the manual measuring layer: never selected by `--changed`, and not
        // part of a rule that reads plan text (docs/test-plans.md, "Performance Measurement").
        if (relativePlan.startsWith('test/plans/perf/')) continue
        let doc
        try {
            doc = parseTomlFile(planFile)
        } catch (error) {
            problems.push(`${relativePlan}: not readable (${error.message})`)
            continue
        }
        const tags = (doc.tags || []).map((tag) => String(tag))
        const devicePlan = tags.includes('gpu')
        // `[defaults]` is merged the way the runner merges it (loadPlanTests): a declaration or a
        // pin written there belongs to every test of the plan.
        for (const test of (doc.tests || []).map((entry) => ({ ...(doc.defaults || {}), ...entry }))) {
            const entry = pythonEntryPoints(test)
            const namesPythonCase = typeof test.case === 'string' && test.case.endsWith('.py')
            const where = `${relativePlan}: test '${test.name}'`
            const casePath = path.resolve(path.dirname(planFile), test.case || '')
            let caseText = ''
            try {
                caseText = fs.readFileSync(casePath, 'utf8')
            } catch {
                // A case the plan cannot resolve is the runner's business, not this guard's: it
                // fails the test by name. Nothing to read here.
            }
            // A case that runs another Python program reaches that program's imports too. Only
            // the case file's own text is read for a pinned value: a pin inside a module the case
            // imports is outside what this guard can see (see the header).
            const spawnedFiles = namedPythonFiles(caseText, path.dirname(casePath))
            const reachesPython = entry.tool || entry.precondition || namesPythonCase || spawnedFiles.length > 0
            if (!reachesPython) continue
            if (devicePlan) {
                deviceLayer.push({ plan: relativePlan, name: test.name })
                // A plan tagged `gpu` exists to run against the cards. The runner hides the
                // devices from a Python child that declared nothing, so such a test would keep
                // passing while measuring nothing: the layer must say that it wants them.
                if (!declaresDevices(test)) {
                    problems.push(
                        `${where} runs Python in a plan tagged 'gpu' and does not declare cuda_devices = true: ` +
                            `the runner hides the devices from a Python child that declares nothing, so this ` +
                            `test would pass against the CPU. Declare it in the test or in [defaults].`
                    )
                }
            }
            const imports = new Set(cudaAwareImports(caseText))
            for (const file of spawnedFiles) {
                let text = ''
                try {
                    text = fs.readFileSync(file, 'utf8')
                } catch {
                    continue
                }
                for (const name of cudaAwareImports(text)) imports.add(name)
            }
            for (const word of test.skip_unless || []) {
                for (const name of cudaAwareImports(String(word))) imports.add(name)
            }
            if (imports.size === 0) continue
            const importList = [...imports].sort().join(', ')
            const examinedRow = { plan: relativePlan, name: test.name, imports: importList }
            if (declaresDevices(test)) {
                declared += 1
                examined.push({ ...examinedRow, verdict: 'declares a device' })
                continue
            }
            const own = declaredVisibility(test)
            if (own !== null && hidesEveryDevice(own)) {
                planPinned += 1
                examined.push({ ...examinedRow, verdict: `pins ${CUDA_VISIBLE_DEVICES}=${own}` })
                continue
            }
            const script = scriptVisibility(caseText)
            if (script !== null && hidesEveryDevice(script)) {
                scriptPinned += 1
                examined.push({ ...examinedRow, verdict: 'the case script hides the devices' })
                continue
            }
            // The two ways this test still reaches a card. A value the test writes itself wins over
            // the runner's default (that is what "the test decides" means), so it is the case that
            // needs the declaration even when the runner would otherwise wrap the Python child.
            if (own !== null) {
                problems.push(
                    `${where} imports ${importList} and sets ${CUDA_VISIBLE_DEVICES}=${own}: a CPU-only ` +
                        `test names a card without declaring that it wants one. Set ` +
                        `${CUDA_VISIBLE_DEVICES}=-1, or declare cuda_devices = true if it really uses a device.`
                )
                examined.push({ ...examinedRow, verdict: 'unclassified' })
                continue
            }
            if (entry.tool || entry.precondition) {
                runnerDefault += 1
                examined.push({ ...examinedRow, verdict: 'the runner hides the devices' })
                continue
            }
            problems.push(
                `${where} imports ${importList} and reaches Python outside the runner's Python entry ` +
                    `points (tool = "python" or skip_unless = ["\${python}", ...]), so nothing decides ` +
                    `which devices it sees: pin ${CUDA_VISIBLE_DEVICES} to name no device, or declare ` +
                    `cuda_devices = true.`
            )
            examined.push({ ...examinedRow, verdict: 'unclassified' })
        }
    }
    const populationProblem = (kind, found, expected, what) =>
        `${kind}: the audit examined ${found} ${what}, not the pinned ${expected}: ` +
        `${found < expected ? 'something left the population' : 'something joined it'} (a plan stopped ` +
        `naming its case, a scan stopped reading one, or a new test appeared). Check the list and ` +
        `update the pinned number deliberately.`
    if (checkPopulation && examined.length !== expectedExamined) {
        problems.push(populationProblem('cuda-aware python', examined.length, expectedExamined, 'CUDA-aware Python test(s)'))
    }
    if (checkPopulation && deviceLayer.length !== expectedDeviceLayer) {
        problems.push(populationProblem('device layer', deviceLayer.length, expectedDeviceLayer, "Python test(s) in plans tagged 'gpu'"))
    }
    if (examined.length === 0 || deviceLayer.length === 0) {
        problems.push(
            `the audit examined ${examined.length} CUDA-aware and ${deviceLayer.length} device-layer ` +
                `test(s): a guard that finds nothing cannot fail, so an empty population is itself the failure`
        )
    }
    return {
        problems,
        examined,
        deviceLayer,
        counts: { runnerDefault, planPinned, scriptPinned, declared },
    }
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(url.fileURLToPath(import.meta.url))
if (isMain) {
    let root = REPO_ROOT
    for (let i = 2; i < process.argv.length; i++) {
        if (process.argv[i] === '--root' && i + 1 < process.argv.length) {
            root = path.resolve(process.argv[++i])
        } else {
            console.log(`FAIL unknown argument: ${process.argv[i]}`)
            process.exit(2)
        }
    }
    // A fixture tree is not the tree the pinned numbers were measured on: the runner's self-test
    // reads the fixture with `expectations = false` and judges the problems itself.
    const audit = root === REPO_ROOT ? cudaVisibilityAudit(root) : cudaVisibilityAudit(root, false)
    const { runnerDefault, planPinned, scriptPinned, declared } = audit.counts
    for (const problem of audit.problems) console.log(`FAIL ${problem}`)
    console.log(
        `examined ${audit.examined.length} CUDA-aware Python test(s): ${runnerDefault} hidden by the ` +
            `runner's default, ${planPinned} pinned by their plan, ${scriptPinned} hidden by the case ` +
            `script, ${declared} declaring a device; and ${audit.deviceLayer.length} Python test(s) in ` +
            `plans tagged 'gpu'`
    )
    if (audit.problems.length > 0) {
        console.log(`FAIL no CUDA-aware Python test reaches a card by accident: ${audit.problems.length} problem(s)`)
        process.exit(1)
    }
    console.log(
        `PASS every CUDA-aware Python test either sees no card or declares that it wants one ` +
            `(pinned population: ${EXPECTED_EXAMINED} CUDA-aware, ${EXPECTED_DEVICE_LAYER} in the gpu layer)`
    )
}