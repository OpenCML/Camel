// The type corpus the type tests share: source spellings that walk every type constructor, and the
// declarations they refer to.

// Declarations the spellings below refer to.
export const PRELUDE = `
type Color = enum { Red, Green, Blue }
type Shape = enum { Circle(float), Rect { w: float, h: float }, Dot }
type Point = { x: int, y: int }
type Box<T> = { value: T }
type Pair<A, B> = { first: A, second: B }
type Option<T> = enum { Some(T), Empty }
`

export const SPELLINGS = [
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
