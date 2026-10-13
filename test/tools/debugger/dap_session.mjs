// A scripted debug session over the Debug Adapter Protocol, for looking at behaviour by hand
// and for trying the adapter without an editor:
//
//     node test/tools/debugger/dap_session.mjs <program.cml> [--passes a,b] [--args n=1,m=2]
//          [--entry] <step>...
//
// Steps: `b:<line>[,<line>...]` line breakpoints (before `go`, or while stopped), `go`
// (configurationDone and wait for the first stop or the end), `c` continue, `n` next, `s` step
// in, `o` step out, `ni` / `si` the same by graph node, `stack`, `vars`, `nodes` (the Nodes
// scope), `e:<expr>` evaluate, `x` exception info, `filters:<a,b>`, `pause`, `wait`,
// `raw:<command>` any request without arguments.
// Every request is shown as `-> command` and what came back in a short form; `--trace` prints
// the messages themselves.

import path from 'node:path'
import { DapClient } from './client.mjs'

const argv = process.argv.slice(2)
const program = path.resolve(argv.shift())
const config = {}
let trace = false
const steps = []
while (argv.length) {
    const a = argv.shift()
    if (a === '--passes') config.passes = argv.shift().split(',').filter(Boolean)
    else if (a === '--args') config.args = argv.shift().split(',').filter(Boolean)
    else if (a === '--entry') config.stopOnEntry = true
    else if (a === '--trace') trace = true
    else steps.push(a)
}

const dap = await DapClient.start()
const shown = new Set()
function drain() {
    dap.events.forEach((e, i) => {
        if (shown.has(i)) return
        shown.add(i)
        if (e.event === 'output') process.stdout.write(`   [${e.body.category}] ${e.body.output.replace(/\n$/, '')}\n`)
        else if (e.event === 'camel/stopped') {
            const n = e.body.frame?.node
            if (n) console.log(`   (node %${n.ref} ${n.kind} ${n.label})`)
        } else if (e.event !== 'stopped') console.log(`<- event ${e.event} ${JSON.stringify(e.body)}`)
    })
}
async function req(command, args) {
    console.log(`-> ${command}${args ? ' ' + JSON.stringify(args) : ''}`)
    const r = await dap.request(command, args)
    if (trace) console.log(JSON.stringify(r, null, 1))
    if (!r.success) console.log(`<- error: ${r.message}`)
    return r
}
let frameId = 0
let ended = null // one wait for the end of the session, shared by every step
async function waitStop() {
    ended = ended || dap.waitEvent('terminated', 3600000).then(() => 'end')
    const done = ended
    const stop = dap.waitEvent('stopped').then((e) => e)
    const first = await Promise.race([done, stop])
    drain()
    if (first === 'end' || first === null) {
        await new Promise((r) => setTimeout(r, 100))
        drain()
        return
    }
    const st = await dap.request('stackTrace', { threadId: 1 })
    const frames = st.body?.stackFrames || []
    frameId = frames[0]?.id || 0
    const where = frames[0]?.source ? `${path.basename(frames[0].source.path)}:${frames[0].line}:${frames[0].column}` : '(no source)'
    console.log(`<- stopped (${first.body.reason}${first.body.text ? ': ' + first.body.text : ''}) in ${frames[0]?.name} at ${where}`)
}

await req('initialize', { clientID: 'dap_session', adapterID: 'camel' })
const launched = await req('launch', { program, ...config })
drain()
if (!launched.success) {
    await dap.stop()
    process.exit(1)
}
await dap.waitEvent('initialized')
for (const step of steps) {
    if (step.startsWith('b:')) {
        const r = await req('setBreakpoints', { source: { path: program }, breakpoints: step.slice(2).split(',').map((l) => ({ line: Number(l) })) })
        for (const b of r.body?.breakpoints || []) console.log(`   line ${b.line}: ${b.verified ? 'verified' : 'not verified: ' + b.message}`)
    } else if (step.startsWith('filters:')) await req('setExceptionBreakpoints', { filters: step.slice(8).split(',').filter(Boolean) })
    else if (step === 'go') { await req('configurationDone'); await waitStop() }
    else if (step === 'c') { await req('continue', { threadId: 1 }); await waitStop() }
    else if (step === 'n') { await req('next', { threadId: 1 }); await waitStop() }
    else if (step === 's') { await req('stepIn', { threadId: 1 }); await waitStop() }
    else if (step === 'o') { await req('stepOut', { threadId: 1 }); await waitStop() }
    else if (step === 'ni') { await req('next', { threadId: 1, granularity: 'instruction' }); await waitStop() }
    else if (step === 'si') { await req('stepIn', { threadId: 1, granularity: 'instruction' }); await waitStop() }
    else if (step === 'wait') await waitStop()
    else if (step === 'pause') await req('pause', { threadId: 1 })
    else if (step === 'stack') {
        const r = await req('stackTrace', { threadId: 1 })
        for (const f of r.body?.stackFrames || []) console.log(`   #${f.id} ${f.name}${f.source ? `  ${path.basename(f.source.path)}:${f.line}` : ''}`)
    } else if (step === 'vars' || step === 'nodes') {
        const scopes = (await req('scopes', { frameId })).body?.scopes || []
        for (const s of scopes) {
            if ((s.name === 'Nodes') !== (step === 'nodes')) continue
            console.log(`   [${s.name}]`)
            for (const v of (await dap.request('variables', { variablesReference: s.variablesReference })).body?.variables || []) {
                console.log(`     ${v.name}: ${v.type} = ${v.value}`)
                if (v.variablesReference) {
                    for (const c of (await dap.request('variables', { variablesReference: v.variablesReference })).body?.variables || []) {
                        console.log(`        ${c.name}: ${c.type} = ${c.value}`)
                    }
                }
            }
        }
    } else if (step.startsWith('e:')) {
        const r = await req('evaluate', { expression: step.slice(2), frameId, context: 'watch' })
        if (r.success) console.log(`   ${step.slice(2)}: ${r.body.type} = ${r.body.result}`)
    } else if (step === 'x') {
        const r = await req('exceptionInfo', { threadId: 1 })
        if (r.success) console.log(`   ${r.body.exceptionId}: ${r.body.description}\n${r.body.details?.stackTrace || ''}`)
    } else if (step.startsWith('raw:')) console.log(JSON.stringify((await req(step.slice(4))).body))
    else console.log(`unknown step ${step}`)
    drain()
}
await dap.stop()
drain()
if (dap.protocolErrors.length) console.log(`protocol errors: ${dap.protocolErrors.join('; ')}`)
process.exit(0)
