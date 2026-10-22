#!/usr/bin/env node
// Counted claims in the reader-facing pages, checked against the tree.
//
//   node scripts/check-doc-counts.mjs          check every claim
//   node scripts/check-doc-counts.mjs --list   print each rule and the truth it uses
//
// A number in a page is a claim about the tree, and no other document guard reads it: links,
// retired names and snippets all pass while "eleven declaration modules" or "387 overloads" is
// stale. This guard is the narrow half of that problem: the claims that have one unambiguous
// source of truth, which it can read without a build.
//
// The scope is the reader-facing pages: README.md, CONTRIBUTING.md, ROADMAP.md and docs/**/*.md.
// Task reports and dated reviews (docs/handover, docs/reviews) are records, not pages, and are
// not scanned. Generated pages (docs/stdlib-reference.md, docs/diagnostics.md) are not scanned
// either: their generators have their own --verify-doc checks, and a hand edit is the defect
// those checks already name.
//
// What is deliberately not guarded:
//   * the default test set ("2255 tests"): only a run of the layer gives the number, and the
//     runner expands cases and sweeps, so a static count would be a second, wrong truth;
//   * `feat/opt`'s "710 tests": the same reason (the runner expands the plan, the TOML does not
//     contain 710 entries);
//   * the number of registered operator overloads ("387"): the honest source is
//     `scripts/stdlib-reference.mjs --verify-ops`, which needs the built compiler and costs
//     seconds; the pages that state it are dated records, which this guard does not scan;
//   * "N declaration modules": `scripts/stdlib-declarations.mjs` has the truth and this guard
//     reads it, but no current reader-facing page states the number. The rule below is kept so
//     the next page that does state it is checked; it reports 0 live claims today.
//
// A dated snapshot must not make the guard red: the ALLOWED list below names the page, the
// rules it is excused from, and why. A page is excused as a whole page, not line by line, so an
// edit to a snapshot does not need an edit here.

import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { parseTomlFile } from '../test/tools/toml-lite.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const ROOTS = ['README.md', 'CONTRIBUTING.md', 'ROADMAP.md', 'docs']
const RECORDS = ['docs/handover', 'docs/reviews']
const GENERATED = new Set(['docs/stdlib-reference.md', 'docs/diagnostics.md'])

const ALLOWED = [
    {
        file: 'docs/design/stdlib/inventory.cn.md',
        rules: ['signature-files', 'module-subdirs'],
        why: 'the 2026-10-20 read-only inventory; its counts are the snapshot it declares itself to be (H136 allowlist)',
    },
    {
        file: 'docs/design/stdlib/README.cn.md',
        rules: ['signature-files'],
        why: 'the 2026-10-20 design analysis; "18 signature files" is the state it argues against, not a current count',
    },
    {
        file: 'docs/technical/38_compile_pipeline_cost.md',
        rules: ['cml-files', 'snapshot-files'],
        why: 'the measurement report of the GCT refactor; 1,120 .cml files and 1,581 snapshot files are that run\'s corpus',
    },
    {
        file: 'docs/design/any-typing/README.cn.md',
        rules: ['cml-files'],
        why: 'the survey at commit 6dd61bcd (v0.2.9); 1,266 .cml files is that baseline\'s corpus',
    },
    {
        file: 'docs/design/cpp-memory/README.cn.md',
        rules: ['cml-files'],
        why: 'the coverage run at commit 6dd61bcd (v0.2.9); 1,082 .cml files is the corpus it walked',
    },
    {
        file: 'docs/design/graph-model/README.cn.md',
        rules: ['cml-files'],
        why: 'the census at commit e4652d81 (v0.2.9); 1,099 .cml files is that tree',
    },
]

const WORDS = new Map(
    (
        'zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen ' +
        'fifteen sixteen seventeen eighteen nineteen twenty'
    )
        .split(' ')
        .map((word, index) => [word, index]),
)

function read(relative) {
    return fs.readFileSync(path.join(ROOT, relative), 'utf8')
}

function int(value) {
    return Number(String(value).replace(/,/g, ''))
}

function number(value) {
    if (/^\d+$/.test(value)) return Number(value)
    return WORDS.get(String(value).toLowerCase())
}

function lineAt(text, index) {
    return text.slice(0, index).split('\n').length
}

function cmakeMinimum() {
    const match = read('CMakeLists.txt').match(/cmake_minimum_required\(VERSION\s+(\d+\.\d+)/)
    if (!match) throw new Error('CMakeLists.txt has no cmake_minimum_required(VERSION x.y)')
    return { value: match[1], source: 'CMakeLists.txt:1' }
}

function exportMatrix() {
    const match = read('export_matrix.md').match(
        /^(\d+) operator URIs.*?\*\*(\d+) forward operators\*\* and \*\*(\d+) gradient operators\*\*/m,
    )
    if (!match) throw new Error('export_matrix.md has no "N operator URIs ... N forward ... N gradient" line')
    return {
        value: { operators: int(match[1]), forward: int(match[2]), gradient: int(match[3]) },
        source: 'export_matrix.md (generated by scripts/gen_export_matrix.py)',
    }
}

function gateCounts() {
    const output = execFileSync(process.execPath, ['scripts/release-gate.mjs', '--dry-run'], {
        cwd: ROOT,
        encoding: 'utf8',
        maxBuffer: 32 * 1024 * 1024,
    })
    const slow = output.match(/slow: (\d+) named plan file/)
    const forbidden = output.match(/skipped by policy: (\d+) plan files/)
    const staticChecks = output
        .split('\n')
        .filter((line) => /\[gate\] \$ .*scripts\/[^\s]+\.mjs/.test(line)).length
    if (!slow || !forbidden) throw new Error('release-gate --dry-run did not print the slow and skipped counts')
    if (staticChecks === 0) throw new Error('release-gate --dry-run printed no static check command')
    return {
        value: { slow: Number(slow[1]), static: staticChecks, forbidden: Number(forbidden[1]) },
        source: 'scripts/release-gate.mjs --dry-run (SLOW_PLANS, STATIC_CHECKS, FORBIDDEN)',
    }
}

function tilesSelection() {
    const selection = JSON.parse(read('test/plan-selection.json'))
    const rule = selection.rules.find((each) => each.paths.includes('modules/tiles/'))
    if (!rule) throw new Error('test/plan-selection.json has no modules/tiles/ rule')
    let gpuTests = 0
    for (const plan of rule.plans) {
        const parsed = parseTomlFile(path.join(ROOT, 'test/plans', plan))
        if (parsed.tier === 'slow') gpuTests += (parsed.tests || []).length
    }
    return {
        value: { plans: rule.plans.length, gpuTests },
        source: 'test/plan-selection.json (modules/tiles/ rule) and the slow GPU plan files',
    }
}

function signatureFiles() {
    const dir = path.join(ROOT, 'stdlib', 'signatures')
    return {
        value: fs.readdirSync(dir).filter((name) => name.endsWith('.cml')).length,
        source: 'stdlib/signatures/*.cml',
    }
}

function cmlFiles() {
    const output = execFileSync('git', ['ls-files', '*.cml'], { cwd: ROOT, encoding: 'utf8' })
    return {
        value: output.split('\n').filter(Boolean).length,
        source: "git ls-files '*.cml'",
    }
}

function snapshotFiles() {
    const lines = read('test/plans/feat/parse/golden/ast_hashes.txt').split(/\r?\n/)
    return {
        value: lines.filter((line) => line.trim() && !line.trim().startsWith('#')).length,
        source: 'test/plans/feat/parse/golden/ast_hashes.txt',
    }
}

function moduleSubdirectories() {
    const text = read('CMakeLists.txt')
    const matches = [...text.matchAll(/add_subdirectory\(modules\/[^)]+\)/g)]
    if (matches.length === 0) throw new Error('CMakeLists.txt has no add_subdirectory(modules/...) line')
    return { value: matches.length, source: 'CMakeLists.txt (add_subdirectory(modules/...))' }
}

function syntaxErrorCodes() {
    const text = read('include/camel/core/error/diagnostics/messages/syntax.h')
    const values = [...text.matchAll(/(\w+)\s*=\s*(0x[0-9A-Fa-f]+)/g)]
    const errors = values.filter((match) => ((parseInt(match[2], 16) >> 20) & 0xf) === 0).length
    return {
        value: errors,
        source: 'include/camel/core/error/diagnostics/messages/syntax.h (severity Error)',
    }
}

const SOURCE_EXTENSIONS = new Set(['.h', '.hpp', '.hxx', '.cpp', '.cc', '.cxx'])
const SKIP_DIRECTORIES = new Set(['.git', 'out', 'build', 'node_modules', 'third_party'])

function runtimeErrorReferences() {
    const count = (directory) => {
        let total = 0
        const walk = (current) => {
            for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
                const full = path.join(current, entry.name)
                if (entry.isDirectory()) {
                    if (!SKIP_DIRECTORIES.has(entry.name)) walk(full)
                } else if (SOURCE_EXTENSIONS.has(path.extname(entry.name))) {
                    const text = fs.readFileSync(full, 'utf8')
                    let at = 0
                    while ((at = text.indexOf('RuntimeDiag::RuntimeError', at)) >= 0) {
                        total += 1
                        at += 'RuntimeDiag::RuntimeError'.length
                    }
                }
            }
        }
        if (fs.existsSync(path.join(ROOT, directory))) walk(path.join(ROOT, directory))
        return total
    }
    return {
        value: {
            total: ['src', 'include', 'modules', 'tools'].reduce((sum, dir) => sum + count(dir), 0),
            python: count('modules/python'),
        },
        source: 'src/, include/, modules/, tools/ (substring RuntimeDiag::RuntimeError)',
    }
}

async function declarationModuleCount() {
    const { declarationModules } = await import('./stdlib-declarations.mjs')
    return {
        value: declarationModules().length,
        source: 'scripts/stdlib-declarations.mjs declarationModules()',
    }
}

function passAliases() {
    const text = read('src/execute/pass.cpp')
    const start = text.indexOf('passAliases')
    if (start < 0) throw new Error('src/execute/pass.cpp has no passAliases map')
    const open = text.indexOf('{', start)
    const close = text.indexOf('};', open)
    if (open < 0 || close < 0) throw new Error('cannot delimit the passAliases map')
    const entries = text.slice(open, close).match(/\{\s*"std::[^"]+"\s*,\s*"std::[^"]+"\s*\}/g) || []
    return { value: entries.length, source: 'src/execute/pass.cpp passAliases map' }
}

const TRUTHS = {
    cmake: cmakeMinimum,
    exportMatrix,
    gate: gateCounts,
    tiles: tilesSelection,
    signatures: signatureFiles,
    cml: cmlFiles,
    snapshot: snapshotFiles,
    subdirectories: moduleSubdirectories,
    syntax: syntaxErrorCodes,
    runtimeErrors: runtimeErrorReferences,
    declarationModules: declarationModuleCount,
    passAliases,
}

const RULES = [
    {
        id: 'cmake-minimum',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                const match = line.match(/^\|\s*CMake\s*\|\s*(\d+\.\d+)\s*\|/)
                if (!match) return
                findings.push({
                    line: index + 1,
                    claim: 'CMake minimum',
                    says: match[1],
                    truth: TRUTHS.cmake.value,
                    source: TRUTHS.cmake.source,
                    ok: match[1] === TRUTHS.cmake.value,
                })
            })
            return findings
        },
    },
    {
        id: 'export-matrix',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                if (!line.includes('export_matrix.md')) return
                const match = line.match(
                    /([\d,]+)\s*个算子中\s*([\d,]+)\s*个前向算子、\s*([\d,]+)\s*个梯度算子/,
                )
                if (!match) return
                const says = [int(match[1]), int(match[2]), int(match[3])]
                const truth = TRUTHS.exportMatrix.value
                findings.push({
                    line: index + 1,
                    claim: 'export_matrix.md counts',
                    says: `${says[0]} operators / ${says[1]} forward / ${says[2]} gradient`,
                    truth: `${truth.operators} / ${truth.forward} / ${truth.gradient}`,
                    source: TRUTHS.exportMatrix.source,
                    ok: says[0] === truth.operators && says[1] === truth.forward && says[2] === truth.gradient,
                })
            })
            return findings
        },
    },
    {
        id: 'runtime-error-refs',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                const match = line.match(/RuntimeDiag::RuntimeError`?\s*仍被引用\s*([\d,]+)\s*次/)
                if (!match) return
                const python = line.match(/其中\s*`?python`?\s*模块\s*([\d,]+)\s*次/)
                const truth = TRUTHS.runtimeErrors.value
                const saysTotal = int(match[1])
                const saysPython = python ? int(python[1]) : null
                findings.push({
                    line: index + 1,
                    claim: 'RuntimeDiag::RuntimeError references',
                    says: `${saysTotal}${saysPython === null ? '' : ` (python ${saysPython})`}`,
                    truth: `${truth.total}${saysPython === null ? '' : ` (python ${truth.python})`}`,
                    source: TRUTHS.runtimeErrors.source,
                    ok: saysTotal === truth.total && (saysPython === null || saysPython === truth.python),
                })
            })
            return findings
        },
    },
    {
        id: 'syntax-codes',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                if (!line.includes('syntax.h')) return
                const match = line.match(/([\d,]+)\s*个(?:错误)?码/)
                if (!match) return
                const says = int(match[1])
                findings.push({
                    line: index + 1,
                    claim: 'SyntaxDiag error codes',
                    says,
                    truth: TRUTHS.syntax.value,
                    source: TRUTHS.syntax.source,
                    ok: says === TRUTHS.syntax.value,
                })
            })
            return findings
        },
    },
    {
        id: 'signature-files',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                const claims = []
                const direct = line.match(/([\d,]+)\s*个签名文件/)
                if (direct) claims.push({ claim: 'signature files', says: int(direct[1]) })
                const covered = line.match(/签名文件.*?([\d,]+)\s*个模块/)
                if (covered) claims.push({ claim: 'modules covered by signature files', says: int(covered[1]) })
                if (line.includes('stdlib/signatures')) {
                    const files = line.match(/([\d,]+)\s*个文件/)
                    if (files) claims.push({ claim: 'signature files', says: int(files[1]) })
                }
                for (const each of claims) {
                    findings.push({
                        line: index + 1,
                        claim: each.claim,
                        says: each.says,
                        truth: TRUTHS.signatures.value,
                        source: TRUTHS.signatures.source,
                        ok: each.says === TRUTHS.signatures.value,
                    })
                }
            })
            return findings
        },
    },
    {
        id: 'cml-files',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                const match = line.match(/([\d,]+)\s*个\s*`?\.cml/)
                if (!match) return
                const says = int(match[1])
                findings.push({
                    line: index + 1,
                    claim: '.cml files',
                    says,
                    truth: TRUTHS.cml.value,
                    source: TRUTHS.cml.source,
                    ok: says === TRUTHS.cml.value,
                })
            })
            return findings
        },
    },
    {
        id: 'snapshot-files',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                if (!line.includes('快照')) return
                const match = line.match(/([\d,]+)\s*个文件/)
                if (!match) return
                const says = int(match[1])
                findings.push({
                    line: index + 1,
                    claim: 'AST snapshot files',
                    says,
                    truth: TRUTHS.snapshot.value,
                    source: TRUTHS.snapshot.source,
                    ok: says === TRUTHS.snapshot.value,
                })
            })
            return findings
        },
    },
    {
        id: 'module-subdirs',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                const match = line.match(/([\d,]+)\s*个子目录/)
                if (!match) return
                const says = int(match[1])
                findings.push({
                    line: index + 1,
                    claim: 'module subdirectories',
                    says,
                    truth: TRUTHS.subdirectories.value,
                    source: TRUTHS.subdirectories.source,
                    ok: says === TRUTHS.subdirectories.value,
                })
            })
            return findings
        },
    },
    {
        id: 'gate-counts',
        check(file, text) {
            const findings = []
            const definitions = [
                {
                    claim: 'slow-layer plan files',
                    match: (line) => line.match(/([A-Za-z]+|\d+)\s+explicit plan files/),
                    truth: TRUTHS.gate.value.slow,
                },
                {
                    claim: 'static checks',
                    match: (line) => line.match(/([A-Za-z]+|\d+)\s+static checks/),
                    truth: TRUTHS.gate.value.static,
                },
                {
                    claim: 'forbidden plan files',
                    match: (line) => line.match(/([A-Za-z]+|\d+)\s+plan files\s+`agents\/test\.md`\s+forbids/),
                    truth: TRUTHS.gate.value.forbidden,
                },
            ]
            text.split(/\r?\n/).forEach((line, index) => {
                for (const definition of definitions) {
                    const match = definition.match(line)
                    if (!match) continue
                    const says = number(match[1])
                    if (says === undefined) continue
                    findings.push({
                        line: index + 1,
                        claim: definition.claim,
                        says: match[1],
                        truth: definition.truth,
                        source: TRUTHS.gate.source,
                        ok: says === definition.truth,
                    })
                }
            })
            return findings
        },
    },
    {
        id: 'tiles-selection',
        check(file, text) {
            const findings = []
            const pattern = /selects\s+([A-Za-z]+|\d+)\s+plan files\s+and\s+the\s+([\d,]+)\s+GPU tests/g
            for (const match of text.matchAll(pattern)) {
                const plans = number(match[1])
                const gpuTests = int(match[2])
                const truth = TRUTHS.tiles.value
                findings.push({
                    line: lineAt(text, match.index),
                    claim: 'modules/tiles/** selection',
                    says: `${match[1]} plan files / ${gpuTests} GPU tests`,
                    truth: `${truth.plans} / ${truth.gpuTests}`,
                    source: TRUTHS.tiles.source,
                    ok: plans === truth.plans && gpuTests === truth.gpuTests,
                })
            }
            return findings
        },
    },
    {
        id: 'pass-aliases',
        check(file, text) {
            const findings = []
            text.split(/\r?\n/).forEach((line, index) => {
                const match = line.match(/另有\s*([\d,]+)\s*个别名/)
                if (!match) return
                const says = int(match[1])
                findings.push({
                    line: index + 1,
                    claim: 'built-in pass aliases',
                    says,
                    truth: TRUTHS.passAliases.value,
                    source: TRUTHS.passAliases.source,
                    ok: says === TRUTHS.passAliases.value,
                })
            })
            return findings
        },
    },
    {
        id: 'declaration-modules',
        check(file, text) {
            const findings = []
            const pattern = /([\d,]+)\s*个声明模块|\b([\d,]+)\s+declaration modules?\b/g
            for (const match of text.matchAll(pattern)) {
                const says = int(match[1] || match[2])
                findings.push({
                    line: lineAt(text, match.index),
                    claim: 'declaration modules',
                    says,
                    truth: TRUTHS.declarationModules.value,
                    source: TRUTHS.declarationModules.source,
                    ok: says === TRUTHS.declarationModules.value,
                })
            }
            return findings
        },
    },
]

function collect() {
    const files = []
    const walk = (relative) => {
        const absolute = path.join(ROOT, relative)
        const stat = fs.statSync(absolute)
        if (stat.isFile()) {
            const normalized = relative.split(path.sep).join('/')
            if (normalized.endsWith('.md') && !GENERATED.has(normalized)) files.push(normalized)
            return
        }
        for (const entry of fs.readdirSync(absolute, { withFileTypes: true })) {
            const child = path.posix.join(relative.split(path.sep).join('/'), entry.name)
            if (entry.isDirectory()) {
                if (RECORDS.some((record) => child === record || child.startsWith(`${record}/`))) continue
                walk(child)
            } else if (entry.name.endsWith('.md')) {
                const normalized = child.split(path.sep).join('/')
                if (!GENERATED.has(normalized)) files.push(normalized)
            }
        }
    }
    for (const root of ROOTS) walk(root)
    return [...new Set(files)].sort()
}

function isAllowed(file, rule) {
    return ALLOWED.find((entry) => entry.file === file && entry.rules.includes(rule))
}

async function main() {
    const list = process.argv.includes('--list')

    for (const [name, compute] of Object.entries(TRUTHS)) {
        try {
            TRUTHS[name] = await compute()
        } catch (error) {
            console.error(`[doc-counts] cannot compute the truth for '${name}': ${error.message}`)
            process.exit(2)
        }
    }

    if (list) {
        for (const rule of RULES) console.log(`rule: ${rule.id}`)
        for (const entry of ALLOWED) console.log(`allowlisted: ${entry.file} (${entry.rules.join(', ')}): ${entry.why}`)
        return
    }

    const files = collect()
    const problems = []
    const allowed = []
    let checked = 0

    for (const file of files) {
        const text = read(file)
        for (const rule of RULES) {
            for (const finding of rule.check(file, text)) {
                if (isAllowed(file, rule.id)) {
                    allowed.push({ file, rule: rule.id, line: finding.line })
                    continue
                }
                checked += 1
                if (!finding.ok) problems.push({ file, rule: rule.id, ...finding })
            }
        }
    }

    for (const problem of problems) {
        console.log(
            `${problem.file}:${problem.line}: ${problem.rule}: ${problem.claim} says ${problem.says}, ` +
                `truth is ${problem.truth} (${problem.source})`,
        )
    }

    if (problems.length > 0) {
        const documents = new Set(problems.map((problem) => problem.file)).size
        console.log(
            `${problems.length} stale count(s) in ${documents} reader-facing document(s): ` +
                `${checked} claim(s) checked, ${allowed.length} allowlisted (dated snapshot)`,
        )
        process.exit(1)
    }

    console.log(
        `no stale count in ${files.length} reader-facing document(s): ${checked} claim(s) checked, ` +
            `${allowed.length} allowlisted (dated snapshot; reasons in scripts/check-doc-counts.mjs)`,
    )
}

await main()