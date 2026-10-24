# A read that takes the version node: what should the CML printer write for it?

Status: open, for the language author. Raised by the lane that lands "a write's version is its
cell" (handover/h464-escape-cell, based on `origin/develop`).

## What the lane changed

Following the ruling in `docs/handover/reviews/questions-2026-10-24-evening.md`,
§`var-version-node`, a `var` that escapes keeps its cell, `:op/assn*` returns this version's cell
(`TupleType::cell(T)`, `return toSlot(cell)`), and **a read's data port takes the latest write's
product** (`src/compile/gir/builder_expr.cpp`, `readVariableThrough`). Where the writer is not a
cell producer (a call, a loop), the read keeps the cell.

## What the round trip now does

`test/plans/feat/trans/cml.plan.toml`, test `print_and_recompile`
(`test/cases/trans/cml_contract.py` `check_round_trip`, on `test/cases/trans/cml/linear.cml`),
fails on the printed document, not on the run:

```
File "test/cases/trans/cml_contract.py", line 201, in main
    check_round_trip(source)
File "test/cases/trans/cml_contract.py", line 159, in check_round_trip
    after_text, after = canonical(printed_path)
File "test/cases/trans/cml_contract.py", line 82, in canonical
    assert done.returncode == 0, (source, done.stdout, done.stderr)
AssertionError: (PosixPath('/tmp/cml-contract-.../linear.printed.cml'),
  "error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'cml1'.
   --> /tmp/.../linear.printed.cml:11:24
 11 |     println('bonus =', cml1)
   --> /tmp/.../linear.printed.cml:12:19
 12 |     return string(cml1) + ' points'
camel: 2 errors", '')
```

`cml1` is the name the builder bound for the write's product (`bindTemporary`); the printer emits
that name where the program read the variable, and nothing in the printed document declares it.
Read with the cell as its data port, the same program prints the variable itself and compiles.

## The question

Is the printer (or the materialization the printer reads from) supposed to resolve a read whose
data port is a write's product back to the variable/cell, or should a version read be given a
name of its own that the printed document can declare? The lane did not touch the printer
(`src/trans/cml` and the printing side are outside its file scope); the round trip is red until
one of the two is decided.

## What the lane measured otherwise

With the lane's change on `origin/develop` (`922a00d8e`), `feat/linear` and the anchor
`var_param_reference.an_escaping_var_keeps_its_cell` are green, and the gate set
(`-Plan smoke,test/plans/feat -Tier fast -Jobs 8`) is red on exactly: this round trip, the
`modules/resources` footprint record (updated by the lane), the `onnx` `export_var_*` family
(handed to the memory-planning lane, §4.3.1 of `docs/design/arrays-memory/README.cn.md`), and
nothing else.