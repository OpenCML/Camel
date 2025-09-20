import fs from 'fs'
import fsp from 'fs/promises'
import readline from 'readline'
import { execSync } from 'child_process'

let updatedFiles = 0

function formatDate(date) {
    const months = [
        'Jan.',
        'Feb.',
        'Mar.',
        'Apr.',
        'May.',
        'Jun.',
        'Jul.',
        'Aug.',
        'Sep.',
        'Oct.',
        'Nov.',
        'Dec.'
    ]
    return `${months[date.getMonth()]} ${date
        .getDate()
        .toString()
        .padStart(2, '0')}, ${date.getFullYear()}`
}

async function updateFile(filePath) {
    try {
        const stats = await fsp.stat(filePath)
        const updatedDate = formatDate(stats.mtime)
        const rl = readline.createInterface({
            input: fs.createReadStream(filePath),
            output: process.stdout,
            terminal: false
        })

        let lines = []
        let needUpdate = false
        let updateIndex = 0

        const updatedLine = ` * Updated: ${updatedDate}`

        for await (const line of rl) {
            lines.push(line)
        }
        lines.push('')

        for (let i = 0; i < lines.length; i++) {
            if (lines[i].includes('* Created:')) {
                needUpdate = true
                if (lines[i + 1]?.includes('* Updated:')) {
                    if (lines[i + 1] === updatedLine) {
                        needUpdate = false
                        break
                    }
                    lines[i + 1] = updatedLine
                } else {
                    updateIndex = i + 1
                }
                break
            }
        }

        if (needUpdate) {
            if (updateIndex) {
                lines.splice(updateIndex, 0, updatedLine)
            }
            await fsp.writeFile(filePath, lines.join('\r\n'), 'utf8')
            console.log(`Updated: ${filePath}`)
            updatedFiles++
        }
    } catch (err) {
        console.error(`Could not update file: ${err}`)
    }
}

function getChangedFiles() {
    try {
        const stdout = execSync('git diff --name-only && git diff --name-only --cached', {
            encoding: 'utf8'
        })
        const files = stdout
            .split('\n')
            .map((line) => line.trim())
            .filter(
                (line) =>
                    line &&
                    (line.endsWith('.c') ||
                        line.endsWith('.cpp') ||
                        line.endsWith('.h') ||
                        line.endsWith('.hpp'))
            )
        return [...new Set(files)]
    } catch (err) {
        console.error('Failed to get changed files from git:', err)
        return []
    }
}

;(async () => {
    const changedFiles = getChangedFiles()

    if (changedFiles.length === 0) {
        console.log('No changes detected by git. Nothing to update.')
        return
    }

    for (const filePath of changedFiles) {
        await updateFile(filePath)
    }

    console.log(`Updated ${updatedFiles} file(s).`)
})()
