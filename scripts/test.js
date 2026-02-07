import fs from 'fs'
import path from 'path'
import { runCommand, BASEDIR, logStep, logDone, logFail } from './common.js'

function getKeyPrompt(i, files) {
    const curr = files[i] ?? '(无)'
    const next = i + 1 < files.length ? files[i + 1] : '(无)'
    const prev = i > 0 ? files[i - 1] : '(无)'
    return [
        `Enter 执行: ${curr}`,
        `n 下一个: ${next}`,
        `b 上一个: ${prev}`,
        'q 退出 | c 清屏'
    ].join('\n')
}

function waitForAction(keyPrompt) {
    process.stdout.write(keyPrompt + '\n按键: ')

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
            else if (k === 'b') resolve('prev')
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
