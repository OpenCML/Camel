"""Random Camel programs with a reference output, from a seed.

`generate(seed, features)` returns a program tree (model.N) whose reference evaluation
succeeded, together with the lines it must print. Everything is drawn from one
`random.Random` seeded with the seed and the feature set, so a (seed, features) pair always
gives the same program.

Features switch constructs on and off (see FEATURES); `--features` of progen.py takes a
comma-separated list, with `-name` to remove one from the default set.

Every construct here was first run by hand on the node VM without passes and compared with
what model.py computes for it; constructs that turned out not to work as documented are not
generated (README.md lists them).
"""

from __future__ import annotations

import random
from typing import List, Optional

from model import (ARR, BOOL, FLOAT, FN, INT, MAX_HEAVY, N, OPT, REC, STR, TUP, Reject, effect_violations,
                   evaluate, heavy_graphs, static_faults, walk)

FEATURES = {
    "float": "float arithmetic (exact values only), float(int), int(float), precision placeholders",
    "string": "string literals, concatenation, len, string(int), string(bool), format, comparisons, +=, join",
    "array": "array literals, index, len, concat, slice, contains",
    "tuple": "tuple literals and .N access",
    "struct": "struct types, literals and field access",
    "var": "var declarations and assignments in sync blocks",
    "varparam": "functions with var parameters (sync func), writes through them",
    "capture": "lambdas and nested functions that read or write captured variables",
    "if": "if expressions, block arms, if statements with effects in the arms",
    "match": "match on an int, string or bool name, with literal cases",
    "func": "user functions and calls",
    "rec": "bounded recursion: tail, non-tail, tree, mutual, and recursion that writes a var",
    "closure": "function values: lambdas, named functions as values, functions returning lambdas",
    "hof": "user functions that take function parameters",
    "builtin": "map, filter, reduce, foreach, zip, range",
    "effects": "println in functions with sync bodies, effects in branch and match arms",
    "wait": "wait before writes and calls that write",
    "env": "values read from the environment (unknown to every pass)",
    "macro": "pure functions declared macro",
    "comprehension": "list comprehensions [e for x in xs if c] with pure element and condition",
    "nullable": "T?: null, values that enter a T? position (argument, typed let, branch arm, return), ?? chains",
    "partial": "functions with context (with) parameters, called in place (f<w>(x)) and partially applied (f<w> as a value)",
    "ifnoelse": "if statements without else whose arm is one expression of any type",
    "logic": "&& and || with effects on the right side (branches: the right side runs only when needed), & and | (both sides run)",
    "narrowing": "x == null / x != null as bool expressions, and the arm of if x != null (also joined by &&) where x is a T",
    "modules": "top-level values written in any order, and the program split into three modules "
               "(values and types; functions that call only each other; the rest), imported by "
               "name; not in the default set",
    "append": "xs->append<v> (a new array, xs unchanged) and acc = acc->append<v> (rebinding a variable, also in foreach element functions)",
    "loop": "loops (design principles 2.13): `while` with a counter, `for` over an array, `for ... while`, one or several values of state, struct and tuple states, and loops whose body is a sync block with effects",
    "opover": "operator overloads of the program (func operator +, unary -, *, <, []) on the struct P, next to the built-in operators",
}
# `modules` is left out of the default set: the feature set is part of every program's random
# stream, and the recorded seeds must keep naming the same programs. `--features all,modules`.
DEFAULT_FEATURES = frozenset(FEATURES) - {"modules"}

# Part of the random stream: a change of the generator that changes the programs of existing
# seeds gets a new number, so that "seed N" of an old report is not silently another program.
VERSION = 6


def parse_features(text: Optional[str]) -> frozenset:
    if not text:
        return DEFAULT_FEATURES
    names = [x.strip() for x in text.split(",") if x.strip()]
    base = set(DEFAULT_FEATURES) if all(x.startswith("-") for x in names) else set()
    for x in names:
        name = x.lstrip("-+")
        if name == "all":
            base |= DEFAULT_FEATURES
            continue
        if name not in FEATURES:
            raise SystemExit(f"unknown feature {name!r}; known: {', '.join(sorted(FEATURES))}")
        if x.startswith("-"):
            base.discard(name)
        else:
            base.add(name)
    return frozenset(base)


class Sym:
    """A name in scope. kind: 'let' (immutable: let, parameter), 'var', 'fn' (a declared
    function). `eff`: calling it has effects. For a function: `params` [(name, type, isvar)],
    `ret`, `rec` (index of the depth parameter, largest depth) and `ret_eff` (the function it
    returns has effects)."""

    __slots__ = ("name", "t", "kind", "eff", "params", "ret", "rec", "ret_eff", "value", "shown")

    def __init__(self, name, t, kind, eff=False, params=None, ret=None, rec=None, ret_eff=False, value=None):
        self.name, self.t, self.kind, self.eff = name, t, kind, eff
        self.params, self.ret, self.rec, self.ret_eff, self.value = params, ret, rec, ret_eff, value
        self.shown = False


def lit(t, v):
    return N("lit", t=t, v=v)


def ref(s: Sym):
    return N("ref", t=s.t, name=s.name)


def bin_(op, a, b, t):
    return N("bin", [a, b], t=t, op=op)


WORDS = ["a", "b", "ab", "cd", "x", "yz", "", "k9", "pq", "m"]
FLOATS = [0.5, 1.5, 2.0, 0.25, -1.5, 3.0, 4.75, -0.5, 1.0, 10.0, 0.0, 2.5]
INTS = [0, 1, 2, 3, 4, 5, 7, 10, -1, -2, -3, -7, 12, 100, 31]
SCALARS = (INT, FLOAT, BOOL, STR)


class Gen:
    def __init__(self, rng: random.Random, features: frozenset, size: int = 1):
        self.r = rng
        self.f = features
        self.size = size            # multiplies the number of functions and of statements of `main`
        self.scope: List[Sym] = []
        self.counter = 0
        self.types = {}
        self.env = {}
        self.funcs: List[N] = []
        self.label = 0
        self.operators = False     # the overloads of operator_overloads() are declared
        if "struct" in features:
            self.types["P"] = [("x", INT), ("y", INT)]
            second = [("n", INT)]
            second.append(("tag", STR) if "string" in features else ("on", BOOL))
            if "float" in features and self.r.random() < 0.5:
                second.append(("w", FLOAT))
            self.types["Q"] = second

    # -- helpers ----------------------------------------------------------------------------
    def on(self, name: str) -> bool:
        return name in self.f

    def fresh(self, prefix: str) -> str:
        self.counter += 1
        return f"{prefix}{self.counter}"

    def chance(self, p: float) -> bool:
        return self.r.random() < p

    def pick(self, options):
        """options: [(weight, thunk)]"""
        total = sum(w for w, _ in options)
        x = self.r.random() * total
        for w, thunk in options:
            x -= w
            if x <= 0:
                return thunk()
        return options[-1][1]()

    def scalars(self):
        out = [INT, INT, BOOL]
        if self.on("float"):
            out.append(FLOAT)
        if self.on("string"):
            out += [STR, STR]
        return out

    def scalar(self):
        return self.r.choice(self.scalars())

    def rand_type(self, composite: float = 0.4):
        if not self.chance(composite):
            return self.scalar()
        opts = []
        if self.on("array"):
            opts += [lambda: ARR(self.scalar())] * 3
            if self.on("tuple"):
                opts += [lambda: ARR(TUP(self.scalar(), self.scalar()))] * 2
            if self.on("struct"):
                opts.append(lambda: ARR(REC("P")))
        if self.on("tuple"):
            opts += [lambda: TUP(*[self.scalar() for _ in range(self.r.choice([2, 2, 3]))])] * 2
            if self.on("array"):
                opts.append(lambda: TUP(self.scalar(), ARR(self.scalar())))
        if self.on("struct"):
            opts += [lambda: REC("P"), lambda: REC("Q")]
        return self.r.choice(opts)() if opts else self.scalar()

    def fn_type(self):
        return FN([self.scalar() for _ in range(self.r.choice([1, 1, 2]))], self.scalar())

    def fields(self, t):
        return self.types[t[1]]

    def values(self, t=None, kinds=("let", "var")):
        return [s for s in self.scope if s.kind in kinds and not s.eff and (t is None or s.t == t)]

    # -- leaves -----------------------------------------------------------------------------
    def leaf(self, t):
        k = t[0]
        if k == "fn":
            return self.fn_expr(t, 0)
        cands = self.values(t)
        if cands and self.chance(0.6 if t in SCALARS else 0.8):
            return ref(self.r.choice(cands[-8:]) if self.chance(0.7) else self.r.choice(cands))
        if t == INT:
            return lit(INT, self.r.choice(INTS))
        if t == FLOAT:
            return lit(FLOAT, self.r.choice(FLOATS))
        if t == BOOL:
            return lit(BOOL, self.chance(0.5))
        if t == STR:
            return lit(STR, self.r.choice(WORDS))
        if k == "arr":
            return N("arr", [self.leaf(t[1]) for _ in range(self.r.choice([1, 2, 2, 3, 4]))], t=t)
        if k == "tup":
            return N("tup", [self.leaf(x) for x in t[1]], t=t)
        if k == "rec":
            fs = self.fields(t)
            return N("rec", [self.leaf(ft) for _, ft in fs], t=t, fields=[f for f, _ in fs])
        raise ValueError(t)

    def small(self, most: int):
        """An int expression whose value is at most `most` (a recursion depth)."""
        known = [s for s in self.values(INT, ("let",)) if s.value is not None and 0 <= s.value <= most]
        if known and self.chance(0.35):
            return ref(self.r.choice(known))
        if self.chance(0.2):
            return bin_("%", self.expr(INT, 1), lit(INT, most + 1), INT)
        return lit(INT, self.r.randint(0, most))

    # -- pure expressions -------------------------------------------------------------------
    def expr(self, t, d: int):
        if t[0] == "fn":
            return self.fn_expr(t, d)
        if t[0] == "opt":
            return self.entering(t[1], d)
        if d <= 0 or self.chance(0.1):
            return self.leaf(t)
        k = t[0]
        o = [(1.0, lambda: self.leaf(t))]
        if self.on("if"):
            o.append((1.6, lambda: self.if_expr(t, d)))
        if self.on("match") and self.scrutinees():
            o.append((1.0, lambda: self.match_expr(t, d)))
        acc = self.accesses(t)
        if acc:
            o.append((2.0, lambda: self.r.choice(acc)()))
        calls = self.callables(t)
        if calls:
            o.append((3.0, lambda: self.call(self.r.choice(calls), d)))
        if self.on("builtin") and self.on("array") and t in SCALARS:
            o.append((0.9, lambda: self.reduce_expr(t, d)))
        if self.on("nullable") and k != "rec":
            o.append((1.1, lambda: self.coalesce(t, d)))
        if self.on("narrowing") and self.nullable_lets(t):
            o.append((1.2, lambda: self.narrowed_if(t, d)))
        applied = self.with_functions(ret=t)
        if applied:
            o.append((1.4, lambda: self.applied_call(self.r.choice(applied), d)))
        if self.operators:
            o += self.operator_options(t, d)
        if self.on("loop") and d > 0 and k not in ("opt",):
            o.append((0.9, lambda: self.loop_expr(t, d)))
        if t == INT:
            o.append((4.0, lambda: self.int_arith(d)))
            if self.on("array") or self.on("string"):
                o.append((1.2, lambda: N("conv", [self.expr(self.len_type(), d - 1)], t=INT, fn="len")))
            if self.on("float"):
                o.append((0.6, lambda: N("conv", [self.expr(FLOAT, d - 1)], t=INT, fn="int")))
        elif t == FLOAT:
            o.append((4.0, lambda: self.float_arith(d)))
            o.append((1.0, lambda: N("conv", [self.expr(INT, d - 1)], t=FLOAT, fn="float")))
        elif t == BOOL:
            o.append((4.0, lambda: self.compare(d)))
            o.append((2.0, lambda: self.logical(self.r.choice(["&&", "||"]), self.expr(BOOL, d - 1),
                                                lambda: self.expr(BOOL, d - 1))))
            if self.on("logic"):
                o.append((0.8, lambda: bin_(self.r.choice(["&", "|"]), self.expr(BOOL, d - 1), self.expr(BOOL, d - 1), BOOL)))
            if self.on("narrowing") and self.nullable_lets():
                o.append((0.8, lambda: self.null_test(self.r.choice(self.nullable_lets()), self.chance(0.5))))
                o.append((0.8, lambda: self.narrowed_and(d)))
            o.append((0.8, lambda: N("un", [self.expr(BOOL, d - 1)], t=BOOL, op="!")))
            if self.on("array"):
                o.append((0.8, lambda: self.contains(d)))
        elif t == STR:
            o.append((3.0, lambda: bin_("+", self.expr(STR, d - 1), self.expr(STR, d - 1), STR)))
            o.append((1.5, lambda: N("conv", [self.expr(INT, d - 1)], t=STR, fn="string")))
            o.append((0.5, lambda: N("conv", [self.expr(BOOL, d - 1)], t=STR, fn="string")))
            o.append((2.0, lambda: self.fmt(d)))
            if self.on("array"):
                o.append((0.6, lambda: N("join", [self.expr(ARR(STR), d - 1)], t=STR, sep=self.r.choice([",", "", "-"]))))
        elif k == "arr":
            o += self.array_options(t, d)
        elif k == "tup":
            o.append((3.0, lambda: N("tup", [self.expr(x, d - 1) for x in t[1]], t=t)))
        elif k == "rec":
            fs = self.fields(t)
            o.append((3.0, lambda: N("rec", [self.expr(ft, d - 1) for _, ft in fs], t=t, fields=[f for f, _ in fs])))
        return self.pick(o)

    def len_type(self):
        opts = []
        if self.on("array"):
            opts += [ARR(self.scalar()), ARR(INT)]
        if self.on("string"):
            opts.append(STR)
        return self.r.choice(opts)

    def int_arith(self, d):
        r = self.r.random()
        a = self.expr(INT, d - 1)
        if r < 0.5:
            e = bin_(self.r.choice(["+", "-"]), a, self.expr(INT, d - 1), INT)
        elif r < 0.7:
            e = bin_("*", a, lit(INT, self.r.choice([2, 3, -1, -2, 5, 10, 0, 1])), INT)
            if self.chance(0.3):
                e = bin_("*", a, self.expr(INT, d - 1), INT)
        elif r < 0.8:
            e = bin_("/", a, lit(INT, self.r.choice([1, 2, 3, -2, 4, 7, -1])), INT)
        elif r < 0.93:
            e = bin_("%", a, lit(INT, self.r.choice([2, 3, 5, 7, 10, 97, -3])), INT)
        else:
            return N("un", [a], t=INT, op="-")
        if self.chance(0.25):
            e = bin_("%", e, lit(INT, self.r.choice([7, 11, 100, 997])), INT)
        return e

    def float_arith(self, d):
        r = self.r.random()
        a = self.expr(FLOAT, d - 1)
        if r < 0.55:
            return bin_(self.r.choice(["+", "-"]), a, self.expr(FLOAT, d - 1), FLOAT)
        if r < 0.75:
            return bin_("*", a, lit(FLOAT, self.r.choice([2.0, 0.5, -1.0, 4.0, 1.5, 0.25])), FLOAT)
        if r < 0.9:
            return bin_("/", a, lit(FLOAT, self.r.choice([2.0, 4.0, 0.5, -2.0, 0.25])), FLOAT)
        return N("un", [a], t=FLOAT, op="-")

    def compare(self, d):
        ts = [INT, INT, INT]
        if self.on("float"):
            ts.append(FLOAT)
        if self.on("string"):
            ts.append(STR)
        t = self.r.choice(ts)
        ops = ["==", "!=", "<", "<=", ">", ">="]
        return bin_(self.r.choice(ops), self.expr(t, d - 1), self.expr(t, d - 1), BOOL)

    def contains(self, d):
        t = self.r.choice([INT, STR] if self.on("string") else [INT])
        return N("contains", [self.expr(ARR(t), d - 1), self.expr(t, d - 1)], t=BOOL)

    def fmt(self, d):
        n = self.r.choice([1, 2, 2, 3])
        parts, args = [self.r.choice(["", "v=", "<", "r:"])], []
        for i in range(n):
            t = self.r.choice(self.scalars())
            e = self.expr(t, d - 1)
            spec = "{}"
            if t == FLOAT and self.chance(0.5):
                spec = "{:.%df}" % self.r.choice([1, 2, 3])
            args.append(e)
            parts.append(spec + self.r.choice([" ", ",", "|", ">", ""]))
        return N("fmt", args, t=STR, s="".join(parts))

    # -- T? (design principles 2.10) ----------------------------------------------------------
    def null(self, t):
        return N("null", t=OPT(t))

    def nullable(self, t, d):
        """An expression whose own type is `t?`: what may stand on the left of `??`. (A plain
        value or a bare `null` may not: they only enter a position that expects a `t?`.)"""
        o = [(1.5, lambda: self.branch_with_null(t, d))]
        named = self.values(OPT(t))
        if named:
            o.append((3.0, lambda: ref(self.r.choice(named))))
        makers = self.callables(OPT(t))
        if makers and d > 0:
            o.append((2.5, lambda: self.call(self.r.choice(makers), d)))
        if d > 1:
            # `a ?? b` with a nullable `b` is itself nullable.
            o.append((0.7, lambda: N("coal", [self.nullable(t, d - 1), self.nullable(t, d - 1)], t=OPT(t))))
        return self.pick(o)

    def branch_with_null(self, t, d):
        """`if c then e else null`: the arms join to `t?`."""
        cond = self.expr(BOOL, max(d - 1, 0))
        arms = [lambda: self.expr(t, max(d - 1, 0)), lambda: self.null(t)]
        if self.chance(0.4):
            arms.reverse()
        return self.if_node(cond, arms[0], arms[1], OPT(t))

    def entering(self, t, d):
        """What is written where a `t?` is expected (an argument, a typed `let`, `return`): a
        plain value, which is put into the form by the compiler, `null`, or a `t?`."""
        return self.pick([(2.0, lambda: self.expr(t, d)),
                          (1.0, lambda: self.null(t)),
                          (2.0, lambda: self.nullable(t, d))])

    def coalesce(self, t, d):
        """`a ?? d`, `a ?? b ?? d`: the first that is not null."""
        parts = [self.nullable(t, d - 1) for _ in range(self.r.choice([1, 1, 2]))]
        return N("coal", parts + [self.expr(t, d - 1)], t=t)

    def nullable_let(self, d):
        t = self.r.choice(self.scalars())
        s = Sym(self.fresh("o"), OPT(t), "let")
        e = self.entering(t, d)
        self.scope.append(s)
        s.shown = True                     # printed by itself below, not through `format`
        return N("let", [e], name=s.name, ann=OPT(t))

    # -- narrowing (design principles 2.11, 2.12) --------------------------------------------
    def nullable_lets(self, t=None):
        """The names a test against null narrows: `T?` names bound by `let` or parameters."""
        return [s for s in self.scope if s.kind == "let" and not s.eff and s.t[0] == "opt" and
                (t is None or s.t[1] == t)]

    def null_test(self, s: Sym, is_null: bool):
        """`s == null` / `s != null`, either way round."""
        sides = [ref(s), self.null(s.t[1])]
        if self.chance(0.3):
            sides.reverse()
        return bin_("==" if is_null else "!=", sides[0], sides[1], BOOL)

    def narrowable(self, cond, holds: bool):
        """The names in scope that `cond` narrows where it holds (`holds`) or where it does not
        (design principles 2.11, 2.12): a test `x != null` (either order) that is `cond` or an
        operand `&&` joins in it narrows where `cond` holds; `cond` being `x == null` narrows
        where it does not. The generator writes the arm with those names as a `T`, as the
        compiler sees them, also where the test was not written to narrow."""
        def test(e):
            if e.k == "bin" and e.a["op"] in ("==", "!="):
                a, b = e.c
                if a.k == "ref" and b.k == "null":
                    return a.a["name"], e.a["op"] == "!="
                if b.k == "ref" and a.k == "null":
                    return b.a["name"], e.a["op"] == "!="
            return None

        def conjuncts(e):
            if e.k == "bin" and e.a["op"] == "&&":
                return conjuncts(e.c[0]) + conjuncts(e.c[1])
            return [e]
        names = set()
        if holds:
            for c in conjuncts(cond):
                t = test(c)
                if t and t[1]:
                    names.add(t[0])
        else:
            t = test(cond)
            if t and not t[1]:
                names.add(t[0])
        return [s for s in self.nullable_lets() if s.name in names]

    def logical(self, op, left, make_right):
        """`left op right` with `right` made as the arm it is."""
        right = self.narrowed(self.narrowable(left, op == "&&"), make_right) if op in ("&&", "||") else make_right()
        return bin_(op, left, right, BOOL)

    def narrowed(self, syms, make):
        """make() with each of `syms` a `T` instead of a `T?`, as inside the arm a test narrows.
        The `T?` itself is out of scope there: the name means the value."""
        saved = list(self.scope)
        for s in syms:
            if s not in self.scope:
                continue
            i = self.scope.index(s)
            self.scope[i] = Sym(s.name, s.t[1], "let")
        try:
            return make()
        finally:
            self.scope = saved

    def narrowed_if(self, t, d):
        """`if x != null then A else B` with `x` a `T` in `A`; also `x == null` with the arms the
        other way round, and several tests joined by `&&`, possibly with a plain condition."""
        opts = self.nullable_lets()
        s = self.r.choice(self.nullable_lets(t) if self.chance(0.6) else opts)
        r = self.r.random()
        if r < 0.5:
            cond = self.null_test(s, False)
            return self.if_node(cond, lambda: self.pure_arm(t, d - 1), lambda: self.pure_arm(t, d - 1), t)
        if r < 0.75:
            cond = self.null_test(s, True)
            return self.if_node(cond, lambda: self.pure_arm(t, d - 1), lambda: self.pure_arm(t, d - 1), t)
        syms = [s] + [x for x in self.r.sample(opts, min(len(opts), 2)) if x is not s][:1]

        def test_of(x):
            # Each operand of `&&` is the arm of the ones before it: a name they narrowed is a
            # `T` there, and a `T` is not compared with null.
            return lambda: self.null_test(x, False) if x in self.nullable_lets() else self.expr(BOOL, 1)
        makers = [test_of(x) for x in syms]
        if self.chance(0.4):
            makers.insert(self.r.randint(0, len(makers)), lambda: self.expr(BOOL, 1))
        cond = makers[0]()
        for make in makers[1:]:
            cond = self.logical("&&", cond, make)
        return self.if_node(cond, lambda: self.pure_arm(t, d - 1), lambda: self.pure_arm(t, d - 1), t)

    def if_node(self, cond, make_then, make_other, t):
        """`if cond then .. else ..` with each arm made under the narrowing `cond` gives it."""
        then = self.narrowed(self.narrowable(cond, True), make_then)
        other = self.narrowed(self.narrowable(cond, False), make_other)
        return N("if", [cond, then, other], t=t)

    def narrowed_and(self, d):
        """`x != null && e` with `x` a `T` in `e`: the right side of `&&` is the arm."""
        s = self.r.choice(self.nullable_lets())
        return self.logical("&&", self.null_test(s, False), lambda: self.expr(BOOL, d - 1))

    # -- `&&` / `||` with effects on the right side (design principles 2.11) -----------------
    def logic_probe(self):
        """`func qN(a: int, b: bool): bool sync { println(...); b }`: an effect whose value is a
        bool, to stand on the right side of `&&` and `||`."""
        name = self.fresh("q")
        params = [(self.fresh("a"), INT, False), (self.fresh("a"), BOOL, False)]
        a, b = params[0][0], params[1][0]
        body = N("blk", [[N("expr", [N("println", [N("fmt", [N("ref", t=INT, name=a)], t=STR, s=name + " {}")])])],
                         N("ref", t=BOOL, name=b)], t=BOOL, sync=True)
        sym = self.fn_sym(name, params, BOOL, eff=True)
        sym.t = ("fneff",)
        self.add_func(name, params, BOOL, body, sym)

    def logic_stmt(self, d):
        """`let n = c && q(..)` (or `||`, `&`, `|`): the effect of `q` happens only when the right
        side is needed, or always for `&` and `|`."""
        probes = [s for s in self.callables(BOOL, eff=True) if self.can_call(s)]
        if not probes:
            return self.println(d)
        op = self.r.choice(["&&", "||", "&&", "||", "&", "|"])
        probe = self.r.choice(probes)
        e = self.logical(op, self.expr(BOOL, 2), lambda: self.call(probe, 2, eff=True))
        s = Sym(self.fresh("n"), BOOL, "let")
        self.scope.append(s)
        return N("let", [e], name=s.name)

    def nullable_functions(self):
        """`func m(a: T, c: bool): T? { return if c then .. else null }` and
        `func u(x: T?, d: T): T { return x ?? .. }`: results and parameters of type `T?`."""
        t = self.r.choice(self.scalars())
        name = self.fresh("m")
        params = [(self.fresh("a"), t, False), (self.fresh("a"), BOOL, False)]
        body = self.with_params(params, lambda: N("blk", [[], self.branch_with_null(t, 2)], t=OPT(t)))
        self.add_func(name, params, OPT(t), body, self.fn_sym(name, params, OPT(t)))
        name = self.fresh("u")
        params = [(self.fresh("a"), OPT(t), False), (self.fresh("a"), t, False)]
        body = self.with_params(params, lambda: N("blk", [[], self.coalesce(t, 2)], t=t))
        self.add_func(name, params, t, body, self.fn_sym(name, params, t))

    # -- context parameters and partial application (design principles 2.10) ------------------
    def with_functions(self, ret=None, fn=None):
        return [s for s in self.scope if s.kind == "wfn" and (ret is None or s.ret == ret) and
                (fn is None or FN([p[1] for p in s.params], s.ret) == fn)]

    def applied(self, s: Sym, d):
        """`f<w>`: the function with its context arguments, a function value."""
        args = [self.expr(wt, min(max(d - 1, 0), 1)) for _n, wt, _v in s.value]
        return N("papp", args, t=FN([p[1] for p in s.params], s.ret), name=s.name)

    def applied_call(self, s: Sym, d):
        """`f<w>(x)`."""
        args = [self.expr(pt, min(max(d - 1, 0), 2)) for _n, pt, _v in s.params]
        return N("call", [self.applied(s, d)] + args, t=s.ret, eff=False, vargs=[])

    def with_function(self):
        name = self.fresh("w")
        wparams = [(self.fresh("k"), self.scalar(), False) for _ in range(self.r.choice([1, 1, 2]))]
        params = [(self.fresh("a"), self.scalar(), False) for _ in range(self.r.choice([1, 1, 2]))]
        ret = self.rand_type(0.2)
        body = self.with_params(wparams + params, lambda: N("blk", [[], self.expr(ret, 3)], t=ret))
        sym = Sym(name, ("wfn",), "wfn", params=params, ret=ret, value=wparams)
        self.funcs.append(N("func", [body], name=name, params=params, ret=ret, **{"with": wparams}))
        self.scope.append(sym)

    def partial_let(self, d):
        s = self.r.choice(self.with_functions())
        e = self.applied(s, d)
        g = Sym(self.fresh("n"), e.t, "let")
        self.scope.append(g)
        return N("let", [e], name=g.name)

    def array_options(self, t, d):
        e = t[1]
        o = [(3.0, lambda: N("arr", [self.expr(e, d - 1) for _ in range(self.r.choice([1, 2, 3, 3, 4]))], t=t)),
             (1.5, lambda: N("concat", [self.expr(t, d - 1), self.expr(t, d - 1)], t=t))]
        if self.on("append"):
            # A new array; the array appended to is unchanged (design principles 2.12).
            o.append((1.2, lambda: N("append", [self.expr(t, d - 1), self.expr(e, d - 1)], t=t)))
        named = self.values(t)
        if named:
            def slice_():
                a = self.r.choice(named)
                lo = lit(INT, self.r.choice([0, 0, 1]))
                hi = N("conv", [ref(a)], t=INT, fn="len") if self.chance(0.6) else lit(INT, self.r.choice([1, 2]))
                return N("slice", [ref(a), lo, hi], t=t)
            o.append((1.6, slice_))
        if self.on("builtin"):
            def map_():
                src = self.scalar() if self.chance(0.7) else e
                return N("map", [self.expr(ARR(src), d - 1), self.fn_expr(FN([src], e), d - 1)], t=t)
            o.append((2.5, map_))
            o.append((1.8, lambda: N("filter", [self.expr(t, d - 1), self.fn_expr(FN([e], BOOL), d - 1)], t=t)))
            if self.on("comprehension"):
                def comp_():
                    src = self.scalar() if self.chance(0.7) else e
                    source = self.expr(ARR(src), d - 1)
                    mark = len(self.scope)
                    x = Sym(self.fresh("q"), src, "let")
                    self.scope.append(x)
                    elem = self.expr(e, d - 1)
                    cond = self.expr(BOOL, d - 1) if self.chance(0.6) else None
                    del self.scope[mark:]
                    return N("comp", [source, elem, cond], t=t, var=x.name)
                o.append((2.4, comp_))
            if e == INT:
                def range_():
                    lo = self.r.choice([0, 0, 1, 2, -2])
                    if named and self.chance(0.3):
                        return N("range", [lit(INT, 0), N("conv", [ref(self.r.choice(named))], t=INT, fn="len")], t=t)
                    return N("range", [lit(INT, lo), lit(INT, lo + self.r.choice([0, 1, 2, 3, 4, 5]))], t=t)
                o.append((2.2, range_))
            if e[0] == "tup" and len(e[1]) == 2:
                def zip_():
                    left = self.expr(ARR(e[1][0]), d - 1)
                    right = N("map", [left.clone(), self.fn_expr(FN([e[1][0]], e[1][1]), d - 1)], t=ARR(e[1][1]))
                    return N("zip", [left, right], t=t)
                o.append((2.5, zip_))
        return o

    def if_expr(self, t, d):
        cond = self.expr(BOOL, d - 1)
        return self.if_node(cond, lambda: self.pure_arm(t, d - 1), lambda: self.pure_arm(t, d - 1), t)

    def pure_arm(self, t, d):
        if d > 0 and self.chance(0.25):
            return self.pure_block(t, d)
        return self.expr(t, d)

    def pure_block(self, t, d):
        mark = len(self.scope)
        stmts = [self.let(self.rand_type(0.3), d) for _ in range(self.r.choice([1, 1, 2]))]
        result = self.expr(t, d)
        del self.scope[mark:]
        return N("blk", [stmts, result], t=t)

    def let(self, t, d):
        e = self.expr(t, d)
        s = Sym(self.fresh("n"), t, "let")
        self.scope.append(s)
        return N("let", [e], name=s.name)

    def scrutinees(self):
        ts = [INT, BOOL] + ([STR] if self.on("string") else [])
        return [s for s in self.scope if s.kind == "let" and s.t in ts and not s.eff]

    def match_expr(self, t, d, arm=None):
        s = self.r.choice(self.scrutinees())
        arm = arm or (lambda: self.pure_arm(t, d - 1))
        cases = []
        if s.t == BOOL:
            first = self.chance(0.5)
            cases.append(N("case", [arm()], pat=first, pt=BOOL))
            cases.append(N("case", [arm()], pat=None, pt=BOOL) if self.chance(0.5)
                         else N("case", [arm()], pat=not first, pt=BOOL))
        else:
            pool = [0, 1, 2, 3, 5, -1, 7, 10] if s.t == INT else WORDS
            for p in self.r.sample(pool, self.r.choice([1, 2, 3])):
                cases.append(N("case", [arm()], pat=p, pt=s.t))
            cases.append(N("case", [arm()], pat=None, pt=s.t))
        return N("match", cases, t=t, var=s.name)

    def accesses(self, t):
        """Thunks for expressions of type t that read a part of a value in scope."""
        out = []

        def paths(make, ty, depth):
            if ty == t and depth < 2:
                out.append(make)
            if depth == 0:
                return
            if ty[0] == "tup":
                for i, ct in enumerate(ty[1]):
                    paths(lambda make=make, i=i, ct=ct: N("tix", [make()], t=ct, i=i), ct, depth - 1)
            elif ty[0] == "rec":
                for f, ft in self.fields(ty):
                    paths(lambda make=make, f=f, ft=ft: N("fld", [make()], t=ft, name=f), ft, depth - 1)
            elif ty[0] == "arr":
                def at(make=make, et=ty[1]):
                    a = make()
                    i = self.r.choice([0, 0, 1, 2])
                    read = N("idx", [a, lit(INT, i)], t=et)
                    if et != t or self.chance(0.3):
                        return read
                    cond = bin_(">", N("conv", [a.clone()], t=INT, fn="len"), lit(INT, i), BOOL)
                    return N("if", [cond, read, self.leaf(et)], t=et)
                paths(at, ty[1], depth - 1)

        for s in self.values():
            if s.t[0] in ("tup", "rec", "arr"):
                paths(lambda s=s: ref(s), s.t, 2)
        return out

    def callables(self, t, eff=False):
        out = []
        for s in self.scope:
            if s.eff != eff:
                continue
            if s.kind == "fn" and s.ret == t:
                out.append(s)
            elif s.kind == "let" and s.t[0] == "fn" and s.t[2] == t:
                out.append(s)
        return out

    def call(self, s: Sym, d, eff=False):
        params = s.params if s.kind == "fn" else [(None, pt, False) for pt in s.t[1]]
        args, vargs = [], []
        used = set()
        for i, (_n, pt, isvar) in enumerate(params):
            if isvar:
                cands = [v for v in self.scope if v.kind == "var" and v.t == pt and v.name not in used]
                v = self.r.choice(cands)
                used.add(v.name)
                args.append(ref(v))
                vargs.append(i)
            elif s.rec and s.rec[0] == i:
                args.append(self.small(s.rec[1]))
            else:
                args.append(self.expr(pt, min(d - 1, 2)))
        ret = s.ret if s.kind == "fn" else s.t[2]
        return N("call", [ref(s)] + args, t=ret, eff=eff or s.eff, vargs=vargs)

    def can_call(self, s: Sym) -> bool:
        """Is there a distinct variable in scope for every var parameter?"""
        if s.kind != "fn":
            return True
        need = {}
        for _n, pt, isvar in s.params:
            if isvar:
                need[pt] = need.get(pt, 0) + 1
        return all(len([v for v in self.scope if v.kind == "var" and v.t == pt]) >= k for pt, k in need.items())

    def reduce_expr(self, t, d):
        et = self.scalar()
        fn = self.lam(FN([t, et], t), d - 1)
        return N("reduce", [self.expr(ARR(et), d - 1), fn, self.expr(t, d - 1)], t=t)

    # -- function values --------------------------------------------------------------------
    def fn_expr(self, t, d):
        named = [s for s in self.scope if not s.eff and s.t == t and
                 (s.kind == "let" or (s.kind == "fn" and not s.rec and not any(p[2] for p in s.params)))]
        makers = self.callables(t) if d > 0 else []
        o = [(2.0, lambda: self.lam(t, d))]
        partial = self.with_functions(fn=t)
        if partial:
            o.append((2.5, lambda: self.applied(self.r.choice(partial), d)))
        if named and self.on("closure"):
            o.append((2.0, lambda: ref(self.r.choice(named))))
        if makers and self.on("closure"):
            o.append((1.5, lambda: self.call(self.r.choice(makers), d)))
        return self.pick(o)

    def lam(self, t, d):
        mark = len(self.scope)
        params = []
        for pt in t[1]:
            s = Sym(self.fresh("p"), pt, "let")
            params.append((s.name, pt, False))
            self.scope.append(s)
        body = self.pure_block(t[2], d) if d > 0 and self.chance(0.25) else self.expr(t[2], max(d, 1) if self.chance(0.7) else 0)
        del self.scope[mark:]
        return N("lam", [body], t=t, params=params, ret=t[2])

    # -- pure functions ---------------------------------------------------------------------
    def add_func(self, name, params, ret, body, sym: Sym, **attrs):
        self.funcs.append(N("func", [body], name=name, params=params, ret=ret, **attrs))
        self.scope.append(sym)

    def fn_sym(self, name, params, ret, **kw) -> Sym:
        plain = not any(p[2] for p in params)
        return Sym(name, FN([p[1] for p in params], ret) if plain else ("fnvar",), "fn", params=params, ret=ret, **kw)

    def with_params(self, params, make):
        """Runs make() with only the declared functions and the parameters in scope."""
        saved = self.scope
        self.scope = [s for s in saved if s.kind in ("fn", "wfn")]
        for name, t, isvar in params:
            self.scope.append(Sym(name, t, "var" if isvar else "let"))
        try:
            return make()
        finally:
            self.scope = saved

    def param_types(self, n, fn_chance=0.0):
        out = []
        for _ in range(n):
            if self.on("hof") and self.on("closure") and self.chance(fn_chance):
                out.append(self.fn_type())
            else:
                out.append(self.rand_type(0.35))
        return out

    def pure_function(self):
        name = self.fresh("f")
        params = [(self.fresh("a"), t, False) for t in self.param_types(self.r.choice([1, 2, 2, 3]), 0.25)]
        ret = self.fn_type() if self.on("closure") and self.chance(0.15) else self.rand_type(0.3)

        def make():
            stmts = [self.let(self.rand_type(0.3), 2) for _ in range(self.r.choice([0, 0, 1, 2]))]
            return N("blk", [stmts, self.expr(ret, 3)], t=ret)
        body = self.with_params(params, make)
        macro = self.on("macro") and self.chance(0.35) and macro_safe(body)
        self.add_func(name, params, ret, body, self.fn_sym(name, params, ret), macro=macro)

    def recursive_function(self):
        """f(n, x): tail, non-tail, tree or mutual recursion on a depth parameter."""
        shape = self.r.choice(["tail", "tail", "nontail", "nontail", "tree", "mutual"])
        t = self.r.choice([INT, INT, INT] + ([STR] if self.on("string") else []) +
                          ([ARR(INT)] if self.on("array") else []) + ([FLOAT] if self.on("float") else []) +
                          ([TUP(INT, INT)] if self.on("tuple") else []))
        name, n, x = self.fresh("r"), self.fresh("a"), self.fresh("a")
        params = [(n, INT, False), (x, t, False)]
        extra = self.chance(0.4)
        if extra:
            params.append((self.fresh("a"), self.scalar(), False))
        most = 5 if shape == "tree" else 9
        sym = self.fn_sym(name, params, t, rec=(0, most))
        nref = lambda: N("ref", t=INT, name=n)
        down = lambda k=1: bin_("-", nref(), lit(INT, k), INT)
        rest = lambda: [N("ref", t=p[1], name=p[0]) for p in params[2:]]

        def bounded(e):
            return bin_("%", e, lit(INT, self.r.choice([97, 997, 1000])), INT) if t == INT and self.chance(0.7) else e

        def self_call(fname, k=1):
            return N("call", [N("ref", t=sym.t, name=fname), down(k), bounded(self.expr(t, 2))] + rest(), t=t, eff=False, vargs=[])

        def body_of(fname_next):
            def make():
                base = self.expr(t, 2)
                if shape in ("tail", "mutual"):
                    step = self_call(fname_next)
                else:
                    mark = len(self.scope)
                    stmts = []
                    for k in ([1, 2] if shape == "tree" else [1]):
                        s = Sym(self.fresh("n"), t, "let")
                        stmts.append(N("let", [self_call(fname_next, k)], name=s.name))
                        self.scope.append(s)
                    result = bounded(self.expr(t, 2))
                    if not any(x.k == "ref" and x.a["name"] == stmts[0].a["name"] for x in walk(result)):
                        result = self.combine(t, result, N("ref", t=t, name=stmts[0].a["name"]))
                    del self.scope[mark:]
                    step = N("blk", [stmts, result], t=t)
                cond = bin_("<=", nref(), lit(INT, 1 if shape == "tree" else 0), BOOL)
                return N("blk", [[], N("if", [cond, base, step], t=t)], t=t)
            return self.with_params(params, make)

        if shape == "mutual":
            other = self.fresh("r")
            self.funcs.append(N("func", [body_of(other)], name=name, params=params, ret=t))
            self.funcs.append(N("func", [body_of(name)], name=other, params=params, ret=t))
            self.scope.append(sym)
            self.scope.append(self.fn_sym(other, params, t, rec=(0, most)))
        else:
            body = body_of(name)
            self.add_func(name, params, t, body, sym, macro=self.on("macro") and self.chance(0.25) and macro_safe(body))

    def combine(self, t, a, b):
        if t == INT:
            return bin_("%", bin_(self.r.choice(["+", "-"]), a, b, INT), lit(INT, 997), INT)
        if t == FLOAT:
            return bin_("+", bin_("*", a, lit(FLOAT, 0.5), FLOAT), b, FLOAT) if self.chance(0.5) else bin_("-", b, a, FLOAT)
        if t == STR:
            return bin_("+", a, b, STR)
        if t[0] == "arr":
            return N("concat", [a, b], t=t)
        if t[0] == "tup":
            return N("tup", [bin_("+", N("tix", [a], t=INT, i=0), N("tix", [b.clone()], t=INT, i=1), INT),
                             N("tix", [b], t=INT, i=0)], t=t)
        raise ValueError(t)

    # -- functions with effects -------------------------------------------------------------
    def var_types(self):
        out = [INT, INT, INT]
        if self.on("string"):
            out.append(STR)
        if self.on("float"):
            out.append(FLOAT)
        if self.on("array"):
            out.append(ARR(INT))
        out.append(BOOL)
        if self.on("tuple") and self.chance(0.3):
            out.append(TUP(INT, self.scalar()))
        return out

    def effect_function(self):
        name = self.fresh("e")
        params = []
        if self.on("varparam") and self.on("var"):
            for _ in range(self.r.choice([0, 1, 1, 2])):
                params.append((self.fresh("c"), self.r.choice(self.var_types()), True))
        for t in self.param_types(self.r.choice([0, 1, 1, 2])):
            params.append((self.fresh("a"), t, False))
        eff_param = None
        if self.on("hof") and self.on("closure") and self.chance(0.25):
            eff_param = (self.fresh("g"), FN([INT], INT), False)
            params.append(eff_param)
        self.r.shuffle(params)
        ret = self.rand_type(0.25)

        def make():
            if eff_param:
                for s in self.scope:
                    if s.name == eff_param[0]:
                        s.eff = True
            stmts = self.stmts(self.r.randint(2, 5), 2)
            return N("blk", [stmts, self.expr(ret, 2)], t=ret, sync=True)
        body = self.with_params(params, make)
        sym = self.fn_sym(name, params, ret, eff=True)
        sym.t = ("fneff",)
        self.add_func(name, params, ret, body, sym, syncfn=any(p[2] for p in params))

    def effect_recursion(self):
        """Writes a var parameter on the way down and / or up, prints, tail or non-tail."""
        name, n, c = self.fresh("w"), self.fresh("a"), self.fresh("c")
        ct = self.r.choice([INT, INT] + ([STR] if self.on("string") else []) + ([ARR(INT)] if self.on("array") else []))
        params = [(n, INT, False), (c, ct, True)]
        if self.chance(0.5):
            params.append((self.fresh("a"), INT, False))
        sym = self.fn_sym(name, params, INT, eff=True, rec=(0, 7))
        sym.t = ("fneff",)
        tail = self.chance(0.35)

        def make():
            cs = [s for s in self.scope if s.name == c][0]
            nref = N("ref", t=INT, name=n)
            base_stmts = [self.write(cs, 1)] if self.chance(0.4) else []
            if self.chance(0.4):
                base_stmts.append(self.println(1))
            base_val = self.expr(INT, 1)
            base = N("blk", [base_stmts, base_val], t=INT, sync=True) if base_stmts or self.chance(0.3) else base_val
            pre = [self.write(cs, 1)] if self.chance(0.7) else []
            if self.chance(0.3):
                pre.append(self.println(1))
            call = N("call", [N("ref", t=sym.t, name=name), bin_("-", nref.clone(), lit(INT, 1), INT), ref(cs)] +
                     [N("ref", t=p[1], name=p[0]) for p in params[2:]], t=INT, eff=True, vargs=[1])
            call = self.maybe_wait(call, 0.7)
            if tail:
                step = N("blk", [pre, call], t=INT, sync=True)
            else:
                r = Sym(self.fresh("n"), INT, "let")
                stmts = pre + [N("let", [call], name=r.name)]
                self.scope.append(r)
                if self.chance(0.7) or not pre:
                    stmts.append(self.write(cs, 1))
                if self.chance(0.3):
                    stmts.append(self.println(1))
                step = N("blk", [stmts, bin_("+", ref(r), lit(INT, self.r.choice([0, 1, 1, 3])), INT)], t=INT, sync=True)
            cond = bin_("<=", nref, lit(INT, 0), BOOL)
            return N("blk", [[], N("if", [cond, base, step], t=INT)], t=INT, sync=True)
        body = self.with_params(params, make)
        self.add_func(name, params, INT, body, sym, syncfn=True)

    def counter_factory(self):
        """A function that returns a lambda writing a variable of its own."""
        name, start = self.fresh("k"), self.fresh("a")
        params = [(start, INT, False)]
        ft = FN([INT], INT)

        def make():
            v = Sym(self.fresh("v"), INT, "var")
            decl = N("var", [self.expr(INT, 1)], name=v.name)
            self.scope.append(v)
            x = Sym(self.fresh("p"), INT, "let")
            self.scope.append(x)
            stmts = [self.write(v, 1)]
            inner = N("blk", [stmts, self.expr(INT, 1)], t=INT, sync=True)
            lam = N("lam", [inner], t=ft, params=[(x.name, INT, False)], ret=INT, syncfn=True)
            return N("blk", [[decl], lam], t=ft, sync=True)
        body = self.with_params(params, make)
        sym = self.fn_sym(name, params, ft, eff=True, ret_eff=True)
        sym.t = ("fneff",)
        self.add_func(name, params, ft, body, sym)

    # -- statements of a sync block ---------------------------------------------------------
    def maybe_wait(self, e, p=0.4):
        return N("wait", [e], t=e.t) if self.on("wait") and self.chance(p) else e

    def write(self, v: Sym, d):
        t = v.t
        if t == INT:
            op = self.r.choice(["+=", "+=", "-=", "=", "*=", "/=", "%="])
            if op == "*=":
                rhs = lit(INT, self.r.choice([2, 3, -1]))
            elif op == "/=":
                rhs = lit(INT, self.r.choice([2, 3, -2]))
            elif op == "%=":
                rhs = lit(INT, self.r.choice([2, 3, 7, 10, 97, -3]))
            elif op == "=":
                rhs = bin_("%", self.expr(INT, d), lit(INT, 1000), INT) if self.chance(0.5) else self.expr(INT, d)
            else:
                rhs = self.expr(INT, d)
        elif t == FLOAT:
            op = self.r.choice(["+=", "-=", "=", "*="])
            rhs = lit(FLOAT, self.r.choice([2.0, 0.5, -1.0])) if op == "*=" else self.expr(FLOAT, d)
        elif t == STR:
            if self.chance(0.4):
                op, rhs = "+=", self.expr(STR, d)
            else:
                op, rhs = "=", (bin_("+", ref(v), self.expr(STR, d), STR) if self.chance(0.5) else self.expr(STR, d))
        elif t[0] == "arr":
            op = "="
            if self.on("append") and self.chance(0.5):
                # `acc = acc->append<x>`: the rebinding the compiler may carry out in place.
                rhs = N("append", [ref(v), self.expr(t[1], d)], t=t)
            elif self.chance(0.7):
                rhs = N("concat", [ref(v), N("arr", [self.expr(t[1], d)], t=t)], t=t)
            else:
                rhs = self.expr(t, d)
        elif t == BOOL:
            op, rhs = "=", (N("un", [ref(v)], t=BOOL, op="!") if self.chance(0.5) else self.expr(BOOL, d))
        else:
            op, rhs = "=", self.expr(t, d)
        return N("expr", [self.maybe_wait(N("assign", [rhs], t=t, name=v.name, op=op))])

    def println(self, d):
        self.label += 1
        shown = [s for s in self.values() if s.t[0] not in ("fn", "opt")]
        fresh_ = [s for s in shown if not s.shown]
        picks = []
        for _ in range(self.r.choice([1, 2, 2, 3])):
            pool = fresh_ if fresh_ and self.chance(0.7) else shown
            if pool:
                s = self.r.choice(pool)
                s.shown = True
                picks.append(ref(s))
        if not picks or self.chance(0.2):
            picks.append(self.expr(self.rand_type(0.2), d))
        if len(picks) == 1 and self.chance(0.3):
            return N("expr", [N("println", [picks[0]])])
        text = f"L{self.label}" + "".join(" {}" for _ in picks)
        return N("expr", [N("println", [N("fmt", picks, t=STR, s=text)])])

    def effect_call(self, d):
        cands = [s for s in self.scope if s.eff and (s.kind == "fn" or s.t[0] == "fn") and self.can_call(s)]
        if not cands:
            return self.println(d)
        s = self.r.choice(cands)
        call = self.call(s, d, eff=True)
        for i, (_n, pt, _v) in enumerate(s.params if s.kind == "fn" else []):
            if pt[0] == "fn" and pt == FN([INT], INT) and self.chance(0.7):       # an effectful function argument
                call.c[1 + i] = self.effect_lambda(d)
        call = self.maybe_wait(call, 0.5 if call.a["vargs"] else 0.15)
        if self.chance(0.2):
            return N("expr", [call])
        r = Sym(self.fresh("n"), call.t, "let", eff=bool(s.ret_eff))
        self.scope.append(r)
        return N("let", [call], name=r.name)

    def effect_lambda(self, d):
        """sync (x: int): int => sync { ...; return e }"""
        mark = len(self.scope)
        x = Sym(self.fresh("p"), INT, "let")
        self.scope.append(x)
        stmts = self.stmts(self.r.choice([1, 1, 2]), d - 1, simple=True)
        result = self.expr(INT, 1)
        del self.scope[mark:]
        body = N("blk", [stmts, result], t=INT, sync=True)
        return N("lam", [body], t=FN([INT], INT), params=[(x.name, INT, False)], ret=INT, syncfn=writes_captured(body))

    def effect_block(self, t, d, n=None):
        mark = len(self.scope)
        stmts = self.stmts(n or self.r.choice([1, 2, 2, 3]), d - 1, simple=d <= 1)
        result = None if t is None else self.expr(t, 1)
        del self.scope[mark:]
        return N("blk", [stmts, result], t=t, sync=True, null=t is None)

    def stmts(self, count, d, simple=False):
        out = []
        for _ in range(count):
            out.append(self.stmt(d, simple))
        return out

    def stmt(self, d, simple=False):
        variables = [s for s in self.scope if s.kind == "var"]
        o = [(3.0, lambda: self.let(self.rand_type(0.4), 3 if not simple else 2)),
             (2.2, lambda: self.println(1))]
        if self.on("var"):
            o.append((1.6, self.var_decl))
            if variables:
                o.append((3.2, lambda: self.write(self.r.choice(variables), 2)))
        if self.on("effects") or self.on("varparam"):
            o.append((3.6, lambda: self.effect_call(2)))
        if self.on("closure") and self.chance(0.3):
            o.append((1.0, lambda: self.let(self.fn_type(), 2)))
        if self.on("nullable"):
            o.append((1.2, lambda: self.nullable_let(2)))
            nullables = [s for s in self.scope if s.kind == "let" and s.t[0] == "opt"]
            if nullables:
                # A nullable value printed as it is: `null`, or the value.
                o.append((0.8, lambda: N("expr", [N("println", [ref(self.r.choice(nullables))])])))
        if self.with_functions():
            o.append((1.0, lambda: self.partial_let(2)))
        if self.on("ifnoelse") and self.on("effects"):
            o.append((1.3, lambda: self.if_without_else(d)))
        if self.on("loop") and self.on("effects") and d > 0 and not simple:
            o.append((1.0, lambda: self.loop_value(d)))
        if self.on("logic") and self.on("effects"):
            o.append((1.3, lambda: self.logic_stmt(d)))
        if d > 0 and not simple:
            if self.on("if") and self.on("effects"):
                o.append((1.4, lambda: self.if_statement(d)))
                o.append((1.2, lambda: self.if_value(d)))
            if self.on("match") and self.on("effects") and self.scrutinees():
                o.append((0.9, lambda: self.match_value(d)))
            if self.on("builtin") and self.on("array"):
                o.append((1.3, lambda: self.foreach(d)))
                o.append((0.7, lambda: self.effect_reduce(d)))
            if self.on("capture") and self.on("closure") and self.on("var"):
                o.append((1.0, lambda: self.writer_closure(d)))
            if self.on("capture") and self.on("func"):
                o.append((0.7, lambda: self.nested_function(d)))
        return self.pick(o)

    # -- loops ------------------------------------------------------------------------------
    def loop_node(self, t, d, make_body, bare=False):
        """A loop whose value is a `t` (design principles 2.10, 2.13). The form is chosen at
        random; every loop ends: a `while` counts rounds against a small bound, a `for` visits
        an array. `make_body(state_t, d)` gives the body, with the names of the head in scope;
        the loop's value is the state, or (unless `bare`) one value of it."""
        form = self.r.choice(["while", "for", "for", "forwhile"])
        mark = len(self.scope)
        names, types, inits = [], [], []
        seq, var, cond = None, None, None

        def state(prefix, ty, init):
            s = Sym(self.fresh(prefix), ty, "let")
            names.append(s.name)
            types.append(ty)
            inits.append(init)
            return s

        # The first values and the array do not see the names of the head.
        first = self.expr(t, d - 1)
        if form != "for" or self.chance(0.3):
            counted = form == "while"
            extra_t = INT if counted else self.scalar()
            extra_init = lit(INT, 0) if counted else self.expr(extra_t, 1)
        else:
            counted, extra_t, extra_init = False, None, None
        elem_t = None
        if form != "while":
            elem_t = self.scalar() if self.chance(0.7) else t
            seq = self.expr(ARR(elem_t), d - 1)
        value = state("s", t, first)
        extra = state("c", extra_t, extra_init) if extra_t is not None else None
        self.scope.extend([value] + ([extra] if extra else []))
        if form == "while":
            # `c < K`, alone or with a test of the state (the counter keeps it finite).
            bound = bin_("<", ref(extra), lit(INT, self.r.choice([0, 1, 2, 3, 4, 5])), BOOL)
            cond = bound if self.chance(0.5) else self.logical("&&", bound, lambda: self.expr(BOOL, 1))
        elif form == "forwhile":
            cond = self.expr(BOOL, 1)
        if seq is not None:
            x = Sym(self.fresh("q"), elem_t, "let")
            var = x.name
            self.scope.append(x)
        state_t = t if extra is None else TUP(t, extra_t)
        if extra is None:
            body = make_body(t, d - 1)
        else:
            step = bin_("+", ref(extra), lit(INT, 1), INT) if counted else None
            body = make_body(state_t, d - 1, step)
        del self.scope[mark:]
        node = N("loop", [inits, seq, cond, body], t=state_t, names=names, var=var)
        return node if extra is None or bare else N("tix", [node], t=t, i=0)

    def loop_expr(self, t, d):
        def body(st, d2, step=None):
            if st == t:
                return self.pure_arm(t, d2)
            nxt = step if step is not None else self.expr(st[1][1], d2)
            return N("tup", [self.expr(t, d2), nxt], t=st)
        return self.loop_node(t, d, body)

    def loop_value(self, d):
        """`let n = loop ... do sync { effects; next }`: a loop in a sync block whose body is a
        sync block, so its effects run every round, in order."""
        t = self.rand_type(0.2)

        def body(st, d2, step=None):
            if st == t:
                return self.effect_block(t, d2)
            blk = self.effect_block(st[1][0], d2)
            nxt = step if step is not None else self.expr(st[1][1], 1)
            blk.c[1] = N("tup", [blk.c[1], nxt], t=st)
            blk.t = st
            return blk
        # The loop is the whole value of the statement (its tuple when it has two values of
        # state): the statement is an effect position, and so is the body (model.effect_violations).
        node = self.loop_node(t, d, body, bare=True)
        s = Sym(self.fresh("n"), node.t, "let")
        self.scope.append(s)
        return N("let", [node], name=s.name)

    def var_decl(self):
        t = self.r.choice(self.var_types())
        e = self.expr(t, 2)
        s = Sym(self.fresh("v"), t, "var")
        self.scope.append(s)
        return N("var", [e], name=s.name)

    def if_statement(self, d):
        cond = self.expr(BOOL, 2)
        then = self.narrowed(self.narrowable(cond, True), lambda: self.effect_block(None, d))
        other = (self.narrowed(self.narrowable(cond, False), lambda: self.effect_block(None, d))
                 if self.chance(0.6) else None)
        return N("ifs", [cond, then, other])

    def if_without_else(self, d):
        """`if c then e` as a statement: the arm is one expression of any type, whose value is
        dropped; an effect written there happens when the condition holds. A pure value there
        is a literal or a name: a call of a pure operator whose result is dropped is an error
        (UnusedPureResult, design principles 2.12)."""
        cond = self.expr(BOOL, 2)

        def make():
            r = self.r.random()
            if r < 0.65:
                return self.println(1).c[0]
            if r < 0.85:
                call = self.effect_call(1)
                # effect_call may bind the result; only the call itself can be the arm
                if call.k == "let":
                    self.scope.pop()
                return call.c[0]
            return self.leaf(self.rand_type(0.2))
        arm = self.narrowed(self.narrowable(cond, True), make)
        return N("ifs", [cond, arm, None])

    def if_value(self, d):
        t = self.rand_type(0.2)
        cond = self.expr(BOOL, 2)
        block = lambda: self.effect_block(t, d)
        other = lambda: self.effect_block(t, d) if self.chance(0.5) else self.expr(t, 2)
        node = self.if_node(cond, other, block, t) if self.chance(0.5) else self.if_node(cond, block, other, t)
        s = Sym(self.fresh("n"), t, "let")
        self.scope.append(s)
        return N("let", [node], name=s.name)

    def match_value(self, d):
        t = self.rand_type(0.2)
        e = self.match_expr(t, 2, arm=lambda: self.effect_block(t, d, 1) if self.chance(0.6) else self.expr(t, 1))
        s = Sym(self.fresh("n"), t, "let")
        self.scope.append(s)
        return N("let", [e], name=s.name)

    def foreach(self, d):
        et = self.scalar()
        arr = self.expr(ARR(et), 2)
        mark = len(self.scope)
        x = Sym(self.fresh("p"), et, "let")
        self.scope.append(x)
        variables = [s for s in self.scope if s.kind == "var"]
        stmts = []
        if self.chance(0.4):
            stmts.append(self.let(self.rand_type(0.2), 2))
        if variables and self.chance(0.8):
            stmts.append(self.write(self.r.choice(variables), 1))
        if not stmts or stmts[-1].k == "let" or self.chance(0.5):
            stmts.append(self.println(1))
        del self.scope[mark:]
        body = N("blk", [stmts, None], sync=True)
        lam = N("lam", [body], params=[(x.name, et, False)], ret=None, syncfn=writes_captured(body))
        return N("expr", [N("foreach", [arr, lam])])

    def effect_reduce(self, d):
        t, et = self.r.choice([INT, INT] + ([STR] if self.on("string") else [])), self.scalar()
        arr, init = self.expr(ARR(et), 2), self.expr(t, 1)
        mark = len(self.scope)
        acc, x = Sym(self.fresh("p"), t, "let"), Sym(self.fresh("p"), et, "let")
        self.scope += [acc, x]
        variables = [s for s in self.scope if s.kind == "var"]
        stmts = [self.println(1)]
        if variables and self.chance(0.5):
            stmts.append(self.write(self.r.choice(variables), 1))
        result = self.expr(t, 2)
        if t == INT:
            result = bin_("%", result, lit(INT, 997), INT)
        del self.scope[mark:]
        body = N("blk", [stmts, result], t=t, sync=True)
        lam = N("lam", [body], t=FN([t, et], t), params=[(acc.name, t, False), (x.name, et, False)], ret=t,
                syncfn=writes_captured(body))
        s = Sym(self.fresh("n"), t, "let")
        self.scope.append(s)
        return N("let", [N("reduce", [arr, lam, init], t=t, eff=True)], name=s.name)

    def writer_closure(self, d):
        """let f = sync (x: int): int => sync { c += ...; return ... }, called later."""
        lam = self.effect_lambda(max(d, 1))
        s = Sym(self.fresh("h"), FN([INT], INT), "let", eff=True)
        self.scope.append(s)
        return N("let", [lam], name=s.name)

    def nested_function(self, d):
        name = self.fresh("g")
        params = [(self.fresh("a"), self.scalar(), False) for _ in range(self.r.choice([1, 1, 2]))]
        ret = self.scalar()
        mark = len(self.scope)
        for pn, pt, _ in params:
            self.scope.append(Sym(pn, pt, "let"))
        effectful = self.on("effects") and self.chance(0.5)
        if effectful:
            stmts = self.stmts(self.r.choice([1, 2]), 0, simple=True)
            body = N("blk", [stmts, self.expr(ret, 2)], t=ret, sync=True)
        else:
            body = N("blk", [[], self.expr(ret, 3)], t=ret)
        del self.scope[mark:]
        sym = self.fn_sym(name, params, ret, eff=effectful)
        if effectful:
            sym.t = ("fneff",)
        self.scope.append(sym)
        return N("func", [body], name=name, params=params, ret=ret, syncfn=effectful and writes_captured(body))

    # -- the program ------------------------------------------------------------------------
    # -- operator overloads of the program (design principles 2.12) ----------------------------
    def operator_overloads(self):
        """`func operator +(a: P, b: P): P` and the others on the struct P. Their bodies are fixed;
        what varies is where the operators are used (operator_options)."""
        P = REC("P")
        a, b = N("ref", t=P, name="a"), N("ref", t=P, name="b")

        def fld(r, f):
            return N("fld", [r.clone()], t=INT, name=f)

        def both(make):
            return N("rec", [make("x"), make("y")], t=P, fields=["x", "y"])

        def decl(symbol, key, params, ret, result):
            self.funcs.append(N("func", [N("blk", [[], result], t=ret)], name="operator " + symbol,
                                key=key, params=params, ret=ret))

        decl("+", "operator +/2", [("a", P, False), ("b", P, False)], P,
             both(lambda f: bin_("+", fld(a, f), fld(b, f), INT)))
        decl("-", "operator -/1", [("a", P, False)], P,
             both(lambda f: N("un", [fld(a, f)], t=INT, op="-")))
        k = N("ref", t=INT, name="k")
        decl("*", "operator */2", [("k", INT, False), ("a", P, False)], P,
             both(lambda f: bin_("*", k.clone(), fld(a, f), INT)))
        decl("<", "operator </2", [("a", P, False), ("b", P, False)], BOOL,
             bin_("<", fld(a, "x"), fld(b, "x"), BOOL))
        i = N("ref", t=INT, name="i")
        decl("[]", "operator []/2", [("a", P, False), ("i", INT, False)], INT,
             N("if", [bin_("==", i, lit(INT, 0), BOOL), fld(a, "x"), fld(a, "y")], t=INT))
        self.operators = True

    def operator_options(self, t, d):
        P = REC("P")
        if t == P:
            return [(2.0, lambda: bin_("+", self.expr(P, d - 1), self.expr(P, d - 1), P)),
                    (1.0, lambda: N("un", [self.expr(P, d - 1)], t=P, op="-")),
                    (1.0, lambda: bin_("*", self.expr(INT, d - 1), self.expr(P, d - 1), P))]
        if t == BOOL:
            return [(0.8, lambda: bin_("<", self.expr(P, d - 1), self.expr(P, d - 1), BOOL))]
        if t == INT:
            return [(0.8, lambda: N("idx", [self.expr(P, d - 1), lit(INT, self.r.choice([0, 1]))], t=INT))]
        return []

    def top_level_values(self) -> list:
        """Feature `modules`: a few top-level `let`s of scalar types, each a pure expression
        over literals and the ones before it, in scope for every function and for main. They
        are written in a shuffled order (`global_order`): the top level does not depend on the
        order it is written in."""
        made = []
        for _ in range(self.r.choice([2, 3, 3, 4]) * self.size):
            t = self.scalar()
            e = self.expr(t, 2)
            s = Sym(self.fresh("k"), t, "let")
            self.scope.append(s)
            made.append(N("let", [e], name=s.name))
        self.global_order = list(range(len(made)))
        self.r.shuffle(self.global_order)
        return made

    def split(self) -> set:
        """Feature `modules`: the functions that go to the second module, a random set closed
        under the functions they name (the second module imports only the first one)."""
        names = {f.a["name"] for f in self.funcs}
        named = {f.a["name"]: {x.a["name"] for x in walk(f) if x.a.get("name") in names}
                 for f in self.funcs}
        chosen = set()
        for f in self.funcs:
            # The operator overloads stay in the main module: they are used where P is.
            if f.a.get("key"):
                continue
            if self.chance(0.5):
                todo = [f.a["name"]]
                while todo:
                    n = todo.pop()
                    if n not in chosen:
                        chosen.add(n)
                        todo.extend(named.get(n, ()))
        return chosen

    def program(self, module: str) -> N:
        globals_ = self.top_level_values() if self.on("modules") else []
        if self.on("opover") and "P" in self.types:
            self.operator_overloads()
        if self.on("func"):
            for _ in range(self.r.choice([1, 2, 3, 3, 4]) * self.size):
                if self.on("rec") and self.chance(0.3):
                    self.recursive_function()
                else:
                    self.pure_function()
            if self.on("effects") or self.on("varparam"):
                for _ in range(self.r.choice([1, 2, 2, 3]) * self.size):
                    r = self.r.random()
                    if self.on("rec") and self.on("varparam") and self.on("var") and r < 0.25:
                        self.effect_recursion()
                    elif self.on("capture") and self.on("closure") and self.on("var") and r < 0.4:
                        self.counter_factory()
                    else:
                        self.effect_function()
            if self.on("nullable"):
                for _ in range(self.r.choice([1, 1, 2])):
                    self.nullable_functions()
            if self.on("logic") and self.on("effects"):
                for _ in range(self.r.choice([1, 1, 2])):
                    self.logic_probe()
            if self.on("partial"):
                for _ in range(self.r.choice([1, 2, 2])):
                    self.with_function()
        top = {g.a["name"] for g in globals_}
        declared = [s for s in self.scope if s.kind in ("fn", "wfn") or s.name in top]
        self.scope = list(declared)
        stmts = []
        if self.on("env"):
            for i in range(self.r.choice([1, 2, 2, 3])):
                is_int = not self.on("string") or self.chance(0.7)
                key = f"PROGEN_{'I' if is_int else 'S'}{i}"
                value = self.r.choice([0, 1, 2, 3, 3, 4, 5, 8]) if is_int else self.r.choice(WORDS[:-4])
                self.env[key] = str(value)
                s = Sym(self.fresh("n"), INT if is_int else STR, "let", value=value if is_int else None)
                stmts.append(N("let", [N("env", t=s.t, name=key)], name=s.name))
                self.scope.append(s)
        stmts += self.stmts(self.r.randint(6, 14) * self.size, 2)
        for s in self.values():
            if s.t[0] not in ("fn", "opt") and not s.shown and self.chance(0.75):
                self.label += 1
                s.shown = True
                stmts.append(N("expr", [N("println", [N("fmt", [ref(s)], t=STR, s=f"L{self.label} {{}}")])]))
        body = N("blk", [stmts, lit(INT, 0)], t=INT, sync=True)
        main = N("func", [body], name="main", params=[], ret=INT)
        if not self.on("modules"):
            return N("prog", [self.funcs + [main]], module=module, types=dict(self.types), env=dict(self.env))
        return N("prog", [self.funcs + [main], globals_], module=module, types=dict(self.types),
                 env=dict(self.env), split=self.split(), global_order=self.global_order)


def macro_safe(body: N) -> bool:
    """std::macro refuses (with a located diagnostic, which is legal) a macro function whose
    body uses map, filter or reduce; such a function is not declared macro, so that the
    pipelines with std::macro run more programs to the end."""
    return not any(x.k in ("map", "filter", "reduce", "foreach") for x in walk(body))


def writes_captured(body: N) -> bool:
    """Does the body assign, or pass a variable to a var parameter? (Then the lambda or nested
    function is declared `sync`, as the compiler asks.)"""
    return any(x.k == "assign" or (x.k == "call" and x.a.get("vargs")) for x in walk(body))


MAX_ATTEMPTS = 200


def generate(seed: int, features: frozenset = DEFAULT_FEATURES, module: Optional[str] = None,
             size: int = 1):
    """Returns (program, expected lines, attempts). Attempts beyond the first are programs the
    reference rejected (see model.Reject); they are replaced, deterministically.

    `size` multiplies the number of functions and of statements of `main`: 1 gives the programs
    of about 115 lines all recorded seeds refer to, n gives programs about n times as long. At
    size 1 a program with more than MAX_HEAVY graphs that own a constant is still replaced:
    that was the compiler's limit when the seeds were recorded (finding F24, fixed since), and
    dropping the filter would change which program a seed names. Larger sizes have no such
    filter and their own random streams."""
    key = ",".join(sorted(features))
    for attempt in range(MAX_ATTEMPTS):
        stream = f"progen|{VERSION}|{seed}|{key}|{attempt}" + (f"|size{size}" if size != 1 else "")
        rng = random.Random(stream)
        gen = Gen(rng, features, size)
        try:
            prog = gen.program(module or f"progen_{seed}")
        except IndexError:
            continue                                   # a production found nothing to choose from
        if size == 1 and heavy_graphs(prog) > MAX_HEAVY:
            continue
        if effect_violations(prog):
            raise AssertionError(f"seed {seed}: generated an effect outside an effect position")
        if static_faults(prog):
            continue
        try:
            expected = evaluate(prog)
        except Reject:
            continue
        if not expected:
            continue
        return prog, expected, attempt + 1
    raise RuntimeError(f"seed {seed}: no usable program in {MAX_ATTEMPTS} attempts")
