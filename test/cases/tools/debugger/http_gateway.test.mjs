// The HTTP API of camel-db as the Web UI uses it: the gateway spawns a worker per file and
// forwards to it; node breakpoints, node steps, continue, restart, terminate, the task's log,
// and the graph after each pass.

import { runTest, check, checkEq, fixture, until } from '../../../tools/debugger/client.mjs'

const CALLS = fixture('calls.cml')

await runTest(async (t) => {
    const gw = await t.gateway()

    // ---- a file becomes a task -------------------------------------------------------
    let r = await gw.post('/api/file', { path: CALLS })
    check(r.json?.ok === true && typeof r.json.taskId === 'string', 'loading a file starts a task', r.text)
    const task = r.json.taskId
    const loaded = await gw.waitTask(task, (x) => x.taskState === 'loaded')
    check(loaded, 'the task is loaded before its first run', await gw.task(task))

    r = await gw.post('/api/file', { path: '/no/such/file.cml' })
    check(r.json?.ok === false, 'a file that does not exist is refused', r.text)

    // ---- a first run, to the end ------------------------------------------------------
    r = await gw.post('/api/run', { target: task })
    checkEq(r.json?.ok, true, 'run is accepted')
    let done = await gw.waitTask(task, (x) => x.taskState === 'completed')
    check(done, 'the program runs to its end', await gw.task(task))
    const log = await until(async () => {
        const l = await gw.get(`/api/log?target=${task}`)
        return l.json?.lines?.includes('10') ? l.json.lines : null
    })
    check(log, "the program's output is in the task's log", (await gw.get(`/api/log?target=${task}`)).text)

    // ---- the graph of that run, and a breakpoint on one of its nodes ------------------
    // (Graph and node ids are those of one compilation: the graph is asked for after a run.)
    r = await gw.get(`/api/gir-json?target=${task}`)
    const mainId = r.json?.graph?.children?.find((g) => g.name === 'main')?.id
    check(mainId, 'the root graph lists main', r.text.slice(0, 300))
    r = await gw.get(`/api/gir-json?target=${task}&graphId=${mainId}`)
    const nodes = r.json?.graph?.nodes || []
    const callDouble = nodes.find((n) => n.type === 'FUNC' && n.funcGraph === 'double')
    const callNext = nodes.find((n) => n.type === 'FUNC' && n.funcGraph === 'next')
    check(callDouble?.originId > 0 && callNext?.originId > 0, 'call nodes carry origins', nodes.slice(0, 5))
    checkEq(callDouble?.span?.startLine, 13, 'the origin of a node resolves to its source line (0-based)')

    r = await gw.post('/api/gir-breakpoints', { target: task, originIds: [callDouble.originId] })
    checkEq(r.json?.ok, true, 'a node breakpoint is accepted')
    r = await gw.get(`/api/gir-breakpoints?target=${task}`)
    checkEq(r.json?.originIds, [callDouble.originId], 'and is reported back')

    // ---- restart, to the breakpoint ---------------------------------------------------
    r = await gw.post('/api/restart', { target: task })
    checkEq(r.json?.ok, true, 'restart is accepted')
    let paused = await gw.waitTask(task, (x) => x.paused)
    check(paused, 'the task pauses at the breakpoint', await gw.task(task))
    checkEq(paused?.lastAlloc?.phase, 'gir_node', 'the pause is a node pause')
    checkEq(paused?.lastAlloc?.origin, callDouble.originId, 'at the node of the breakpoint')
    const mainGraph = paused?.lastAlloc?.graphId
    check(String(mainGraph).startsWith('main'), 'in the graph of main', paused?.lastAlloc)

    // ---- node steps ------------------------------------------------------------------
    // Step in: the next node the scheduler runs, in whichever graph.
    const before = paused.lastAlloc.nodeId
    await gw.post('/api/step', { target: task })
    let stepped = await gw.waitTask(task, (x) => x.paused && x.lastAlloc?.nodeId !== before)
    check(stepped, 'a step pauses at another node', await gw.task(task))
    check(String(stepped?.lastAlloc?.graphId).startsWith('double'), 'step in enters the callee graph', stepped?.lastAlloc)

    // Step over, constrained to the graph of main: runs the rest of the callee.
    await gw.post('/api/step', { target: task, graphId: mainGraph })
    stepped = await gw.waitTask(task, (x) => x.paused && x.lastAlloc?.graphId === mainGraph && x.lastAlloc?.nodeId !== before)
    check(stepped, 'a step constrained to a graph pauses at the next node of that graph', await gw.task(task))

    // ---- continue to the end ----------------------------------------------------------
    await gw.post('/api/gir-breakpoints', { target: task, originIds: [] })
    await gw.post('/api/continue', { target: task })
    done = await gw.waitTask(task, (x) => x.taskState === 'completed' && !x.paused)
    check(done, 'continue runs the program to its end', await gw.task(task))

    // ---- the graph after each pass ----------------------------------------------------
    r = await gw.post('/api/run', { target: task, passes: ['std::inline'] })
    checkEq(r.json?.ok, true, 'a completed task runs again, with passes')
    const snapshots = await until(async () => {
        const g = await gw.get(`/api/pass-graphs?target=${task}`)
        return g.json?.graphs?.length >= 2 ? g.json.graphs : null
    })
    check(snapshots, 'a run records the graph before the passes and after each pass', (await gw.get(`/api/pass-graphs?target=${task}`)).text)
    checkEq(snapshots?.map((g) => g.pass), [null, 'std::inline'], 'one snapshot per pass, the input first')
    r = await gw.get(`/api/pass-graph?target=${task}&index=1`)
    check(r.json?.dot?.startsWith('digraph'), 'a snapshot is the DOT text of the graph', r.text.slice(0, 200))
    const input = await gw.get(`/api/pass-graph?target=${task}&index=0`)
    check(input.json?.dot?.includes('label="double", type="FUNC"') && !r.json?.dot?.includes('label="double", type="FUNC"'),
        'std::inline removed the call that the input graph has')
    r = await gw.get(`/api/pass-graph?target=${task}&index=99`)
    checkEq(r.status, 400, 'a snapshot that does not exist is a 400')
    await gw.waitTask(task, (x) => x.taskState === 'completed')

    // ---- restart keeps the breakpoints and the passes; terminate ends a paused task ---
    // The restarted run inlines again: the call nodes are gone, the addition of main is not.
    const add = nodes.find((n) => n.type === 'OPER' && n.operName === '__add__')
    await gw.post('/api/gir-breakpoints', { target: task, originIds: [callNext.originId, add.originId] })
    r = await gw.post('/api/restart', { target: task })
    checkEq(r.json?.ok, true, 'a second restart is accepted')
    paused = await gw.waitTask(task, (x) => x.paused)
    check(paused, 'the restarted task pauses at a breakpoint set before', await gw.task(task))
    checkEq(paused?.lastAlloc?.origin, add.originId, 'at the node that is still in the graph after std::inline')

    r = await gw.post('/api/terminate', { target: task })
    checkEq(r.json?.ok, true, 'terminate is accepted')
    const ended = await gw.waitTask(task, (x) => x.taskState === 'terminated' && !x.paused)
    check(ended, 'a paused task is terminated', await gw.task(task))

    // ---- requests that name no task, or a task that does not exist --------------------
    r = await gw.post('/api/continue', { target: '1' })
    check(r.json?.ok === false, 'continue for an unknown task is refused', r.text)
    r = await gw.get('/api/task-state')
    check(r.status === 400 || r.json?.error, 'task-state without a target is refused', r.text)
})
