// Check the residency conclusion (docs/design/gpu/plan.cn.md 3.2) against the compiled graph.
//
// `TILES_RESIDENCY` is the conclusion the transfer-inserting half of the step reads, and the
// outlines the same run prints are what today's pass does with it. The relation pinned here is the
// one the plan names: a call between two graphs of one class moves no value (its parameters follow
// their arguments), while the outline still leaves a transfer at the callee's entry -- the number
// the insertion changes. The loop's state stays on the device, so a round adds no transfer.
//
// The driver is a node script rather than a Python one so that it is not a Python test of a plan
// tagged `gpu`: it starts camel with `--devices cpu`, so there is no device to declare and nothing
// for the device-layer census (test/tools/check-cuda-visibility.mjs) to count.
import { execFileSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../../../tools/camel-arg.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const root = path.resolve(here, '../../../..')
// The install this case measures: `--camel <path>` names it, this tree's `out/latest/bin/camel` without it. One rule, in one place: test/tools/camel-arg.mjs.
function usage(message) {
    if (message) console.error(`resident_loop: ${message}`)
    console.error('usage: node test/cases/modules/tiles/resident_loop.mjs [--camel PATH]')
    process.exit(2)
}

const { camel: exe } = takeCamel(process.argv.slice(2), root, usage)
const kase = path.join(here, 'resident_loop.cml')

const stdout = execFileSync(exe, ['--devices', 'cpu', kase, 'tiles::compile', 'std::jgir'], {
    cwd: root,
    env: { ...process.env, CUDA_VISIBLE_DEVICES: '-1' },
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
})

const lines = stdout.split('\n')
const residency = {}
for (const line of lines) {
    if (line.startsWith('TILES_RESIDENCY ')) {
        const entry = JSON.parse(line.slice('TILES_RESIDENCY '.length))
        residency[entry.graph] = entry
    }
}
const names = Object.keys(residency)
if (names.length === 0) throw new Error(`no residency conclusion was printed: ${stdout.slice(0, 400)}`)

const step = residency.step
if (step.body !== 'device') throw new Error(`step is not a device body: ${JSON.stringify(step)}`)
// The call's parameters follow the argument's block: two device graphs, one block, no transfer.
const calls = step.calls.filter((call) => call.callee === 'twice')
if (calls.length !== 1) throw new Error(`step does not call twice once: ${JSON.stringify(step.calls)}`)
const expected = { callee: 'twice', callee_at: 'device', caller: 'device', transfers: 0 }
if (JSON.stringify(calls[0]) !== JSON.stringify(expected)) {
    throw new Error(`the call does not move its argument in one block: ${JSON.stringify(calls[0])}`)
}
// The loop's state is made on the device and read on the device, so no round transfers anything.
if (step.per_round !== 0) throw new Error(`a round transfers ${step.per_round} value(s): ${JSON.stringify(step)}`)
if (step.crossings.length !== 0) throw new Error(`the device body crosses a boundary: ${JSON.stringify(step.crossings)}`)
// A host scope calling a device graph is the boundary that does transfer, and it transfers once
// for the call, not once per value.
const main = residency.main
if (main.body !== 'host') throw new Error(`main is not a host body: ${JSON.stringify(main)}`)
const into = main.calls.filter((call) => call.callee === 'step')
if (into.length !== 1 || into[0].transfers !== 1) {
    throw new Error(`the host-to-device call does not transfer once: ${JSON.stringify(main.calls)}`)
}

// What today's outline does with that conclusion: the callee's entry still carries a transfer of
// its own, because each outlined region moves its inputs instead of following the argument's block.
const document = JSON.parse(lines.slice(lines.findIndex((line) => line.startsWith('{'))).join('\n'))
const graphs = Object.fromEntries(document.graphs.map((graph) => [graph.name, graph]))
const labels = graphs.twice.nodes.map((node) => node.label)
if (labels.filter((label) => label === 'tiles:run_preferred_resident').length !== 1) {
    throw new Error(`twice is not one resident call: ${JSON.stringify(labels)}`)
}
const transfers = labels.filter((label) => label === 'tiles:download' || label === 'tiles:upload')
if (transfers.length !== 1) throw new Error(`twice's entry carries ${transfers.length} transfer(s)`)

console.log('Resident loop: PASS')