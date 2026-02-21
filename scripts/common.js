import { execSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
export const BASEDIR = path.dirname(path.dirname(__filename))

const isWindows = process.platform === 'win32'
const executableName = `camel${isWindows ? '.exe' : ''}`
const libName = isWindows
    ? 'libcamel.dll'
    : process.platform === 'darwin'
        ? 'libcamel.dylib'
        : 'libcamel.so'

const colors = {
    reset: '\x1b[0m',
    cyan: '\x1b[36m',
    yellow: '\x1b[33m',
    green: '\x1b[32m',
    red: '\x1b[31m'
}

export function logStep(message) {
    console.log(`${colors.cyan}[STEP]${colors.reset} ${message}`)
}

export function logDone(message) {
    console.log(`${colors.green}[DONE]${colors.reset} ${message}`)
}

export function logWarn(message) {
    console.log(`${colors.yellow}[WARN]${colors.reset} ${message}`)
}

export function logFail(message) {
    console.error(`${colors.red}[FAIL]${colors.reset} ${message}`)
}

export function runCommand(command, options = {}) {
    try {
        logStep(`Running: ${command}`)
        execSync(command, { stdio: 'inherit', ...options })
    } catch (error) {
        logFail(`Error executing command: ${command}\n${error}`)
        process.exit(1)
    }
}

/** Windows: 避免 node_modules/.bin/rc 被 CMake 当作 Resource Compiler，将含 node_modules 的 PATH 项移到末尾 */
export function getCmakeEnv() {
    if (process.platform !== 'win32') return undefined
    const pathParts = (process.env.PATH || '').split(path.delimiter)
    const normal = pathParts.filter((p) => !p.includes('node_modules'))
    const nodeParts = pathParts.filter((p) => p.includes('node_modules'))
    if (nodeParts.length === 0) return undefined
    return { ...process.env, PATH: [...normal, ...nodeParts].join(path.delimiter) }
}

/** Windows + Clang: 显式指定编译器，因 Conan 工具链可能未设置，CMake 会误选 MSVC */
export function getCmakeClangFlags() {
    if (process.platform !== 'win32') return ''
    return '-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-rc'
}

export function removeDir(dir) {
    if (fs.existsSync(dir)) {
        fs.rmSync(dir, { recursive: true, force: true })
        console.log(`Removed directory: ${dir}`)
    }
}

export function copyFile(src, dest) {
    fs.copyFileSync(src, dest)
    console.log(`Copied: ${src} to ${dest}`)
}

export function copyDir(src, dest) {
    if (!fs.existsSync(dest)) {
        fs.mkdirSync(dest, { recursive: true })
    }
    fs.readdirSync(src).forEach((file) => {
        const srcFile = path.join(src, file)
        const destFile = path.join(dest, file)
        if (fs.lstatSync(srcFile).isDirectory()) {
            copyDir(srcFile, destFile)
        } else {
            copyFile(srcFile, destFile)
        }
    })
}

/** 供 build/debug/profile 使用：git describe 生成版本后缀 */
export function getGitVersion() {
    try {
        const output = execSync('git describe --tags --always --dirty', {
            cwd: BASEDIR,
            stdio: ['ignore', 'pipe', 'ignore'],
            encoding: 'utf-8'
        })
            .trim()
            .replace(/-/g, '_')
        return `${output}`
    } catch {
        return ''
    }
}

/**
 * 执行 Conan 检查、CMake 配置与构建。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @param {string} cmakeOptionFlags - 来自 getCmakeOptionFlags() 的 -D 参数
 */
export function runCmakeBuild(config, cmakeOptionFlags) {
    process.chdir(BASEDIR)
    const buildDir = path.join(BASEDIR, 'build')
    const conanToolchain = path.join(buildDir, 'conan_toolchain.cmake')
    if (!fs.existsSync(conanToolchain)) {
        logStep('build/ or conan_toolchain.cmake missing, running conan install...')
        runCommand('conan install . --output-folder=build --build=missing', { cwd: BASEDIR })
        runCommand('conan install . --output-folder=build --build=missing --settings=build_type=Debug', {
            cwd: BASEDIR
        })
        runCommand('conan install . --output-folder=build --build=missing --settings=build_type=RelWithDebInfo', {
            cwd: BASEDIR
        })
    }
    process.chdir(buildDir)

    const gitVersion = getGitVersion()
    const BUILD_FOOTPRINT =
        new Date().toISOString().replace(/[-:]/g, '').replace(/[T]/g, '_').slice(2, 15) +
        (gitVersion ? `_${gitVersion}` : '')

    logStep(`Building ${config}... (${BUILD_FOOTPRINT})`)
    const cmakeEnv = getCmakeEnv()
    const clangFlags = getCmakeClangFlags()
    runCommand(
        `cmake .. -G "Ninja Multi-Config" -DBUILD_FOOTPRINT="${BUILD_FOOTPRINT}" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake ${clangFlags} ${cmakeOptionFlags}`,
        cmakeEnv ? { env: cmakeEnv } : {}
    )
    runCommand(`cmake --build . --config ${config}`, cmakeEnv ? { env: cmakeEnv } : {})
}

/**
 * 将构建产物复制到项目根目录与 stdlib：camel 可执行文件、libcamel、以及 libs/* 下的所有 .cmo 到 stdlib/。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 */
export function copyBuildArtifacts(config) {
    const exeSrcDir = path.join(BASEDIR, 'build', 'tools', 'camel-cli', config)
    const libSrcDir = path.join(BASEDIR, 'build', config)
    const libsBuildDir = path.join(BASEDIR, 'build', 'libs')
    const stdlibDir = path.join(BASEDIR, 'stdlib')

    copyFile(path.join(exeSrcDir, executableName), path.join(BASEDIR, executableName))
    const libSrc = path.join(libSrcDir, libName)
    if (fs.existsSync(libSrc)) {
        // 把exe复制到libSrc的同级目录以便调试
        copyFile(path.join(exeSrcDir, executableName), path.join(libSrcDir, executableName))
        // debug模式下额外复制.pdb文件
        if (config === 'Debug') {
            const pdbName = `${executableName.replace('.exe', '')}.pdb`
            const pdbSrc = path.join(exeSrcDir, pdbName)
            if (fs.existsSync(pdbSrc)) {
                copyFile(pdbSrc, path.join(libSrcDir, pdbName))
            }
        }
        copyFile(libSrc, path.join(BASEDIR, libName))
    }

    if (fs.existsSync(libsBuildDir)) {
        if (!fs.existsSync(stdlibDir)) {
            fs.mkdirSync(stdlibDir, { recursive: true })
        }
        const subdirs = fs.readdirSync(libsBuildDir, { withFileTypes: true })
        for (const ent of subdirs) {
            if (!ent.isDirectory()) continue
            const configDir = path.join(libsBuildDir, ent.name, config)
            if (!fs.existsSync(configDir)) continue
            const files = fs.readdirSync(configDir)
            for (const f of files) {
                if (f.endsWith('.cmo')) {
                    copyFile(path.join(configDir, f), path.join(stdlibDir, f))
                }
            }
        }
    }
}