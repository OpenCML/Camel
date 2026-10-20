// How the debugger presents a program over the Debug Adapter Protocol (design principles 12.1,
// 10.2; docs/technical/35_debugger.md 4.2, 5.4, 3.3), on both debuggable back ends:
//  - the arms of a branch are part of the frame of their function: a line step goes through
//    an arm as through the rest of the function, the arm's names are the frame's while it
//    runs; with frames "subgraphs" the arm is the frame it is;
//  - a line step that goes past a line nothing uses the result of says so, and so does a
//    breakpoint on such a line when the program ends without hitting it;
//  - names are full names: `module::path::name`, without the module for the entry module's
//    own; function breakpoints take them, or the name without the module when only one
//    module has it; a captured value of a module is shown and found by its full name.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const FOLDED = fixture('folded.cml')
const NAMES = fixture('names.cml')
const NAMES_LIB = fixture('names_lib.cml')

await runTest(async (t) => {
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`
        const at = (stop) => (stop ? `${stop.name}:${stop.line}` : 'no stop')
        const L = (mark) => lineOf(FOLDED, `mark:${mark}`)
        const step = async (dap, command, args = {}) => {
            const r = await dap.request(command, { threadId: 1, ...args })
            check(r.success, on(`${command} is accepted`), r)
            return dap.waitStop()
        }

        // ---- frames of functions: line steps through the arms, skipped lines explained ----------
        {
            const dap = await t.dap()
            const s = await dap.launch(FOLDED, { passes: [backend] }, { [FOLDED]: [L('call'), L('unused')] })
            const [, unused] = s.breakpoints[FOLDED]
            checkEq(unused?.verified, false, on('a line nothing uses the value of holds no breakpoint'))
            checkEq(
                unused?.message,
                `Line ${L('unused')} is not executed: nothing uses the value of \`unused\`. Evaluation is driven by dependencies; to run it, use its result or put it in a sync block.`,
                on('and the breakpoint says why, by the name'),
            )

            let stop = await dap.waitStop()
            checkEq(at(stop), `main:${L('call')}`, on('stopped at the call of pick'))
            stop = await step(dap, 'stepIn')
            checkEq(at(stop), `pick:${L('base')}`, on('stepIn: the first line pick executes'))

            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${lineOf(FOLDED, 'let r = if')}`, on('next: the line nothing uses is not a stop'))
            const skipped = await dap.waitEvent('camel/stopped', (b) => b.skipped)
            checkEq(
                skipped?.body?.skipped?.map((l) => [l.line, l.names, l.text]),
                [[L('unused'), ['unused'], `line ${L('unused')} was not executed: nothing uses the value of \`unused\``]],
                on('the stop says which line the step went past, and why'),
            )
            check(
                dap.outputOf('console').includes(`line ${L('unused')} was not executed: nothing uses the value of \`unused\`\n`),
                on('so does the debug console'),
                dap.outputOf('console'),
            )
            let locals = await dap.variables(stop.top.id)
            checkEq(locals.unused?.value, '<not in the graph: nothing uses its value>', on('the variable says the same'))

            // Into the arm that is taken: the same frame, named after the function.
            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${L('big')}`, on('next: the first line of the arm that is taken'))
            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${L('bigret')}`, on('next goes on in the arm, line by line'))
            checkEq(stop?.names, ['pick', 'main', '__root__'], on('the arm is part of the frame of pick'))
            locals = await dap.variables(stop.top.id)
            checkEq(
                [locals.n?.value, locals.base?.value, locals.big?.value, locals.r?.value],
                ['3', '4', '12', '<not evaluated>'],
                on('the frame has the names of pick and of the arm that runs'),
            )
            check(!('small' in locals), on('not those of the arm that does not run'), locals)
            const hover = await dap.request('evaluate', { expression: 'big', frameId: stop.top.id, context: 'hover' })
            checkEq(hover.body?.result, '12', on('a name of the arm can be evaluated in the frame'))

            // The switch: the arm as the subgraph frame it is, then folded again.
            check((await dap.request('camel/setFrames', { frames: 'subgraphs' })).success, on('switched to subgraphs'))
            const st = (await dap.request('stackTrace', { threadId: 1 })).body?.stackFrames || []
            checkEq(
                st.map((f) => f.name),
                [`pick [arm at line ${L('big')}]`, 'pick', 'main', '__root__'],
                on('subgraphs: the arm is a frame above the frame of pick'),
            )
            const armLocals = await dap.variables(st[0].id)
            checkEq([armLocals.base?.value, armLocals.big?.value], ['4', '12'], on('subgraphs: the arm has what it was passed and its own names'))
            const pickLocals = await dap.variables(st[1].id)
            check(!('big' in pickLocals) && pickLocals.base?.value === '4', on('subgraphs: the frame of pick has its own names only'), pickLocals)
            check((await dap.request('camel/setFrames', { frames: 'functions' })).success, on('switched back to functions'))

            // Out of the arm, back to the function: the join, then the next lines.
            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${lineOf(FOLDED, 'let r = if')}`, on('next: back in the function where the branch joins'))
            locals = await dap.variables(stop.top.id)
            check(!('big' in locals), on('the arm is over: its names are gone'), locals)
            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${L('and')}`, on('next: the line of &&'))
            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${L('ret')}`, on('next: past the arm of && on the same line'))
            stop = await step(dap, 'next')
            checkEq(at(stop), `main:${L('print')}`, on('next at the end of pick returns to main'))

            await dap.request('continue', { threadId: 1 })
            checkEq((await dap.waitEvent('exited'))?.body?.exitCode, 0, on('the program ends'))
            check(
                dap.outputOf('console').includes(
                    `Breakpoint at folded.cml:${L('unused')} was never hit: line ${L('unused')} was not executed: nothing uses the value of \`unused\`.`,
                ),
                on('at the end, the breakpoint that was never hit is explained'),
                dap.outputOf('console'),
            )
            checkEq(dap.outputOf('stdout'), '14\n', on('with the output of the program'))
            await dap.stop()
        }

        // ---- frames of subgraphs from the launch: a line step goes over the arm ------------------
        {
            const dap = await t.dap()
            await dap.launch(FOLDED, { passes: [backend], frames: 'subgraphs' }, { [FOLDED]: [L('big')] })
            let stop = await dap.waitStop()
            checkEq(at(stop), `pick:${L('big')}`, on('subgraphs: stopped at the call of the arm, in pick'))
            stop = await step(dap, 'next')
            checkEq(at(stop), `pick:${lineOf(FOLDED, 'let r = if')}`, on('subgraphs: next steps over the arm, a call of a subgraph'))
            await dap.stop()
        }

        // ---- full names: stack, captured values, evaluate, function breakpoints ------------------
        {
            const dap = await t.dap()
            const N = (mark) => lineOf(NAMES, `mark:${mark}`)
            const out = {}
            out.initialize = await dap.request('initialize', { clientID: 'test', adapterID: 'camel' })
            check(out.initialize.body?.supportsFunctionBreakpoints === true, on('function breakpoints are announced'))
            out.launch = await dap.request('launch', { program: NAMES, passes: [backend] })
            await dap.waitEvent('initialized')
            const fb = await dap.request('setFunctionBreakpoints', {
                breakpoints: ['twice', 'Config::clamp', 'util::half', 'names_other::twice', 'neverCalled', 'nothere'].map((name) => ({ name })),
            })
            const answers = fb.body?.breakpoints || []
            checkEq(answers.map((b) => b.verified), [false, true, true, true, true, false], on('function breakpoints are answered'))
            checkEq(
                answers[0]?.message,
                '`twice` is ambiguous: names_lib::twice, names_other::twice. Write the full name.',
                on('a name two modules have is ambiguous without the module'),
            )
            checkEq(answers[4]?.message, 'Nothing in the program calls `neverCalled`.', on('a function nothing calls is found, and the answer says so'))
            checkEq(answers[5]?.message, 'No function named `nothere` in the program.', on('a function that does not exist'))
            const never = await dap.request('setBreakpoints', { source: { path: NAMES }, breakpoints: [{ line: N('never') }] })
            checkEq(
                [never.body?.breakpoints?.[0]?.verified, never.body?.breakpoints?.[0]?.message],
                [true, 'Nothing in the program calls `neverCalled`.'],
                on('a line of a function nothing calls holds a breakpoint, which says so'),
            )
            await dap.request('configurationDone')

            let stop = await dap.waitStop()
            checkEq(stop?.reason, 'function breakpoint', on('a function breakpoint stops, with the reason DAP has for it'))
            checkEq(stop?.body?.hitBreakpointIds, [answers[1].id], on('with its id'))
            checkEq(
                [stop?.name, stop?.line, stop?.top?.source?.name],
                ['names_lib::Config::clamp', lineOf(NAMES_LIB, 'mark:clamp'), 'names_lib.cml'],
                on('at the first line of the function, named with its module and namespace'),
            )
            checkEq(stop?.names, ['names_lib::Config::clamp', 'main', '__root__'], on('the stack shows full names'))
            const locals = await dap.variables(stop.top.id)
            checkEq(Object.keys(locals), ['x'], on('the parameter, by its name'))
            checkEq(locals.x?.value, '42', on('with its value'))
            const captured = await dap.variables(stop.top.id, 'Captured')
            checkEq(
                Object.entries(captured || {}).map(([k, v]) => [k, v.value]),
                [['names_lib::Config::limit', '10']],
                on('the value of the module it uses, by its full name'),
            )
            for (const expression of ['limit', 'Config::limit', 'names_lib::Config::limit']) {
                const r = await dap.request('evaluate', { expression, frameId: stop.top.id, context: 'hover' })
                checkEq(r.body?.result, '10', on(`hover over ${expression}`))
            }

            await dap.request('continue', { threadId: 1 })
            stop = await dap.waitStop()
            checkEq([stop?.name, stop?.line], ['util::half', N('half')], on('a function of a namespace of the entry module: without the module'))
            await dap.request('continue', { threadId: 1 })
            stop = await dap.waitStop()
            checkEq(stop?.name, 'names_other::twice', on('the full name picks one of two functions of the same name'))
            await dap.request('continue', { threadId: 1 })
            checkEq((await dap.waitEvent('exited'))?.body?.exitCode, 0, on('the program ends'))
            checkEq(dap.outputOf('stdout'), '10\n', on('with its output'))
            check(
                dap.outputOf('console').includes(`Breakpoint at names.cml:${N('never')} was never hit: line ${N('never')} was not executed: nothing calls \`neverCalled\`.`),
                on('and the line breakpoint that was never hit is explained'),
                dap.outputOf('console'),
            )
            await dap.stop()
        }
    }
})
