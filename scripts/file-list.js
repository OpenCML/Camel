export function normalizeRepoPath(file) {
    return file.replaceAll('\\', '/').replace(/^\.\//, '')
}

export function uniqueFiles(files) {
    return [...new Set(files)]
}

export function explicitFilesArg(argv = process.argv) {
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
