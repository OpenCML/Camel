# The C++ backend reads a non-escaping `var` through `.get()`, and `rt::Array<T>` has no such member

Measurement by the flip integration line on `tip=c5c506e44`, 2026-10-25. Everything below is a
reading; this file decides nothing and changes no expectation.

## 1. What is red

`test/plans/feat/trans/cpp.plan.toml:31` (`agree_with_fvm`, `tier = "slow"`) runs
`test/tools/cppexec/run.py --expect test/tools/cppexec/expect.txt`: every program in the expect list
is translated with `std::cpp`, built with the system compiler and run, and its output is compared
with `std::fvm`'s. Two consecutive runs on this tip:

```
[FAIL] agree_with_fvm   82.82 s
[FAIL] agree_with_fvm   73.72 s
[stdout]    115   97.5%  same
[stdout]      3    2.5%  does not build
[stdout] of the 115 that agree, 112 are standalone (no runtime linked)
[stdout] C++ compile time: median 2.38 s [0.78, 5.79]
[stdout]   test/cases/consistency/new_values/peek.cml: does not build: …:87:45: error: no member
          named 'get' in 'camel::standalone::Array<long>'
[stdout]   test/cases/consistency/new_values/peek_loop.cml: does not build: …:86:50: error: no member
          named 'get' in 'camel::standalone::Array<long>'
[stdout]   test/cases/consistency/new_values/rebinding_loop.cml: does not build: …:297:54: error: no
          member named 'get' in 'camel::standalone::Array<long>'
```

So the failure is not a disagreement between the two backends: three of the 115 programs fail to
**compile** as C++. The three are the `int[]` rebinding cases (`acc = acc->append<v>` with reads in
between).

## 2. The emitted program, verbatim

`camel test/cases/consistency/new_values/peek.cml std::cpp` on this tip, lines 82-91:

```cpp
int64_t main_([[maybe_unused]] rt::Depth depth) {
    rt::Array<int64_t> var1 = lit::array;
    const int64_t item2 = 1;
    var1 = rt::append(std::move(var1), item2);
    const rt::Array<int64_t> v3_read = var1.get();
    const int64_t item4 = 2;
    var1 = rt::append(std::move(var1), item4);
    const rt::Array<int64_t> v5_read = var1.get();
```

`var1` is declared as a plain `rt::Array<int64_t>` (no `rt::Cell<…>` anywhere in the file), and the
take/read of it is emitted as `var1.get()`, which `camel::standalone::Array<long>` does not have.
The interpreters agree with the expected output of the same case on all three backends
(`std::nvm`, `std::fvm`, `std::jit` print the four expected lines), so the defect is the C++ backend
alone.

## 3. What emits the `.get()`

`src/passes/trans/cpp/function_emitter.cpp`:

- `:1591-1614` (`:var/new`): `cellEscapes` decides the shape -- a plain `T name = init;` with
  `Cell::Local`, or a `const rt::Cell<T> name{init}` with `Cell::Shared`. For this case the plain
  local is emitted, so the marking is `Cell::Local` at the declaration.
- `:1654-1656` (a variable read): `read = cell.cell == Cell::Local ? cell.text : cell.text + ".get()"`.
- `:1618-1624` (`:var/take`): the same test, `cell.text + ".get()"` when not local.
- `:1736-1740` (`:struct/append_arr_in_place`): emits `var1 = rt::append(std::move(var1), item2);`
  for the local cell, and then stores the result as `CppValue{cell.text, false}` -- the marking is
  dropped from the frame entry it writes.
- `:1698-1704` (a write): stores `CppValue{written, false, kind}` -- this one keeps the marking.

The read that becomes `.get()` resolves to the append-in-place node, whose frame entry no longer
carries `Cell::Local`, so `:1656` takes the shared-cell branch while the declaration emitted a plain
array. The declaration shape and the read shape disagree; only the read shape is wrong.

## 4. The minimal difference from develop

The same command on develop gives a program that is identical except for the suffix:

```
(tip=0a1a509f5)  const rt::Array<int64_t> v3_read = var1;
(tip=c5c506e44)  const rt::Array<int64_t> v3_read = var1.get();
```

The declaration, the `rt::append(std::move(var1), item2)` form, the value names and the order are
the same on both, so nothing about the flip's types is involved in this program (it holds no
tensor). Eleven other trees built on the host were probed with the same command: everything derived
from develop (`tip=147e2d2d4`, `tip=0a1a509f5`, and the lanes based on them) emits **0** `.get()`,
and every tree derived from this integration line emits **13**.

The change that produced the difference is the var-version machinery of the in-place planning merge
on this line (`42d5ef8c0`), whose commit `0b1be856d8` changed `src/compile/gir/builder_expr.cpp`,
`src/compile/gir/builder_operators.cpp`, `src/builtin/operators/ops.cpp` and 13 lines of
`src/passes/trans/cpp/function_emitter.cpp` (the comment at `:1698-1701` describes it: a later read
of the variable reads the write's node). That commit is in this line and not in `origin/develop`,
so the emission difference is this line's, and it is not the tensor-alias flip's: the affected
programs are `int[]`-only.

## 5. What this file asks

Whether the fix belongs in the C++ backend (keep `Cell::Local` through the append-in-place and take
results, so a read of a plain local stays `var1`) or in the graph (a read of a non-escaping `var`
should resolve to the declaration's cell rather than to the write's node). Both are one place; the
readings above do not choose between them, and neither the plan nor any expectation was touched.