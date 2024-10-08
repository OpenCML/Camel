import fs from 'fs'
import path from 'path'
import readline from 'readline'

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
    return `${months[date.getMonth()]} ${date.getDate().toString().padStart(2, '0')}, ${date.getFullYear()}`
}

function updateFile(filePath) {
    fs.stat(filePath, (err, stats) => {
        if (err) {
            console.error(`Could not get file stats: ${err}`)
            return
        }

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

        rl.on('line', (line) => lines.push(line))

        rl.on('close', () => {
            lines.push('')
            for (let i = 0; i < lines.length; i++) {
                if (lines[i].includes('* Created:')) {
                    needUpdate = true
                    if (lines[i + 1].includes('* Updated:')) {
                        if (lines[i + 1] == updatedLine) {
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
                fs.writeFileSync(filePath, lines.join('\r\n'), 'utf8')
                console.log(`Updated: ${filePath}`)
            }
        })
    })
}

function traverseDirectory(dir) {
    fs.readdir(dir, (err, files) => {
        if (err) {
            console.error(`Could not list the directory: ${err}`)
            return
        }

        files.forEach((file) => {
            const filePath = path.join(dir, file)
            fs.stat(filePath, (error, stat) => {
                if (error) {
                    console.error(`Could not stat file: ${error}`)
                    return
                }

                if (stat.isFile()) {
                    updateFile(filePath)
                } else if (stat.isDirectory()) {
                    traverseDirectory(filePath)
                }
            })
        })
    })
}

const targetDirs = ['src', 'include']
targetDirs.forEach((targetDirectory) => {
    traverseDirectory(targetDirectory)
})
