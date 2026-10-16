// A GIR origin breakpoint uses the same transformed execution graph as source debugging.
import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'
await runTest(async t => {
    const file = fixture('calls.cml'), line = lineOf(file, 'let c = a + b')
    for (const passes of [['std::nvm'], ['std::fvm'], ['std::macro', 'std::inline', 'std::nvm']]) {
        const dap = await t.dap()
        await dap.launch(file, { passes, stopOnPassEntry: true })
        check(await dap.waitEvent('stopped'), 'initial compiler boundary')
        await dap.waitEvent('camel/stopped')
        const graph = JSON.parse((await dap.request('camel/structuredGraph')).body.gir)
        const target = graph.graphs.flatMap(g => g.nodes).find(n => n.kind === 'OPER' && n.source?.start.line === line - 1)
        check(target?.origin, 'source-linked node has a stable origin')
        if (!target?.origin) continue
        check((await dap.request('camel/setNodeBreakpoints', { origins: [Number(target.origin)] })).success, 'configure origin before passes')
        await dap.request('continue', { threadId: 1 })
        checkEq((await dap.waitEvent('stopped'))?.body.reason, 'breakpoint', 'runtime node breakpoint hits after transformations')
        const detail = (await dap.waitEvent('camel/stopped'))?.body
        checkEq(String(detail?.frame?.node?.origin), target.origin, 'paused node carries the selected origin')
        checkEq(detail?.frame?.line, line, 'paused source matches GIR node')
        check((await dap.request('camel/structuredGraph')).success, 'stopped worker exposes actual GIR')
        await dap.request('camel/setNodeBreakpoints', { origins: [] })
        await dap.request('continue', { threadId: 1 })
        check(await dap.waitEvent('terminated'), 'clear and resume without a stale node stop')
    }
})
