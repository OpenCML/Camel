import fs from 'fs'
import path from 'path'
import { runCommand, BASEDIR, logStep, logDone, logFail } from './common.js'

const C = {
    reset: '\x1b[0m',
    bright: '\x1b[1m',
    cyan: '\x1b[36m',
    dim: '\x1b[2m'
}

function getTermWidth() {
    const cols = process.stdout.columns
    return typeof cols === 'number' && cols > 0 ? Math.min(cols, 60) : 40
}

function padVis(s, n) {
    const raw = s.replace(/\x1b\[[0-9;]*m/g, '')
    if (raw.length >= n) return s
    return s + ' '.repeat(n - raw.length)
}

function getKeyPrompt(i, files) {
    const W = getTermWidth() - 2
    const HL = Math.floor((W - 1) / 2)
    const HR = W - 1 - HL
    const curr = files[i] ?? '(none)'
    const next = i + 1 < files.length ? files[i + 1] : '(none)'
    const prev = i > 0 ? files[i - 1] : '(none)'
    const currLine = `> ${curr}`
    const k = (c) => `${C.bright}${C.cyan}${c}${C.reset}`
    const nLine = padVis(`(${k('N')})ext ${next}`, HL - 2)
    const pLine = padVis(`(${k('P')})rev ${prev}`, HR - 2)
    const lines = [
        `+${'-'.repeat(W)}+`,
        `| ${C.bright}${C.cyan}${padVis(currLine, W - 2)}${C.reset} |`,
        `+${'-'.repeat(HL)}+${'-'.repeat(HR)}+`,
        `| ${nLine} | ${pLine} |`,
        `+${'-'.repeat(HL)}+${'-'.repeat(HR)}+`,
        `| ${padVis(`(${k('Q')})uit`, HL - 2)} | ${padVis(`(${k('C')})lear`, HR - 2)} |`,
        `+${'-'.repeat(HL)}+${'-'.repeat(HR)}+`
    ]
    return lines.join('\n')
}

function waitForAction(keyPrompt) {
    process.stdout.write(keyPrompt + '\n> ')

    return new Promise((resolve) => {
        if (process.stdin.isTTY) {
            process.stdin.setRawMode(true)
        }
        process.stdin.resume()
        process.stdin.setEncoding('utf8')

        const onData = (key) => {
            if (process.stdin.isTTY) {
                process.stdin.setRawMode(false)
            }
            process.stdin.removeListener('data', onData)
            process.stdin.pause()
            process.stdout.write('\n')

            const k = String(key).charAt(0).toLowerCase()
            if (key === '\r' || key === '\n' || k === '\r' || k === '\n') resolve('retry')
            else if (k === 'q') resolve('quit')
            else if (k === 'n') resolve('next')
            else if (k === 'p') resolve('prev')
            else if (k === 'c') resolve('clear')
            else resolve('retry')
        }

        process.stdin.once('data', onData)
    })
}

const TEST_DIR = path.join(BASEDIR, 'test', 'run')

function parseArgs() {
    const args = process.argv.slice(2)
    const suite = args[0] || 'linear'
    let template = args.slice(1).join(' ').trim() || 'run {file}'
    if (!template.includes('{file}')) {
        template += ' {file}'
    }
    return { suite, template }
}

async function main() {
    const { suite, template } = parseArgs()

    const dir = path.join(TEST_DIR, suite)

    if (!fs.existsSync(dir)) {
        logFail(`Test directory not found: ${dir}`)
        process.exit(1)
    }

    const files = fs.readdirSync(dir).filter((f) => f.endsWith('.cml')).sort()
    if (files.length === 0) {
        logFail(`No .cml files in ${dir}`)
        process.exit(1)
    }

    const isWindows = process.platform === 'win32'
    const camel = path.join(BASEDIR, `camel${isWindows ? '.exe' : ''}`)
    if (!fs.existsSync(camel)) {
        logFail(`camel not found. Run "npm run build" first.`)
        process.exit(1)
    }

    const fullTemplate = `"${camel}" ${template}`.replace(/\s+/g, ' ').trim()
    logStep(`Running ${files.length} tests in test/run/${suite}/`)
    logStep(`命令模板: camel ${template}`)

    for (let i = 0; i < files.length; ) {
        let action
        do {
            action = await waitForAction(getKeyPrompt(i, files))
            if (action === 'quit') process.exit(0)
            if (action === 'clear') {
                console.clear()
                logStep(`[${i + 1}/${files.length}] ${files[i]}`)
                logStep(`命令模板: camel ${template}`)
                continue
            }
            if (action === 'next') {
                i = Math.min(files.length - 1, i + 1)
                continue
            }
            if (action === 'prev') {
                i = Math.max(0, i - 1)
                continue
            }
        } while (action !== 'retry')

        // Enter: 执行当前
        const file = files[i]
        const filePath = path.join(dir, file)
        const cmd = fullTemplate.replace(/\{file\}/g, `"${filePath}"`).replace(/\s+/g, ' ').trim()
        logStep(`[${i + 1}/${files.length}] 即将执行: ${cmd}`)
        runCommand(cmd)
    }

    logDone(`All ${files.length} tests passed.`)
}

main()
