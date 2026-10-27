// One identity per type: `equals`, assignability and the canonical id agree, whichever way a type is
// written (design principles 5.2: a type has a structure and no other identity).
//
// Every spelling of the shared corpus must be assignable to itself (a call passes a value of the
// type to a parameter of the type). Then a list of pairs says, for two spellings, whether they are
// one type (`graph` compares types by identity) and whether a value of either is accepted where
// the other is written. A pair that is one type is accepted both ways; the others are what
// the principles say they are.
//
//   node identity.mjs --camel <path to camel> [--only <substring>]

import { spawnSync } from 'node:child_process'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { PRELUDE, SPELLINGS } from '../type_text/corpus.mjs'

const args = process.argv.slice(2)
const option = (name) => {
    const at = args.indexOf(name)
    return at >= 0 ? args[at + 1] : undefined
}
const camel = option('--camel') ?? 'camel'
const only = option('--only')

// [first spelling, second spelling, one type, first goes where second is written, second goes where first is]
const PAIRS = [
    // the same type written two ways
    ['int[]', 'int[?]', true, true, true],
    ['i64', 'int', true, true, true],
    ['f64', 'float', true, true, true],
    ['(a: int) => int', '(b: int) => int', true, true, true],
    ['f32[n, 4]', 'f32[m, 4]', true, true, true],
    ['f32[n, m]', 'f32[m, n]', true, true, true],
    ['f32[n + m]', 'f32[a + b]', true, true, true],
    ['(a: int) => int pure', '(a: int) => int pure', true, true, true],
    // different types
    ['(a: int) => int', '(a: int, b: int) => int', false, false, false],
    ['(a: int) => int', '(a: int) => float', false, false, false],
    ['<w: int> (a: int) => int', '(a: int) => int', false, false, false],
    ['int[3]', 'int[4]', false, false, false],
    // extents are linear forms over names, and their equality is decidable
    ['f32[n]', 'f32[n + 1]', false, true, true],
    ['f32[n]', 'f32[2 * n]', false, true, true],
    ['f32[n, n]', 'f32[n, m]', false, true, true],
    ['f32[n]', 'f32[]', false, true, true],
    ['int', 'float', false, false, false],
    ['(int, string)', '(string, int)', false, false, false],
    ['int?', 'float?', false, false, false],
    ['(void, int)', '(string, int)', false, false, false],
    ['{ x: int, y: int }', '{ y: int, x: int }', false, false, false],
    // the result of a function is the one place that discards
    ['(a: int) => void', '(a: int) => int', false, false, true],
    // a value of one type where the other is written, one way only
    ['int[3]', 'int[]', false, true, false],
    ['int', 'int?', false, true, false],
    ['Point', '{ x: int, y: int }', false, true, true],
]

const dir = mkdtempSync(join(tmpdir(), 'camel-type-identity-'))
let file = 0

function run(source) {
    const path = join(dir, `t${file++}.cml`)
    writeFileSync(path, source)
    const result = spawnSync(camel, [path], { encoding: 'utf8', timeout: 60000 })
    return { status: result.status, out: (result.stdout ?? '') + (result.stderr ?? '') }
}

// Whether a value of type `from` is accepted where `to` is written: a call compiles.
function accepts(from, to) {
    const result = run(`${PRELUDE}
func take(x: ${to}, y: ${to}): int {
    return 0
}
func give(x: ${from}, y: ${from}): int {
    return take(x, y)
}
func main(): int sync {
    return 0
}
`)
    return { ok: result.status === 0, out: result.out }
}

const PRINT = `
import { graphs_of, name, nodes, type_of } from graph
import { Graph, Node, TypeRef } from graph

func firstType(g: Graph): TypeRef? {
    return type_of(nodes(g)[0])
}

func graphNamed(label: string): Graph {
    let found = graphs_of(reach)->filter<(g: Graph): bool => name(g) == label>
    return found[0]
}
`

// Whether two spellings are one type, by the identity the graph compares.
function sameType(a, b) {
    const result = run(`${PRELUDE}
${PRINT}
func first(x: ${a}, y: ${a}): int {
    return 0
}
func second(x: ${b}, y: ${b}): int {
    return 0
}
/// The graphs the probe reads are the ones this function reaches: it names both without calling them.
func reach(): int {
    let a = first
    let b = second
    return 0
}
func main(): int sync {
    println(match firstType(graphNamed('first')) {
        case null => false
        case p => match firstType(graphNamed('second')) {
            case null => false
            case q => p == q
        }
    })
    return 0
}
`)
    return { ok: result.status === 0 && result.out.trim() === 'true', ran: result.status === 0, out: result.out }
}

let failures = 0
let checked = 0
const fail = (message, detail) => {
    failures++
    console.log(`FAIL  ${message}${detail ? '\n      ' + detail.split('\n').slice(0, 3).join('\n      ') : ''}`)
}

for (const spelling of SPELLINGS) {
    if (only && !spelling.includes(only)) continue
    checked++
    const own = accepts(spelling, spelling)
    if (!own.ok) fail(`${spelling} is not accepted where ${spelling} is written`, own.out)
}

for (const [a, b, equal, ab, ba] of PAIRS) {
    if (only && !(a + b).includes(only)) continue
    checked++
    const same = sameType(a, b)
    if (!same.ran) {
        fail(`${a} | ${b}: the program does not compile`, same.out)
        continue
    }
    if (same.ok !== equal) fail(`${a} and ${b} are ${equal ? '' : 'not '}one type, but the graph says the opposite`)
    const forward = accepts(a, b)
    const backward = accepts(b, a)
    if (forward.ok !== ab) fail(`a value of ${a} ${ab ? 'is refused' : 'is accepted'} where ${b} is written`, forward.out)
    if (backward.ok !== ba) fail(`a value of ${b} ${ba ? 'is refused' : 'is accepted'} where ${a} is written`, backward.out)
    if (equal && !(forward.ok && backward.ok)) fail(`${a} and ${b} are one type but not accepted both ways`)
}

rmSync(dir, { recursive: true, force: true })
console.log(`${checked} checks, ${failures} that fail`)
process.exit(failures === 0 ? 0 : 1)
