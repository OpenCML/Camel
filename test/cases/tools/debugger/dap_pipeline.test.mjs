// Release-capable compiler boundary stops use the same core as runtime debugging.
import { runTest, check, checkEq, fixture } from '../../../tools/debugger/client.mjs'
const program = fixture('calls.cml')
await runTest(async t => {
    const dap = await t.dap()
    const launch = await dap.launch(program, { passes: ['std::macro', 'std::inline', 'std::nvm'], stopOnPassEntry: true })
    check(launch.launch.success, 'launch compiler debugging')
    for (const [index, pass] of [[0, null], [1, 'std::macro'], [2, 'std::inline']]) {
        const stop = await dap.waitEvent('stopped')
        checkEq(stop?.body.reason, 'pause', 'compiler pause is distinct from a source step')
        const detail = await dap.waitEvent('camel/stopped')
        checkEq(detail?.body.pipeline, { boundary: index === 0 ? 'initial' : 'after', index, pass }, 'exact boundary identity')
        const graph = await dap.request('camel/pipelineGraph')
        check(graph.success && JSON.parse(graph.body.gir).format === 'camel.gir', 'actual graph uses versioned structured GIR')
        checkEq((await dap.request('stackTrace', { threadId: 1 })).body?.stackFrames, [], 'compiler stops do not fabricate runtime frames')
        check(!(await dap.request('next', { threadId: 1 })).success, 'source step cannot silently advance a pass')
        check((await dap.request('camel/nextPass')).success, 'advance compiler boundary')
    }
    checkEq((await dap.waitEvent('stopped'))?.body.reason, 'entry', 'last compiler step hands off at execution entry')
    check(!(await dap.request('camel/nextPass')).success, 'pass step rejects a runtime stop')
    check((await dap.request('continue', { threadId: 1 })).success, 'continue runtime')
    check(await dap.waitEvent('terminated'), 'normal termination')

    const repeated = await t.dap()
    await repeated.launch(program, { passes: ['std::macro', 'std::macro', 'std::nvm'], passBreakpoints: [2] })
    check(await repeated.waitEvent('stopped'), 'requested boundary stops without entry pause')
    checkEq((await repeated.waitEvent('camel/stopped'))?.body.pipeline?.index, 2, 'repeated pass names retain occurrence identity')
    check(!(await repeated.request('camel/setPassBreakpoints', { indices: [-1] })).success, 'reject invalid boundary indices')
    check((await repeated.request('camel/setPassBreakpoints', { indices: [] })).success, 'update pass breakpoints while stopped')
    check((await repeated.request('terminate')).success, 'terminate at compiler stop')
    check(await repeated.waitEvent('terminated'), 'termination unblocks program thread')

    const consumed = await t.dap()
    await consumed.launch(program, { passes: ['std::gir'], stopOnPassEntry: true })
    await consumed.waitEvent('stopped')
    check((await consumed.request('camel/nextPass')).success, 'step a consuming translation pass')
    check(await consumed.waitEvent('terminated'), 'consumed graph ends without a fake after snapshot')

    const failed = await t.dap()
    await failed.launch(program, { passes: ['std::not_a_pass'], stopOnPassEntry: true })
    checkEq((await failed.waitEvent('exited'))?.body.exitCode, 1, 'invalid pipeline exits as a failure')
    check(await failed.waitEvent('terminated'), 'failed pipeline terminates coherently')
    check(!failed.events.some(e => e.event === 'camel/stopped'), 'failed pipeline does not fabricate a graph stop')
    check(failed.outputOf('stdout').includes('UnrecognizedGraphPass'), 'pipeline failure diagnostic is preserved')

    const invalid = await t.dap()
    const rejected = await invalid.launch(program, { passBreakpoints: [-1] })
    check(!rejected.launch.success, 'invalid launch breakpoints rejected before starting a worker')
})
