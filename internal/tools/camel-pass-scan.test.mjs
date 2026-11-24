// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What `scripts/camel-pass-scan.mjs` reads as a command line, and what it must not miss.
 *
 * The scan counts pipelines that end in a translating pass and therefore need `std::nop`. It was
 * wrong once in a way no count could show: a plan element written `'onnx::export out="..."'` is a
 * TOML *literal* string, the old reader looked only inside double quotes, so the pass word was
 * invisible, the pipeline looked like it ended in a rewriting pass, and 42 pipelines were reported as
 * needing nothing. The patch then agreed with the scan because it was the same mistake.
 *
 * So the checks here are about *reading*, one spelling at a time:
 *
 *   [x] a double-quoted element is a pass
 *   [x] a single-quoted element is a pass, and an inner `"` does not end the array
 *   [x] a bare (unquoted) word is a pass
 *   [x] a comma-joined `--passes` list is one pass per comma
 *   [x] `std::nvm/10` is `std::nvm`, and a scheduling pass ends the pipeline (no nop)
 *   [x] a case script that runs the binary as `EXE ... std::jgir` is a pipeline too
 *   [x] `# camel-pass-scan: keep` is listed and not patched
 *   [x] the audit's set difference is empty on a tree where every spelling is read
 *
 * Every check runs the real script on a throwaway root, so the reader tested is the one that patches
 * the plans.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const here = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1')
const scanner = path.join(here, '..', '..', 'scripts', 'camel-pass-scan.mjs')
let failures = 0
let checks = 0

function check(name, condition, detail) {
    checks++
    if (condition) return
    failures++
    console.log(`[FAIL] ${name}${detail ? `\n    ${detail}` : ''}`)
}

function tree() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-pass-scan-'))
    fs.mkdirSync(path.join(root, 'test', 'plans'), { recursive: true })
    fs.mkdirSync(path.join(root, 'test', 'cases', 'x'), { recursive: true })
    return root
}

function scan(root, args = []) {
    return spawnSync(process.execPath, [scanner, '--root', root, ...args], { encoding: 'utf8' })
}

// One plan with every spelling the tree writes, in the order the checks above name them.
const plan = `id = "spellings"

[[tests]]
name = "double_quoted"
args = ["\${case}", "std::gir"]

[[tests]]
name = "single_quoted"
args = ["\${case}", 'onnx::export out="\${test_tmp}/m.onnx"']

[[tests]]
name = "bare_word"
args = ["\${case}", std::jgir]

[[tests]]
name = "comma_joined"
args = ["\${case}", "--passes", "std::opt safe_fp=false,std::gir"]

[[tests]]
name = "scheduling"
args = ["\${case}", "std::nvm/10"]

[[tests]]
name = "kept"
args = ["\${case}", "std::gir"]  # camel-pass-scan: keep -- this case pins that the program runs
`

// A case script: the binary is named by a variable, and the dump is read as JSON.
const script = `import subprocess
EXE = "camel"
for name in ("tiles::compile",):
    # the dump, not the program
    result = subprocess.run([EXE, "--devices", "cpu", "p.cml", name, "std::jgir"], capture_output=True, text=True)
`

const root = tree()
fs.writeFileSync(path.join(root, 'test', 'plans', 'spellings.plan.toml'), plan)
fs.writeFileSync(path.join(root, 'test', 'cases', 'x', 'run.py'), script)

const listed = scan(root, ['--list', '--audit'])
const out = listed.stdout ?? ''
check('read every spelling', /need std::nop: 5/.test(out), out.split('\n').slice(0, 6).join('\n'))
check('the single-quoted element is a pipeline',
    /spellings\.plan\.toml:\d+\s+onnx::export/.test(out), out)
check('the bare word is a pipeline', /spellings\.plan\.toml:\d+\s+std::jgir/.test(out), out)
check('the comma-joined list is read to its last word',
    /spellings\.plan\.toml:\d+\s+std::opt std::gir/.test(out), out)
check('a scheduling pass ends the pipeline without nop',
    !/spellings\.plan\.toml:\d+\s+std::nvm/.test(out), out)
check('a script that runs EXE is a pipeline',
    /run\.py:\d+\s+std::jgir/.test(out), out)
check('the keep line is listed, not patched',
    /kept by '# camel-pass-scan: keep': 1/.test(out), out)
check('the audit difference is empty', /audit: 0 pass word\(s\)/.test(out), out)
check('the audit exits 0 on a clean read', listed.status === 0, `exit ${listed.status}`)

// The gate: one pipeline needs `std::nop`, so `--check` refuses.
const gate = scan(root, ['--check'])
check('--check refuses while a pipeline needs nop', gate.status === 1, `exit ${gate.status}`)

// The patch script must agree with the scan, or the two are guessing separately.
const patcher = path.join(here, '..', '..', 'scripts', 'camel-pass-nop-patch.mjs')
const dry = spawnSync(process.execPath, [patcher, '--root', root], { encoding: 'utf8' })
check('the patch script finds the same 4 pipelines', /would patch 4 pipeline/.test(dry.stdout ?? ''), dry.stdout)

const applied = spawnSync(process.execPath, [patcher, '--root', root, '--apply'], { encoding: 'utf8' })
check('the patch applies', /patched 4 pipeline/.test(applied.stdout ?? ''), applied.stdout)
const after = scan(root, ['--audit'])
// The patch script writes plans only: a pipeline in a case script is left for a person, so exactly
// that one is still listed and the plans are clean.
check('the plans are clean after the patch', /ending in a translating pass: 1 \(plans 0, scripts 1\)/.test(after.stdout ?? ''), after.stdout)
check('a single-quoted array keeps its own quoting', /'onnx::export out="\$\{test_tmp\}\/m\.onnx"', "std::nop"/
    .test(fs.readFileSync(path.join(root, 'test', 'plans', 'spellings.plan.toml'), 'utf8')))
check('the audit is still empty after the patch', /audit: 0 pass word\(s\)/.test(after.stdout ?? ''), after.stdout)

fs.rmSync(root, { recursive: true, force: true })

console.log(`${checks - failures}/${checks} checks passed`)
process.exit(failures === 0 ? 0 : 1)