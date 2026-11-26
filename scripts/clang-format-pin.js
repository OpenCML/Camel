// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import { spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'

// The repository pins one clang-format major version in `.clang-format-version`.
export function readPin(repoRoot) {
    const text = fs.readFileSync(path.join(repoRoot, '.clang-format-version'), 'utf-8')
    const major = parseInt(text.trim(), 10)
    if (!Number.isInteger(major)) {
        throw new Error('.clang-format-version must hold a single major version number')
    }
    return major
}

export function majorOf(versionText) {
    const m = /version\s+(\d+)\./.exec(versionText)
    return m ? parseInt(m[1], 10) : null
}

function spawnTool(bin, args) {
    // A Windows batch wrapper cannot be started without a shell.
    const shell = process.platform === 'win32' && /\.(cmd|bat)$/i.test(bin)
    return spawnSync(bin, args, { encoding: 'utf-8', shell })
}

// The major version of the formatter `bin` names, or null when it is missing or unreadable.
export function formatterMajor(bin) {
    const proc = spawnTool(bin, ['--version'])
    if (proc.error || proc.status !== 0) {
        return null
    }
    return majorOf(`${proc.stdout}`)
}

// Formats `files` in place with `bin` when it is the pinned major version. Otherwise no file is
// touched: one line says what was found and what is pinned. Returns true when files were formatted.
export function formatWithPin(files, { bin, pin, log = console.log, logError = console.error }) {
    const found = formatterMajor(bin)
    if (found !== pin) {
        const what = found === null ? 'no usable clang-format' : `clang-format ${found}`
        log(
            `[format] found ${what}, the repository pins clang-format ${pin}: ` +
                `${files.length} staged file(s) left as they are`
        )
        return false
    }
    for (const file of files) {
        const proc = spawnTool(bin, ['-i', file])
        if (proc.error || proc.status !== 0) {
            logError(`Failed to format: ${file}`)
        } else {
            log(`Formatted: ${file}`)
        }
    }
    return true
}
