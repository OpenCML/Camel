// A node step must not stop at the members of a fusion group: they do not execute. The group runs
// at its tail, and the stop there says which group ran and what it computed (design principles
// 9.6 and 7.3; docs/technical/35_debugger.md, 2). The fixture's VJP chain is a private
// `vjp_div_neg` group, so the walk below crosses it; the guard fails if the fixture stops having a
// group (no stop carries `fusion`), not merely if a message differs.
import { runTest, check, checkEq, fixture } from '../../../tools/debugger/client.mjs'

const FILE = fixture('fusion_group.cml')
const MESSAGE =
    'fusion group `vjp_div_neg` runs here: it computes tensor:divide, tensor:multiply, tensor:neg ' +
    'in one pass; those nodes do not execute separately.'

await runTest(async (t) => {
    const dap = await t.dap()
    const session = await dap.launch(FILE, { passes: ['std::nvm'], stepBy: 'node', stopOnEntry: true }, {})
    check(session.launch?.success, 'the program is launched', session.launch)
    const first = await dap.waitStopOrEnd()
    check(first !== 'end', 'the entry stop is reached', first)
    const entryDetail = (await dap.waitEvent('camel/stopped'))?.body || {}
    checkEq(entryDetail.fusion, undefined, 'the entry stop is not a group')

    const stops = [] // { ref, label, fusion }
    let ended = false
    for (let i = 0; i < 600; i++) {
        const r = await dap.request('stepIn', { threadId: 1, granularity: 'instruction' })
        check(r.success, `node step ${i + 1} is accepted`, r)
        const stop = await dap.waitStopOrEnd()
        if (stop === 'end') {
            ended = true
            break
        }
        if (!stop) {
            break
        }
        const detail = (await dap.waitEvent('camel/stopped'))?.body || {}
        stops.push({
            ref: detail.frame?.node?.ref,
            label: detail.frame?.node?.label,
            graph: detail.frame?.graph,
            fusion: detail.fusion,
        })
    }

    check(ended, 'the node walk runs the program to its end', stops.length)
    check(stops.length > 10, 'the walk examines the program, not an empty graph', stops.length)

    const fusionStops = stops.filter((s) => s.fusion)
    checkEq(fusionStops.length, 1, 'the group is reported once, at its tail')

    const group = fusionStops[0]
    checkEq(group?.fusion?.name, 'vjp_div_neg', 'the stop names the group')
    checkEq(
        group?.fusion?.members,
        ['tensor:divide', 'tensor:multiply', 'tensor:neg'],
        'the stop names the members, in execution order',
    )
    checkEq(group?.label, 'tensor:neg', 'the stop is at the tail the group runs at')
    const members = group?.fusion?.refs || []
    checkEq(members.length, 3, 'the stop carries the three member refs')
    checkEq(group?.ref, members[members.length - 1], 'the tail is the last member')

    // The lie the guard is for: a member "executing" on its own. No stop other than the group's
    // tail may be one of the members, and the tail itself is only ever the group's. A ref is local
    // to a graph (`with_grad` runs the gradient as its own graph), so a member is matched by
    // (graph, ref): the main graph reuses the same ref numbers for other nodes.
    const inGroupGraph = stops.filter((s) => s.graph === group?.graph)
    const seenMembers = inGroupGraph.filter((s) => members.includes(s.ref))
    checkEq(
        seenMembers.length,
        1,
        `no member node is ever stopped at: only the tail is (in ${group?.graph}: ${inGroupGraph
            .filter((s) => members.includes(s.ref))
            .map((s) => `${s.ref}:${s.label}`)
            .join(', ')})`,
    )
    check(
        stops.some((s) => s.label === 'tensor:divide' && s !== group),
        'the forward divide does run as its own node (so labels alone cannot be the check)',
    )

    // The debug console says the same sentence the stop carries, once.
    const consoleText = dap.outputOf('console')
    checkEq(
        consoleText.split('\n').filter((l) => l === MESSAGE).length,
        1,
        'the console says it once',
    )
    checkEq(group?.fusion?.message, MESSAGE, 'and the stop carries that exact sentence')
    await dap.stop()
})