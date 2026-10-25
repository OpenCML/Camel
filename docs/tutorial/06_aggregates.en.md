# Chapter 6 · Structs, tuples, maps and sets

[中文](06_aggregates.cn.md) | English · [Back to the contents](tutorial.en.md)

Up to chapter 5 the values in a program are loose: one number, one string, one array. To model a real thing — a
record, a table from keys to values, a pile of items with the duplicates removed — a program has to be able to
**put several values together into one**. This chapter covers four ways of doing that, and one rule that makes them
comfortable to use: `==` compares contents, not places.

## Structs and tuples: giving data names

The first half of [`examples/06_collections.cml`](examples/06_collections.cml):

```cml
// Structs, tuples, Map and Set: the shapes a program's data takes.
// Run: camel docs/tutorial/examples/06_collections.cml

type Point = { x: int, y: int }

func main(): int sync {
    // A struct names its fields; a tuple is positional and carries no names.
    let a: Point = { x: 3, y: 4 }
    println(a, a.x + a.y)
    let pair = (1, 'one')
    println(pair, pair.0, pair.1)

    // Map<K, V> and Set<T> are built in; both are made from an array with `as`.
    let ages = [('bob', 31), ('alice', 28)] as Map<string, int>
    println(ages)                              // printed in key order
    println(ages['alice'] ?? 0, ages['carol'] ?? 0)

    let more = ages->put<'carol', 45>->remove<'bob'>
    println(more, len(more))
    println(ages)                              // unchanged: every operation returns a new value
    println(keys(more), values(more))

    let seen = [3, 1, 2, 3, 1] as Set<int>
    println(seen, seen->contains<2>, seen->contains<9>)
    println(union(seen, [9] as Set<int>), intersect(seen, [1, 9] as Set<int>))
    return 0
}
```

```bash
camel docs/tutorial/examples/06_collections.cml
```

```text
{ x: 3, y: 4 } 7
(1, one) 1 one
{alice: 28, bob: 31}
28 0
{alice: 28, carol: 45} 2
{alice: 28, bob: 31}
[alice, carol] [28, 45]
{1, 2, 3} true false
{1, 2, 3, 9} {1}
```

- **A struct is read by field name**: `a.x`, `a.y`. Its type gets a name first (`type Point = ...`), it is built as
  `{ x: 3, y: 4 }`, and every field has to be written.
- **A tuple is read by position**: `pair.0`, `pair.1`. It has no names, which suits "put two values together for a
  moment".
- Both nest, and both go inside arrays and maps — `[('bob', 31), ('alice', 28)]` is an array of tuples.

## Maps and sets: keys to values, and items without duplicates

`Map<K, V>` and `Set<T>` are built-in types, made from an array with `as`:

```cml
let ages = [('bob', 31), ('alice', 28)] as Map<string, int>
let seen = [3, 1, 2, 3, 1] as Set<int>          // {1, 2, 3}
```

- Reading a key is `m['alice']` and what comes back is an `int?` — the key may not be there. `?? 0` supplies a
  substitute; chapter 7 is about that `?`.
- `put`, `remove` and `contains` **return a new value**, and the map they were given does not change: `more` is the
  changed one and `ages` is still what it was. This is chapter 2's `let` again: a value is not changed behind your
  back, the change is written in what comes back.
- A map **prints in key order**, and `keys` and `values` come out in that order too: the same data prints the same
  way every time, which is what makes tests and comparisons trustworthy.
- A set removes duplicates, and `union` and `intersect` give the union and the intersection.

## `==` compares contents

Two values are equal when what they hold is equal, not when they are the same place:

[`examples/06_equality.cml`](examples/06_equality.cml):

```cml
// `==` compares two values by what they hold, not by where they are.
// Run: camel docs/tutorial/examples/06_equality.cml

type Point = { x: int, y: int }

func main(): int sync {
    let a: Point = { x: 3, y: 4 }
    let b: Point = { x: 3, y: 4 }
    println(a == b)
    println((1, 'one') == (1, 'one'))
    println([1, 2] == [1, 2])
    println('ab' == 'ab')
    return 0
}
```

```bash
camel docs/tutorial/examples/06_equality.cml
```

```text
true
true
true
true
```

The four `true`s all say the same thing: `a` and `b` are two structs built separately, their contents are equal, so
they are equal. Asking whether they are the same piece of data is the other operator, `===`, which chapter 13 uses
on arrays.

## On the graph

- Building a struct or a tuple is **one node** on the graph whose inputs are the field values. Reading a field
  (`a.x`) is a node too, whose input is the struct. So "build a struct and read one field back" can be seen through
  and simplified on the graph.
- Building a map or a set, `put` and `contains` are each one node as well; they return a new value, so the graph has
  no "map that was changed" state — every change is a new path.
- That is also why `==` can be a pure computation: the equality node only reads the contents of its two inputs.

## Exercise

Count how often each word occurs in `['fig', 'pear', 'fig', 'apple', 'fig']`. Start from an empty map and, for each
word, add one to what the map says — this `counts` is the heart of the wordcount tool of part one, which chapter 8
finishes.

The answer is [`examples/06_exercise.cml`](examples/06_exercise.cml):

```cml
// The answer to the exercise of chapter 6: counting with a map is a fold over `put`.
// Run: camel docs/tutorial/examples/06_exercise.cml

func tally(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(): int sync {
    let counts = tally(['fig', 'pear', 'fig', 'apple', 'fig'])
    println(counts, counts['fig'], counts['cherry'] ?? 0)
    return 0
}
```

```bash
camel docs/tutorial/examples/06_exercise.cml
```

```text
{apple: 1, fig: 3, pear: 1} 3 0
```

## References

- [Language reference 3.3 Compound values](../language/en/03_data_model.md#33-compound-values) and
  [3.6 Maps and sets](../language/en/03_data_model.md#36-maps-and-sets)
- [Language reference 6.7.3 Comparison](../language/en/06_expressions.md#673-comparison) (`==` and `===`)