# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Program model of the random program generator: the tree, its Camel source text and its
reference evaluation.

A program is a tree of `N` nodes. Three things are defined on it, kind by kind, in this file:

  emit(prog)      the Camel source text;
  evaluate(prog)  the lines the program must print, computed here in Python (the reference);
  children        every node keeps its sub-trees in `c`, so the reducer (reduce.py) can delete
                  statements and replace expressions without knowing the kinds.

The evaluator is a plain eager interpreter. That is only a correct model of Camel because of
what the generator (generate.py) never writes; the rules are listed in README.md ("Why the
reference is valid") and checked by `check_determinism` below, which the reducer also runs on
every candidate so that a reduced program cannot leave the determined subset.

`Reject` is raised for a program whose reference would not be reliable (integer near the
64-bit range, inexact float arithmetic, index out of range, division by zero, too deep or too
long). Such a program is discarded, never reported.
"""

from __future__ import annotations

import math
import re
import zlib
from fractions import Fraction

INT, FLOAT, BOOL, STR = ("int",), ("float",), ("bool",), ("string",)
VOID = ("void",)


def ARR(t):
    return ("arr", t)


def TUP(*ts):
    return ("tup", tuple(ts))


def REC(name):
    return ("rec", name)


def FN(args, ret):
    return ("fn", tuple(args), ret)


def OPT(t):
    """`T?`: a value of type `t`, or null (None in the reference)."""
    return ("opt", t)


def type_src(t) -> str:
    k = t[0]
    if k in ("int", "float", "bool", "string", "void"):
        return k
    if k == "arr":
        return type_src(t[1]) + "[]"
    if k == "tup":
        return "(" + ", ".join(type_src(x) for x in t[1]) + ")"
    if k == "rec":
        return t[1]
    if k == "fn":
        return "(" + ", ".join(f"a{i}: {type_src(a)}" for i, a in enumerate(t[1])) + ") => " + type_src(t[2])
    if k == "opt":
        inner = type_src(t[1])
        return ("(" + inner + ")" if t[1][0] == "fn" else inner) + "?"
    raise ValueError(t)


class N:
    """A node: kind `k`, children `c` (each a node, a list of nodes, or None), attributes `a`,
    and for expressions the type `t`."""

    __slots__ = ("k", "c", "a", "t")

    def __init__(self, k, c=(), t=None, **a):
        self.k, self.c, self.a, self.t = k, list(c), a, t

    def clone(self) -> "N":
        def cp(x):
            if isinstance(x, N):
                return x.clone()
            if isinstance(x, list):
                return [cp(y) for y in x]
            return x
        return N(self.k, [cp(x) for x in self.c], self.t, **dict(self.a))

    def __repr__(self):
        return f"N({self.k}, {self.a})"


def walk(node):
    """All nodes of a tree, parents first."""
    stack = [node]
    while stack:
        x = stack.pop()
        if isinstance(x, N):
            yield x
            stack.extend(reversed(x.c))
        elif isinstance(x, list):
            stack.extend(reversed(x))


# ----------------------------------------------------------------------------------------------
# Source text
# ----------------------------------------------------------------------------------------------

IND = "    "


def str_lit(s: str) -> str:
    assert all(ch not in s for ch in "'\\\n"), s
    return "'" + s + "'"


def float_lit(v: float) -> str:
    text = repr(float(v))
    assert "e" not in text and "inf" not in text and "nan" not in text, text
    return text


def lit_src(t, v) -> str:
    if t == INT:
        return str(v)
    if t == FLOAT:
        return float_lit(v)
    if t == BOOL:
        return "true" if v else "false"
    if t == STR:
        return str_lit(v)
    raise ValueError(t)


def params_src(params) -> str:
    return ", ".join(("var " if isvar else "") + f"{name}: {type_src(t)}" for name, t, isvar in params)


def variant(text: str, n: int) -> int:
    """One of `n` spellings, chosen by the text itself: the random stream of the generator is
    not touched, so a seed still names the same program (only its spelling differs)."""
    return zlib.crc32(text.encode("utf-8")) % n


def bin_src(a: str, op: str, b: str) -> str:
    """`a op b`. `+`, `-` and `*` are also written without one or both spaces (`a-b`, `a -b`,
    `a- b`): the lexer once took a sign into the number that follows, so that `a-1` was `a`
    followed by the literal -1 (a subtraction that silently did not happen)."""
    if op in ("+", "-", "*"):
        return (f"{a} {op} {b}", f"{a}{op}{b}", f"{a} {op}{b}", f"{a}{op} {b}")[variant(a + op + b, 4)]
    return f"{a} {op} {b}"


ATOMS = {"lit", "ref", "call", "conv", "arr", "tup", "rec", "idx", "fld", "range", "zip", "concat",
         "println", "env", "comp", "papp", "null"}


# Forms the grammar does not accept bare as the test of an `if` or the right side of an assignment.
LOW = {"if", "match", "lam", "wait", "assign", "loop"}


def ex(n: N, ind: int, top: bool = False) -> str:
    """Source of an expression. `ind` is the indentation level of the line it starts on; `top`
    means the context needs no parentheses."""
    k = n.k

    def sub(x):                        # an operand
        return ex(x, ind)

    def arg(x):                        # a position delimited by commas or brackets
        return ex(x, ind, True)

    def post(x):                       # the receiver of a postfix form
        s = ex(x, ind, True)
        return s if x.k in ATOMS else "(" + s + ")"

    def wrap(s):
        return s if top else "(" + s + ")"

    if k == "lit":
        s = lit_src(n.t, n.a["v"])
        return s if top or not s.startswith("-") else "(" + s + ")"
    if k == "ref":
        return n.a["name"]
    if k == "null":
        return "null"
    if k == "coal":
        # `a ?? b ?? d`: every operand but the last is a `T?`; the last is a `T` (the result
        # is a `T`) or a `T?` (the result is a `T?`). Design principles 3.8.
        return wrap(" ?? ".join(sub(x) for x in n.c))
    if k == "comp":
        # `[e for x in xs if c]`: filter then map (design principles 4.2).
        cond = "" if n.c[2] is None else " if " + arg(n.c[2])
        return f"[{arg(n.c[1])} for {n.a['var']} in {arg(n.c[0])}{cond}]"
    if k == "loop":
        # `loop s = a, t = b (for x in xs)? (while c)? do body` (design principles 3.10). The
        # first values, the array and the condition are operands (an `if` among them takes
        # parentheses); what follows `do` reads as after `then`.
        inits, seq, cond, body = n.c
        head = ", ".join(f"{name} = {sub(x)}" for name, x in zip(n.a["names"], inits))
        text = "loop " + head
        if seq is not None:
            text += f" for {n.a['var']} in {sub(seq)}"
        if cond is not None:
            text += f" while {sub(cond)}"
        return wrap(text + " do " + arm(body, ind))
    if k == "papp":
        # `f<w>`: the function `f` with its context arguments given (a partial application).
        return n.a["name"] + "<" + ", ".join(arg(x) for x in n.c) + ">"
    if k == "env":
        s = f"get_env({str_lit(n.a['name'])})"
        return f"int({s})" if n.t == INT else s
    if k == "bin":
        return wrap(bin_src(sub(n.c[0]), n.a["op"], sub(n.c[1])))
    if k == "un":
        return wrap(n.a["op"] + sub(n.c[0]))
    if k == "conv":
        return f"{n.a['fn']}({arg(n.c[0])})"
    if k == "call":
        return post(n.c[0]) + "(" + ", ".join(arg(x) for x in n.c[1:]) + ")"
    if k == "fmt":
        return wrap(str_lit(n.a["s"]) + "->format<" + ", ".join(arg(x) for x in n.c) + ">")
    if k == "arr":
        return "[" + ", ".join(arg(x) for x in n.c) + "]"
    if k == "tup":
        return "(" + ", ".join(arg(x) for x in n.c) + ")"
    if k == "rec":
        return "{ " + ", ".join(f"{f}: {arg(x)}" for f, x in zip(n.a["fields"], n.c)) + " }"
    if k == "idx":
        return f"{post(n.c[0])}[{arg(n.c[1])}]"
    if k == "fld":
        return f"{post(n.c[0])}.{n.a['name']}"
    if k == "tix":
        # `t.0.1` reaches the parser as `t` . `0.1` (one REAL), which it has to split: half of
        # the nested accesses are written that way, the others as `(t.0).1`.
        inner = n.c[0]
        if inner.k == "tix" and variant(ex(inner, ind, True), 2) == 0:
            return f"{ex(inner, ind, True)}.{n.a['i']}"
        return f"{post(n.c[0])}.{n.a['i']}"
    if k == "if":
        cond = sub(n.c[0]) if n.c[0].k in LOW else arg(n.c[0])
        return wrap(f"if {cond} then {arm(n.c[1], ind)} else {arm(n.c[2], ind)}")
    if k == "blk":
        return block(n, ind)
    if k == "match":
        lines = [f"match {n.a['var']} {{"]
        for case in n.c:
            pat = "_" if case.a["pat"] is None else lit_src(case.a["pt"], case.a["pat"])
            lines.append(IND * (ind + 1) + f"case {pat} => " + arm(case.c[0], ind + 1))
        lines.append(IND * ind + "}")
        return wrap("\n".join(lines))
    if k == "lam":
        ret = "" if n.a["ret"] is None else ": " + type_src(n.a["ret"])
        mod = "sync " if n.a.get("syncfn") else ""
        return wrap(f"{mod}({params_src(n.a['params'])}){ret} => {arm(n.c[0], ind)}")
    if k in ("map", "filter", "foreach"):
        return wrap(f"{post(n.c[0])}->{k}<{arg(n.c[1])}>")
    if k == "reduce":
        return wrap(f"{post(n.c[0])}->reduce<{arg(n.c[1])}, {arg(n.c[2])}>")
    if k == "slice":
        return wrap(f"{post(n.c[0])}->slice<{arg(n.c[1])}, {arg(n.c[2])}>")
    if k == "contains":
        return wrap(f"{post(n.c[0])}->contains<{arg(n.c[1])}>")
    if k == "append":
        return wrap(f"{post(n.c[0])}->append<{arg(n.c[1])}>")
    if k == "join":
        return wrap(f"{post(n.c[0])}->join<{str_lit(n.a['sep'])}>")
    if k in ("zip", "range", "concat"):
        return f"{k}({arg(n.c[0])}, {arg(n.c[1])})"
    if k == "println":
        return f"println({arg(n.c[0])})"
    if k == "wait":
        return wrap("wait " + ex(n.c[0], ind, True))
    if k == "assign":
        rhs = sub(n.c[0]) if n.c[0].k in LOW else arg(n.c[0])
        return wrap(f"{n.a['name']} {n.a['op']} {rhs}")
    raise ValueError(k)


def arm(n: N, ind: int) -> str:
    """A branch arm, a match arm or a lambda body: a block or a single expression."""
    return block(n, ind) if n.k == "blk" else ex(n, ind, True)


def block(n: N, ind: int) -> str:
    # Every statement ends with `;`. The grammar has no line ends, and without the separator
    # `xs->foreach<f>` followed by a line that starts an expression can be read as the
    # comparison chain `xs->foreach < f > next...`.
    lines = [("sync " if n.a.get("sync") else "") + "{"]
    for s in n.c[0]:
        text = st(s, ind + 1)
        text[-1] += ";"
        lines += text
    if n.c[1] is not None:
        lines.append(IND * (ind + 1) + "return " + ex(n.c[1], ind + 1, True) + ";")
    elif n.a.get("null"):
        # An arm that gives nothing ends in `null`. (`return null` there is now reported by
        # ExitInUnusedBranch: an arm needs no value to compile, design principles 3.7.)
        lines.append(IND * (ind + 1) + "null;")
    lines.append(IND * ind + "}")
    return "\n".join(lines)


def st(n: N, ind: int) -> list:
    pad = IND * ind
    k = n.k
    if k in ("let", "var"):
        ann = ": " + type_src(n.a["ann"]) if n.a.get("ann") else ""
        return [f"{pad}{k} {n.a['name']}{ann} = {ex(n.c[0], ind, True)}"]
    if k == "expr":
        return [pad + ex(n.c[0], ind, True)]
    if k == "ifs":
        # The arm of an `if` without `else` may also be one expression of any type, whose
        # value is dropped (design principles 3.9): `if c then println(x)`.
        text = f"{pad}if {ex(n.c[0], ind, n.c[0].k not in LOW)} then {arm(n.c[1], ind)}"
        if n.c[2] is not None:
            text += " else " + block(n.c[2], ind)
        return [text]
    if k == "func":
        return func_src(n, ind)
    raise ValueError(k)


def func_src(n: N, ind: int) -> list:
    pad = IND * ind
    mods = ("#macro " if n.a.get("macro") else "") + ("sync " if n.a.get("syncfn") else "")
    head = f"{pad}{mods}func {n.a['name']}({params_src(n.a['params'])}): {type_src(n.a['ret'])} "
    lines = [head + block(n.c[0], ind)]
    if n.a.get("with"):
        # Context parameters: `with <k: int>` on the line before the function.
        lines.insert(0, f"{pad}with <{params_src(n.a['with'])}>")
    return lines


def prog_globals(prog: N) -> list:
    """The top-level `let`s of the program (feature `modules`), in the order they were made:
    each uses only the ones before it."""
    return list(prog.c[1]) if len(prog.c) > 1 else []


def type_decls_src(prog: N, export: bool) -> list:
    out = []
    for name, fields in prog.a.get("types", {}).items():
        out += [("export " if export else "") + f"type {name} = {{ " +
                ", ".join(f"{f}: {type_src(t)}" for f, t in fields) + " }", ""]
    return out


def globals_src(prog: N, export: bool) -> list:
    # Written in the order recorded by the generator, not the order they were made: the top
    # level of a module does not depend on the order it is written in (design principles 10.1).
    made = prog_globals(prog)
    order = prog.a.get("global_order") or list(range(len(made)))
    out = []
    for i in order:
        if i < len(made):
            g = made[i]
            out += [("export " if export else "") + f"let {g.a['name']} = {ex(g.c[0], 0, True)}", ""]
    return out


def module_names(prog: N):
    """With the feature `modules`: the module that holds the top-level values and the types,
    and the one that holds part of the functions."""
    return f"{prog.a['module']}_vals", f"{prog.a['module']}_funcs"


def emit(prog: N) -> str:
    """The source of the entry module."""
    split = prog.a.get("split")
    out = [f"module {prog.a['module']}", ""]
    if prog.a.get("env"):
        out += ["import { get_env } from env", ""]
    if split is None:
        out += type_decls_src(prog, False)
        out += globals_src(prog, False)
        for f in prog.c[0]:
            out += func_src(f, 0) + [""]
        return "\n".join(out)
    vals, funcs = module_names(prog)
    names = list(prog.a.get("types", {})) + [g.a["name"] for g in prog_globals(prog)]
    moved = [f for f in prog.c[0] if f.a["name"] in split]
    if names:
        out += ["import { " + ", ".join(names) + " } from " + vals]
    if moved:
        out += ["import { " + ", ".join(f.a["name"] for f in moved) + " } from " + funcs]
    out += [""]
    for f in prog.c[0]:
        if f.a["name"] not in split:
            out += func_src(f, 0) + [""]
    return "\n".join(out)


def emit_modules(prog: N) -> dict:
    """The other modules of a program split by the feature `modules` (file stem -> source):
    one with the top-level values and the types, exported; one with the functions that call
    only each other, exported, which imports the first one. Empty for a program of one module."""
    split = prog.a.get("split")
    if split is None:
        return {}
    vals, funcs = module_names(prog)
    out_vals = [f"module {vals}", ""] + type_decls_src(prog, True) + globals_src(prog, True)
    names = list(prog.a.get("types", {})) + [g.a["name"] for g in prog_globals(prog)]
    out_funcs = [f"module {funcs}", ""]
    if names:
        out_funcs += ["import { " + ", ".join(names) + " } from " + vals, ""]
    for f in prog.c[0]:
        if f.a["name"] in split:
            lines = func_src(f, 0)
            head = 1 if lines[0].startswith("with ") else 0   # `with <...>` comes before `export`
            lines[head] = "export " + lines[head]
            out_funcs += lines + [""]
    return {vals: "\n".join(out_vals), funcs: "\n".join(out_funcs)}


# ----------------------------------------------------------------------------------------------
# Reference evaluation
# ----------------------------------------------------------------------------------------------

class Reject(Exception):
    """The program is outside what the reference can vouch for."""


class Cell:
    __slots__ = ("v",)

    def __init__(self, v):
        self.v = v


class Closure:
    __slots__ = ("params", "body", "env", "name", "wparams")

    def __init__(self, params, body, env, name, wparams=None):
        self.params, self.body, self.env, self.name = params, body, env, name
        self.wparams = wparams or []          # context parameters still to be given


class Env:
    __slots__ = ("d", "up")

    def __init__(self, up=None):
        self.d, self.up = {}, up

    def get(self, name):
        e = self
        while e is not None:
            if name in e.d:
                return e.d[name]
            e = e.up
        raise Reject(f"unbound name {name}")


INT_MAX = 2 ** 62
MAX_DEPTH = 48
MAX_STEPS = 200_000
MAX_STR = 160
MAX_ARR = 64
MAX_LINES = 400


def check_int(v: int) -> int:
    if not -INT_MAX <= v <= INT_MAX:
        raise Reject("integer out of range")
    return v


def check_float(exact: Fraction) -> float:
    """Float arithmetic is only used where it is exact (so that fused or reordered machine
    arithmetic cannot differ) and where the value has at most 4 binary places below a
    magnitude of 2^20 (so that every way of printing it agrees)."""
    if abs(exact) > 2 ** 20 or (exact * 16).denominator != 1:
        raise Reject("float value outside the exact range")
    return float(exact)


def float_result(r: float, exact: Fraction) -> float:
    """`r` is what IEEE arithmetic gave. A negative zero is rejected: it prints as `-0`."""
    check_float(exact)
    if Fraction(r) != exact or (r == 0.0 and math.copysign(1.0, r) < 0):
        raise Reject("inexact float or negative zero")
    return r


def tdiv(a: int, b: int) -> int:
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def show(v) -> str:
    """Text of a value as `println` and a `{}` placeholder write it."""
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return show_float(v)
    if isinstance(v, str):
        return v
    if isinstance(v, list):
        return "[" + ", ".join(show_in(x) for x in v) + "]"
    if isinstance(v, tuple):
        return "(" + ", ".join(show_in(x) for x in v) + ")"
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{k}: {show_in(x)}" for k, x in v.items()) + " }"
    raise Reject(f"value without a text: {type(v).__name__}")


def show_in(v) -> str:
    """Text of a value inside an array, tuple or struct."""
    return show(v)


def show_float(v: float) -> str:
    # The one text of a float (design principles 7.7): the shortest that reads back as the
    # same number, with a fraction or an exponent. Python's repr follows the same layout
    # (an exponent below -4 or from 16 up, `1e+16`, `1e-05`, `-0.0`, `inf`); a NaN has no sign.
    return "nan" if v != v else repr(float(v))


class Evaluator:
    def __init__(self, prog: N):
        self.prog = prog
        self.out = []
        self.depth = 0
        self.steps = 0
        self.envv = prog.a.get("env", {})

    def run(self) -> list:
        g = Env()
        for f in prog_funcs(self.prog):
            # An operator overload is found by its symbol and arity (`operator -/1`), not by
            # the name it is declared with (`operator -`), which two overloads may share.
            key = f.a.get("key", f.a["name"])
            g.d[key] = Closure(f.a["params"], f.c[0], g, f.a["name"], f.a.get("with"))
        # The top-level values are pure and each uses only the ones made before it: evaluated
        # in that order, before main (design principles 10.1), whatever order they are written in.
        for v in prog_globals(self.prog):
            g.d[v.a["name"]] = self.ev(v.c[0], g)
        main = g.get("main")
        self.call(main, [])
        return self.out

    # -- calls ------------------------------------------------------------------------------
    def call(self, fn, args):
        if not isinstance(fn, Closure) or fn.wparams:
            raise Reject("call of a non-function")
        if len(args) != len(fn.params):
            raise Reject("wrong number of arguments")
        self.depth += 1
        if self.depth > MAX_DEPTH:
            raise Reject("too deep")
        env = Env(fn.env)
        for (name, _t, isvar), a in zip(fn.params, args):
            if isvar != isinstance(a, Cell):
                raise Reject("var parameter mismatch")
            env.d[name] = a
        r = self.blk(fn.body, env, new_env=False) if fn.body.k == "blk" else self.ev(fn.body, env)
        self.depth -= 1
        return r

    def args_of(self, fn, nodes, env):
        if not isinstance(fn, Closure):
            raise Reject("call of a non-function")
        if len(nodes) != len(fn.params):
            raise Reject("wrong number of arguments")
        args = []
        for (_n, _t, isvar), a in zip(fn.params, nodes):
            if isvar:
                cell = env.get(a.a["name"]) if a.k == "ref" else None
                if not isinstance(cell, Cell):
                    raise Reject("a var parameter needs a variable")
                args.append(cell)
            else:
                args.append(self.ev(a, env))
        return args

    def blk(self, n: N, env: Env, new_env: bool = True):
        if new_env:
            env = Env(env)
        for s in n.c[0]:
            self.stmt(s, env)
        return None if n.c[1] is None else self.ev(n.c[1], env)

    def arm(self, n: N, env: Env):
        return self.blk(n, env) if n.k == "blk" else self.ev(n, env)

    def stmt(self, n: N, env: Env):
        k = n.k
        if k == "let":
            env.d[n.a["name"]] = self.ev(n.c[0], env)
        elif k == "var":
            env.d[n.a["name"]] = Cell(self.ev(n.c[0], env))
        elif k == "expr":
            self.ev(n.c[0], env)
        elif k == "ifs":
            if self.ev(n.c[0], env):
                self.arm(n.c[1], env)
            elif n.c[2] is not None:
                self.blk(n.c[2], env)
        elif k == "func":
            env.d[n.a["name"]] = Closure(n.a["params"], n.c[0], env, n.a["name"])
        else:
            raise Reject(f"statement kind {k}")

    # -- expressions ------------------------------------------------------------------------
    def ev(self, n: N, env: Env):
        self.steps += 1
        if self.steps > MAX_STEPS:
            raise Reject("too many steps")
        return getattr(self, "e_" + n.k)(n, env)

    def e_lit(self, n, env):
        return n.a["v"]

    def e_ref(self, n, env):
        v = env.get(n.a["name"])
        return v.v if isinstance(v, Cell) else v

    def e_null(self, n, env):
        return None

    def e_coal(self, n, env):
        # The right side is evaluated only when the left one is null.
        v = None
        for x in n.c:
            v = self.ev(x, env)
            if v is not None:
                break
        return v

    def e_loop(self, n, env):
        # The state is given by the head; each round: the condition (it sees the state), then
        # the next element, then the body (it sees the state and the element); its value is the
        # next state, the tuple of the values when there are several (design principles 3.10).
        inits, seq, cond, body = n.c
        names = n.a["names"]
        state = [self.ev(x, env) for x in inits]
        items = None if seq is None else self.ev(seq, env)
        rounds = 0
        while True:
            self.steps += 1
            if self.steps > MAX_STEPS:
                raise Reject("too many steps")
            inner = Env(env)
            for name, v in zip(names, state):
                inner.d[name] = v
            if cond is not None and not self.ev(cond, inner):
                break
            if items is not None:
                if rounds >= len(items):
                    break
                inner.d[n.a["var"]] = items[rounds]
            nxt = self.arm(body, inner)
            state = list(nxt) if len(names) > 1 else [nxt]
            rounds += 1
        return tuple(state) if len(names) > 1 else state[0]

    def e_comp(self, n, env):
        out = []
        for x in self.ev(n.c[0], env):
            inner = Env(env)
            inner.d[n.a["var"]] = x
            if n.c[2] is None or self.ev(n.c[2], inner):
                out.append(self.ev(n.c[1], inner))
        return out

    def e_papp(self, n, env):
        fn = env.get(n.a["name"])
        if not isinstance(fn, Closure) or len(fn.wparams) != len(n.c):
            raise Reject("context arguments of a function that does not take them")
        bound = Env(fn.env)
        for (name, _t, _v), x in zip(fn.wparams, n.c):
            bound.d[name] = self.ev(x, env)       # evaluated once, where `f<w>` is written
        return Closure(fn.params, fn.body, bound, fn.name)

    def e_env(self, n, env):
        v = self.envv[n.a["name"]]
        return int(v) if n.t == INT else v

    def e_bin(self, n, env):
        op = n.a["op"]
        if op in ("&&", "||"):
            # Branches (design principles 4.2): `a && b` is `if a then b else false`, so the
            # right side, its effects and its faults included, is evaluated only when the left
            # side does not decide.
            left = bool(self.ev(n.c[0], env))
            if left == (op == "||"):
                return left
            return bool(self.ev(n.c[1], env))
        if op in ("&", "|"):
            # Both sides evaluated, left first.
            left, right = bool(self.ev(n.c[0], env)), bool(self.ev(n.c[1], env))
            return (left and right) if op == "&" else (left or right)
        if op in ("==", "!=") and "null" in (n.c[0].k, n.c[1].k):
            # The test of a `T?` against null (design principles 3.8).
            v = self.ev(n.c[1] if n.c[0].k == "null" else n.c[0], env)
            return (v is None) == (op == "==")
        a, b = self.ev(n.c[0], env), self.ev(n.c[1], env)
        if isinstance(a, dict) or isinstance(b, dict):
            return self.overload(env, op, [a, b])
        return binop(op, a, b)

    def overload(self, env, symbol, args):
        """An operator on a struct: the program's overload of it (design principles 4.3)."""
        return self.call(env.get(f"operator {symbol}/{len(args)}"), args)

    def e_un(self, n, env):
        a = self.ev(n.c[0], env)
        if isinstance(a, dict):
            return self.overload(env, n.a["op"], [a])
        if n.a["op"] == "!":
            return not a
        if isinstance(a, float):
            return float_result(-a, -Fraction(a))
        return check_int(-a)

    def e_conv(self, n, env):
        a, fn = self.ev(n.c[0], env), n.a["fn"]
        if fn == "len":
            return len(a)
        if fn == "float":
            return check_float(Fraction(a))
        if fn == "int":
            return check_int(math.trunc(a))
        if fn == "string":
            if isinstance(a, float):
                raise Reject("string of a float")
            return show(a)
        raise Reject(fn)

    def e_call(self, n, env):
        fn = self.ev(n.c[0], env)
        return self.call(fn, self.args_of(fn, n.c[1:], env))

    def e_fmt(self, n, env):
        parts = PLACEHOLDER.split(n.a["s"])          # text, spec, text, spec, ..., text
        if len(parts) != 2 * len(n.c) + 1:
            raise Reject("placeholder count")
        s = parts[0]
        for i, x in enumerate(n.c):
            v, spec = self.ev(x, env), parts[2 * i + 1]
            if spec:
                if not isinstance(v, float):
                    raise Reject("precision on a non-float")
                s += "%.*f" % (int(spec), v)
            else:
                s += show(v)
            s += parts[2 * i + 2]
        return check_str(s)

    def e_arr(self, n, env):
        return [self.ev(x, env) for x in n.c]

    def e_tup(self, n, env):
        return tuple(self.ev(x, env) for x in n.c)

    def e_rec(self, n, env):
        return {f: self.ev(x, env) for f, x in zip(n.a["fields"], n.c)}

    def e_idx(self, n, env):
        a, i = self.ev(n.c[0], env), self.ev(n.c[1], env)
        if isinstance(a, dict):
            return self.overload(env, "[]", [a, i])
        if not 0 <= i < len(a):
            raise Reject("index out of range")
        return a[i]

    def e_fld(self, n, env):
        return self.ev(n.c[0], env)[n.a["name"]]

    def e_tix(self, n, env):
        return self.ev(n.c[0], env)[n.a["i"]]

    def e_if(self, n, env):
        return self.arm(n.c[1] if self.ev(n.c[0], env) else n.c[2], env)

    def e_blk(self, n, env):
        return self.blk(n, env)

    def e_match(self, n, env):
        v = env.get(n.a["var"])
        v = v.v if isinstance(v, Cell) else v
        for case in n.c:
            if case.a["pat"] is None or case.a["pat"] == v:
                return self.arm(case.c[0], env)
        raise Reject("no case matched")

    def e_lam(self, n, env):
        return Closure(n.a["params"], n.c[0], env, "<lambda>")

    def e_map(self, n, env):
        a, f = self.ev(n.c[0], env), self.ev(n.c[1], env)
        return [self.call(f, [x]) for x in a]

    def e_filter(self, n, env):
        a, f = self.ev(n.c[0], env), self.ev(n.c[1], env)
        return [x for x in a if self.call(f, [x])]

    def e_foreach(self, n, env):
        a, f = self.ev(n.c[0], env), self.ev(n.c[1], env)
        for x in a:
            self.call(f, [x])
        return None

    def e_reduce(self, n, env):
        a, f, acc = self.ev(n.c[0], env), self.ev(n.c[1], env), self.ev(n.c[2], env)
        for x in a:
            acc = self.call(f, [acc, x])
        return acc

    def e_slice(self, n, env):
        a, i, j = self.ev(n.c[0], env), self.ev(n.c[1], env), self.ev(n.c[2], env)
        if not 0 <= i <= j <= len(a):
            raise Reject("slice out of range")
        return a[i:j]

    def e_append(self, n, env):
        # A new array: the one appended to is unchanged (design principles 11.3).
        r = self.ev(n.c[0], env) + [self.ev(n.c[1], env)]
        if len(r) > MAX_ARR:
            raise Reject("array too long")
        return r

    def e_contains(self, n, env):
        a, v = self.ev(n.c[0], env), self.ev(n.c[1], env)
        return v in a

    def e_join(self, n, env):
        return check_str(n.a["sep"].join(self.ev(n.c[0], env)))

    def e_zip(self, n, env):
        a, b = self.ev(n.c[0], env), self.ev(n.c[1], env)
        if len(a) != len(b):
            raise Reject("zip of different lengths")
        return [(x, y) for x, y in zip(a, b)]

    def e_range(self, n, env):
        a, b = self.ev(n.c[0], env), self.ev(n.c[1], env)
        if not (0 <= b - a <= MAX_ARR):
            raise Reject("range bounds")
        return list(range(a, b))

    def e_concat(self, n, env):
        r = self.ev(n.c[0], env) + self.ev(n.c[1], env)
        if len(r) > MAX_ARR:
            raise Reject("array too long")
        return r

    def e_println(self, n, env):
        self.out.append(show(self.ev(n.c[0], env)))
        if len(self.out) > MAX_LINES:
            raise Reject("too many lines")
        return None

    def e_wait(self, n, env):
        return self.ev(n.c[0], env)

    def e_assign(self, n, env):
        cell = env.get(n.a["name"])
        if not isinstance(cell, Cell):
            raise Reject("assignment to a non-variable")
        v = self.ev(n.c[0], env)
        op = n.a["op"]
        cell.v = v if op == "=" else binop(op[:-1], cell.v, v)
        return cell.v


PLACEHOLDER = re.compile(r"\{(?::\.(\d)f)?\}")


def check_str(s: str) -> str:
    if len(s) > MAX_STR:
        raise Reject("string too long")
    return s


def binop(op, a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        if op == "==":
            return a == b
        if op == "!=":
            return a != b
        raise Reject("operator on bool")
    if isinstance(a, int) and isinstance(b, int):
        if op == "+":
            return check_int(a + b)
        if op == "-":
            return check_int(a - b)
        if op == "*":
            return check_int(a * b)
        if op == "/":
            if b == 0:
                raise Reject("division by zero")
            return tdiv(a, b)
        if op == "%":
            if b == 0:
                raise Reject("division by zero")
            return a - b * tdiv(a, b)
    elif isinstance(a, float) and isinstance(b, float):
        fa, fb = Fraction(a), Fraction(b)
        if op == "+":
            return float_result(a + b, fa + fb)
        if op == "-":
            return float_result(a - b, fa - fb)
        if op == "*":
            return float_result(a * b, fa * fb)
        if op == "/":
            if fb == 0:
                raise Reject("division by zero")
            return float_result(a / b, fa / fb)
    elif isinstance(a, str) and isinstance(b, str):
        if op == "+":
            return check_str(a + b)
    else:
        raise Reject(f"operand types of {op}")
    if op == "==":
        return a == b
    if op == "!=":
        return a != b
    if op == "<":
        return a < b
    if op == "<=":
        return a <= b
    if op == ">":
        return a > b
    if op == ">=":
        return a >= b
    raise Reject(f"operator {op}")


def prog_funcs(prog: N):
    return [f for f in prog.c[0] if f.k == "func"]


def evaluate(prog: N) -> list:
    """The lines the program prints. Raises Reject."""
    try:
        return Evaluator(prog).run()
    except RecursionError:
        raise Reject("python recursion limit")
    except (KeyError, IndexError, TypeError, AttributeError, ValueError) as e:
        # A reduced candidate can be ill-formed in ways the generator never produces.
        raise Reject(f"ill-formed: {type(e).__name__}: {e}")


# ----------------------------------------------------------------------------------------------
# The determined subset
# ----------------------------------------------------------------------------------------------

EFFECT_KINDS = {"println", "assign", "foreach", "wait"}


def is_effect(n: N) -> bool:
    return n.k in EFFECT_KINDS or (n.k in ("call", "reduce") and bool(n.a.get("eff")))


def effect_violations(prog: N) -> list:
    """Effects that are not in an effect position. An effect position is the expression of a
    statement (or the result) of a `sync` block whose enclosing blocks, up to the body of its
    function or lambda, are all `sync` blocks; `wait`, and `if` / `match` whose arms are `sync`
    blocks, pass the position on. Everywhere else an effect would run on demand (design
    principles 3.1), which the eager reference does not model, so the generator never writes one
    there and the reducer must not create one. Calls are judged by the `eff` mark the generator
    put on them."""
    bad = []

    def blk(b: N, ok: bool):
        for s in b.c[0]:
            if s.k in ("let", "var", "expr"):
                expr(s.c[0], ok)
            elif s.k == "ifs":
                expr(s.c[0], False)
                for a in s.c[1:]:
                    if a is None:
                        continue
                    if a.k == "blk":
                        blk(a, ok and bool(a.a.get("sync")))
                    else:
                        # One expression as the arm: it is the value of the arm, which is
                        # evaluated when the arm is taken, so an effect there runs.
                        expr(a, ok)
            elif s.k == "func":
                func(s)
        if b.c[1] is not None:
            expr(b.c[1], ok)

    def body(b: N):
        if b.k == "blk":
            blk(b, bool(b.a.get("sync")))
        else:
            expr(b, False)

    def func(f: N):
        body(f.c[0])

    def arm(a: N, pos: bool):
        if a.k == "blk":
            blk(a, pos and bool(a.a.get("sync")))
        else:
            expr(a, False)

    def expr(e: N, pos: bool):
        if is_effect(e) and not pos:
            bad.append(e.k)
        k = e.k
        if k == "wait":
            expr(e.c[0], pos)
        elif k == "if":
            expr(e.c[0], False)
            arm(e.c[1], pos)
            arm(e.c[2], pos)
        elif k == "match":
            for case in e.c:
                arm(case.c[0], pos)
        elif k == "blk":
            blk(e, pos and bool(e.a.get("sync")))
        elif k == "bin" and e.a["op"] in ("&&", "||"):
            # The right side is an arm with one expression: entered, it is evaluated.
            expr(e.c[0], False)
            expr(e.c[1], pos)
        elif k == "bin" and e.a["op"] in ("&", "|"):
            # Both operands, in order, like the arguments of a call statement.
            expr(e.c[0], pos)
            expr(e.c[1], pos)
        elif k == "loop":
            # The first values, the array and the condition are values; the body runs every
            # round the loop runs, so it passes the position on (when it is a sync block).
            inits, seq, cond, b = e.c
            for x in inits:
                expr(x, False)
            if seq is not None:
                expr(seq, False)
            if cond is not None:
                arm(cond, False)
            arm(b, pos)
        elif k == "lam":
            body(e.c[0])
        else:
            for x in e.c:
                if isinstance(x, N):
                    expr(x, False)

    for f in prog_funcs(prog):
        func(f)
    return bad



HEAVY_KINDS = {"fmt", "join", "arr", "tup", "rec"}


def heavy_graphs(prog: N) -> int:
    """How many graphs of the program own a constant object. A graph is a function, a lambda,
    or an arm of an `if` or `match`; it owns a constant object when a string literal or an
    array, tuple or struct literal is written directly in it. The compiler could not build a
    program with 62 such graphs (finding F24 in test/cases/opt/progen/README.md, fixed since:
    an unlocated std::bad_alloc under every pipeline), so the generator kept below MAX_HEAVY.
    Programs of size 1 are still held to it, only so that the recorded seeds keep naming the
    same programs (see generate.generate); larger sizes are not."""
    count = 0

    def graph(root) -> None:
        nonlocal count
        heavy = False
        stack = [root]
        while stack:
            x = stack.pop()
            if isinstance(x, list):
                stack.extend(x)
                continue
            if not isinstance(x, N):
                continue
            if x.k in HEAVY_KINDS or (x.k == "lit" and x.t == STR):
                heavy = True
            if x.k in ("func", "lam"):
                graph(x.c[0])
            elif x.k == "if":
                stack.append(x.c[0])
                graph(x.c[1])
                graph(x.c[2])
            elif x.k == "ifs":
                stack.append(x.c[0])
                graph(x.c[1])
                if x.c[2] is not None:
                    graph(x.c[2])
            elif x.k == "match":
                for case in x.c:
                    graph(case.c[0])
            elif x.k == "comp":
                stack.append(x.c[0])
                graph(x.c[1])
                if x.c[2] is not None:
                    graph(x.c[2])
            elif x.k == "loop":
                stack.extend(x.c[0])
                if x.c[1] is not None:
                    stack.append(x.c[1])
                if x.c[2] is not None:
                    graph(x.c[2])
                graph(x.c[3])
            else:
                stack.extend(x.c)
        count += heavy

    for f in prog_funcs(prog):
        graph(f.c[0])
    return count


MAX_HEAVY = 34


def static_faults(prog: N) -> list:
    """Faults that are visible without running: an index known to be a literal outside an array
    whose length is known (a literal, a `let` bound to one, a member of a tuple literal), a
    division by a literal zero. The design principles (3.9) make the first a compile-time
    error, so a program that has one, even in code that never runs, is not a valid test of the
    optimizer; the reducer must not shrink a program into one."""
    out = []
    # What a `let` name stands for: the compiler knows an array or an index it can trace to a
    # literal (through a `let`, through a member of a tuple literal), and reports an index
    # outside the array where the access is written, also where it would never run (the
    # right side of `||`, an arm not taken).
    lets = {x.a["name"]: x.c[0] for x in walk(prog) if x.k == "let"}

    def known(e, depth=0):
        """The literal an expression is known to be, or None."""
        while depth < 8:
            depth += 1
            if e.k == "ref" and e.a["name"] in lets:
                e = lets[e.a["name"]]
            elif e.k == "tix":
                inner = known(e.c[0], depth)
                if inner is None or inner.k != "tup" or not 0 <= e.a["i"] < len(inner.c):
                    return None
                e = inner.c[e.a["i"]]
            else:
                break
        return e if e.k in ("arr", "tup", "lit") else None

    for x in walk(prog):
        if x.k == "idx":
            array, index = known(x.c[0]), known(x.c[1])
            if (array is not None and array.k == "arr" and index is not None and index.k == "lit" and
                    not 0 <= index.a["v"] < len(array.c)):
                out.append("index")
        if x.k == "bin" and x.a["op"] in ("/", "%") and x.c[1].k == "lit" and x.c[1].a["v"] == 0:
            out.append("division")
    return out
