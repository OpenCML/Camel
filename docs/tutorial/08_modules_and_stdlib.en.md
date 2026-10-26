# Chapter 8 · Modules, the standard library, and a small tool

[中文](08_modules_and_stdlib.cn.md) | English · [Back to the contents](tutorial.en.md)

Up to chapter 7 every line of code lived in one file. A real program uses what someone else wrote: it reads a file,
cuts a piece of text up, writes the result as JSON. This chapter covers splitting code across files, what the
standard library holds, and then **finishing the wordcount tool that part one has been building towards**.

## A file is a module

**There is no `module` declaration in Camel**: a `.cml` file is a module, and the module's name is the file's name —
`geometry.cml` is the module `geometry`. To let the outside see something, write `export`:

[`examples/geometry.cml`](examples/geometry.cml):

```cml
// A library module used by 08_modules.cml: a file is a module, and its name is the file's name.
// Run: camel docs/tutorial/examples/08_modules.cml

import { sqrt } from math

export type Vec = { x: float, y: float }

export func hypot(v: Vec): float {
    return sqrt(v.x * v.x + v.y * v.y)
}

export func area(w: float, h: float): float {
    return w * h
}
```

The side that uses it has two spellings, and
[`examples/08_modules.cml`](examples/08_modules.cml) shows both:

```cml
// Importing from a standard module and from a file next to this one.
// Run: camel docs/tutorial/examples/08_modules.cml

import { sqrt } from math
import { hypot, Vec } from geometry
import geometry

func main(): int sync {
    println(sqrt(2.0))
    let v: Vec = { x: 3.0, y: 4.0 }
    println(hypot(v))
    println(geometry::area(2.0, 5.0))
    return 0
}
```

```bash
camel docs/tutorial/examples/08_modules.cml
```

```text
1.4142135623730951
5.0
10.0
```

- **`import { sqrt } from math`** brings in only the names written out; afterwards `sqrt(...)` is used directly.
- **`import geometry`** brings in the whole module and `::` says where the name comes from: `geometry::area(...)`.
  That spelling is safer when names collide, and a reader does not have to go looking for where `area` lives.
- Modules are resolved at **compile time**: from a file in the same directory (or under a root given by
  `CAMEL_PACKAGES`). `export` decides which names the outside sees; a name without `export` is the module's own
  business.

## The standard library is a set of modules

`math`, `str`, `fs`, `json`, `array`, `map`, `tensor` and the rest ship with Camel and are written exactly like a
module you write yourself. This chapter uses three:

- [`fs`](../stdlib-reference.md#fs): `path->read_text` reads a text file (a failure is an error value), `text->write_text<path>` writes one.
- [`str`](../stdlib-reference.md#str): `split<' '>` cuts on a separator, `to_lower` and `trim` tidy up words.
- [`json`](../stdlib-reference.md#json): `encode(value)` writes a value as one line of JSON, `decode(text)` reads
  text back.

The whole list, with every signature, is the [standard library reference](../stdlib-reference.md).

## Finishing the wordcount

Chapter 6's `tally` (a fold over a map), plus reading a file and cutting the text up, is the tool of part one:
[`examples/08_wordcount.cml`](examples/08_wordcount.cml). The path comes from `--arg`, which is a parameter of
`main`:

```cml
// The wordcount tool of part one: read a text, count the words, print the counts as JSON.
// Run: camel docs/tutorial/examples/08_wordcount.cml --arg path=docs/tutorial/examples/08_words.txt

import { read_text } from fs
import { split, to_lower, trim } from str
import { encode } from json

func words_of(text: string): string[] {
    return text->split<' '>->map<(w: string): string => to_lower(trim(w))>->filter<(w: string): bool => w != ''>
}

func tally(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(path: string): int sync {
    let words = words_of(path->read_text !! '')
    let counts = tally(words)
    println(len(words))
    println(encode(counts))
    return 0
}
```

```bash
camel docs/tutorial/examples/08_wordcount.cml --arg path=docs/tutorial/examples/08_words.txt
```

```text
9
{"brown":1,"dog":1,"fox":2,"lazy":1,"quick":1,"the":3}
```

Three parts and that is all of it: read it in, work it out, write it out. **The smallest tool that runs is shorter
than you would think** — which is the point of part one: once types, functions, branches, arrays, maps and failure
handling are each in place, what is left is joining them up.

The path is not written into the code; it is a parameter of `main`. **`main` may take parameters**: `camel xxx.cml
--arg path=...` reads the value at the declared type (a `string` parameter is that piece of text itself), what `main`
returns is the result of the program, and `-o` writes it to a file. All of that is in
[the command line](../cli.md) chapter.

## On the graph

- `import` is a **compile-time** matter: there is no such thing as a module boundary on the graph. A function that is
  used becomes a `CALL` node, and which file its name came from does not change the graph's shape.
- `export` is only **visibility**: it decides whether you can write that name in another file, and it produces
  nothing at run time.
- So splitting into files is an arrangement for readers, the same as splitting into functions, and the graph is that
  same graph. Chapter 19 shows the other side of this: a module can be used as a subgraph whole.

## Exercise

Wrap the tool up as `tally(path)`: given a path it gives the word counts; then print how many different words there
are and how often the most common one (`fox`) occurs.

The answer is [`examples/08_exercise.cml`](examples/08_exercise.cml):

```cml
// The answer to the exercise of chapter 8: the same tool, with the reading in one function.
// Run: camel docs/tutorial/examples/08_exercise.cml --arg path=docs/tutorial/examples/08_words.txt

import { read_text } from fs
import { split, to_lower, trim } from str
import { encode } from json

func words_of(text: string): string[] {
    return text->split<' '>->map<(w: string): string => to_lower(trim(w))>->filter<(w: string): bool => w != ''>
}

func tally(path: string): Map<string, int> {
    let words = words_of(path->read_text !! '')
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(path: string): int sync {
    let counts = tally(path)
    println(len(keys(counts)), counts['fox'] ?? 0, encode(counts))
    return 0
}
```

```bash
camel docs/tutorial/examples/08_exercise.cml --arg path=docs/tutorial/examples/08_words.txt
```

```text
6 2 {"brown":1,"dog":1,"fox":2,"lazy":1,"quick":1,"the":3}
```

## References

- [Language reference 5 Modules and the standard library](../language/en/05_modules.md)
- [Standard library reference](../stdlib-reference.md) ([`fs`](../stdlib-reference.md#fs),
  [`str`](../stdlib-reference.md#str), [`json`](../stdlib-reference.md#json))
- [The command line](../cli.md) (`--arg`, `-o`, standard output)