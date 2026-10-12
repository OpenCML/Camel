#!/usr/bin/env node
// Builds the installable extension: checks the grammar and the manifest, compiles the TypeScript
// source and runs `vsce package`. The .vsix lands in editors/vscode (or where --out says).
//
// Usage: npm run package [-- --out <file.vsix>]
// Needs `npm install` in editors/vscode first.

import { spawnSync } from 'node:child_process'
import path from 'node:path'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'

const EXT_ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const require = createRequire(import.meta.url)

function run(label, command, args) {
    console.log(`> ${label}`)
    const proc = spawnSync(command, args, { cwd: EXT_ROOT, stdio: 'inherit' })
    if (proc.error) {
        console.error(`${label}: ${proc.error.message}`)
        process.exit(1)
    }
    if (proc.status !== 0) {
        process.exit(typeof proc.status === 'number' ? proc.status : 1)
    }
}

function script(packageName, relative) {
    try {
        return path.join(path.dirname(require.resolve(`${packageName}/package.json`)), relative)
    } catch {
        console.error(`${packageName} is not installed: run \`npm install\` in editors/vscode first`)
        process.exit(1)
    }
}

run('check the grammar tables', process.execPath, [path.join(EXT_ROOT, 'scripts', 'check-grammar.mjs')])
run('check the manifest', process.execPath, [path.join(EXT_ROOT, 'scripts', 'check-manifest.mjs')])
run('compile', process.execPath, [script('typescript', 'bin/tsc'), '-p', EXT_ROOT])
run('package', process.execPath, [script('@vscode/vsce', 'vsce'), 'package', ...process.argv.slice(2)])
