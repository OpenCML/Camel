// A scripted session with a camel-db worker over its debug API, for looking at behaviour by
// hand while developing tests:
//
//     node test/tools/debugger/session.mjs <program.cml> [--passes a,b] [--args n=1,m=2] <step>...
//
// Steps: `b:<line>[,<line>...]` set line breakpoints, `go` start, `c` continue, `n` line step,
// `s` step in, `o` step out, `ni` / `si` node steps, `stack`, `vars`, `graph`, `e:<expr>`,
// `filters:<a,b>`, `pause`, `wait` (for the next stop or the end).
// Every answer is printed. The worker is killed at the end.

import { spawn } from 'node:child_process'
import path from 'node:path'
import { debuggerPath, http, until } from './client.mjs'

const argv = process.argv.slice(2)
const program = path.resolve(argv.shift())
let passes = []
let args = []
const steps = []
while (argv.length) {
    const a = argv.shift()
    if (a === '--passes') passes = argv.shift().split(',')
    else if (a === '--args') args = argv.shift().split(',')
    else steps.push(a)
}

const port = 21000 + Math.floor(Math.random() * 20000)
const proc = spawn(debuggerPath(), ['--run-worker', program], {
    stdio: ['ignore', 'pipe', 'pipe'],
    env: { ...process.env, CAMEL_DB_WORKER_PORT: String(port) },
})
proc.stdout.on('data', (d) => process.stdout.write(`[out] ${d}`))
proc.stderr.on('data', (d) => process.stdout.write(`[err] ${d}`))
const cmd = async (name, body = {}) => (await http(port, 'POST', `/api/debug/${name}`, body, 60000)).json
let seen = 0
async function wait() {
    for (;;) {
        const r = await http(port, 'GET', `/api/debug/events?after=${seen}&timeout=5000`, undefined, 10000)
        const events = r.json?.events || []
        for (const e of events) {
            seen = e.seq
            if (e.type === 'stopped') {
                const f = e.frame || {}
                console.log(`STOPPED ${e.reason} ${f.name} line ${f.line ?? '-'} node ${f.node ? `%${f.node.ref} ${f.node.kind} ${f.node.label}` : '-'}${e.exception ? ` :: ${e.exception.message}` : ''}`)
                return e
            }
            console.log(`event ${JSON.stringify(e)}`)
            if (e.type === 'finished') return e
        }
        if (!events.length && proc.exitCode !== null) return null
    }
}
const show = (x) => console.log(JSON.stringify(x, null, 1).split('\n').slice(0, 80).join('\n'))

await until(async () => (await http(port, 'GET', '/api/state')).status === 200)
show(await cmd('load', { passes, args, quiet: true }))
for (const step of steps) {
    console.log(`--- ${step}`)
    if (step.startsWith('b:')) show(await cmd('setBreakpoints', { path: program, lines: step.slice(2).split(',').map(Number) }))
    else if (step.startsWith('filters:')) show(await cmd('setExceptionFilters', { filters: step.slice(8).split(',').filter(Boolean) }))
    else if (step === 'go') { show(await cmd('start')); await wait() }
    else if (step === 'c') { await cmd('continue'); await wait() }
    else if (step === 'n') { await cmd('step', { unit: 'line', mode: 'over' }); await wait() }
    else if (step === 's') { await cmd('step', { unit: 'line', mode: 'in' }); await wait() }
    else if (step === 'o') { await cmd('step', { mode: 'out' }); await wait() }
    else if (step === 'ni') { await cmd('step', { unit: 'node', mode: 'over' }); await wait() }
    else if (step === 'si') { await cmd('step', { unit: 'node', mode: 'in' }); await wait() }
    else if (step === 'wait') await wait()
    else if (step === 'pause') show(await cmd('pause'))
    else if (step === 'stack') console.log(((await cmd('stackTrace')).frames || []).map((f) => `  ${f.name} @${f.line ?? '-'} ${f.node ? `%${f.node.ref} ${f.node.kind} ${f.node.label}` : ''}`).join('\n'))
    else if (step === 'vars') {
        const scopes = (await cmd('scopes')).scopes || []
        for (const s of scopes) {
            console.log(` [${s.name}]`)
            for (const v of (await cmd('variables', { reference: s.reference })).variables || []) {
                console.log(`   ${v.name}: ${v.type} = ${v.value}${v.reference ? ` (+)` : ''}`)
                if (v.reference) {
                    for (const c of (await cmd('variables', { reference: v.reference })).variables || []) {
                        console.log(`      ${c.name}: ${c.type} = ${c.value}`)
                    }
                }
            }
        }
    } else if (step === 'graph') {
        const g = await cmd('graph')
        for (const n of g.nodes || []) console.log(`   %${n.ref} ${n.kind} ${n.label} line=${n.line ?? '-'} ${n.state}${n.value !== undefined ? ` = ${n.value}` : ''} ${n.names.join(',')}`)
    } else if (step.startsWith('e:')) show(await cmd('evaluate', { expression: step.slice(2) }))
    else console.log('unknown step')
}
proc.kill('SIGKILL')
process.exit(0)
