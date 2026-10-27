// Every type the compiler prints is a spelling a program can write, and it reads back as the same
// type (design principles 5.1: a diagnostic and a graph use the type names of the source).
//
// For each source spelling S of a type, the type is built from `func probe(x: S)`, printed with
// `graph.type_name`, and the printed text P is written back as `func again(x: P)` in a program
// that also holds `probe`: the program must compile, and the two parameters must have the same
// type (`graph` compares types by identity). The spellings walk every type constructor.
//
//   node roundtrip.mjs --camel <path to camel> [--only <substring>]

import { spawnSync } from 'node:child_process'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const args = process.argv.slice(2)
const option = (name) => {
    const at = args.indexOf(name)
    return at >= 0 ? args[at + 1] : undefined
}
const camel = option('--camel') ?? 'camel'
const only = option('--only')

import { PRELUDE, SPELLINGS } from './corpus.mjs'

const dir = mkdtempSync(join(tmpdir(), 'camel-type-text-'))
let file = 0

function run(source) {
    const path = join(dir, `t${file++}.cml`)
    writeFileSync(path, source)
    const result = spawnSync(camel, [path], { encoding: 'utf8', timeout: 60000 })
    return { status: result.status, out: (result.stdout ?? '') + (result.stderr ?? '') }
}

const PRINT = `
import { graphs_of, name, nodes, type_of, type_name } from graph
import { Graph, Node, TypeRef } from graph

func firstType(g: Graph): TypeRef? {
    return type_of(nodes(g)[0])
}

func graphNamed(label: string): Graph {
    let found = graphs_of(reach)->filter<(g: Graph): bool => name(g) == label>
    return found[0]
}
`

function program(spelling, again) {
    return `${PRELUDE}
${PRINT}
func probe(x: ${spelling}, y: ${spelling}): int {
    return 0
}
${again === undefined ? '' : `func again(x: (${again}), y: (${again})): int {
    return 0
}`}
/// The graphs the probe reads are the ones this function reaches: it names them without calling them.
func reach(): int {
    let a = probe${again === undefined ? '' : '\n    let b = again'}
    return 0
}
func main(): int sync {
    let first = firstType(graphNamed('probe'))
    println(match first {
        case null => '<none>'
        case t => type_name(t)
    })
${again === undefined ? '' : `    let second = firstType(graphNamed('again'))
    println(match first {
        case null => false
        case a => match second {
            case null => false
            case b => a == b
        }
    })`}
    return 0
}
`
}

let failures = 0
let checked = 0
for (const spelling of SPELLINGS) {
    if (only && !spelling.includes(only)) continue
    checked++
    const first = run(program(spelling))
    if (first.status !== 0) {
        failures++
        console.log(`FAIL  ${spelling}\n      the spelling itself does not compile:\n      ${first.out.split('\n').slice(0, 3).join('\n      ')}`)
        continue
    }
    const printed = first.out.split('\n')[0]
    const second = run(program(spelling, printed))
    if (second.status !== 0) {
        failures++
        console.log(`FAIL  ${spelling}\n      printed as: ${printed}\n      which does not compile:\n      ${second.out.split('\n').slice(0, 3).join('\n      ')}`)
        continue
    }
    const lines = second.out.split('\n')
    if (lines[1] !== 'true') {
        failures++
        console.log(`FAIL  ${spelling}\n      printed as: ${printed}\n      which reads back as another type`)
        continue
    }
}

rmSync(dir, { recursive: true, force: true })
console.log(`${checked} spellings, ${failures} that do not round-trip`)
process.exit(failures === 0 ? 0 : 1)
