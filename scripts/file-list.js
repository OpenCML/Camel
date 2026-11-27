// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import fs from 'fs'

export function normalizeRepoPath(file) {
    return file.replaceAll('\\', '/').replace(/^\.\//, '')
}

export function uniqueFiles(files) {
    return [...new Set(files)]
}

// The files a script is pointed at: `--files a b c`, or `--files-from <list>` with the paths
// separated by NUL. A merge can change thousands of paths, more than one command line holds on
// Windows, so the hook hands its list over in a file.
export function explicitFilesArg(argv = process.argv) {
    const from = argv.indexOf('--files-from')
    if (from >= 0) {
        return uniqueFiles(splitNulList(fs.readFileSync(argv[from + 1], 'utf8')))
    }
    const idx = argv.indexOf('--files')
    if (idx < 0) {
        return null
    }
    return uniqueFiles(argv.slice(idx + 1).map(normalizeRepoPath).filter(Boolean))
}

export function splitLineList(output) {
    return output
        .split('\n')
        .map((line) => normalizeRepoPath(line.trim()))
        .filter(Boolean)
}

export function splitNulList(output) {
    return output
        .split('\0')
        .map(normalizeRepoPath)
        .filter(Boolean)
}
