#!/usr/bin/env node
// Tokenises Camel source files with syntaxes/camel.tmLanguage.json, the way VS Code does
// (vscode-textmate + vscode-oniguruma), and prints the scopes of every token:
//
//     >let x = 0xFF
//     #^^^ meta.declaration.let.camel storage.type.let.camel
//     #    ^ meta.declaration.let.camel variable.other.constant.camel
//     #      ^ keyword.operator.assignment.camel
//     #        ^^^^ constant.numeric.integer.hexadecimal.camel
//
// A '>' line is a source line; the '#' lines under it mark one token each and list its scopes
// without the root scope 'source.camel'. Tokens with no scope of their own (plain identifiers,
// white space; 'meta.*' region scopes do not count) have no line. The output is the golden format of
// test/plans/feat/tools/grammar.plan.toml.
//
// Usage: node editors/vscode/scripts/tokenize.mjs <file.cml>...
//        (paths relative to the current directory, else to the repository root)
// Needs the devDependencies of editors/vscode: run `npm install` there first.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const EXT_ROOT = path.dirname(HERE)
const REPO_ROOT = path.resolve(EXT_ROOT, '..', '..')
const GRAMMAR = path.join(EXT_ROOT, 'syntaxes', 'camel.tmLanguage.json')
const ROOT_SCOPE = 'source.camel'

const require = createRequire(import.meta.url)

// V8 reserves several gigabytes of address space for every WebAssembly memory unless its trap
// handler is off; under a virtual memory limit (`ulimit -v`, as some CI runners set) loading
// oniguruma then fails with "Cannot allocate Wasm memory". Where Node knows the option, run
// again with it.
const NO_TRAP_HANDLER = '--disable-wasm-trap-handler'
if (process.allowedNodeEnvironmentFlags.has(NO_TRAP_HANDLER) && !process.execArgv.includes(NO_TRAP_HANDLER)) {
    const proc = spawnSync(process.execPath, [NO_TRAP_HANDLER, fileURLToPath(import.meta.url), ...process.argv.slice(2)], {
        stdio: 'inherit',
    })
    if (proc.error) {
        console.error(proc.error.message)
    }
    process.exit(typeof proc.status === 'number' ? proc.status : 1)
}

function loadDependencies() {
    try {
        const vsctm = require('vscode-textmate')
        const oniguruma = require('vscode-oniguruma')
        const wasmPath = path.join(path.dirname(require.resolve('vscode-oniguruma/package.json')), 'release', 'onig.wasm')
        return { vsctm, oniguruma, wasmPath }
    } catch (err) {
        console.error(`cannot load vscode-textmate / vscode-oniguruma: ${err.message}`)
        console.error('run `npm install` in editors/vscode first')
        process.exit(2)
    }
}

function resolveInput(arg) {
    if (path.isAbsolute(arg)) return arg
    const fromCwd = path.resolve(process.cwd(), arg)
    return fs.existsSync(fromCwd) ? fromCwd : path.resolve(REPO_ROOT, arg)
}

async function main() {
    const inputs = process.argv.slice(2)
    if (inputs.length === 0) {
        console.error('usage: node tokenize.mjs <file.cml>...')
        process.exit(2)
    }

    const { vsctm, oniguruma, wasmPath } = loadDependencies()
    const wasm = fs.readFileSync(wasmPath)
    await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength))

    const registry = new vsctm.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
            createOnigString: (text) => new oniguruma.OnigString(text),
        }),
        loadGrammar: async (scopeName) =>
            scopeName === ROOT_SCOPE ? vsctm.parseRawGrammar(fs.readFileSync(GRAMMAR, 'utf8'), GRAMMAR) : null,
    })
    const grammar = await registry.loadGrammar(ROOT_SCOPE)
    if (!grammar) {
        console.error(`cannot load ${GRAMMAR}`)
        process.exit(2)
    }

    const out = []
    for (const input of inputs) {
        const file = resolveInput(input)
        const text = fs.readFileSync(file, 'utf8').replace(/\r\n?/g, '\n')
        const lines = text.split('\n')
        if (lines[lines.length - 1] === '') lines.pop()

        if (inputs.length > 1) {
            out.push(`== ${path.relative(REPO_ROOT, file).split(path.sep).join('/')}`)
        }
        let ruleStack = vsctm.INITIAL
        for (const line of lines) {
            out.push(`>${line}`)
            const result = grammar.tokenizeLine(line, ruleStack)
            ruleStack = result.ruleStack
            for (const token of result.tokens) {
                const scopes = token.scopes.filter((scope) => scope !== ROOT_SCOPE)
                if (scopes.length === 0 || token.endIndex <= token.startIndex) continue
                if (line.slice(token.startIndex, token.endIndex).trim() === '') continue
                // A token with region scopes only (a plain identifier inside brackets) says
                // nothing its neighbours do not say.
                if (scopes.every((scope) => scope.startsWith('meta.') && !scope.startsWith('meta.preprocessor.'))) continue
                out.push(`#${' '.repeat(token.startIndex)}${'^'.repeat(token.endIndex - token.startIndex)} ${scopes.join(' ')}`)
            }
        }
    }
    process.stdout.write(out.join('\n') + '\n')
}

main().catch((err) => {
    console.error(err && err.stack ? err.stack : String(err))
    process.exit(1)
})
