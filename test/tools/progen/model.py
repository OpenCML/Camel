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


ATOMS = {"lit", "ref", "call", "conv", "arr", "tup", "rec", "idx", "fld", "range", "zip", "concat",
         "println", "env"}


# Forms the grammar does not accept bare as the test of an `if` or the right side of an assignment.
LOW = {"if", "match", "lam", "wait", "assign"}


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
    if k == "env":
        s = f"get_env({str_lit(n.a['name'])})"
        return f"int({s})" if n.t == INT else s
    if k == "bin":
        return wrap(f"{sub(n.c[0])} {n.a['op']} {sub(n.c[1])}")
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
    if k == "tix":                     # `t.0.1` would be read as `t` . `0.1`
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
        lines.append(IND * (ind + 1) + "return null;")
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
        text = f"{pad}if {ex(n.c[0], ind, n.c[0].k not in LOW)} then {block(n.c[1], ind)}"
        if n.c[2] is not None:
            text += " else " + block(n.c[2], ind)
        return [text]
    if k == "func":
        return func_src(n, ind)
    raise ValueError(k)


def func_src(n: N, ind: int) -> list:
    pad = IND * ind
    mods = ("macro " if n.a.get("macro") else "") + ("sync " if n.a.get("syncfn") else "")
    head = f"{pad}{mods}func {n.a['name']}({params_src(n.a['params'])}): {type_src(n.a['ret'])} "
    return [head + block(n.c[0], ind)]


def emit(prog: N) -> str:
    out = [f"module {prog.a['module']}", ""]
    if prog.a.get("env"):
        out += ["import { get_env } from sys", ""]
    for name, fields in prog.a.get("types", {}).items():
        out += [f"type {name} = {{ " + ", ".join(f"{f}: {type_src(t)}" for f, t in fields) + " }", ""]
    for f in prog.c[0]:
        out += func_src(f, 0) + [""]
    return "\n".join(out)


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
    __slots__ = ("params", "body", "env", "name")

    def __init__(self, params, body, env, name):
        self.params, self.body, self.env, self.name = params, body, env, name


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
    return "%g" % v


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
            g.d[f.a["name"]] = Closure(f.a["params"], f.c[0], g, f.a["name"])
        main = g.get("main")
        self.call(main, [])
        return self.out

    # -- calls ------------------------------------------------------------------------------
    def call(self, fn, args):
        if not isinstance(fn, Closure):
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
                self.blk(n.c[1], env)
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

    def e_env(self, n, env):
        v = self.envv[n.a["name"]]
        return int(v) if n.t == INT else v

    def e_bin(self, n, env):
        op = n.a["op"]
        if op == "&&":
            return bool(self.ev(n.c[0], env)) and bool(self.ev(n.c[1], env))
        if op == "||":
            return bool(self.ev(n.c[0], env)) or bool(self.ev(n.c[1], env))
        a, b = self.ev(n.c[0], env), self.ev(n.c[1], env)
        return binop(op, a, b)

    def e_un(self, n, env):
        a = self.ev(n.c[0], env)
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
    principles 2.1), which the eager reference does not model, so the generator never writes one
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
                    if a is not None:
                        blk(a, ok and bool(a.a.get("sync")))
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
            else:
                stack.extend(x.c)
        count += heavy

    for f in prog_funcs(prog):
        graph(f.c[0])
    return count


MAX_HEAVY = 34


def static_faults(prog: N) -> list:
    """Faults that are visible without running: a literal index outside a literal array, a
    division by a literal zero. The design principles (2.7) make the first a compile-time
    error, so a program that has one, even in code that never runs, is not a valid test of the
    optimizer; the reducer must not shrink a program into one."""
    out = []
    for x in walk(prog):
        if x.k == "idx" and x.c[0].k == "arr" and x.c[1].k == "lit" and not 0 <= x.c[1].a["v"] < len(x.c[0].c):
            out.append("index")
        if x.k == "bin" and x.a["op"] in ("/", "%") and x.c[1].k == "lit" and x.c[1].a["v"] == 0:
            out.append("division")
    return out
