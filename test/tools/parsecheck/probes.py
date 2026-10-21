#!/usr/bin/env python3
"""Writes the probe programs of the parser harness: one small file per place where the grammar
is ambiguous or a decision needs more than one token (docs/technical/34_handwritten_parser.md,
section 4).

    python3 test/tools/parsecheck/probes.py DIR      # writes DIR/probe_NNN.cml and DIR/list.txt

Each probe is a statement or a declaration; statements are wrapped in a function.
"""
import sys
from pathlib import Path

STATEMENTS = [
    # `{` as a statement, a branch arm, a lambda body
    "{}", "{ }", "{ a: 1 }", "{ ...p }", "{ a }", "{ a; b }", "{ { a: 1 } }",
    "wait {}", "wait { a: 1 }", "wait { a }", "wait sync { a }", "sync { a }",
    "let v = if c then {} else {}", "let v = if c then { a: 1 } else { b }",
    "let f = () => {}", "let f = () => { a: 1 }", "let f = (): int => { a }",
    "let v = match x { case 1 => {} case _ => { a: 1 } }",
    # lambda or tuple
    "let t = ()", "let t = (,)", "let t = (a)", "let t = (a,)", "let t = (a, b)",
    "let f = (a: int) => a", "let f = (var a: int) => a", "let f = (): int => 1",
    "let f = (,) => 1", "let f = <T: type>(a: T) => a", "let f = sync (a: int) => a",
    "let f = (a: int): (b: int) => int => a", "let f = (): () => int => g",
    "let f = (a: int = 1, b: int = (2)) => a", "let s = xs[():1]",
    # arguments
    "f()", "f(,)", "f(a)", "f(a,)", "f(a, b)", "f(...a)", "f(a, ...b)", "f(k: 1)",
    "f(a, k: 1)", "f(a, k: 1, ...b)", "f(...a, k: 1)", "f(k: 1, j: 2,)", "f<a>(b)",
    "f<>(b)", "f<,>(b)", "f<k: 1>(b)", "f<a, k: 1>()",
    # postfix and adjacency
    "f (a)", "xs [0]", "f(a)(b)[c]<d>.e.0", "t.0.1", "t.0.1.2", "f(a).0.1", "a!", "a !b",
    "f<a>!", "x.y.z(1).w", "xs[1:2]", "xs[1:2:3]",
    # angle brackets
    "let v = a < b", "let v = a > b", "let v = a<b>", "let v = a<b<c>>", "let v = a<b<c>>(d)",
    "let v = f<a > b>(c)", "let v = f<(a > b)>(c)", "let v = f<a, b>(c) > g<d>(e)",
    "let v = xs->filter<(x: int): bool => x > 1>", "let v = xs->map<(x: int) => x>->len",
    "println(f<a>(m), g<b>(m))", "let v = a < b > c", "let v = a >= b", "let v = f<a>= b",
    "let v = (a < 2, f<b>)", "let v = x<y>(z) < w",
    # literals and signs
    "let v = -1", "let v = - 1", "let v = +1", "let v = -a", "let v = a-1", "let v = a -1",
    "let v = a - -1", "let v = -1->f", "let v = -a->f", "let v = -2 ^ 2", "let v = 2 * -3",
    "let v = !a", "let v = ~a", "let v = !a as bool", "let v = -a as int", "let v = [-1, +2]",
    "let v = f(-1)", "let v = 1e5 + 0x1f + 0b11 + 0o17 + 1.5e-3",
    "let v = -a ^ 2", "let v = 2 ^ -1", "let v = -a ^ -b ^ 2", "let v = -a * b", "let v = - -a",
    "let v = -2->f ^ 2", "let v = -9223372036854775808", "let v = -(2)", "let v = !a->f",
    "let v = a & b | c & d", "let v = a == b & c == d || e | f && g", "let v = a | b ?? c",
    # operators
    "let v = a ?? b ?? c", "let v = a !! b + c", "let v = a * b ?? c + d", "let v = a ?? b = c",
    "let v = a -> f -> g", "let v = a ?-> f", "let v = f .. g", "let v = f ?.. g",
    "let v = a + b * c ^ d @ e % f / g", "let v = a || b && c == d != e === f !== g",
    "let v = a < b <= c >= d", "a = b", "a += b", "a @= b", "a |= b", "a &= b",
    "let v = wait f(a)", "let v = a ?? if c then 1 else 2", "let v = a ?? wait b",
    "let v = if a then if b then 1 else 2", "let v = if a then 1 else if b then 2 else 3",
    # casts and types in expressions
    "let v = a as int", "let v = a is int", "let v = a as int + 1", "let v = a as int + b",
    "let v = a as int - b", "let v = a as int ^ 2", "let v = a as int ^ b",
    "let v = a as int | string", "let v = a as int?", "let v = a as int? ?? 0",
    "let v = a as int!", "let v = a as int?!", "let v = a as int ? string",
    "let v = a as int[]", "let v = a as int[][]", "let v = a as Map<K, V>",
    "let v = a as (int)", "let v = a as (int, string)", "let v = a as (x: int) => int",
    "let v = a as { x: int }", "let v = a as int < 3", "let v = a as int<3>",
    "let v = a as T<const 1, U>", "let v = a as k of T", "let v = a as typeas T",
    # types in declarations
    "let v: int = 1", "let a, b = 1, 2", "let (a, b) = t", "let { a, b } = s",
    "let [a, b] = xs", "let (a,) = t", "let v: int? = null", "let v: int! = e",
    "let v: () = t", "let v: (int) = 1", "let v: (int,) = t", "let v: () => int = f",
    "let v: (x: int) => int = f", "let v: sync (x: int) => int = f", "let v: A | B & C = x",
    "let v: A \\ B = x", "let v: A + B - C ^ D = x", "let v: { x: int, y: T[] } = s",
    "let v: typeof a = b", "let v: <T: type>(x: T) => T = f", "let a: int, b: string = 1, 's'",
    # control
    "let v = match x { case 1 | 2 => a case -1 => b case 'k' => c case _ => d }",
    "let v = match x { case error e => a case v => b case null => c }",
    "let v = match x { case (a, b) => 1 case { a, b } => 2 case { a: 1 } => 3 case () => 4 }",
    "let v = try { a } catch e: E { b } finally { c }",
    "let v = [x for x in xs if c]", "let v = [x for x in xs]", "let v = [...xs, 1,]",
    "let v = [1,]", "let v = []", "let v = { a: 1, ...b, }",
    "return a, b", "throw e", "use a = b::c", "use b::c", "using namespace a::b",
    "type P = (int, int)", "inner type Q = 'uri'", "type P<T> = (T, T)", "type P<T>= T",
    "type P<A, B,> = A", "let v = raise", "let a = 1 let b = 2", "a b", "a; b",
    # `fields` and `match type` are followed by a type position: a `(` there is a type, so `()`
    # is the empty tuple type and a function type takes one more pair of parentheses (H79 ruling 1).
    "static for (n, F) in fields () => {}",
    "let v = match type () { case tuple => 1 case _ => 2 }",
    "static for (n, F) in fields ((x: int) => int) => {}",
    "let v = match type ((x: int) => int) { case #leaf => 1 case _ => 2 }",
]

# Not here: `import {} from a` and `export {}`, on which the ANTLR-based builder dereferences a
# missing identifier list and crashes; the hand-written front end builds an import without names.
DECLARATIONS = [
    "module m", "import a", "import a.b.c", "import .a", "import ..a.b", "import ...a",
    "import x from a.b", "import { x, y } from a", "import { x, } from a",
    "export { a, b }", "export let a = 1", "export type T = int", "export inner type T = 'u'",
    "export func f() {}", "export inner func f() {}", "inner func f() {}",
    "with <T: type> func f(a: T): T { return a }", "with <k: int = 1> export sync func f() {}",
    "@a func f() {}", "@a @b<1> func f() {}", "@a::b<k: 1> with <T: type> macro func f() {}",
    "func f(): int? {}", "func f(): int? { return 1 }", "func f(): int sync { return 1 }",
    "func f(): int! sync {}", "func f(): (x: int) => int { return g }",
    "func f(a: int, var b: T[] = [],): () { }", "atomic shared func f() {}",
    "namespace a::b { func f() {} type T = int; namespace c { use x } }",
    "namespace a { import b; export { c } using namespace d }",
    "let a = 1; let b = 2", ";let a = 1", "type T = int type U = T",
]


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for i, stmt in enumerate(STATEMENTS):
        text = "func probe() {\n    " + stmt + "\n}\n"
        paths.append((out / f"probe_s{i:03d}.cml", text))
    for i, decl in enumerate(DECLARATIONS):
        paths.append((out / f"probe_d{i:03d}.cml", decl + "\n"))
    with open(out / "list.txt", "w", encoding="utf-8", newline="\n") as listing:
        for path, text in paths:
            path.write_text(text, encoding="utf-8", newline="\n")
            listing.write(str(path).replace("\\", "/") + "\n")
    print("probes:", len(paths))


if __name__ == "__main__":
    main()
