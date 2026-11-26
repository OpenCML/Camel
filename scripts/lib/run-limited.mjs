// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Runs one command with a time limit and ends its whole process tree when the limit passes.
 *
 * `spawnSync(..., { timeout })` ends only the process it started; the programs that process started
 * go on. A synchronous caller therefore runs its command through this file: it is a separate node
 * process that starts the command so that its tree can be named (`test/tools/kill-tree.mjs`), waits
 * for it asynchronously, and ends the tree at the limit.
 *
 * Usage: node run-limited.mjs --timeout-ms <n> --result <file> [--shell] [--cwd <dir>] -- <command line>
 *
 * The command's output goes to this process's own stdio. `<file>` receives
 * `{"code": <exit code or null>, "signal": <or null>, "timedOut": <bool>}`; the exit code of this
 * process is the command's, or 124 when the limit ended it.
 */

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import { DETACH_TREE, killProcessTree } from '../../test/tools/kill-tree.mjs'
import { splitCommand } from './job.mjs'

const argv = process.argv.slice(2)
const split = argv.indexOf('--')
const options = argv.slice(0, split)
const line = argv.slice(split + 1).join(' ')
const valueOf = (name) => options[options.indexOf(name) + 1]
const timeoutMs = Number(valueOf('--timeout-ms'))
const resultFile = valueOf('--result')
const shell = options.includes('--shell')
const cwd = options.includes('--cwd') ? valueOf('--cwd') : undefined

function finish(result, code) {
    fs.writeFileSync(resultFile, JSON.stringify(result))
    process.exit(code)
}

let child
if (shell) {
    child = spawn(line, { cwd, shell: true, detached: DETACH_TREE, stdio: 'inherit', windowsHide: true })
} else {
    const [program, ...args] = splitCommand(line)
    child = spawn(program, args, { cwd, detached: DETACH_TREE, stdio: 'inherit', windowsHide: true })
}
let timedOut = false
const timer = setTimeout(() => {
    timedOut = true
    killProcessTree(child)
    finish({ code: null, signal: null, timedOut: true }, 124)
}, timeoutMs)
child.on('error', () => {
    clearTimeout(timer)
    finish({ code: null, signal: null, timedOut: false, error: true }, 127)
})
child.on('close', (code, signal) => {
    clearTimeout(timer)
    if (!timedOut) finish({ code, signal, timedOut: false }, code ?? 1)
})
