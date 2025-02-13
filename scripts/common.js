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

export function runCommand(command) {
    try {
        logStep(`Running: ${command}`)
        execSync(command, { stdio: 'inherit' })
    } catch (error) {
        logFail(`Error executing command: ${command}\n${error}`)
        process.exit(1)
    }
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