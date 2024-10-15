import fs from 'fs'
import fsp from 'fs/promises'
import path from 'path'
import readline from 'readline'
import crypto from 'crypto'

const CACHE_FILE_PATH = '.updcache'

let updatedFiles = 0

let cacheMap = {}
let cacheHasChanges = false

async function initializeCache() {
    try {
        const cacheContent = await fsp.readFile(CACHE_FILE_PATH, 'utf8').catch(() => '')
        const lines = cacheContent.split('\n')
        cacheMap = {}

        for (const line of lines) {
            const trimmedLine = line.trim()
            if (trimmedLine) {
                const [cachedPath, cachedMD5Base64] = trimmedLine.split(':')
                cacheMap[cachedPath] = cachedMD5Base64
            }
        }
    } catch (err) {
        console.error(`Error initializing cache: ${err}`)
    }
}

async function updateCacheFile() {
    try {
        const updatedCacheContent = Object.entries(cacheMap)
            .map(([path, md5]) => `${path}:${md5}`)
            .join('\n')
        await fsp.writeFile(CACHE_FILE_PATH, updatedCacheContent, 'utf8')
    } catch (err) {
        console.error(`Error updating cache file: ${err}`)
    }
}

function calculateMD5Base64(filePath) {
    const fileBuffer = fs.readFileSync(filePath)
    const hash = crypto.createHash('md5')
    hash.update(fileBuffer)
    return hash.digest('base64')
}

async function compareAndCacheMD5(srcPath) {
    const srcMD5Base64 = calculateMD5Base64(srcPath)
    const cachedMD5Base64 = cacheMap[srcPath]

    let isSame = false
    if (cachedMD5Base64) {
        isSame = cachedMD5Base64 === srcMD5Base64
    }

    if (!isSame) {
        cacheMap[srcPath] = srcMD5Base64
        cacheHasChanges = true
    }

    return isSame
}

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
        if (needUpdate && !(await compareAndCacheMD5(filePath))) {
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

async function traverseDirectory(dir) {
    try {
        const files = await fsp.readdir(dir)

        for (const file of files) {
            const filePath = path.join(dir, file)
            const stat = await fsp.stat(filePath)

            if (stat.isFile()) {
                await updateFile(filePath)
            } else if (stat.isDirectory()) {
                await traverseDirectory(filePath)
            }
        }
    } catch (err) {
        console.error(`Could not list the directory: ${err}`)
    }
}

async function cleanUpCache() {
    for (const cachedPath in cacheMap) {
        try {
            await fsp.access(cachedPath);
        } catch (err) {
            console.log(`File ${path.resolve(cachedPath)} is missing. Removing from cache...`);
            delete cacheMap[cachedPath];
            cacheHasChanges = true;
        }
    }
}

;(async () => {
    await initializeCache()

    const targetDirs = ['src', 'include']
    for (const targetDirectory of targetDirs) {
        await traverseDirectory(targetDirectory)
    }

    console.log(`Updated ${updatedFiles} files.`)

    await cleanUpCache()

    if (cacheHasChanges) {
        console.log('Cache has changes. Updating cache file...')
        await updateCacheFile()
    }
})()
