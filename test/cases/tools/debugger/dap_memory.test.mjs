// Bounded, paused-thread memory inspection through the native debug adapter.
import { runTest, check, checkEq, fixture } from '../../../tools/debugger/client.mjs'
await runTest(async t => {
    const dap = await t.dap()
    check(!(await dap.request('camel/memorySnapshot')).success, 'no process means no heap inspection')
    await dap.launch(fixture('calls.cml'), { passes: ['std::nvm'], stopOnEntry: true })
    check(await dap.waitEvent('stopped'), 'runtime is paused before inspection')
    const snapshot = await dap.request('camel/memorySnapshot')
    check(snapshot.success && snapshot.body.regions.length > 0, 'region overview exists')
    check(snapshot.body.regions.every(r => !Object.hasOwn(r, 'objects')), 'overview omits bulk object arrays')
    const region = snapshot.body.regions.find(r => r.type === 'bump' && r.used > 0)
    check(region && /^0x[0-9a-f]+$/.test(region.start), 'region addresses are lossless hex strings')
    const args = { region: region.name, offset: 0, limit: 16 }
    const bytes = await dap.request('camel/regionMemory', args)
    check(bytes.success && bytes.body.data.length === 16, 'bounded byte page')
    check(bytes.body.data.every(v => Number.isInteger(v) && v >= 0 && v <= 255), 'actual byte values')
    checkEq((await dap.request('camel/regionMemory', { ...args, offset: 16 })).body.offset, 16, 'second page offset')
    const end = await dap.request('camel/regionMemory', { ...args, offset: region.capacity })
    check(end.success && end.body.data.length === 0 && !end.body.hasMore, 'empty final page')
    const objects = await dap.request('camel/regionObjects', { ...args, limit: 1 })
    check(objects.success && objects.body.objects.length === 1, 'bounded object page')
    check(/^0x[0-9a-f]+$/.test(objects.body.objects[0].addr), 'object address is a hex string')
    for (const request of [{ ...args, offset: -1 }, { ...args, limit: 4097 }, { ...args, region: 'missing' }]) {
        check(!(await dap.request('camel/regionMemory', request)).success, 'invalid byte request rejected')
    }
    check(!(await dap.request('camel/regionObjects', { ...args, limit: 201 })).success, 'object page bound enforced')
    const alloc = await dap.request('camel/allocationBreakpoints')
    check(alloc.success && typeof alloc.body.supported === 'boolean', 'allocation build capability reported')
    if (!alloc.body.supported) {
        const unsupported = await dap.request('camel/allocationBreakpoints', { enabled: true, spaces: [] })
        check(!unsupported.success && unsupported.message.includes('Debug'), 'Release refuses unsupported allocation breakpoints')
    }
    check((await dap.request('continue', { threadId: 1 })).success, 'resume after inspection')
    check(await dap.waitEvent('terminated'), 'natural termination after memory inspection')
    check(!(await dap.request('camel/memorySnapshot')).success, 'ended program cannot expose a stale heap')
})
