# The function form `slice(t, 0, a, b)`: what the core's one slice operator answers of it, and the two readings it leaves

Status: open, for the language author. Raised by the lane that fixes the core slice adjoint's
element type, deletes the tensor module's subscript copy and `isTensorSubscript`
(handover/h585-core-slice-adjoint, based on `handover/h465-flip-integration` tip `e2bcfeac1`, not
pushed).

## What the ruling asks

`docs/design/arrays-memory/plan.cn.md:393` -- on `origin/develop`; the flip line's own copy of that
file predates the row (the flip tip has no `isTensorSubscript` row at all):

> | 切片：张量模块的 `tensor:slice`（含 `__slice__` 导出）与 `globals.cpp` 里按"是不是程序自己的数组"分派下标的 `isTensorSubscript` | 模块的 `Tensor` 值经它参加 `t[a:b]`，核心只答 `T[]` | 切片只有核心一个算子、一条伴随规则（`t[a:b]` 与 `slice(t, 0, a, b)` 同一个节点，对任何秩、任何元素类型），模块副本与按类型分派的那段一起删掉；伴随交回的值带写明的元素类型与形状 |

and the author's ruling of 2026-10-25 (`docs/handover/reviews/questions-2026-10-24-evening.md`,
section `discrete-gradient-and-rank-one-slice-ownership`, point 2):

> **切片没有收窄线，`t[idx]` 也没有**：两种 `[]`（取行、切片）都**只由核心回答**，对任何秩、任何元素类型都是同一条规则、同一条伴随规则。翻转线上按"是不是程序自己的数组"分派给核心或模块，只是 P4 合入之前两种类型并存的产物，和 `idx_take` 的模块副本是同一类东西。我已把**切片的模块副本和 `isTensorSubscript` 分派列进 `plan.cn.md` 5.1a 的依赖表，P4 合入时一起删，develop 上不出现这条线**。翻转线上眼下的分派只为让用例先跑通，**不要再往里收或往外放**。**秩 1 且长度已知的值，它的伴随类型问题由核心的切片伴随直接给出写明的类型来解决**，不靠把它划给模块。

The target state: one operator, one adjoint rule, `t[a:b]` and `slice(t, 0, a, b)` the same node --
for any rank and any element type.

## What this lane did

* The core's slice adjoint hands back a value of the element type the forward value has: the
  `:tangent/scatter_slice_arr` kernel created its result with `Array::create(allocator, size)`, an
  array of bare slots, so the gradient carried no element type and the next tensor operator to read
  it refused with `sum: Unsupported Tensor dtype`. That is the "伴随交回的值带写明的元素类型与形状"
  column. `:tangent/scatter_add_arr` (the element take's adjoint) had the same defect and is fixed
  with it.
* The subscript is the core's alone: `isTensorSubscript` is deleted, and the tensor module's
  subscript copy (`__slice__` export, `sliceSubKernel`, `sliceSubInfer`, and the argument-count
  dispatch in `sliceKernel`/`sliceInfer`) is deleted with it. `sliceVjp`'s six-word mask branch goes
  too, since no subscript reaches the module any more.
* The function form is the core's for the calls the core can read (below); every other call is still
  the tensor module's function form (`sliceFunctionKernel`/`sliceFunctionInfer`/`sliceVjp`).
* A registered operator of the core needs a declaration in `stdlib/signatures/globals.cml` or the
  registry checks refuse it (`no declaration of 'slice' matches the registered :op/slice_sub`), so
  the core's function form is declared there, beside `:struct/slice_arr`, and the two generated
  documents were regenerated (`docs/stdlib-reference.md`, `docs/reference/camel-for-ai.md`).

## One blocker this lane cannot clear: the AI reference's whole-file budget

`docs/reference/camel-for-ai.md` was 111972 characters against the 112000 its own line 37 states:
28 characters of slack. The declaration above adds one line (66 characters) to the generated
`stdlib-signatures` region, so the regenerated document is 112030, and `docs.ai_reference.current`
and the `camel-for-ai` checker now fail on the size alone. The document's parts have room (that
region is 26243 of 28000, the hand-written half is 55193 of 55400), so the whole-file budget is the
binding one. Two remedies, both outside this lane's files (its `docs/**` allowance is question pages
only, and the budget is stated in the document and read from it): trim about 30 characters of
hand-written prose in that document, or raise the number it states. Every later built-in declaration
meets the same wall.

## The reading: what lands where

Measured on `f32[4]` (`reshape([1., 2., 3., 4.] as f32[], [4])`), recovered whole from `std::gir`
(the runner prints only the last twelve lines of a failed test's stdout, so the text below was
walked back one window at a time; `<ROOT>` is the normalizer's stand-in for the worktree path):

`x[1:4]` -- `test/cases/modules/tensor/subscript/slice_node_subscript.cml`:

```
N02 [label="tensor:reshape", type="OPER", origin=978, span="10:12-10:19", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_subscript.cml", value_type="f32[4]"];
N08 [label=":op/slice_sub", type="OPER", origin=982, span="11:13-11:14", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_subscript.cml", value_type="f32[]"];
N00 -> N02 [label="0|0", type="NORM"];
N01 -> N02 [label="0|1", type="NORM"];
N02 -> N08 [label="0|0", type="NORM"];
N03 -> N08 [label="0|1", type="NORM"];
N04 -> N08 [label="0|2", type="NORM"];
N05 -> N08 [label="0|3", type="NORM"];
N06 -> N08 [label="0|4", type="NORM"];
N07 -> N08 [label="0|5", type="NORM"];
N08 -> N09 [label="0|0", type="NORM"];
```

`x[2:]` -- `slice_node_open_end.cml`:

```
N02 [label="tensor:reshape", type="OPER", origin=977, span="9:12-9:19", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_open_end.cml", value_type="f32[4]"];
N08 [label=":op/slice_sub", type="OPER", origin=980, span="10:13-10:14", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_open_end.cml", value_type="f32[]"];
```

`slice(x, 0, 1, 4)` -- `slice_node_function.cml`:

```
N02 [label="tensor:reshape", type="OPER", origin=980, span="10:12-10:19", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_function.cml", value_type="f32[4]"];
N06 [label=":op/slice_sub", type="OPER", origin=985, span="11:12-11:17", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_function.cml", value_type="f32[]"];
N02 -> N06 [label="0|0", type="NORM"];
N03 -> N06 [label="0|1", type="NORM"];
N04 -> N06 [label="0|2", type="NORM"];
N05 -> N06 [label="0|3", type="NORM"];
```

`x[[0, 2]]` (`t[idx]`, rank 1, length stated) -- `slice_node_take.cml`:

```
N02 [label="tensor:reshape", type="OPER", origin=991, span="11:12-11:19", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_take.cml", value_type="f32[4]"];
N04 [label=":op/idx_take", type="OPER", origin=994, span="13:13-13:14", srcfile="<ROOT>/test/cases/modules/tensor/subscript/slice_node_take.cml", value_type="f32[4]"];
N02 -> N04 [label="0|0", type="NORM"];
N03 -> N04 [label="0|1", type="NORM"];
```

The three slice spellings are one operator, `:op/slice_sub`, and the spelling is what the argument
count says: the subscript writes six words (value, axis, start, end, step, written mask), the
function form four or five (value, axis, start, end[, step]). `t[idx]` is the core's `:op/idx_take`
-- the operator the ruling says answers it for a rank-1 value, already true before this lane. The
plans assert these labels byte for byte (`slice_node_subscript`, `slice_node_open_end`,
`slice_node_function`, `slice_node_take`, and `slice_one_node` with all four in one graph).

## The function form: what the core answers, and why not more

The core's `slice` group gained a `:op/slice_sub` rule for `slice(t, axis, start, end, step?)`. It
answers exactly: `t` is a rank-1 array of a stated element type, `axis` is the constant 0 (the only
axis a rank-1 array has), `step` is the constant 1 or not written, and both bounds are constants
inside the length the type states. Anything else -- another axis, rank 2 or an open rank, an axis or
a bound the compiler does not know, a bound outside the length, a bound that counts from the end --
stays with the module. Measured: `slice(x, 0, 1, 4)` on `f32[4]` is the print above; the same call
on a value of unknown length or another axis is `tensor:slice`.

Why the rest is left where it is: the core's reading of the arguments and the module's are not the
same reading, and giving one the other's calls would change what a program computes, not only where
it computes it.

* **A negative bound.** The module's function form counts a negative bound from the end
  (`k::normalizeSliceBounds`, `modules/tensor/ops/layout_ops.cpp`); the core's slice reads it as a
  fault (`sliceRange`, `src/builtin/operators/struct.cpp`). `slice(x, 0, -2, 3)` on a length-3 value
  is `x[1:3]` from the module today (asserted by `bounds.in_range`).
* **A bound outside the length.** The module refuses it as `slice bounds 8..9 are outside axis 0 of
  extent 3` (asserted by `bounds.slice_const`); the core refuses the subscript as `slice bounds 0..9
  are outside the first dimension of extent 4` (asserted by `subscript.slice_const_bounds`). Two
  wordings, both asserted, so each spelling keeps its own for now.
* **A rank above 1, or an open rank.** The module's function form normalizes the axis and slices any
  axis of any rank; the core's slice operator is rank 1 only (`tiles/index_maps.cml` writes
  `slice(x, -1, -6, -1, 2)`).
* **A step other than 1.** The module's function form keeps it; the core's slice refuses a step
  other than the constant 1 as the H17 view with strides.

## The question

1. Is this split the wanted end state, or should the core's function form take over every
   `slice(...)` call -- which means moving the module's reading of its arguments (a negative bound
   from the end, the axis of any rank, per-axis normalization) into the core, or changing what those
   programs compute?
2. If the core's slice is "one rule for any rank and any element type", which rank does it answer:
   rank 1 only (today, and the H17 view for anything else), or a slice of another dimension as a
   value too -- the multidimensional array representation H17 waits for?
3. Which reading of a negative bound is the language's: from the end (the function form today) or a
   fault (the subscript today)? `t[-1:]` has no written "from the end" anywhere today; the mask says
   only which bound was written.
4. Is the argument count the wanted spelling boundary (the subscript writes the two-word
   step-and-mask tail, the function form does not), or should the function form write the mask too,
   so that one word count answers `slice` and `[]` alike?

Until the author rules, this lane kept every call the core cannot answer on the module, changed no
diagnostic wording, and relaxed no expectation.