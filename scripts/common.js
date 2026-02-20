import { execSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
export const BASEDIR = path.dirname(path.dirname(__filename))

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