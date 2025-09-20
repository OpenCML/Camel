import { execSync } from 'child_process'

function getChangedFiles() {
    try {
        const output = execSync('git diff --name-only HEAD', { encoding: 'utf-8' })
        return output
            .split('\n')
            .map((f) => f.trim())
            .filter((f) => f.match(/\.(cpp|h)$/) && f.startsWith('src/'))
    } catch (err) {
        console.error('Error getting changed files from git:', err)
        return []
    }
}

const changedFiles = getChangedFiles()

if (changedFiles.length === 0) {
    console.log('No modified .cpp or .h files to format.')
} else {
    changedFiles.forEach((file) => {
        try {
            execSync(`clang-format -i "${file}"`, { stdio: 'inherit' })
            console.log(`Formatted: ${file}`)
        } catch (error) {
            console.error(`Failed to format: ${file}`)
        }
    })
}
