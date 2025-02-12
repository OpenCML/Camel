import path from 'path'
import { runCommand, copyDir, removeDir, BASEDIR, logStep, logDone } from './common.js'
import { ensureDirSync } from 'fs-extra/esm'
import build from './build.js'
import { execSync } from 'child_process'
import os from 'os'
import fs from 'fs'

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

    logStep('Copying files to pypi directory')
    const PYPI_BASE = path.join(BASEDIR, 'pypi', 'camel-lang')
    const CAMEL_BASE = path.join(PYPI_BASE, 'src', 'camel')

    copyDir(path.join(BASEDIR, 'build', 'Release'), path.join(CAMEL_BASE, '.cmlenv', 'bin'))
    copyDir(path.join(BASEDIR, 'stdlib'), path.join(CAMEL_BASE, '.cmlenv', 'lib', 'std'))
    logDone('Files copied')

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
    removeDir(path.join(PYPI_BASE, 'dist'))
    removeDir(path.join(PYPI_BASE, 'build'))
    removeDir(path.join(CAMEL_BASE, '.cmlenv'))
    removeDir(path.join(PYPI_BASE, 'src', 'camel_lang.egg-info'))
    logDone('Cleaned up')
    process.chdir(BASEDIR)
}

main()
