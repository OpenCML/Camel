// A complete debug session over the Debug Adapter Protocol, on both debuggable back ends:
// launch, line breakpoints (one of them on a line that is never executed), the stop, the call
// stack, the variables with those that are not evaluated yet, evaluate, continue, the program's
// output and its exit code.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const CALLS = fixture('calls.cml')
const VALUES = fixture('values.cml')
const EXIT = fixture('exit_code.cml')

await runTest(async (t) => {
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`
        const dap = await t.dap()
        const unusedLine = lineOf(CALLS, 'let unused')
        const sumLine = lineOf(CALLS, 'let c = a + b')
        const s = await dap.launch(CALLS, { passes: [backend] }, { [CALLS]: [unusedLine, sumLine] })

        check(s.initialize.success, on('initialize succeeds'), s.initialize)
        const caps = s.initialize.body || {}
        check(caps.supportsConfigurationDoneRequest === true, on('configurationDone is announced'), caps)
        checkEq(
            (caps.exceptionBreakpointFilters || []).map((f) => [f.filter, f.default]),
            [['uncaught', true], ['isolated', false]],
            on('the two fault filters are announced'),
        )
        check(s.launch.success, on('launch succeeds'), s.launch)
        check(s.initialized, on('initialized follows the launch'))

        // ---- breakpoints are answered against the compiled program, before it runs ----
        const [unused, sum] = s.breakpoints[CALLS]
        check(sum?.verified === true && sum.line === sumLine, on('a line with a node is verified'), sum)
        check(unused?.verified === false, on('a line whose code nothing depends on is not verified'), unused)
        check(
            /^Line \d+ is not executed: nothing uses the value of `unused`\. Evaluation is driven by dependencies/.test(unused?.message || ''),
            on('and the message says why: nothing uses the value the line computes'),
            unused,
        )
        check(s.configurationDone.success, on('configurationDone starts the program'), s.configurationDone)

        const snapshots = await dap.request('camel/passGraphs');
        check(snapshots.success && snapshots.body?.graphs?.[0]?.pass === null,
            on('native DAP exposes the initial GIR snapshot'), snapshots);
        const initialGraph = await dap.request('camel/passGraph', { index: 0 });
        check(initialGraph.success && initialGraph.body?.dot?.includes('origin='),
            on('snapshot preserves source origins'), initialGraph);
        const invalidSnapshot = await dap.request('camel/passGraph', { index: -1 });
        check(!invalidSnapshot.success, on('negative snapshot index is rejected'));
        check((await dap.request('camel/setNodeBreakpoints', { origins: [] })).success,
            on('node breakpoint configuration uses native DAP'));

        // ---- the stop ----------------------------------------------------------------
        const stop = await dap.waitStop()
        check(stop, on('the program stops'))
        if (!stop) {
            continue
        }
        checkEq(stop.reason, 'breakpoint', on('the reason is the breakpoint'))
        checkEq(stop.body.hitBreakpointIds, [sum.id], on('the stop names the breakpoint that was hit'))
        checkEq(stop.body.threadId, 1, on('in the one thread'))
        checkEq(stop.line, sumLine, on('at the line of the breakpoint'))
        checkEq(stop.names, ['main', '__root__'], on('the stack is main called from the entry graph'))
        checkEq(stop.top.source?.path, CALLS, on('the frame names the source file'))
        check(stop.top.column > 1, on('with the column of the node about to run'), stop.top)

        const threads = await dap.request('threads')
        checkEq(threads.body?.threads, [{ id: 1, name: 'main' }], on('there is one thread'))

        // ---- variables: values of nodes that have run; the rest is not evaluated ---------
        const locals = await dap.variables(stop.top.id)
        checkEq(
            Object.fromEntries(Object.entries(locals || {}).map(([k, v]) => [k, `${v.type} ${v.value}`])),
            { x: 'int 3', a: 'int 6', b: 'int 4', c: 'int <not evaluated>' },
            on('locals: the lets that have run have values, the node about to run has none'),
        )
        const scopes = await dap.request('scopes', { frameId: stop.top.id })
        checkEq((scopes.body?.scopes || []).map((x) => x.name), ['Locals', 'Nodes'], on('the scopes of the frame'))
        const nodes = await dap.variables(stop.top.id, 'Nodes')
        check(
            Object.keys(nodes || {}).some((n) => /FUNC double/.test(n) && nodes[n].value === '6'),
            on('the Nodes scope lists the graph nodes with their values'),
            Object.keys(nodes || {}),
        )

        let r = await dap.request('evaluate', { expression: 'a', frameId: stop.top.id, context: 'watch' })
        check(r.success && r.body.result === '6' && r.body.type === 'int', on('evaluate gives the value of a name'), r)
        r = await dap.request('evaluate', { expression: 'c', frameId: stop.top.id })
        check(r.success && r.body.result === '<not evaluated>', on('a name whose node has not run is not evaluated, and not forced'), r)
        r = await dap.request('evaluate', { expression: 'a + b', frameId: stop.top.id })
        check(!r.success && /Only a name/.test(r.message), on('an expression is refused: evaluating it would run code'), r)
        r = await dap.request('evaluate', { expression: 'nowhere', frameId: stop.top.id })
        check(!r.success && /not a name of this frame/.test(r.message), on('an unknown name is an error'), r)

        // ---- to the end --------------------------------------------------------------
        r = await dap.request('continue', { threadId: 1 })
        check(r.success && r.body.allThreadsContinued === true, on('continue is accepted'), r)
        const exited = await dap.waitEvent('exited')
        const terminated = await dap.waitEvent('terminated')
        checkEq(exited?.body?.exitCode, 0, on('the program exits with what main returned'))
        check(terminated, on('and the session is terminated'))
        checkEq(dap.outputOf('stdout'), '10\n', on("the program's output is forwarded, and nothing else"))
        const order = dap.events.map((e) => e.event)
        check(
            order.lastIndexOf('output') < order.indexOf('exited') && order.indexOf('exited') < order.indexOf('terminated'),
            on('output, then exited, then terminated'),
            order,
        )
        r = await dap.request('stackTrace', { threadId: 1 })
        check(!r.success && /has ended/.test(r.message), on('after the end a request is an error, not a hang'), r)
        checkEq(dap.protocolErrors, [], on('every message of the adapter was well formed'))
        checkEq(dap.output, '', on('the adapter writes nothing to its standard error'))
        await dap.stop()
        checkEq((await dap.exited).code, 0, on('the adapter exits cleanly on disconnect'))
    }

    // ---- kinds of values, and a var as the cell it is ------------------------------------
    {
        const dap = await t.dap()
        const line = lineOf(VALUES, 'mark:stop')
        await dap.launch(VALUES, { passes: ['std::fvm'] }, { [VALUES]: [line] })
        const stop = await dap.waitStop()
        checkEq(stop?.line, line, 'values: stopped at the breakpoint')
        const locals = (await dap.variables(stop?.top?.id)) || {}
        const shown = (name) => (locals[name] ? `${locals[name].type} = ${locals[name].value}` : 'missing')
        checkEq(shown('n'), 'int = 42', 'an int')
        checkEq(shown('ratio'), 'float = 2.5', 'a float')
        checkEq(shown('ok'), 'bool = true', 'a bool')
        checkEq(shown('name'), "string = 'camel'", 'a string')
        checkEq(shown('counter'), 'var int = 15', 'a var shows the current content of its cell, after the assignment')
        checkEq(shown('some'), 'int? = 9', 'a nullable with a value')
        checkEq(shown('none'), 'int? = null', 'a nullable without one')
        checkEq(shown('good'), 'int! = 25', 'a fallible with a value')
        check(/^int! = error DivisionByZero/.test(shown('bad')), 'a fallible holding the error of an isolated fault', shown('bad'))
        checkEq(shown('later'), 'int = 43', 'in a sync block the statements before the stop have run')
        check(locals.xs?.variablesReference > 0, 'an array can be expanded', locals.xs)
        const xs = await dap.children(locals.xs?.variablesReference || 0)
        checkEq(Object.entries(xs).map(([k, v]) => `${k}=${v.value}`), ['[0]=1', '[1]=2', '[2]=3'], 'into its elements')
        const point = await dap.children(locals.point?.variablesReference || 0)
        checkEq(Object.entries(point).map(([k, v]) => `${k}=${v.value}`), ['x=1', 'y=2'], 'a struct into its fields')
        let r = await dap.request('evaluate', { expression: 'pair.1', frameId: stop?.top?.id })
        checkEq(r.body?.result, "'seven'", 'evaluate follows a tuple index')
        r = await dap.request('evaluate', { expression: 'xs[2]', frameId: stop?.top?.id })
        checkEq(r.body?.result, '3', 'and an array index')
        r = await dap.request('evaluate', { expression: 'xs[9]', frameId: stop?.top?.id })
        check(!r.success && /no member/.test(r.message), 'an index that does not exist is an error', r)
        r = await dap.request('continue', { threadId: 1 })
        check(await dap.waitEvent('terminated'), 'values: the program runs to its end')
        r = await dap.request('variables', { variablesReference: locals.xs?.variablesReference || 1 })
        check(!r.success, 'a variables reference is void once the program has resumed', r)
    }

    // ---- arguments of main and the exit code ---------------------------------------------
    {
        const dap = await t.dap()
        const s = await dap.launch(EXIT, { passes: 'std::fvm', args: ['n=6'] })
        check(s.launch.success, 'exit: launch with a value for a parameter of main', s.launch)
        const exited = await dap.waitEvent('exited')
        checkEq(exited?.body?.exitCode, 7, 'exit: the exit code is what main returned')
        checkEq(dap.outputOf('stdout'), '6\n', 'exit: the argument reached the program')
        check(await dap.waitEvent('terminated'), 'exit: terminated')
    }
})
