import { execFileSync, execSync } from 'child_process'
import { explicitFilesArg, splitLineList, uniqueFiles } from './file-list.js'

const dirs = ['src', 'include', 'modules', 'tools']

function getChangedFiles() {
    try {
        const output = execSync('git diff --name-only --diff-filter=ACMR HEAD', { encoding: 'utf-8' })
        return splitLineList(output)
    } catch (err) {
        console.error('Error getting changed files from git:', err)
        return []
    }
}

function formatTargets(files) {
    return uniqueFiles(files).filter(
        (file) =>
            /\.(cpp|h)$/.test(file) && dirs.some((dir) => file.startsWith(`${dir}/`))
    )
}

const changedFiles = formatTargets(explicitFilesArg() ?? getChangedFiles())

if (changedFiles.length === 0) {
    console.log('No modified .cpp or .h files to format.')
} else {
    changedFiles.forEach((file) => {
        try {
            execFileSync('clang-format', ['-i', file], { stdio: 'inherit' })
            console.log(`Formatted: ${file}`)
        } catch (error) {
            console.error(`Failed to format: ${file}`)
        }
    })
}
