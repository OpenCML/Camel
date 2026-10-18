// Tail-call source stepping: F10 returns to the real caller, F11 enters the next
// activation with immutable parameter values. Breakpoints still interrupt F10.
import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'
const program = fixture('tail_steps.cml')
const recur = lineOf(program, 'mark:recur')
const returned = lineOf(program, 'mark:returned')
await runTest(async t => {
    for (const backend of ['std::nvm', 'std::fvm']) {
        const label = s => `${backend}: ${s}`
        const native = dap => [...dap.events].reverse().find(e => e.event === 'camel/stopped')?.body?.frame
        const args = frame => frame?.arguments?.map(a => [a.name, a.value, a.evaluated])
        for (const granularity of ['line', 'node']) {
            const dap = await t.dap()
            await dap.launch(program, { passes: [backend] }, { [program]: [recur] })
            const before = await dap.waitStop()
            checkEq(args(native(dap)), [['remaining', '3', true], ['ticks', '0', true]], label('original arguments'))
            await dap.request('setBreakpoints', { source: { path: program }, breakpoints: [] })
            await dap.request('next', { threadId: 1, granularity: granularity === 'node' ? 'instruction' : 'line' })
            let stop = await dap.waitStop()
            // A node step may finish argument calculations on the same source line.
            for (let i = 0; granularity === 'node' && stop?.line === recur && i < 12; ++i) {
                check(!/tail call/.test(stop.name), label('F10 does not enter a replacement activation'))
                await dap.request('next', { threadId: 1, granularity: 'instruction' })
                stop = await dap.waitStop()
            }
            checkEq(stop?.line, returned, label(`${granularity} F10 returns to caller`))
            checkEq((await dap.variables(stop.top.id)).result?.value, '3', label('recursive call completed'))
            check(before.top.id !== stop.top.id, label('real caller is a different frame'))
            await dap.stop()
        }
        {
            const dap = await t.dap()
            await dap.launch(program, { passes: [backend] }, { [program]: [recur] })
            let stop = await dap.waitStop()
            const old = native(dap)
            await dap.request('setBreakpoints', { source: { path: program }, breakpoints: [] })
            await dap.request('stepIn', { threadId: 1 })
            stop = await dap.waitStop()
            const entered = native(dap)
            check(entered?.entry === true && entered.activation !== old.activation, label('F11 marks entry into a new activation'))
            checkEq(entered?.functionId, old.functionId, label('recursive function identity remains stable'))
            checkEq(entered?.tailReplaced, 1, label('tail replacement is explicit'))
            checkEq(args(entered), [['remaining', '2', true], ['ticks', '1', true]], label('new arguments are ready at entry'))
            checkEq(stop?.frames.length, 3, label('no fictitious recursive stack growth'))
            await dap.request('next', { threadId: 1 })
            stop = await dap.waitStop()
            checkEq(stop?.line, recur, label('F10 within the activation reaches its recursive call line'))
            checkEq(native(dap)?.entry, false, label('ordinary source stops are not labelled entry'))
            // A real breakpoint takes precedence while F10 skips the tail-call chain.
            await dap.request('setBreakpoints', { source: { path: program }, breakpoints: [{ line: recur }] })
            await dap.request('next', { threadId: 1 })
            stop = await dap.waitStop()
            checkEq(stop?.reason, 'breakpoint', label('breakpoints interrupt F10'))
            checkEq(args(native(dap)), [['remaining', '1', true], ['ticks', '2', true]], label('breakpoint has the next round values'))
            checkEq(args(old), [['remaining', '3', true], ['ticks', '0', true]], label('earlier snapshots remain immutable'))
            await dap.stop()
        }
    }
})
