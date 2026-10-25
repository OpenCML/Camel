// The Debug Adapter Protocol at its edges: requests that are wrong or come at the wrong time
// get an error response (never silence, never a crash of the adapter); a program that does not
// compile or names an unknown pass ends the session with its diagnostics; pause, terminate and
// disconnect on a program that never ends; the back ends that cannot be stopped run on one
// that can, and say so; stopOnEntry; zero-based lines.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const CALLS = fixture('calls.cml')
const LOOP = fixture('loop_forever.cml')
const BROKEN = fixture('broken.cml')

const failed = (r, pattern) => r.success === false && !r.timeout && pattern.test(r.message || '')

await runTest(async (t) => {
    // ---- before launch ----------------------------------------------------------------
    {
        const dap = await t.dap()
        let r = await dap.request('stackTrace', { threadId: 1 })
        check(failed(r, /launch first/), 'a query before launch is an error that says what to do', r)
        r = await dap.request('initialize', { adapterID: 'camel' })
        check(r.success, 'initialize', r)
        r = await dap.request('setBreakpoints', { source: { path: CALLS }, breakpoints: [{ line: 14 }] })
        check(failed(r, /before launch/), 'breakpoints before launch are refused: the program is not loaded', r)
        r = await dap.request('configurationDone')
        check(failed(r, /before launch/), 'configurationDone before launch is refused', r)
        r = await dap.request('frobnicate', { x: 1 })
        check(failed(r, /Unsupported request: frobnicate/), 'an unknown request is an error response', r)
        checkEq(r.command, 'frobnicate', 'that echoes the command')
        check(r.body?.error?.format, 'and carries the error in the body as the protocol defines it', r)
        r = await dap.request('attach', {})
        check(failed(r, /attach is not supported/), 'attach is refused', r)
        r = await dap.request('threads')
        check(r.success, 'threads answers at any time', r)

        r = await dap.request('launch', {})
        check(failed(r, /"program"/), 'launch without a program is refused', r)
        r = await dap.request('launch', { program: '/no/such/program.cml' })
        check(failed(r, /no such file/), 'launch of a file that does not exist is refused', r)
        r = await dap.request('launch', { program: CALLS, passes: 7 })
        check(failed(r, /"passes"/), 'launch with passes of the wrong type is refused', r)

        // Not requests at all.
        dap.sendMessage({ seq: 900, type: 'request' })
        dap.sendMessage({ seq: 901, type: 'bogus', command: 'threads' })
        dap.sendRaw('Content-Length: 9\r\n\r\nnot json!')
        r = await dap.request('threads')
        check(r.success, 'the adapter is still there after malformed messages', r)
        check(
            dap.responses.length === 3 && dap.responses.every((x) => x.success === false && /Malformed message/.test(x.message)),
            'each malformed message got an error response',
            dap.responses,
        )

        // After all that a launch still works.
        const s = await dap.launch(CALLS, { passes: ['std::fvm'] }, { [CALLS]: [lineOf(CALLS, 'let c = a + b')] })
        check(s.launch.success, 'a correct launch after the refused ones succeeds', s.launch)
        const stop = await dap.waitStop()
        checkEq(stop?.name, 'main', 'and reaches its breakpoint')

        r = await dap.request('launch', { program: CALLS })
        check(failed(r, /already launched/), 'a second launch in the session is refused', r)
        r = await dap.request('configurationDone')
        check(failed(r, /already sent/), 'a second configurationDone is refused', r)
        r = await dap.request('scopes', { frameId: 987654 })
        check(failed(r, /no such frame/), 'scopes of a frame that does not exist', r)
        r = await dap.request('variables', { variablesReference: 987654 })
        check(failed(r, /no such variables reference/), 'variables of a reference that does not exist', r)
        r = await dap.request('evaluate', { frameId: stop?.top?.id })
        check(failed(r, /expression is required/), 'evaluate without an expression', r)
        r = await dap.request('setBreakpoints', { breakpoints: [{ line: 1 }] })
        check(failed(r, /source\.path is required/), 'setBreakpoints without a source', r)
        r = await dap.request('setBreakpoints', { source: { path: '/somewhere/else.cml' }, breakpoints: [{ line: 3 }] })
        check(
            r.success && r.body.breakpoints[0].verified === false && /not part of the program/.test(r.body.breakpoints[0].message),
            'a breakpoint in a file that is not part of the program is unverified, with the reason',
            r,
        )
        r = await dap.request('setBreakpoints', { source: { path: CALLS }, breakpoints: [{ line: 2 }, { line: 9999 }] })
        check(
            r.success && r.body.breakpoints.every((b) => b.verified === false && /No code on this line/.test(b.message)),
            'a breakpoint on a line without code is unverified, with the reason',
            r,
        )
        r = await dap.request('exceptionInfo', { threadId: 1 })
        check(failed(r, /not stopped at a fault/), 'exceptionInfo at a stop that is no fault', r)
        r = await dap.request('camel/graph', { frameId: stop?.top?.id })
        check(
            r.success && r.body.nodes?.some((n) => n.state === 'current') && typeof r.body.dot === 'string',
            'the graph view of the frame: its nodes with their state, and DOT text',
            r.body?.nodes?.slice(0, 3),
        )
        checkEq(dap.protocolErrors, [], 'every message of the adapter was well formed')
        await dap.stop()
    }

    // ---- a program that does not compile ---------------------------------------------------
    {
        const dap = await t.dap()
        await dap.request('initialize', { adapterID: 'camel' })
        const r = await dap.request('launch', { program: BROKEN })
        check(failed(r, /did not compile/), 'launch of a program with an error fails', r)
        const exited = await dap.waitEvent('exited')
        checkEq(exited?.body?.exitCode, 1, 'the session ends with exit code 1')
        check(await dap.waitEvent('terminated'), 'and terminated')
        check(
            /error\[SemanticDiag::UnresolvedReference\].*missing_name[\s\S]*broken\.cml:4:13/.test(dap.outputOf('stdout')),
            'after the diagnostic of the compiler, with its position',
            dap.outputOf('stdout'),
        )
        // A program that never started did not end: the reason a request is refused says so.
        const late = await dap.request('stackTrace', { threadId: 1 })
        check(
            !late.success && /did not start/.test(late.message) && !/has ended/.test(late.message),
            'a request after a launch that never started the program says that, not that it ended',
            late,
        )
        await dap.stop()
    }

    // ---- an unknown pass --------------------------------------------------------------------
    {
        const dap = await t.dap()
        const s = await dap.launch(CALLS, { passes: ['std::no_such_pass'] })
        check(s.launch.success, 'a pass sequence is checked when the program starts', s.launch)
        const exited = await dap.waitEvent('exited')
        checkEq(exited?.body?.exitCode, 1, 'an unknown pass ends the session with exit code 1')
        check(/std::no_such_pass/.test(dap.outputOf('stderr') + dap.outputOf('stdout')), 'and names the pass', dap.outputOf('stdout'))
        check(await dap.waitEvent('terminated'), 'and terminated')
        // This program did run, and then failed: that is the end the error names.
        const late = await dap.request('stackTrace', { threadId: 1 })
        check(
            !late.success && /has ended/.test(late.message),
            'a request after a pipeline that failed says the program ended',
            late,
        )
        await dap.stop()
    }

    // ---- a program that never ends: pause, queries while running, terminate ------------------
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`
        const dap = await t.dap()
        await dap.launch(LOOP, { passes: [backend] })
        check(await dap.waitOutput('stdout', 'spinning\n'), on('the output of a running program arrives while it runs'))
        let r = await dap.request('stackTrace', { threadId: 1 })
        check(failed(r, /not stopped/), on('the stack of a running program is an error'), r)
        r = await dap.request('continue', { threadId: 1 })
        check(failed(r, /not stopped/), on('continue of a running program is an error'), r)
        r = await dap.request('next', { threadId: 1 })
        check(failed(r, /not stopped/), on('a step of a running program is an error'), r)

        r = await dap.request('pause', { threadId: 1 })
        check(r.success, on('pause is accepted'), r)
        const stop = await dap.waitStop()
        checkEq(stop?.reason, 'pause', on('the program stops with reason pause'))
        check(/^spin \(tail call, \d+ frames replaced\)$/.test(stop?.name || ''), on('inside the endless tail recursion, one frame'), stop?.name)
        checkEq(stop?.frames.length, 3, on('under main and the entry graph'))
        const n = Number((await dap.variables(stop.top.id)).n?.value)
        check(n > 0, on('n has advanced'), n)

        r = await dap.request('terminate')
        check(r.success, on('terminate is accepted'), r)
        const exited = await dap.waitEvent('exited')
        checkEq(exited?.body?.exitCode, 143, on('the program is ended; exit code of a terminated program'))
        check(await dap.waitEvent('terminated'), on('terminated'))
        await dap.stop()
        checkEq((await dap.exited).code, 0, on('the adapter exits'))
    }

    // ---- terminate and disconnect while the program runs --------------------------------------
    {
        const dap = await t.dap()
        await dap.launch(LOOP, { passes: ['std::fvm'] })
        await dap.waitOutput('stdout', 'spinning\n')
        const r = await dap.request('terminate')
        check(r.success, 'terminate of a running program is accepted', r)
        checkEq((await dap.waitEvent('exited'))?.body?.exitCode, 143, 'and ends it at its next node')
        await dap.stop()
    }
    {
        const dap = await t.dap()
        await dap.launch(LOOP, { passes: ['std::fvm'] })
        await dap.waitOutput('stdout', 'spinning\n')
        const r = await dap.request('disconnect', { terminateDebuggee: true })
        check(r.success, 'disconnect of a running program is answered', r)
        const end = await Promise.race([dap.exited, new Promise((resolve) => setTimeout(() => resolve(null), 20000))])
        checkEq(end?.code, 0, 'and the adapter exits, taking the program with it')
    }
    {
        // The editor goes away without a word: the adapter ends when its input closes.
        const dap = await t.dap()
        await dap.launch(LOOP, { passes: ['std::fvm'] })
        await dap.waitOutput('stdout', 'spinning\n')
        dap.proc.stdin.end()
        const end = await Promise.race([dap.exited, new Promise((resolve) => setTimeout(() => resolve(null), 20000))])
        checkEq(end?.code, 0, 'the adapter exits when its input is closed')
    }

    // ---- back ends without hooks run on std::fvm, and say so ----------------------------------
    for (const passes of [['std::jit'], ['std::opt::inline', 'std::prl']]) {
        const name = passes[passes.length - 1]
        const dap = await t.dap()
        const line = lineOf(CALLS, 'println(c)')
        const s = await dap.launch(CALLS, { passes }, { [CALLS]: [line] })
        check(s.breakpoints[CALLS][0].verified, `${name}: breakpoints are verified`, s.breakpoints)
        const stop = await dap.waitStop()
        checkEq(stop?.line, line, `${name}: the program stops at the breakpoint`)
        check(
            await dap.waitOutput('console', `${name} runs as std::fvm under the debugger`),
            `${name}: the console says which back end runs instead, and why`,
            dap.outputOf('console'),
        )
        await dap.request('continue', { threadId: 1 })
        checkEq((await dap.waitEvent('exited'))?.body?.exitCode, 0, `${name}: the program runs to its end`)
        checkEq(dap.outputOf('stdout'), '10\n', `${name}: with its output`)
        await dap.stop()
    }

    // ---- after optimisation passes: no crash, positions kept where the nodes survive ------------
    {
        const dap = await t.dap()
        const callLine = lineOf(CALLS, 'let a = double(x)')
        const sumLine = lineOf(CALLS, 'let c = a + b')
        const printLine = lineOf(CALLS, 'println(c)')
        const s = await dap.launch(CALLS, { passes: ['std::opt', 'std::fvm'] }, { [CALLS]: [callLine, sumLine, printLine] })
        checkEq(s.breakpoints[CALLS].map((b) => b.verified), [true, true, true], 'opt: all three lines have executable nodes before optimisation')
        const changed = await dap.waitEvent('breakpoint', (b) => b.breakpoint?.id === s.breakpoints[CALLS][0].id)
        check(
            changed?.body.breakpoint.verified === false && /a pass removed or replaced it/.test(changed.body.breakpoint.message),
            'opt: when the passes have run, the breakpoint on the inlined call is reported as gone, with the reason',
            changed,
        )
        // The full optimisation pipeline folds the constant sum too. It remains a
        // readable value, but only the effectful println is an executable stop.
        const folded = await dap.waitEvent('breakpoint', (b) => b.breakpoint?.id === s.breakpoints[CALLS][1].id)
        check(
            folded?.body.breakpoint.verified === false && /only defines constants/.test(folded.body.breakpoint.message),
            'opt: the folded sum loses its breakpoint with the constant-only reason',
            folded,
        )
        // The passes evaluate graphs of their own (the rules library's). That work is not the
        // program's run: a breakpoint on code that survives the passes must stay verified, or a
        //  pass would wipe out every breakpoint of the file before the program starts.
        const stop = await dap.waitStop()
        check(
            !dap.events.some((e) => e.event === 'breakpoint' && e.body.breakpoint?.id === s.breakpoints[CALLS][2].id),
            'opt: a pass running its own graphs does not withdraw the breakpoint that survives',
            dap.events.filter((e) => e.event === 'breakpoint'),
        )
        checkEq(stop?.line, printLine, 'opt: the surviving effect stops at its source line')
        checkEq(stop?.body.hitBreakpointIds, [s.breakpoints[CALLS][2].id], 'opt: only the surviving breakpoint is hit')
        const locals = await dap.variables(stop.top.id)
        check(/not in the graph/.test(locals?.a?.value || ''), 'opt: a name whose node a pass removed says so', locals)
        checkEq(locals?.c?.value, '10', 'opt: the folded sum remains readable as a constant')
        await dap.request('continue', { threadId: 1 })
        checkEq((await dap.waitEvent('exited'))?.body?.exitCode, 0, 'opt: the program runs to its end')
        checkEq(dap.outputOf('stdout'), '10\n', 'opt: the surviving effect prints the folded value')
        await dap.stop()
    }

    // ---- stopOnEntry, the default back end, zero-based lines -----------------------------------
    {
        const dap = await t.dap()
        const s = await dap.launch(CALLS, { stopOnEntry: true }, {}, { linesStartAt1: false, columnsStartAt1: false })
        check(s.launch.success, 'entry: launch without passes runs the graph as compiled on std::nvm', s.launch)
        const stop = await dap.waitStop()
        checkEq(stop?.reason, 'entry', 'entry: the program stops before its first located node')
        const first = lineOf(CALLS, 'let a = double(x)')
        checkEq(stop?.line, first - 1, 'entry: lines are zero-based for a client that asked for it')
        const r = await dap.request('setBreakpoints', { source: { path: CALLS }, breakpoints: [{ line: first + 1 }] })
        checkEq(r.body?.breakpoints?.[0]?.line, first + 1, 'entry: and breakpoints are answered in the same base')
        await dap.request('continue', { threadId: 1 })
        const next = await dap.waitStop()
        checkEq(next?.line, first + 1, 'entry: a breakpoint set while stopped is hit')
        await dap.stop()
    }
})
