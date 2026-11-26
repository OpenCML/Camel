// Release-capable compiler boundary stops use the same core as runtime debugging.
import { runTest, check, checkEq, fixture } from '../../../tools/debugger/client.mjs'
const program = fixture('calls.cml')
await runTest(async t => {
    const dap = await t.dap()
    const launch = await dap.launch(program, { passes: ['std::macro', 'std::cxx::inline', 'std::nvm'], stopOnPassEntry: true })
    check(launch.launch.success, 'launch compiler debugging')
    for (const [index, pass] of [[0, null], [1, 'std::macro'], [2, 'std::cxx::inline']]) {
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

    // A translating pass hands the graph on: `std::gir` prints the program and returns it, and a
    // pipeline that only prints ends with `std::nop`, the pass that ends it without running it.
    const consumed = await t.dap()
    await consumed.launch(program, { passes: ['std::gir', 'std::nop'], stopOnPassEntry: true })
    const boundary = await consumed.waitEvent('stopped')
    check(!(await consumed.waitEvent('camel/stopped'))?.body.pipeline?.pass, 'the first stop is before the pipeline')
    const stepped = await consumed.request('camel/nextPass')
    check(stepped.success, 'step a translating pass', { boundary: boundary?.body, stepped })
    checkEq(
        (await consumed.waitEvent('camel/stopped'))?.body.pipeline?.pass,
        'std::gir',
        'the boundary after it names the pass that printed',
    )
    check(
        (await consumed.request('camel/nextPass')).success,
        'step into the pass that ends the pipeline without running it',
    )
    check(
        await consumed.waitEvent('terminated'),
        'a pipeline that ends without a run ends there, with no stop after the snapshot',
    )

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