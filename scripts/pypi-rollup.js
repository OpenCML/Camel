import path from 'path'
import fs from 'fs'
import { runCommand, copyDir, copyFile, removeDir, BASEDIR, getTag, executableName, libName, logStep, logDone } from './common.js'
import { ensureDirSync } from 'fs-extra/esm'
import build from './build.js'
import { execSync } from 'child_process'
import os from 'os'

function getPythonPlatformTag() {
    const platform = os.platform()
    const arch = os.arch()

    // 处理架构映射
    const archMap = {
        x64: platform === 'win32' ? 'amd64' : 'x86_64',
        arm64: 'arm64'
    }

    // 获取基础架构名称
    const normalizedArch = archMap[arch] || arch

    // 平台特定处理
    switch (platform) {
        case 'win32':
            return `win_${normalizedArch}`

        case 'darwin': // macOS
            const ver = execSync('sw_vers -productVersion').toString().trim()
            const [major, minor] = ver.split('.').slice(0, 2)
            const macosVersion = `${major}_${minor}`

            // 检测实际CPU架构（因为Rosetta可能返回错误信息）
            let realArch = normalizedArch
            try {
                const proc = execSync('sysctl -n machdep.cpu.brand_string').toString()
                realArch = proc.includes('Apple') ? 'arm64' : normalizedArch
            } catch {}

            return `macosx_${macosVersion}_${realArch}`

        case 'linux':
            return `linux_${normalizedArch}`

        default:
            throw new Error(`Unsupported platform: ${platform}`)
    }
}

function main() {
    logStep('Building project')
    process.chdir(BASEDIR)
    build()
    logDone('Project built')

    logStep('Collecting build artifacts to out/')
    runCommand('node scripts/collect-out.js build')
    const outTag = process.env.CAMEL_PYPI_TAG || getTag()
    const OUT_ROOT = path.join(BASEDIR, 'out', outTag)
    logDone(`Using out/${outTag}/`)

    logStep('Copying files to pypi directory')
    const PYPI_BASE = path.join(BASEDIR, 'pypi', 'camel-lang')
    const CAMEL_BASE = path.join(PYPI_BASE, 'src', 'camel')
    const BIN_DIR = path.join(CAMEL_BASE, '.cmlenv', 'bin')
    fs.mkdirSync(BIN_DIR, { recursive: true })

    const exePath = path.join(OUT_ROOT, executableName)
    const libPath = path.join(OUT_ROOT, 'libs', libName)
    if (fs.existsSync(exePath)) copyFile(exePath, path.join(BIN_DIR, executableName))
    if (fs.existsSync(libPath)) copyFile(libPath, path.join(BIN_DIR, libName))

    const includeSrc = path.join(OUT_ROOT, 'include')
    const stdlibSrc = path.join(OUT_ROOT, 'stdlib')
    if (fs.existsSync(includeSrc)) copyDir(includeSrc, path.join(CAMEL_BASE, '.cmlenv', 'include'))
    if (fs.existsSync(stdlibSrc)) copyDir(stdlibSrc, path.join(CAMEL_BASE, '.cmlenv', 'lib', 'std'))
    logDone('Files copied')

    const scriptFiles = [
        `src/camel/.cmlenv/bin/${executableName}`,
        `src/camel/.cmlenv/bin/${libName}`
    ].filter((p) => fs.existsSync(path.join(PYPI_BASE, p)))
    const pyprojectPath = path.join(PYPI_BASE, 'pyproject.toml')
    const pyprojectOriginal = fs.readFileSync(pyprojectPath, 'utf-8')
    const scriptFilesStr = scriptFiles.map((s) => `"${s}"`).join(', ')
    fs.writeFileSync(
        pyprojectPath,
        pyprojectOriginal.replace(/script-files = \[[^\]]*\]/, `script-files = [${scriptFilesStr}]`)
    )
    logStep(`Configured script-files: [${scriptFiles.join(', ')}]`)

    logStep('Building wheel')
    process.chdir(PYPI_BASE)
    runCommand(`python -m build --wheel`)
    logDone('Wheel built')

    logStep('Move wheel to project dist')
    const PYPI_DIST = path.join(BASEDIR, 'dist', 'pypi')
    const platformTag = getPythonPlatformTag()
    const files = path.join(PYPI_BASE, 'dist')
    const filesList = fs.readdirSync(files)
    filesList.forEach((file) => {
        if (file.match(/-any.whl$/)) {
            const newFile = file.replace(/-any.whl$/, `-${platformTag}.whl`)
            fs.renameSync(path.join(files, file), path.join(files, newFile))
            logDone(`Renamed ${file} to ${newFile}`)
        }
    })
    logDone('Files renamed')
    ensureDirSync(PYPI_DIST)
    copyDir(path.join(PYPI_BASE, 'dist'), PYPI_DIST)
    logDone('Wheel moved')

    logStep('Cleaning up')
    fs.writeFileSync(pyprojectPath, pyprojectOriginal)
    removeDir(path.join(PYPI_BASE, 'dist'))
    removeDir(path.join(PYPI_BASE, 'build'))
    removeDir(path.join(CAMEL_BASE, '.cmlenv'))
    removeDir(path.join(PYPI_BASE, 'src', 'camel_lang.egg-info'))
    logDone('Cleaned up')
    process.chdir(BASEDIR)
}

main()
