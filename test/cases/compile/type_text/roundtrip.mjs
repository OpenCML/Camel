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

// Declarations the spellings below refer to.
const PRELUDE = `
type Color = enum { Red, Green, Blue }
type Shape = enum { Circle(float), Rect { w: float, h: float }, Dot }
type Point = { x: int, y: int }
type Box<T> = { value: T }
type Pair<A, B> = { first: A, second: B }
type Option<T> = enum { Some(T), Empty }
`

const SPELLINGS = [
    // scalars
    'int', 'float', 'bool', 'string', 'Bytes', 'i32', 'i64', 'f32', 'f64', 'c64',
    // arrays: every form of extent
    'int[]', 'int[3]', 'int[3, 4]', 'int[?]', 'int[?, 4]', 'int[n]', 'int[n, 4]', 'int[n, m]', 'int[..]',
    'int[n, ..]', 'int[n, ..rest]', 'f32[n, 784]', 'int[][]', 'int[3][4]', 'string[]', 'bool[2, 2]',
    // arrays of everything
    '(int, string)[]', '(int,)[]', '{ a: int }[]', 'Point[]', 'Color[]', 'Box<int>[]', 'Set<int>[]',
    '(int?)[]', '(int?)[3]', '(int!)[]', '(int!)[2, 2]', '(int?!)[]',
    '((a: int) => int)[]', '((a: int) => int)[3]', '(sync (a: int) => int)[]',
    '((a: int) => int pure)[]', '((a: int) => int?)[]', '(((a: int) => int)?)[]',
    
    // tuples
    '()', '(int,)', '(int, string)', '(int, string, bool)', '((int, string), bool)', '((int,),)',
    '(int?, string!)', '(int[], int[3])', '((a: int) => int, int)', '(int..,)', '({ a: int },)',
    // structs, enums, named and generic types
    '{ a: int }', '{ a: int, b: string }', '{ a: { b: int } }', '{ a: int? , b: int! }', '{ f: (a: int) => int }',
    '{ xs: int[3], ys: (int?)[] }', 'Point', 'Color', 'Shape', 'Box<int>', 'Box<int[]>', 'Box<int?>',
    'Box<Box<int>>', 'Pair<int, string>', 'Option<int>', 'Option<(int, int)>', 'Box<(a: int) => int>',
    // nullable and fallible
    'int?', 'int!', 'int?!', 'int!?', 'int[]?', 'int[3]!', '(int, string)?',
    '{ a: int }?', 'Point?', 'Color!', 'Box<int>?', '((a: int) => int)?', '((a: int) => int)!',
    '(sync (a: int) => int)?', '((a: int) => int?)?', '(int..)?', '((int?)..)!', 'Bytes?', 'string!',
    // streams
    'int..', 'string..', 'int[]..', '(int?)..', '(int!)..', '(int, string)..', '{ a: int }..',
    '((a: int) => int)..', 'int..?', 'int..!', 'int[3]..', 'Box<int>..',
    // functions
    '() => int', '(a: int) => int', '(a: int, b: string) => bool', '(a: int[]) => int[]', '(a: int) => int[]',
    '(a: int) => int?', '(a: int) => int!', '(a: int) => (int?)[]', '(a: int) => int..', '(a: int) => (int, int)',
    '(a: int) => (int,)', '(a: int) => ()', '(a: int) => { b: int }', '(a: int) => (b: int) => int',
    '(a: int) => ((b: int) => int)?', '(f: (x: int) => int) => int', '(f: (x: int) => int, g: (y: int) => int) => int',
    '(a: Box<int>) => Box<int>', '(a: int?) => int?', '(a: (int?)[]) => (int!)[3]',
    '(var a: int) => int', 'sync (a: int) => int', '() => ()',
    '<w: int> (a: int) => int', '<w: int, v: string> (a: int) => int', '<w: int> () => int',
    '<var w: int> (a: int) => int', '<w: int[]> (a: int[]) => int',
    '(a: int) => int pure', '() => int pure', '(a: int[]) => int[] pure', '((a: int) => int uses a)',
    '((a: int, b: int) => int uses a)', '((a: int, b: int) => int uses a, b)', '((a: int) => int uses stdout)',
    '((a: int) => int uses fs)', '((a: int) => int uses stdout, a)',
    'sync (a: int) => int pure', '(sync (a: int) => int uses stdout)',
    '<w: int> (a: int) => int pure', '(<w: int> (a: int) => int uses a)',
    // collections
    'Map<string, int>', 'Map<int, string[]>', 'Map<string, (int?)>', 'Map<string, Map<string, int>>',
    'Set<int>', 'Set<string>', 'Set<(int, int)>', 'Map<string, int>?', 'Set<int>!',
    'Map<string, (a: int) => int>',
    '(a: int) => ((b: int) => int) pure', '(a: int) => ((b: int) => int uses b)', '(((a: int) => int uses a), int)',
    '{ f: ((a: int) => int uses a), g: int }', 'Box<((a: int) => int uses a)>', 'Map<string, ((a: int) => int uses stdout)>',
    '(f: ((a: int) => int uses a), n: int) => int', '(((a: int) => int uses a))?', '(((a: int) => int uses a))[]',
    // typeof-free nesting stress
    '((a: int) => (int?)[])[]', '((int?)[])?', '((int?)[3])!', '(Box<int>?)[]', '(Pair<int?, int!>)[]',
    
]

const dir = mkdtempSync(join(tmpdir(), 'camel-type-text-'))
let file = 0

function run(source) {
    const path = join(dir, `t${file++}.cml`)
    writeFileSync(path, source)
    const result = spawnSync(camel, [path], { encoding: 'utf8', timeout: 60000 })
    return { status: result.status, out: (result.stdout ?? '') + (result.stderr ?? '') }
}

const PRINT = `
import { current, graphs, name, nodes, type_of, type_name } from graph
import { Graph, Node, TypeRef } from graph

func firstType(g: Graph): TypeRef? {
    return type_of(nodes(g)[0])
}

func graphNamed(label: string): Graph {
    let found = graphs(current())->filter<(g: Graph): bool => name(g) == label>
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
