# `declaration_evaluator` 的 43 条不一致：声明与内核在混合宽度、未知 extent 的 reshape、空 extent 归约上说法不同（h506b 车道，2026-10-25）

> 本文件只报读数与选题，不定语义。`docs/design/**`、`docs/design-principles*`、`docs/language/**`
> 一个字未动；本车道只改了 `modules/tensor/ops/vjp.{cpp,h}`、四个用例与其计划条目。
> 所有读数取自本车道 tip `8bc2d0e3c`（基座是翻转线 `handover/h465-flip-integration` 的 `3892afcf4`）。

## 一、这条红不是"过渡类型"那一族

用例：`test/plans/feat/modules/index_semantics.plan.toml` 的 `declaration_evaluator`，跑
`test/cases/modules/tensor/index_semantics.cml` 的 `tensor::test_index_eval`
（test-only 遍 `modules/tensor/passes/index_eval.cpp`）。它按声明造实例、与 CPU 内核逐个比对，
**全程没有 `grad`、没有 VJP、也没有裸 `Tensor`**；操作数的元素类型都是写下来的
（`f32`、`f64`、`i64`、`bool`）。

逐字读数（`camel test/cases/modules/tensor/index_semantics.cml tensor::test_index_eval`，exit 0）：

```
EVAL total operators=33 cases=2780 agree=2737 faults=203 points=53887 unsound=0 mismatches=43 undeclared=0
```

打印出的不重复 `EVAL_MISMATCH` 行 27 条，共 43 个 mismatch，分三族：

1. **混合宽度（15 条）**：张量的元素类型与标量的宽度不同时，内核与声明给出不同的结果 dtype。
   ```
   EVAL_MISMATCH op=tensor:add (f32[], -2.895099639892578): dtype differs (kernel f64, declaration f32)
   EVAL_MISMATCH op=tensor:add (3.217700719833374, f32[]): dtype differs (kernel f64, declaration f32)
   ```
   同形的还有 `tensor:subtract`（3 条）、`tensor:multiply`（4 条）、`tensor:maximum`（4 条）。
   `f32[]` 是 rank 1、extent 未知的张量；另一个操作数是 f64 常数。
   同一写法在编译器上是**拒绝**的（逐字）：
   ```
   error[SemanticDiag::ArgumentsRejected]: Invalid call to '__mul__' with argument types <> (float, f32[?, ?]):
   the scalar operand has type 'float', but the tensor's element type is 'f32': a tensor operator computes in
   its element type, and there is no implicit conversion between numeric types (write the conversion, f32(value)).
   ```
2. **未知 extent 的 reshape（4 条）**：内核接受、声明给不出实例。
   ```
   EVAL_MISMATCH op=tensor:reshape (f32[], [1]): no instance for a call the kernel accepts
   ```
   （另有 `bool[]`、`f32[], [?]` 各一条，`f32[], [1]` 出现两次。）
3. **空 extent 的归约（8 条）**：内核 fault、声明给实例。
   ```
   EVAL_MISMATCH op=tensor:min_axis (i64[0], -1): the kernel faults, the declaration gives an instance
   EVAL_MISMATCH op=tensor:argmax_axis (f32[0], 0): the kernel faults, the declaration gives an instance
   ```

按算子计：`add` 4、`argmax_axis` 4、`maximum` 4、`min_axis` 4、`multiply` 4、`reshape` 4、`subtract` 3。

## 二、本车道的改动到不了它

本车道只改梯度节点的类型（`vjpTensorType` 由被微分值的切线类型给出具体元素类型）与四个用例的签名。
`tensor::test_index_eval` 是声明求值器，与 VJP 无关：改动前后这条用例的读数一字不差
（翻转线 tip 上同样是 `mismatches=43`）。同一台主机上另有一份 develop 口径的旧构建
（`~/camel-work/Camel`，`147e2d2d4`，2026-10-23）在同一用例上是：
```
EVAL total operators=33 cases=2780 agree=2780 faults=211 points=53904 unsound=0 mismatches=0 undeclared=0
```

## 三、要请作者定的三件事

1. **结果 dtype 跟随谁**：`add(f64 标量, f32[?] 张量)`——声明给 `f32`（跟随张量），内核给 `f64`（跟随标量）。
   编译器把这种写法整个拒了，所以先要定这一处，才能定求值器要记哪个事实。
2. **未知 extent 的 `reshape`**：`reshape(f32[?], [1])` 内核接受、声明无实例——事实表写哪一侧？
3. **空 extent 的归约**：`min_axis`/`argmax_axis` 在 `f32[0]` 上内核 fault、声明给实例——判据在哪一侧
   （空轴归约是"无单位元"的 fault，还是"实例"）？

## 四、本树上写不出 `f32[?, 1]` 这样的 `var` 形参/收 cell 的签名（读数）

`lrit` 里按"按各自实际的秩写"写 `update(var w: f32[?, 1])`、`train(weights: f32[?, 1])` 时：

```
error[SemanticDiag::ArgumentNeedsConversion]: The call to 'update: <X: f32[?, ?], y: f32[?, 1], lr: float>
(var w: f32[?, 1]) => f32[?, 1]' needs an explicit conversion: argument 1 ('cur', for parameter 'w') has type
'f32[?, ?]', which is less exact than 'f32[?, 1]'.
help: A type that leaves open what a parameter fixes is never accepted implicitly ... a variable has the widened
type of its first value. Pass the value to a parameter that is not var, or declare the var parameter with the
variable's type and convert the value where it is read.
```

`var` cell 的类型是同秩、所有 extent 变 `?` 的加宽型（`docs/design/arrays-memory/plan.cn.md` 的 Q5），
所以收 cell 的形参上固定 extent 就收不到它。本车道因此把这些形参写成 `f32[?, ?]`
（秩 2 与元素类型都写下来，extent 打开）。要请作者确认的是
`docs/design/arrays-memory/plan.cn.md :448` 的"示例里用 `f32[?, 1]` 这类写法"是否随之改口径。

另：`:323` 仍写着 `var w = zeros([2, 1]); w = grow(w) (Tensor any)` "依赖任意秩加宽；按 Q5 的结论
（同秩加宽）改写成两次 `var` 或显式 `as`"。本车道的实例读数相反：`grow` 的结果写成 `f32[?, ?]` 之后，
`w = grow(w)` 原样保留即可（同秩加宽收得下），只有 `ones([5])` 这一处需要自己的 `var`。

## 五、读数口径（补注）

本页的读数全部取自翻转线 `handover/h506b-bare-tensor` tip `8bc2d0e3c`；本页加入时是 `39784865d`，
其父即该 tip。develop 侧同一条用例是另一组读数：`declaration_evaluator` 在
`test/plans/feat/modules/index_semantics.plan.toml` 里，在 `origin/develop` `c3b17207e` 之上只多出
`docs/handover/` 文件的树上它是 `[PASS]`，而它的校验脚本要求 `mismatches === '0'`——与第二节记的
`147e2d2d4` 旧构建读数一致。也就是说第二节那 43 处不一致是翻转线上的读数。
页内引的 `docs/design/arrays-memory/plan.cn.md` 那两段，在 develop 上是同一处文字。