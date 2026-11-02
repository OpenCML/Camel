# 循环的反向半程「按轮倒序」在今天的树上不可观测：先按 4.2 落「每轮一条残差」等聚合状态能 lift，还是先放宽 `pairTypeOf` 让它可判？

**状态**：待裁；我方默认见第 4 节，已落的是「每轮一条残差」，**没有**落「倒序」（第 4 节写明为什么不在这笔里）。

## 1. 设计要求的是什么

`docs/design/program-transforms/README.cn.md` 4.2 的循环一条：每一轮的残差作为收集输出保存（是值不是闭包，原则 9.9），后一个函数里循环按同样的轮数执行，**逐轮取残差**；4.3 把它说成「倒着走，逐轮取 `split` 留下的残差」。

合起来是两件事：**(i) 残差按轮存**——必须有「这一轮」这个身份，不能只留最后一次的值；**(ii) 反向按轮倒序取**——倒着走时第 t 轮的残差配第 t 轮的余切。第二件事只有在「正序与倒序给出不同的数」时才是可观测的。

## 2. 实测：今天的树上，能到达 `split` 的循环输入分辨不出走序

- `jvp` 只给**浮点标量**配对：`pairTypeOf`（`modules/transforms/jvp.cpp#pairTypeOf`）要求 `tangentSpaceOf` 回答 `Self` 且 `isFloatingScalar`，聚合体（元组、数组）一律返回空。
- 所以被 lift 的循环，其状态口只能是**一对 `float`**：状态是标量。
- 单 `float` 的逐轮导数是标量乘积；标量可交换，所以正序累乘与倒序累乘是**同一个数**。
- 读数（`test/cases/transforms/split/split_loop_taped_rounds.cml`，因子 2、3、5）：`loop y= 30.0`、`loop d= 30.0`；三个因子的任何排列都给 30。
- 结论：**本树里不存在一个 `split(jvp(f))` 的输入，使正序与倒序打出不同的数。** 这不是「没测到」，是「在当前 lift 下测不出来」。

## 3. 我试过的造法（想让它可分辨，结果卡在更上一层）

单状态、状态是一个 2×2 矩阵（四个 `float` 打包成元组），每轮 `S ← M(k)·S`，`M(k) = [[1, k], [k², 1]]`，k = 2、3。这两张矩阵**不交换**（`M(2)·M(3) ≠ M(3)·M(2)`），所以正序与倒序必然给出不同的 2×2。

读数：

- **前向原程序跑通**（`primal=` 打出）：语言接受「单状态元组」，这条路的第一半是通的。
- 同一个 `f` 进 `split(jvp(f))`：

  ```text
  REINTERP jvp function=f skip=body
  ```

  即 `modules/transforms/jvp.cpp#liftGraph`，外层报 `transforms: jvp has no forward transform for this function.`，exit 1。
- 卡点**不是**多状态（那会是 `skip=loop`），是 `skip=body`：状态是聚合体 ⇒ 没有对 ⇒ 循环体里的节点不是可 lift 的值。我到不了 `split`，也就拿不到 Y1 / Y2 这一对数。

## 4. 我方默认

- `split` 侧：**按 4.2 落「每轮一条残差」**（已实现，`12b9716c0`）：前向循环多一个收集槽，每轮一个元组；`lin` 迭代这卷胶带、按轮配对，胶带随 `lin` 的闭包过切口，不新增残差元组。读数 `residuals=1 loop=1 tape=2 invariants=0`。
- **倒序那一步没有落，也不在这笔的范围**：它是 4.3 的 `transpose`，而 `transpose` 今天仍按原样复制循环区间 ⇒ **`vjp` 过循环不保序**。这是 4.2 的下一个缺口，单独一页，我没有扩它。
- 因此我把两件事都写进用例注释与 4.2 的下一行：**「倒序未实现」，且「倒序不可观测」**——不写成「已保序」。
- **判别用例等聚合状态能 lift 之后补**（补的时候用第 3 节那条矩阵构造，正序 `Y1`、倒序 `Y2`，`Y1 ≠ Y2` 才算数；若届时仍相等，就照实写「这条读数分辨不出顺序」）。
- **不为让它可观测而放宽 `pairTypeOf`**：聚合体的对是数组余切那一轮的工作，是另一件事，超出本笔。

## 5. 怎么让倒序落地（下一个人照着做）

这一节是 2026-11-02 那一轮只读调查的结论，四件事，按顺序读。

**(1) 顺序从哪来：不是「复制了节点顺序」，是「区间与种类都没变」。**
`transpose` 遇到 `LOOP` 不是整段复制——它**新建**一个 `LoopBody` 区域，把旧体的每个节点按规则逐节点转置成余切链（`modules/transforms/transpose.cpp#buildTransposeMember`，区域在 `draft.addRegion(GCRegionKind::LoopBody, 0)` 处新建，体由 `runSeeded(bodyRegionOld, seeds, statePorts)` 重放），新端口按 state 数加逃逸数重排。照抄的只有两样：**区间这个值**（`forward(oldNorm[i])`，注释原话：the range … copied as they stand, because the reverse loop iterates over the same values）与**循环种类**（`draft.addLoopNode(rt::loopBodyOf(*function, ref), loopType)`）。执行器看到的是「同一个区间值 + 同一个 `LoopKind`」，所以它照原样从第 0 轮走到第 n−1 轮。**要改顺序，改的是这两样之一，不是节点次序。**

**(2) 最小改法：把区间换成 `reverse(range)`。**
语言已有它：`stdlib/array.cml#reverse` 就是 `xs[::-1]`，一个切片节点，返回反序数组。把它插在循环的区间口上，循环仍正序迭代——只是迭代的是反序数组——因此**不动 LOOP 语义、不动四个执行器、不动尾递归**。代价：反向时物化一份 n 元数组（12.3 那种长的循环会多一份与胶带同长的副本；如果 n 大到不能接受，见下一条）。
**替代写法**（更麻烦，别先选）：整数索引倒着取（`n-1 … 0`）配 `Accs(tape, i)`，不需要数组副本，但要把体内**每个**读胶带的点改成按索引取，而且今天 `Accs` 的收集槽语义只有正序下标（收集槽按轮序追加，`include/camel/core/loop_kind.h#LoopKind` 那一族注释、`camel/runtime/loop.h` 的 slot 段）。那是改体，不是改一个口。

**(3) 为什么现在不能落：落了也观测不到。**
`transpose` 的循环体今天只重放旧体，规则需要「当轮状态」时按 `skip=state` 拒绝（同一文件那两处 `skip=state` 出口）；逐轮残差（胶带）只在 `lin` 的闭包里（`modules/transforms/split.cpp#cutLoopGraph`），**不过切口**。所以「倒序」与「正序」目前只差一个标量乘积的次序——可交换，读数相同。只改顺序，改的是一段没人能验证的走法。**落地的那一天，判别用例是第 3 节那族 4-tuple 非交换矩阵，不是今天任何一条**：那族的前向已经跑通，等的只是聚合状态能 lift（第 2 节）。

**(4) 前置条件。**
`stdlib/vjp.cml#vjp` 里那一行（`transpose(lin)(ct)`，`Transpose<F>` 的类型律）先进 develop——**`lane-typelaw` 在做**。那条一落地，胶带才有可能作为 `transpose` 的输入过切口，「倒序」也才有东西可读；再往后才是第 3 节的判别用例。

## 7. P1-9 是四处，不是一处

作者 15:30 的裁定把「聚合状态成对」列进 P1-9，而且是按**一处**（`pairTypeOf`）在排优先级。**实际是四处**，都在 `jvp` 的 lift 路径里；任何一处不落，元组状态都进不了循环体。唯一能分辨进度的指示灯只有一行：`REINTERP jvp function=f skip=body` 是否消失（`modules/transforms/jvp.cpp#liftGraph` 的 `skip=body` 出口）。今天它出现，而前向原程序跑得通（第 3 节）——缺的是 lift，不是语言。

按依赖排，四处与各自的理由：

1. **`pairTypeOf`**（`modules/transforms/jvp.cpp#pairTypeOf`，今天只认浮点标量）。入口：它不回答，后面三处的代码根本走不到。
2. **循环体的端口配对**（`liftGraph` 的 `Port` 分支）。合体之后 body 的区域端口（状态口）必须拿到 pair；今天只有 `pairOf(dataType) != nullptr` 的端口才配，元组端口在这里掉出去。
3. **聚合体的零**（`zeroConstantFor`）。今天写的是标量 `0.0`；元组/数组"没被读到的位置"要按 `tangentSpaceOf` 造零，否则第 2 处配出来的对里会有一个假数字。
4. **`Accs` 投影配对**。状态是元组时，body 里读 `s.0` 是 `Accs(statePort, 0)`，每个投影自己也要成对——这一处不落，元组状态进去了也会在第一个成员上断掉。

顺序就是 **1 → 2 → 3 → 4**：按"能不能进得去"排，每落一处看一次那行指示灯，直到第 4 处它才必然消失。四处全落完，才是第 3 节那个 2×2 非交换矩阵的判别（正序 Y1 / 倒序 Y2 / `Y1 ≠ Y2` / `vjp` 给倒序那个）。

**同一轮顺带否掉一个想法（会改动第 5 节第 (2) 条的估算）**：`:struct/slice_arr` **不在算子注册表里**——`src/builtin/operators/struct.cpp` 的 `:struct/*` 只有 `concat_arr`、`copy_arr`、`len_arr`、`append_arr*`、`zip`、`range` 这些，没有切片；`xs[a:b:s]` 的降级走 `src/compile/gir/builder_operators.cpp` 的路径机制，`step` 只作为 `:tangent/scatter_slice_arr` 的梯度实参出现。所以「拿现成切片算子构造 `reverse(range)`」**在建图 API 上没有现成口**：要么给 draft 加一个数组反转助手，要么在 body 里用整数索引倒着取（即第 5 节说的"更麻烦"那条）。

**2026-11-02 更正：是五处，不是四处；而且第 2 处不用写。** 上面那份清单是读数推进之后回看的版本，按它排优先级会少一处：

1. `pairTypeOf`（入口，待写）。
2. **循环体的端口配对 —— 待激活，不是待写**：`modules/transforms/jvp.cpp` 的 `Port` 分支里已经写着"状态口是循环携带的那个对"（`statePair = pairOf(...)`，为真就建 pair 端口并投影两半），它不跑只是因为 `pairOf` 对聚合体回答 null。第 1 处一放宽，它就自动启动。
3. **`LOOP` 节点自身的配对 —— 新写**：值里的收集槽（`collectionCount` 那段数组）今天在 `jvp.cpp` 里 **0 命中**。第 1+2 激活之后，失败会推到这一处：循环的值是 `Tuple(state…, 数组…)`，`pairOf` 一旦也认数组，循环的配对类型就是 `Tuple(值, 值)`，而循环体交回的是 `(next, tapeTuple)`，两边对不上 ⇒ `complete = false` ⇒ 仍报 `skip=body`（实测：放宽 `pairTypeOf` 后 `split_loop_taped_rounds.nvm` 从过变错，`jvp has no forward transform`）。
4. 聚合体的零（`zeroConstantFor`，待写）。
5. `Accs` 投影配对（待写）。

所以顺序是 **1 → 3 → 4 → 5**，第 2 处只是跟着 1 启动；**1 单独落会让现有 `split` 循环用例变红**，这不是回归的信号乱，而是"门口放宽了、循环节点还没学会配"的必然后果。

## 要作者答的一句

**「循环反向半程的『按轮倒序』在今天的树上不可观测（能到达 `split` 的循环状态只能是 `float`，逐轮映射可交换），而『倒序』本身属 4.3、`transpose` 仍原样复制区间：我按 4.2 落了『每轮一条残差』、没落倒序，并写明『倒序未实现、不可观测』，等聚合状态能 lift 后再补判别用例——除非你要先放宽 `pairTypeOf`。」**

## 这条堵着谁

- 判别用例（本页第 3 节），等**聚合状态的 lift**；
- `vjp` 过循环的正确性，等 **`transpose` 反向走区间**（4.3），与 `stdlib/vjp.cml` 那条 29 红的 `transpose` 类型问题不是同一件事，但都落在同一次 `transpose` 返工里。