import { exec } from 'child_process'

const ignoredFilesPatterns = [/\.md$/, /^docs\//, /\.json$/, /^\.updcache$/]

const isIgnored = (filePath) => {
    return ignoredFilesPatterns.some((pattern) => pattern.test(filePath))
}

const parseGitLog = (logOutput) => {
    const lines = logOutput.split('\n')
    let totalInsertions = 0
    let totalDeletions = 0

    console.log('Insert. Delete. File')

    lines.forEach((line) => {
        const parts = line.split('\t')
        if (parts.length === 3) {
            const [insertions, deletions, filePath] = parts
            if (!isIgnored(filePath)) {
                console.log(line.trim())
                totalInsertions += parseInt(insertions, 10) || 0
                totalDeletions += parseInt(deletions, 10) || 0
            } else {
                console.log(`${line.trim()} (ignored)`)
            }
        }
    })

    return { totalInsertions, totalDeletions }
}

exec('git log --numstat -1', (error, stdout, stderr) => {
    if (error) {
        console.error(`Error executing git log: ${error.message}`)
        return
    }

    if (stderr) {
        console.error(`stderr: ${stderr}`)
        return
    }

    const { totalInsertions, totalDeletions } = parseGitLog(stdout)
    console.log(`Total Insertions: ${totalInsertions}`)
    console.log(`Total Deletions: ${totalDeletions}`)
})
