// Stepping over the Debug Adapter Protocol, on both debuggable back ends. A step runs the
// program to the next graph node that starts another source line (next, stepIn, stepOut) or to
// the next node (granularity "instruction"); the line reached is the next one in execution
// order. Also: recursion as a stack of frames, a tail recursion as the one frame it is, and the
// arm of a branch that is not taken.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const ORDER = fixture('order.cml')
const RECURSION = fixture('recursion.cml')
const BRANCH = fixture('branch.cml')

await runTest(async (t) => {
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`
        const at = (stop) => (stop ? `${stop.name}:${stop.line}` : 'no stop')
        const step = async (dap, command, args = {}) => {
            const r = await dap.request(command, { threadId: 1, ...args })
            check(r.success, on(`${command} is accepted`), r)
            return dap.waitStop()
        }

        // ---- line steps in a function that is not a sync block ----------------------------
        {
            const dap = await t.dap()
            const L = (mark) => lineOf(ORDER, `mark:${mark}`)
            const s = await dap.launch(ORDER, { passes: [backend] }, { [ORDER]: [L('call'), L('unused')] })
            checkEq(
                s.breakpoints[ORDER].map((b) => b.verified),
                [true, false],
                on('a println nothing depends on, outside a sync block, has no node to stop at'),
            )
            let stop = await dap.waitStop()
            checkEq(at(stop), `main:${L('call')}`, on('stopped at the call of work'))

            stop = await step(dap, 'stepIn')
            checkEq(at(stop), `work:${L('a')}`, on('stepIn enters the callee at the first line it executes'))
            checkEq(stop?.reason, 'step', on('the reason is the step'))
            checkEq(stop?.names, ['work', 'main', '__root__'], on('the callee is a frame above its caller'))

            stop = await step(dap, 'next')
            checkEq(at(stop), `work:${L('b')}`, on('next steps over the call on the line, to the next line executed'))
            let locals = await dap.variables(stop.top.id)
            checkEq(
                [locals.a?.value, locals.b?.value, locals.c?.value],
                ['6', '<not evaluated>', '<not evaluated>'],
                on('the line stepped over has been evaluated, the ones ahead have not'),
            )

            stop = await step(dap, 'next')
            checkEq(at(stop), `work:${L('c')}`, on('the line that is never executed is not a stop: execution order, not text order'))

            stop = await step(dap, 'next')
            checkEq(at(stop), `main:${L('print')}`, on('next at the last line of a function returns to the caller'))
            locals = await dap.variables(stop.top.id)
            checkEq(locals.r?.value, '10', on('with the result of the call'))

            await dap.request('next', { threadId: 1 })
            const exited = await dap.waitEvent('exited')
            checkEq(exited?.body?.exitCode, 0, on('a step at the last line runs the program to its end'))
            checkEq(dap.outputOf('stdout'), '10\n', on('whose output is forwarded'))
            await dap.stop()
        }

        // ---- stepIn into a call on the line, stepOut, and steps by node --------------------
        {
            const dap = await t.dap()
            const L = (mark) => lineOf(ORDER, `mark:${mark}`)
            await dap.launch(ORDER, { passes: [backend] }, { [ORDER]: [L('a')] })
            let stop = await dap.waitStop()
            checkEq(at(stop), `work:${L('a')}`, on('stopped in work'))

            stop = await step(dap, 'stepIn')
            checkEq(at(stop), `double:${lineOf(ORDER, 'return x * 2')}`, on('stepIn enters the function called on the line'))
            checkEq(stop?.names, ['double', 'work', 'main', '__root__'], on('four frames deep'))
            checkEq(stop?.frames[1]?.line, L('a'), on('the caller waits at the line of the call'))

            stop = await step(dap, 'stepOut')
            checkEq(at(stop), `work:${L('b')}`, on('stepOut runs to the next line of the caller'))

            // By node: the call of next, then the node inside it.
            stop = await step(dap, 'stepIn', { granularity: 'instruction' })
            checkEq(at(stop), `next:${lineOf(ORDER, 'return x + 1')}`, on('a node step into a call stops at the first node of the callee'))
            stop = await step(dap, 'next', { granularity: 'instruction' })
            checkEq(at(stop), `work:${L('c')}`, on('a node step at the last node of a function returns to the caller'))
            stop = await step(dap, 'next', { granularity: 'instruction' })
            checkEq(at(stop), `main:${L('print')}`, on('and so on, node by node'))

            await dap.request('stepOut', { threadId: 1 })
            checkEq(await dap.waitStopOrEnd(), 'end', on('stepOut of main runs the program to its end'))
            await dap.stop()
        }

        // ---- "stepBy": "node": the ordinary step requests walk the nodes of a line -----------
        {
            const dap = await t.dap()
            const L = (mark) => lineOf(ORDER, `mark:${mark}`)
            await dap.launch(ORDER, { passes: [backend], stepBy: 'node' }, { [ORDER]: [L('a')] })
            let stop = await dap.waitStop()
            checkEq(at(stop), `work:${L('a')}`, on('stepBy node: stopped in work'))
            stop = await step(dap, 'stepIn')
            checkEq(at(stop), `double:${lineOf(ORDER, 'return x * 2')}`, on('stepBy node: stepIn stops at the next node, inside the callee'))
            stop = await step(dap, 'next')
            checkEq(at(stop), `work:${L('b')}`, on('stepBy node: next stops at the next node of the caller'))
            await dap.stop()
        }

        // ---- recursion: a frame per call; a tail recursion: one frame ----------------------
        {
            const dap = await t.dap()
            const factLine = lineOf(RECURSION, 'mark:fact')
            const loopLine = lineOf(RECURSION, 'mark:loop')
            await dap.launch(RECURSION, { passes: [backend] }, { [RECURSION]: [factLine, loopLine] })

            // fact(4): one stop per call, each a level deeper.
            const depths = []
            const ns = []
            let stop
            for (let i = 0; i < 4; i++) {
                stop = await dap.waitStop()
                checkEq(at(stop), `fact:${factLine}`, on(`call ${i + 1} of fact stops at its line, once`))
                depths.push(stop?.frames.length)
                ns.push((await dap.variables(stop.top.id)).n?.value)
                if (i < 3) {
                    await dap.request('continue', { threadId: 1 })
                }
            }
            checkEq(ns, ['4', '3', '2', '1'], on('each frame has its own n'))
            checkEq(depths.map((d) => d - depths[0]), [0, 1, 2, 3], on('each recursive call is one frame deeper'))
            // The callers are the else arms: an arm is a subgraph of its own, entered by a tail
            // call that replaced the frame of fact.
            const callers = stop.names.slice(1, 4)
            check(
                callers.every((n) => /^fact \[arm at line \d+\] \(tail call, 1 frame replaced\)$/.test(n)),
                on('the callers are shown as what they are: arms of fact that replaced its frame'),
                stop.names,
            )
            const armLocals = await dap.variables(stop.frames[1].id)
            checkEq(armLocals?.n?.value, '2', on('an arm sees the n of its function'))

            // loop(5, 0): the recursion is a tail call; the frame is replaced, the stack stays flat.
            await dap.request('continue', { threadId: 1 })
            const loopDepths = []
            const accs = []
            for (let i = 0; i < 3; i++) {
                stop = await dap.waitStop()
                check(/^loop( \(tail call, \d+ frames? replaced\))?$/.test(stop?.name || ''), on(`iteration ${i + 1} of loop stops in loop`), at(stop))
                loopDepths.push(stop?.frames.length)
                accs.push((await dap.variables(stop.top.id)).acc?.value)
                await dap.request('continue', { threadId: 1 })
            }
            checkEq(accs, ['0', '5', '9'], on('the accumulator advances'))
            checkEq(loopDepths, [3, 3, 3], on('a tail recursion keeps one frame: the stack does not grow'))
            check(/tail call, 4 frames replaced/.test(stop.name), on('and the frame says how many it replaced'), stop.name)
            await dap.stop()
        }

        // ---- a branch: the arm that is not taken never stops -------------------------------
        {
            const dap = await t.dap()
            const L = (mark) => lineOf(BRANCH, `mark:${mark}`)
            const s = await dap.launch(BRANCH, { passes: [backend] }, { [BRANCH]: [L('then'), L('else'), L('shout')] })
            checkEq(s.breakpoints[BRANCH].map((b) => b.verified), [true, true, true], on('both arms hold a breakpoint'))
            const stops = []
            for (;;) {
                const stop = await dap.waitStopOrEnd()
                if (stop === 'end' || !stop) {
                    break
                }
                stops.push(`${stop.line}${stop.top.name.includes('[arm') ? ' arm' : ''}`)
                await dap.request('continue', { threadId: 1 })
                if (stops.length > 6) {
                    break
                }
            }
            checkEq(
                stops,
                [`${L('then')}`, `${L('shout')}`],
                on('the taken arm stops once, then the function it calls; the other arm never does'),
            )
            checkEq(dap.outputOf('stdout'), 'big!\n', on('the program printed the taken arm'))
            await dap.stop()
        }
    }
})
