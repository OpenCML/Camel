/**
 * Interactive CMake options config - Linux kernel menuconfig style
 * Run: npm run config
 * - Up/Down: navigate | Left/Right/Enter: toggle | a: apply (save) | q/Esc: quit
 */
import fs from 'fs'
import path from 'path'
import { BASEDIR } from './common.js'
import {
    loadOptions,
    loadUserOpts,
    ensureDefaultOpts,
    getBaselineValue
} from './cmake-opts.js'

const USER_OPTS_FILE = path.join(BASEDIR, '.camel-opts.json')

const colors = {
    reset: '\x1b[0m',
    cyan: '\x1b[36m',
    green: '\x1b[32m',
    yellow: '\x1b[33m',
    dim: '\x1b[2m',
    bold: '\x1b[1m',
    invert: '\x1b[7m'
}

function saveUserOpts(opts) {
    fs.writeFileSync(USER_OPTS_FILE, JSON.stringify(opts, null, 2) + '\n')
}

function clearScreen() {
    process.stdout.write('\x1b[2J\x1b[H')
}

function buildCmakeFlags(opts, sessionOpts) {
    return opts
        .map((opt) => {
            const val = sessionOpts[opt.name] ?? getBaselineValue(opt, loadUserOpts())
            return `-D${opt.name}=${val}`
        })
        .join(' ')
}

function getSessionValue(opt, sessionOpts, baselineOpts) {
    return sessionOpts[opt.name] ?? getBaselineValue(opt, baselineOpts)
}

function toggleOption(opt, sessionOpts, baselineOpts) {
    const curr = getSessionValue(opt, sessionOpts, baselineOpts)
    sessionOpts[opt.name] = curr === 'ON' ? 'OFF' : 'ON'
}

function renderMenu(opts, baselineOpts, sessionOpts, selected) {
    clearScreen()
    console.log(`${colors.cyan}${colors.bold}*** Camel Build Options ***${colors.reset}\n`)
    console.log(
        `${colors.dim}↑/↓: navigate | ←/→/Enter: toggle | a: apply (save) | q/Esc: quit${colors.reset}\n`
    )

    const changed = new Set()
    opts.forEach((opt) => {
        const base = getBaselineValue(opt, baselineOpts)
        const session = getSessionValue(opt, sessionOpts, baselineOpts)
        if (session !== base) changed.add(opt.name)
    })

    opts.forEach((opt, i) => {
        const session = getSessionValue(opt, sessionOpts, baselineOpts)
        const marker = session === 'ON' ? '[*]' : '[ ]'
        const modified = changed.has(opt.name)
        const namePart = modified ? `${colors.yellow}${opt.name}${colors.reset}` : opt.name
        const line = `  ${marker} ${namePart}${colors.dim} - ${opt.description}${colors.reset}`
        const isSelected = i === selected
        console.log(isSelected ? `${colors.invert}${line}${colors.reset}` : line)
    })

    const n = changed.size
    console.log(
        `\n${colors.dim}Changed vs .camel-opts: ${n || 'none'}${colors.reset}`
    )
    process.stdout.write('\n> ')
}

function main() {
    ensureDefaultOpts()
    const opts = loadOptions()
    const baselineOpts = loadUserOpts()
    const sessionOpts = { ...baselineOpts }
    let selected = 0

    process.stdin.setRawMode?.(true)
    process.stdin.resume()
    process.stdin.setEncoding('utf8')

    let buf = ''
    const onData = (chunk) => {
        buf += chunk.toString()
        while (buf.length > 0) {
            const c = buf[0]
            buf = buf.slice(1)

            if (c === 'q' || c === 'Q' || c === '\u0003') {
                process.stdin.setRawMode?.(false)
                process.stdin.pause()
                process.exit(0)
            }

            if (c === '\u001b') {
                if (buf.length >= 2 && buf[0] === '[') {
                    const code = buf[1]
                    buf = buf.slice(2)
                    if (code === 'A') selected = Math.max(0, selected - 1)
                    else if (code === 'B') selected = Math.min(opts.length - 1, selected + 1)
                    else if (code === 'C' || code === 'D') {
                        const opt = opts[selected]
                        toggleOption(opt, sessionOpts, baselineOpts)
                    }
                } else if (buf.length >= 2 && buf[0] === 'O') {
                    const code = buf[1]
                    buf = buf.slice(2)
                    if (code === 'A') selected = Math.max(0, selected - 1)
                    else if (code === 'B') selected = Math.min(opts.length - 1, selected + 1)
                    else if (code === 'C' || code === 'D') {
                        const opt = opts[selected]
                        toggleOption(opt, sessionOpts, baselineOpts)
                    }
                } else if (buf.length >= 1 && buf[0] !== '[' && buf[0] !== 'O') {
                    process.stdin.setRawMode?.(false)
                    process.stdin.pause()
                    process.exit(0)
                } else {
                    buf = c + buf
                    break
                }
                break
            }

            if (c === '\r' || c === '\n') {
                const opt = opts[selected]
                toggleOption(opt, sessionOpts, baselineOpts)
                break
            }

            if (c === 'a' || c === 'A') {
                saveUserOpts(sessionOpts)
                console.log(`\n${colors.green}Saved to .camel-opts.json${colors.reset}`)
                console.log(`${colors.dim}CMake flags: ${buildCmakeFlags(opts, sessionOpts)}${colors.reset}`)
                process.stdin.setRawMode?.(false)
                process.stdin.pause()
                process.exit(0)
                return
            }

            break
        }
        renderMenu(opts, baselineOpts, sessionOpts, selected)
    }

    process.stdin.on('data', onData)
    renderMenu(opts, baselineOpts, sessionOpts, selected)
}

main()
