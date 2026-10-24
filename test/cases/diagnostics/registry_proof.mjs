#!/usr/bin/env node
/**
 * The diagnostics registry's failure paths, on a scratch copy of the tree:
 *
 *   - a duplicate number fails, and --write repairs nothing;
 *   - a number outside its section's range fails;
 *   - a name that already exists fails;
 *   - a generated header that disagrees with the registry fails;
 *   - an empty registry fails (a guard that examines nothing is not a pass);
 *   - one appended line takes the next free number at the end of its section, and --write
 *     changes exactly four files: the registry, that section's header and table, and the
 *     reference; no other declaration moves;
 *   - a declaration that does not say whether it carries a machine-applicable edit fails, and so
 *     do one that says something else, one that gives a cause outside the vocabulary, and one
 *     that promises an edit without the help that tells the reader what it does.
 *
 * Usage: node registry_proof.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import crypto from 'crypto'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..', '..')
const GENERATOR = path.join(ROOT, 'scripts', 'diagnostics-catalog.mjs')
const SECTIONS = ['internal', 'lexical', 'syntax', 'semantic', 'runtime']

let checks = 0
const failures = []
const scratchDirs = []

function check(label, condition, detail) {
    checks++
    if (!condition) failures.push(`${label}${detail ? `: ${detail}` : ''}`)
}

function run(root, mode) {
    return spawnSync(process.execPath, [GENERATOR, mode, '--root', root], {
        encoding: 'utf8',
        cwd: ROOT,
    })
}

function copyInto(root) {
    fs.mkdirSync(path.join(root, 'diagnostics'), { recursive: true })
    fs.copyFileSync(
        path.join(ROOT, 'diagnostics', 'registry.tsv'),
        path.join(root, 'diagnostics', 'registry.tsv')
    )
    // The generated headers are checked against DiagType in base.h, so the scratch tree needs it.
    const baseFrom = path.join(ROOT, 'include', 'camel', 'core', 'error', 'diagnostics', 'base.h')
    const baseTo = path.join(root, 'include', 'camel', 'core', 'error', 'diagnostics')
    fs.mkdirSync(baseTo, { recursive: true })
    fs.copyFileSync(baseFrom, path.join(baseTo, 'base.h'))
    for (const area of ['include', 'src']) {
        const relative =
            area === 'include'
                ? path.join('camel', 'core', 'error', 'diagnostics', 'messages')
                : path.join('core', 'error', 'diagnostics', 'messages')
        const from = path.join(ROOT, area, relative)
        const to = path.join(root, area, relative)
        fs.mkdirSync(to, { recursive: true })
        for (const file of fs.readdirSync(from)) fs.copyFileSync(path.join(from, file), path.join(to, file))
    }
}

function fresh(label) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), `camel-diagnostics-${label}-`))
    scratchDirs.push(root)
    copyInto(root)
    return root
}

function registryPath(root) {
    return path.join(root, 'diagnostics', 'registry.tsv')
}

function headerPath(root, section = 'semantic') {
    return path.join(root, 'include', 'camel', 'core', 'error', 'diagnostics', 'messages', `${section}.h`)
}

function append(root, line) {
    fs.appendFileSync(registryPath(root), line + '\n')
}

function codesIn(file) {
    const text = fs.readFileSync(file, 'utf8')
    const codes = new Map()
    for (const m of text.matchAll(/^\s*(\w+)\s*=\s*(0x[0-9A-F]+)\s*,/gm)) codes.set(m[1], Number(m[2]))
    return codes
}

function expectFailure(label, mode, needle) {
    const root = fresh(label)
    append(root, mode.line)
    const result = run(root, mode.mode)
    check(`${label}: exits 1`, result.status === 1, `exit ${result.status}: ${result.stdout}${result.stderr}`)
    check(
        `${label}: says why`,
        result.stdout.includes(needle),
        `wanted '${needle}' in: ${result.stdout}${result.stderr}`
    )
    return { root, result }
}

// 1. The copied tree is consistent before anything is changed.
{
    const root = fresh('baseline')
    const result = run(root, '--check')
    check('baseline: the copy passes --check', result.status === 0, result.stdout + result.stderr)
}

// 2. A number used twice fails, and --write does not quietly move it.
{
    const { root } = expectFailure('duplicate-number', {
        line: 'semantic\tDuplicateNumberProbe\t0x000001\terror\tA duplicate.\tRemove it.\tnone: choice: the writer decides',
        mode: '--check',
    }, 'have the same value')
    const before = fs.readFileSync(headerPath(root), 'utf8')
    const write = run(root, '--write')
    check('duplicate-number: --write refuses too', write.status === 1, write.stdout + write.stderr)
    const after = fs.readFileSync(headerPath(root), 'utf8')
    check('duplicate-number: nothing was renumbered', before === after)
}

// 3. A number in the wrong range fails.
expectFailure('out-of-range', {
    line: 'semantic\tOutOfRangeProbe\t0x100001\terror\tA number from the warning band.\tRemove it.\tnone: choice: the writer decides',
    mode: '--check',
}, 'is outside the error range')

// 4. A name that already exists fails.
expectFailure('duplicate-name', {
    line: 'semantic\tRedeclaration\t\terror\tA second declaration of one name.\tRemove it.\tnone: choice: the writer decides',
    mode: '--check',
}, 'is already declared')

// 5. A generated header that disagrees with the registry fails.
{
    const root = fresh('stale-header')
    const file = headerPath(root)
    const before = fs.readFileSync(file, 'utf8')
    const after = before.replace(/(Redeclaration\s*=\s*)0x000001/, '$10x000002')
    check('stale-header: the header was edited', after !== before)
    fs.writeFileSync(file, after)
    const result = run(root, '--check')
    check('stale-header: exits 1', result.status === 1, `exit ${result.status}`)
    check(
        'stale-header: names the file and the repair',
        result.stdout.includes('semantic.h is not what diagnostics/registry.tsv produces') &&
            result.stdout.includes('--write'),
        result.stdout + result.stderr
    )
}

// 6. An empty registry is refused, not reported as a pass.
{
    const root = fresh('empty')
    fs.writeFileSync(registryPath(root), '# no declarations\n')
    const result = run(root, '--check')
    check('empty: exits 1', result.status === 1, `exit ${result.status}`)
    check('empty: says nothing was checked', result.stdout.includes('nothing was checked'), result.stdout)
}

// 6a. A new diagnostic has to say whether it carries an edit, and why not when it does not: the
//     column is what makes "every one of them says so" a property of the registry rather than a
//     habit, so each way of leaving it out is a failure of its own.
{
    const root = fresh('fix-column-missing')
    append(root, 'semantic\tNoFixColumnProbe\t\terror\tOne appended line.\tNothing else.')
    const result = run(root, '--check')
    check('fix-column-missing: exits 1', result.status === 1, `exit ${result.status}`)
    check(
        'fix-column-missing: names the seven fields',
        result.stdout.includes('message, help and fix separated by tabs'),
        result.stdout + result.stderr
    )
}
expectFailure('fix-column-word', {
    line: 'semantic\tFixWordProbe\t\terror\tOne appended line.\tNothing else.\tmaybe',
    mode: '--check',
}, "write 'edit' or 'none: <cause>'")

expectFailure('fix-cause-unknown', {
    line: 'semantic\tFixCauseProbe\t\terror\tOne appended line.\tNothing else.\tnone: because',
    mode: '--check',
}, "is not a reason for having no edit")

//     An edit tells a tool the text to write, and a report that does that without telling the
//     reader is refused: the two belong to the same declaration.
expectFailure('fix-edit-without-help', {
    line: 'semantic\tFixNoHelpProbe\t\terror\tOne appended line.\t\tedit',
    mode: '--check',
}, 'declares \'edit\' and has no help')

// 7. One line is all a new diagnostic costs, and it takes the next free number.
{
    const root = fresh('one-line')
    const baseline = run(root, '--write')
    check('one-line: the baseline writes cleanly', baseline.status === 0, baseline.stdout + baseline.stderr)
    const beforeCodes = codesIn(headerPath(root))
    const beforeHash = new Map()
    const hashTree = () => {
        const hashes = new Map()
        const walk = (dir) => {
            for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
                const full = path.join(dir, entry.name)
                if (entry.isDirectory()) walk(full)
                else if (entry.isFile())
                    hashes.set(path.relative(root, full).replaceAll('\\', '/'), crypto.createHash('sha256').update(fs.readFileSync(full)).digest('hex'))
            }
        }
        walk(root)
        return hashes
    }
    for (const [k, v] of hashTree()) beforeHash.set(k, v)
    const expected = Math.max(...[...beforeCodes.values()].filter((code) => code < 0x100000)) + 1
    append(root, 'semantic\tOneLineProbe\t\terror\tOne appended line.\tNothing else.\tnone: choice: the writer decides')
    const result = run(root, '--write')
    check('one-line: write exits 0', result.status === 0, result.stdout + result.stderr)

    const registry = fs.readFileSync(registryPath(root), 'utf8')
    const hex = '0x' + expected.toString(16).toUpperCase().padStart(6, '0')
    check(
        'one-line: the registry line takes the next free number',
        registry.includes(`semantic\tOneLineProbe\t${hex}\terror\tOne appended line.`),
        registry.split('\n').filter((l) => l.includes('OneLineProbe')).join('\n')
    )
    const afterCodes = codesIn(headerPath(root))
    check('one-line: the header has the new number', afterCodes.get('OneLineProbe') === expected, `${afterCodes.get('OneLineProbe')}`)
    const moved = [...beforeCodes].filter(([name, code]) => afterCodes.get(name) !== code)
    check('one-line: no declaration moved', moved.length === 0, JSON.stringify(moved))
    check('one-line: exactly one declaration was added', afterCodes.size === beforeCodes.size + 1)

    const afterHash = hashTree()
    const changed = [...afterHash.keys()].filter((file) => beforeHash.get(file) !== afterHash.get(file))
    const expectedChanged = [
        'diagnostics/registry.tsv',
        'docs/diagnostics.md',
        'include/camel/core/error/diagnostics/messages/semantic.h',
        'src/core/error/diagnostics/messages/semantic.cpp',
    ]
    check(
        'one-line: only the registry and its generated files changed',
        changed.length === expectedChanged.length && expectedChanged.every((file) => changed.includes(file)),
        changed.join(', ')
    )
    const recheck = run(root, '--check')
    check('one-line: the new tree passes --check', recheck.status === 0, recheck.stdout + recheck.stderr)
}

for (const dir of scratchDirs) {
    try {
        fs.rmSync(dir, { recursive: true, force: true })
    } catch {
        /* a leftover temp directory is not a failure */
    }
}

if (failures.length) {
    console.log(`diagnostics registry proof: ${failures.length} of ${checks} checks failed`)
    for (const failure of failures) console.log(`  - ${failure}`)
    process.exit(1)
}
console.log(`diagnostics registry proof: ${checks} checks passed`)