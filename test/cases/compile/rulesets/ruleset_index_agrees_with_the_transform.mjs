// The batching index has two doors -- the table `std::rulesets` prints, and the `asked=/found=`
// reading the batching transform prints when it meets a primitive -- and this checker compares them
// name for name on one program, so a key that is filed but not printed, or printed but not
// reachable, is red rather than a matter of reading the output carefully.
//
// Three numbers have to be one: the table's size, the answers the index gives for the same keys
// (`std::rulesets query=...`, which is `lookupRule`, the function the transform itself calls), and
// the keys the transform actually hits in the same program. The last is a subset by construction --
// a body reaches only the primitives it writes -- so the check is that every hit is a row of the
// table with the same rule name, and that the two full sets (table and lookups) are equal.
import { spawnSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))

function repositoryRoot(start) {
    let dir = resolve(start)
    for (;;) {
        try {
            readFileSync(join(dir, 'stdlib', 'rulesets', 'vmap.cml'), 'utf8')
            return dir
        } catch {
            const parent = dirname(dir)
            if (parent === dir) throw new Error(`no stdlib/rulesets/vmap.cml above ${start}`)
            dir = parent
        }
    }
}

const root = repositoryRoot(here)
const camel = join(root, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const bodies = join(root, 'test', 'cases', 'compile', 'rulesets', 'ruleset_index_bodies.cml')

function run(args) {
    const result = spawnSync(camel, args, { cwd: root, encoding: 'utf8', maxBuffer: 1 << 26 })
    if (result.error) throw result.error
    const stdout = result.stdout ?? ''
    const stderr = result.stderr ?? ''
    if (result.status !== 0) {
        throw new Error(`${args.join(' ')} exited ${result.status}\n${stdout}\n${stderr}`)
    }
    return { stdout, stderr }
}

// ---- the table: one row per key, in key order ----
// A key may hold more than one law: a reduction over an axis is written with the whole reduction's
// name and the axis in `<>` (author's ruling, 2026-11-05), so `sum/1` holds the law of `sum(xs)` and
// the law of `sum<axis, keepdims>(xs)` and the ports say which is which
// (`modules/transforms/rule_expand.cpp#chooseRuleAmong`).
const table = new Map()
const tableText = run([bodies, 'std::rulesets']).stdout
for (const line of tableText.split('\n')) {
    const row = /^RULESET rulesets\.vmap (\S+)\/(\d+) -> (\S+)/.exec(line.trim())
    if (!row) continue
    const key = `${row[1]}/${row[2]}`
    const rules = table.get(key) ?? []
    rules.push(row[3])
    table.set(key, rules)
}
if (table.size === 0) throw new Error('the table printed no rulesets.vmap row at all')

// ---- the same keys asked of the index the transform reads ----
const questions = [...table.keys()].map((key) => {
    const at = key.lastIndexOf('/')
    return `rulesets.vmap|${key.slice(0, at)}|${key.slice(at + 1)}`
})
const lookupText = run([bodies, 'std::rulesets', `query=${questions.join(';')}`]).stdout
const looked = new Map()
for (const line of lookupText.split('\n')) {
    const answer = /^LOOKUP rulesets\.vmap (.+)\/(\d+) -> (\S+)/.exec(line.trim())
    if (!answer) continue
    looked.set(`${answer[1]}/${answer[2]}`, answer[3])
}
if (looked.size !== table.size) {
    throw new Error(`the table printed ${table.size} keys and the index answered ${looked.size}`)
}
for (const [key, rules] of table) {
    const answer = looked.get(key)
    if (rules.length === 1) {
        if (answer !== rules[0]) {
            throw new Error(`${key}: the table says ${rules[0]}, the index answers ${answer ?? 'nothing'}`)
        }
    } else if (answer !== 'no') {
        // The singular lookup refuses a key that holds two laws ("a caller that has only the
        // spelling must not guess which"), and the transform asks the plural one and chooses by
        // the operands' ports -- so the table's two rows and the refusal are the same fact.
        throw new Error(
            `${key}: the table holds ${rules.length} laws, and the singular lookup answered ${answer ?? 'nothing'}`
        )
    }
}

// ---- what the transform hits in the same program ----
const expected = new Map([
    // the five arithmetic keys of a scalar
    ['plus', '+/2'],
    ['minus', '-/2'],
    ['times', '*/2'],
    ['divide', '//2'],
    ['negate', '-/1'],
    // the whole-row reductions
    ['row_sum', 'sum/1'],
    ['row_mean', 'mean/1'],
    ['row_max', 'max/1'],
    ['row_min', 'min/1'],
    ['row_argmax', 'argmax/1'],
    ['row_argmin', 'argmin/1'],
    // the same six with an axis word: the same key as the whole form, because a reduction over an
    // axis is written with the whole reduction's name (`sum<0>(xs)`)
    ['plane_sum_axis', 'sum/1'],
    ['plane_mean_axis', 'mean/1'],
    ['plane_max_axis', 'max/1'],
    ['plane_min_axis', 'min/1'],
    ['plane_argmax_axis', 'argmax/1'],
    ['plane_argmin_axis', 'argmin/1'],
    // a row's matrix product
    ['flat_mul', 'matmul/2'],
    // the layout tier: the shape, axis and permutation written in angle brackets
    ['row_reshape', 'reshape/1'],
    ['row_unsqueeze', 'unsqueeze/1'],
    ['plane_transpose', 'transpose/1'],
    ['plane_permute', 'permute/1'],
    ['plane_concat', 'concat/2'],
    ['plane_dim', 'dim/1'],
    // the condition is not a value arm: where/3 is instantiated on the arms' element
    ['row_where', 'where/3'],
])

// The transform's hits are spoken by the library, so they arrive on the diagnostic side, not in
// the program's stdout (`modules/transforms/readings.h`); the two are read together here because
// this checker cares which keys were hit, not which channel carried them.
const transformRun = run([bodies, 'std::nvm'])
const transformText = `${transformRun.stdout}\n${transformRun.stderr}`
const hits = new Map()
for (const line of transformText.split('\n')) {
    const hit = /REINTERP vmap function=(\S+) .*asked=(\d+) found=(\d+) missing=(\d+) rules=(\S+)/.exec(line)
    if (!hit) continue
    const [, fn, asked, found, missing, rule] = hit
    if (asked !== '1' || found !== '1' || missing !== '0') {
        throw new Error(`${fn}: asked=${asked} found=${found} missing=${missing}`)
    }
    hits.set(fn, rule)
}

const wrong = []
for (const [fn, key] of expected) {
    const rule = hits.get(fn)
    if (!rule) {
        wrong.push(`${fn} did not reach ${key}`)
        continue
    }
    const printed = table.get(key)
    if (printed === undefined || !printed.includes(rule)) {
        wrong.push(`${fn} hit ${rule} through ${key}, the table says ${printed?.join(' and ') ?? 'nothing'}`)
    }
}
if (wrong.length > 0) throw new Error(wrong.join('\n'))
if (hits.size !== expected.size) {
    throw new Error(`the transform hit ${hits.size} functions, the program writes ${expected.size}`)
}

// ---- the eight keys this run exists for ----
// The ones a reader of the *tail* of the table's output does not see (the plan report elides the
// head of a long stdout as `... N line(s) before`, which is what made a complete table look like a
// table of twelve): the five arithmetic tokens and three of the arg* family. Each must be a row.
const head = ['*/2', '+/2', '-/1', '-/2', '//2', 'argmax/1', 'argmin/1', 'sum/1']
const missing = head.filter((key) => !table.has(key))
if (missing.length > 0) throw new Error(`the table does not hold ${missing.join(', ')}`)

console.log(
    `keys=${table.size} looked=${looked.size} hit=${hits.size} ` +
        `six_eights=${head.length} first=${[...table.keys()][0]} last=${[...table.keys()].at(-1)}`
)