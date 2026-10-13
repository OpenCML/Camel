// Faults over the Debug Adapter Protocol, on both debuggable back ends. A fault that will end
// the program stops it where it is raised, with the diagnostic's text and position, the stack
// and the variables of that moment; continuing lets the fault take its course. A fault inside
// an attempt is isolated: it stops only when the "isolated" filter is on, and becomes an error
// value at the boundary.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const FAULTS = fixture('faults.cml')
const UNHANDLED = fixture('unhandled.cml')
const L = (mark) => lineOf(FAULTS, `mark:${mark}`)

await runTest(async (t) => {
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`

        // ---- default filters: only the fault that ends the program ---------------------
        {
            const dap = await t.dap()
            await dap.launch(FAULTS, { passes: [backend] })
            const stop = await dap.waitStop()
            checkEq(stop?.reason, 'exception', on('the uncaught fault stops the program'))
            checkEq(stop?.body.text, 'Attempted division by zero.', on('the stop carries the text of the diagnostic'))
            checkEq(stop?.body.description, 'Fault', on('and calls it a fault'))
            checkEq(`${stop?.name}:${stop?.line}`, `risky:${L('div')}`, on('at the node that raised it'))
            checkEq(stop?.frames[1]?.line, L('uncaught'), on('called from the line outside any attempt'))
            check(
                await dap.waitOutput('stdout', '-1\n'),
                on('the isolated fault before it did not stop: its error value was handled and printed'),
                dap.outputOf('stdout'),
            )
            const locals = await dap.variables(stop.top.id)
            checkEq([locals.d?.value, locals.q?.value], ['0', '<not evaluated>'], on('the variables are those of the fault'))

            const info = await dap.request('exceptionInfo', { threadId: 1 })
            check(info.success, on('exceptionInfo answers'), info)
            checkEq(info.body?.exceptionId, 'RuntimeDiag::DivisionByZero', on('with the name of the diagnostic'))
            checkEq(info.body?.breakMode, 'unhandled', on('as a fault nothing handles'))
            check(
                (info.body?.details?.stackTrace || '').includes(`faults.cml:${L('div')}:`),
                on('and its located text'),
                info.body,
            )

            await dap.request('continue', { threadId: 1 })
            const exited = await dap.waitEvent('exited')
            checkEq(exited?.body?.exitCode, 1, on('resumed, the fault ends the program as without a debugger: exit code 1'))
            check(
                /error\[RuntimeDiag::DivisionByZero\]: Attempted division by zero\.\n\s+--> .*faults\.cml:\d+:\d+/.test(dap.outputOf('stdout')),
                on('and its diagnostic is printed with its position'),
                dap.outputOf('stdout'),
            )
            check(!dap.outputOf('stdout').includes('\u001b['), on('without terminal colour codes'))
            check(await dap.waitEvent('terminated'), on('terminated'))
            const late = await dap.request('exceptionInfo', { threadId: 1 })
            check(!late.success, on('exceptionInfo after the end is an error'), late)
            await dap.stop()
        }

        // ---- with the "isolated" filter: the fault inside attempt stops too ---------------
        {
            const dap = await t.dap()
            await dap.request('initialize', { adapterID: 'camel' })
            await dap.request('launch', { program: FAULTS, passes: [backend] })
            await dap.waitEvent('initialized')
            const set = await dap.request('setExceptionBreakpoints', { filters: ['uncaught', 'isolated'] })
            check(set.success, on('both filters are accepted'), set)
            await dap.request('configurationDone')

            let stop = await dap.waitStop()
            checkEq(stop?.reason, 'exception', on('the fault inside attempt stops'))
            checkEq(stop?.body.description, 'Fault (isolated by attempt)', on('and is described as isolated'))
            checkEq(stop?.frames[1]?.line, L('isolated'), on('under the line of the attempt'))
            const info = await dap.request('exceptionInfo', { threadId: 1 })
            checkEq(info.body?.breakMode, 'always', on('exceptionInfo: a fault that will be handled'))
            checkEq(dap.outputOf('stdout'), '', on('nothing was printed yet'))

            await dap.request('continue', { threadId: 1 })
            stop = await dap.waitStop()
            checkEq(stop?.frames[1]?.line, L('uncaught'), on('resumed, it became an error value; the next stop is the uncaught fault'))
            check(await dap.waitOutput('stdout', '-1\n'), on('after the fallback value was printed'), dap.outputOf('stdout'))
            await dap.stop()
        }

        // ---- an error value is not a fault; unhandled at the exit of main it is reported ----
        {
            const dap = await t.dap()
            const throwLine = lineOf(UNHANDLED, 'mark:throw')
            await dap.launch(UNHANDLED, { passes: [backend] }, { [UNHANDLED]: [throwLine] })
            const stop = await dap.waitStop()
            checkEq(`${stop?.reason}:${stop?.line}`, `breakpoint:${throwLine}`, on('a breakpoint where the error value is made'))
            await dap.request('continue', { threadId: 1 })
            checkEq(await dap.waitStopOrEnd(), 'end', on('the error value does not stop the program: it is a value, not a fault'))
            const exited = await dap.waitEvent('exited')
            checkEq(exited?.body?.exitCode, 1, on('unhandled at the exit of main, it ends the program with exit code 1'))
            check(
                /error\[RuntimeDiag::UnhandledError\][\s\S]*unhandled\.cml:\d+:\d+/.test(dap.outputOf('stderr')),
                on('and is reported on standard error with the place it was made'),
                dap.outputOf('stderr'),
            )
            checkEq(dap.outputOf('stdout'), 'start\n', on("standard output holds only the program's output"))
            await dap.stop()
        }

        // ---- with no filter: no stop ---------------------------------------------------
        {
            const dap = await t.dap()
            await dap.request('initialize', { adapterID: 'camel' })
            await dap.request('launch', { program: FAULTS, passes: [backend] })
            await dap.waitEvent('initialized')
            await dap.request('setExceptionBreakpoints', { filters: [] })
            await dap.request('configurationDone')
            checkEq(await dap.waitStopOrEnd(), 'end', on('without filters a fault does not stop the program'))
            const exited = await dap.waitEvent('exited')
            checkEq(exited?.body?.exitCode, 1, on('it ends with exit code 1'))
            await dap.stop()
        }
    }
})
