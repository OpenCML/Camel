/**
 * 自动运行所有测试用例（test/modules 与 test/run 下的 .cml 文件）
 * 用法: node scripts/test-all.js [--modules] [--run] [--suite SUITE]
 *   --modules  仅运行 test/modules 下测试（默认包含）
 *   --run      仅运行 test/run 下测试（默认包含）
 *   --suite X  指定 test/run 的 suite，如 linear/nn/para（默认全部）
 */
import fs from 'fs'
import path from 'path'
import { execSync } from 'child_process'
import iconv from 'iconv-lite'
import { BASEDIR, logStep, logDone, logFail } from './common.js'

const isWindows = process.platform === 'win32'
const exeName = `camel${isWindows ? '.exe' : ''}`
const LOG_DIR = path.join(BASEDIR, 'test', 'logs')

const C = {
    reset: '\x1b[0m',
    green: '\x1b[32m',
    red: '\x1b[31m'
}

function logFilename(label) {
    const base = label.replace(/[/\\]/g, '.').replace(/\.cml$/i, '')
    return `${base}.log`
}

const camel = path.join(BASEDIR, 'out', 'latest', 'bin', exeName)

function collectCmlFiles(dir, baseDir = dir) {
    const files = []
    if (!fs.existsSync(dir)) return files
    for (const name of fs.readdirSync(dir)) {
        const full = path.join(dir, name)
        const stat = fs.statSync(full)
        if (stat.isDirectory()) {
            files.push(...collectCmlFiles(full, baseDir))
        } else if (name.endsWith('.cml')) {
            files.push(path.relative(baseDir, full))
        }
    }
    return files.sort()
}

function parseArgs() {
    const args = process.argv.slice(2)
    let modules = true
    let run = true
    let suiteFilter = null

    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--modules') {
            run = false
        } else if (args[i] === '--run') {
            modules = false
        } else if (args[i] === '--suite' && i + 1 < args.length) {
            suiteFilter = args[++i]
        }
    }
    return { modules, run, suiteFilter }
}

function decodeStderr(stderr) {
    if (!stderr) return ''
    const buf = Buffer.isBuffer(stderr) ? stderr : Buffer.from(String(stderr), 'utf-8')
    if (isWindows && buf.length > 0) {
        try {
            return iconv.decode(buf, 'gbk')
        } catch {
            return buf.toString('utf-8')
        }
    }
    return buf.toString('utf-8')
}

function runTest(filePath) {
    const cmd = `"${camel}" "${filePath}"`
    const env = { ...process.env, PYTHONUNBUFFERED: '1' }
    let stdout = ''
    let stderr = ''
    try {
        const result = execSync(cmd, { stdio: 'pipe', encoding: undefined, env })
        stdout = Buffer.isBuffer(result) ? result.toString('utf-8') : String(result ?? '')
        return { ok: true, stdout, stderr }
    } catch (e) {
        stdout = e.stdout ? (Buffer.isBuffer(e.stdout) ? e.stdout.toString('utf-8') : String(e.stdout)) : ''
        stderr = decodeStderr(e.stderr) || (e.stderr ? (Buffer.isBuffer(e.stderr) ? e.stderr.toString('utf-8') : String(e.stderr)) : '') || e.message
        return { ok: false, stdout, stderr }
    }
}

function writeLog(logPath, filePath, stdout, stderr) {
    const parts = []
    try {
        parts.push('--- source ---\n' + fs.readFileSync(filePath, 'utf-8'))
    } catch {
        parts.push('--- source ---\n(无法读取)')
    }
    parts.push('--- stdout ---\n' + (stdout || ''))
    if (stderr) parts.push('--- stderr ---\n' + stderr)
    fs.mkdirSync(path.dirname(logPath), { recursive: true })
    fs.writeFileSync(logPath, parts.join('\n'), 'utf-8')
}

function main() {
    const { modules, run, suiteFilter } = parseArgs()

    const allFiles = []
    const labels = []

    if (modules) {
        const modulesDir = path.join(BASEDIR, 'test', 'modules')
        const modFiles = collectCmlFiles(modulesDir)
        for (const f of modFiles) {
            allFiles.push(path.join(modulesDir, f))
            labels.push(`modules/${f}`)
        }
    }

    if (run) {
        const runDir = path.join(BASEDIR, 'test', 'run')
        if (fs.existsSync(runDir)) {
            const suites = fs.readdirSync(runDir, { withFileTypes: true })
                .filter((e) => e.isDirectory())
                .map((e) => e.name)
                .filter((s) => !suiteFilter || s === suiteFilter)

            for (const suite of suites) {
                const suiteDir = path.join(runDir, suite)
                const runFiles = collectCmlFiles(suiteDir)
                for (const f of runFiles) {
                    allFiles.push(path.join(suiteDir, f))
                    labels.push(`run/${suite}/${f}`)
                }
            }
        }
    }

    if (allFiles.length === 0) {
        logFail('未找到任何 .cml 测试文件')
        process.exit(1)
    }

    const runTs = new Date().toISOString().replace(/[-:]/g, '').replace('T', '_').slice(0, 15)
    const runLogDir = path.join(LOG_DIR, runTs)
    const passedDir = path.join(runLogDir, 'passed')
    const failedDir = path.join(runLogDir, 'failed')
    fs.mkdirSync(runLogDir, { recursive: true })

    logStep(`共 ${allFiles.length} 个测试，开始自动执行...`)
    logStep(`日志目录: ${runLogDir}`)

    const passed = []
    const failed = []

    for (let i = 0; i < allFiles.length; i++) {
        const label = labels[i]
        const fp = allFiles[i]
        const result = runTest(fp)
        const subDir = result.ok ? passedDir : failedDir
        writeLog(path.join(subDir, logFilename(label)), fp, result.stdout, result.stderr)

        const mark = result.ok ? `${C.green}[√]${C.reset}` : `${C.red}[✗]${C.reset}`
        console.log(`  ${mark} [${i + 1}/${allFiles.length}] ${label}`)
        if (result.ok) {
            passed.push(label)
        } else {
            failed.push(label)
        }
    }

    console.log('')
    logDone(`通过: ${passed.length}/${allFiles.length}`)
    if (failed.length > 0) {
        logFail(`失败: ${failed.length} (详见 ${failedDir})`)
        process.exit(1)
    }
}

main()
