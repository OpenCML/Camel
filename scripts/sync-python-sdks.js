import fs from 'fs'
import path from 'path'
import { BASEDIR, logDone, logStep, logWarn } from './common.js'

function exists(p) {
    try {
        fs.accessSync(p)
        return true
    } catch {
        return false
    }
}

function copyIfExists(src, dest) {
    if (!exists(src)) return false
    fs.mkdirSync(path.dirname(dest), { recursive: true })
    fs.copyFileSync(src, dest)
    return true
}

function copyDirIfExists(src, dest) {
    if (!exists(src)) return false
    fs.mkdirSync(dest, { recursive: true })
    fs.cpSync(src, dest, { recursive: true })
    return true
}

function main() {
    const archiveRoot =
        process.argv[2] ||
        process.env.CAMEL_PYTHON_ARCHIVE_ROOT ||
        ''
    const outRoot = path.join(BASEDIR, 'modules', 'python', 'sdks')

    if (!archiveRoot) {
        logWarn(
            'Missing Python archive root. Usage: node scripts/sync-python-sdks.js <python-root> (or set CAMEL_PYTHON_ARCHIVE_ROOT).'
        )
        process.exit(1)
    }

    if (!exists(archiveRoot)) {
        logWarn(`Python archive root not found: ${archiveRoot}`)
        process.exit(1)
    }

    fs.mkdirSync(outRoot, { recursive: true })
    const entries = fs.readdirSync(archiveRoot, { withFileTypes: true })
    const candidates = entries
        .filter((e) => e.isDirectory() && /^Python3\d{2}$/i.test(e.name))
        .map((e) => e.name)
        .sort()

    if (candidates.length === 0) {
        logWarn(`No Python3xx folders found under ${archiveRoot}`)
        return
    }

    for (const name of candidates) {
        const srcRoot = path.join(archiveRoot, name)
        const ver = name.slice(-3) // e.g. 311
        const dstRoot = path.join(outRoot, `python${ver}`)
        logStep(`Syncing SDK ${name} -> modules/python/sdks/python${ver}`)

        fs.mkdirSync(dstRoot, { recursive: true })
        copyDirIfExists(path.join(srcRoot, 'include'), path.join(dstRoot, 'include'))
        copyDirIfExists(path.join(srcRoot, 'libs'), path.join(dstRoot, 'libs'))
        copyDirIfExists(path.join(srcRoot, 'DLLs'), path.join(dstRoot, 'DLLs'))
        copyIfExists(path.join(srcRoot, 'python.exe'), path.join(dstRoot, 'python.exe'))
        copyIfExists(path.join(srcRoot, 'pythonw.exe'), path.join(dstRoot, 'pythonw.exe'))
        copyIfExists(path.join(srcRoot, 'python3.dll'), path.join(dstRoot, 'python3.dll'))
        copyIfExists(path.join(srcRoot, `python${ver}.dll`), path.join(dstRoot, `python${ver}.dll`))
        copyIfExists(path.join(srcRoot, 'vcruntime140.dll'), path.join(dstRoot, 'vcruntime140.dll'))
        copyIfExists(path.join(srcRoot, 'vcruntime140_1.dll'), path.join(dstRoot, 'vcruntime140_1.dll'))
    }

    logDone(`Python SDKs synchronized to ${outRoot}`)
}

main()

