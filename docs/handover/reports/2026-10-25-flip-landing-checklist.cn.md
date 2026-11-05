> **来源说明**：这份清单 2026-10-25 写在临时目录 `D:\Projects\Camel-wt\notes\flip-landing-checklist.cn.md` 里，
> 从未进过 git；2026-11-04 由清理车道原样迁入本仓库，**正文一个字未改**。归档页是当时的记录，不改写。
> 它现在是否仍成立，见同一提交的说明与 `docs/handover/questions/`、`docs/handover/reviews/` 里的后续页面。
# 翻转线落地检查清单（协调者自用，仓库外）

这份是给"把 `handover/h419-flip-next`（含 P4 别名、翻转线各车道）合进 develop"那一次用的清单。
每一条都是**实测出来的**、丢了就会再现的红。

## 0. 整批集成的基座与清单（2026-10-25）

- **基座**：`handover/h437-splice-verbatim` 的 `0960856fe`（= 翻转基座 + develop + 不逃逸 `var` 化 SSA），
  它单独量整层是 **6108 / 6087 / fail 2**（只剩两条作者页欠账 `docs.links.citations`、`docs.snippets_compile`）。
- **要合进来的已完工车道**（各以 `handover/h443-flip-survey` + develop 为基座）：
  h445、h446、h448、h449、h450、h451、h453、h454、h455、h456、h457、h458、h460。
- **落 develop 时的生成文件**：`docs/diagnostics.md`（266 码）、AST/文法金标、`docs/stdlib-reference.md`、
  `camel-for-ai.md` 生成区、`export_matrix` —— 一律用脚本重生成，不手合并。
- **P4 的计划文字与依赖表已由决策者照录进 `docs/design/arrays-memory/plan.cn.md` 的 P4 一节**（作者页，别改）；
  `var` 形参裸 `Tensor` 时 cell 按声明形参类型接管是**过渡依赖**，P4 改写成显式泛型后删除；cell 的不变式不放宽。
- **等负责人（人）裁的一条**：数值数组上 `==`/`!=` 是整值相等（`bool`）还是逐元素——裁定前不改分派。

## 1. `Tensor` 别名落地必须带上 `fdc0813d0` 那段放行（否则 76 条红再现）

- 事实（h455 实测，2026-10-25）：`var_tensor.*` 33 + `var_grad.*` 33 + `var_tensor_read*` 10 = **76 条**在
  **h419 翻转线上是 `[PASS]`**（`-Plan test/plans/feat/linear,test/plans/feat/opt` = 1548/1502/42/known 4，
  42 条里没有任何 `var_*`）。76 条只在 `handover/h438-tensor-alias`（= develop + 别名）上是红的。
- 根因：h419 的 `src/compile/gir/builder_operators.cpp` 有
  `fdc0813d0 gir: give a var argument's cell the declared parameter type` 那段放行（负例钉在 `e57d5febe`）；
  `origin/develop` 与 `handover/h438-tensor-alias` 上 `grep isNumericUnknown src/compile/gir/builder_operators.cpp` = **0 命中**。
  也就是说 h438 的分支是"develop + 别名"，**丢掉了 h419 早就有的放行**。
- 落地动作：合并 h438（或整条翻转线）时，**必须确认 `builder_operators.cpp` 里那段按声明形参类型放行 var 实参 cell 的块在**；
  不在就把 `fdc0813d0` 一起带过来。合并后拿 `var_reference.plan.toml`（180 条 `var_*`）与
  `-Plan test/plans/feat/linear,test/plans/feat/opt` 的红集合逐条 Compare-Object 验证。

## 2. 改一个遍名要同步两处

- `pass.cpp` 注册 + `stdlib/std.cml` 的声明（`check-pass-declarations.mjs` 的"声明 ↔ `registerCoreKernel` 双射"），
  只注册不声明会让管道直接 `RuntimeDiag::InvalidPassParameter`（h437 首跑踩到）。

## 3. `print.cpp` 有两处 hunk 都要留

- `pushArray`：每层只推 `]`、只由调用方写一个最外层 `[` ⇒ 秩 ≥2 丢最内层 `[`。
  h438 `32db3ae92`、h451 `ece64b777`、h450 `878b6789d`、h454 `53dd299df` 各修过一次（内容一致），合并时按内容取一份。
- `printValueImpl`：秩 0 的数组走 `pushArray` ⇒ `dim(0)` 越界读 `extents_[0]`（Debug 才有 ASSERT）且下降条件
  `dim + 1 == rank` 永不成立 ⇒ **无限压栈的死循环**（h458 实测：三后端各 27 s，`EXIT=124`）。
  修法是 `printValueImpl` 里新增 rank 0 分支 `pushSlot(stack, array->getSlot(0), elemType)`，与 `Array::print`（`array.cpp:1201`）同文本。
  h458 `dc3c8968b`。**两处 hunk 位置不同（一个在 `pushArray`、一个在 `printValueImpl`），合并时不要互相覆盖。**
- 只在翻转线复现的原因：`Tensor` 成别名后，声明类型是数组时 `wrap`（`modules/tensor/ops/support.cpp:87-100`）把秩 0 结果
  保留成数组 ⇒ 走 slot 数组分支；develop 上是独立 `TensorType`，走 `printScalar`。

## 4. `var` 的读写作模型（H242a）落地顺序（作者 2026-10-24 夜裁定）

1. 先落**不逃逸 `var` 化 SSA**（读接最近一次写、不建 cell；所有权/能否原地交给内存规划，不用 `setHold`）。
2. 再落**逃逸那一类**：保留 cell + 写产出"这一版的 cell" + 读与捕获调用的数据端口 `PortRole::Capture` + 写写之间顺序边。
3. `:var/new` 不取初值只与逃逸那一类有关，与捕获端口同一片。
4. 撞点（h437 报备）：`orderAfterLastWriter`（SSA 后退回 `ORDER`）、`markNonRetainingReads`/`:var/peek`（不再是所有权标记）。

## 5. 已在队列里的翻转线缺陷

- `max`/`min` 轴归约到秩 0、元素含 NaN ⇒ **死循环**（h450 实测：本树 60 s 超时 `EXIT=124`，develop 立刻返回 `nan`）。
- 依赖模型的递归到不了不动点（h449 实测：`int[1]` 精化 `int[]`，特化 key 一直变 `walk$tspec1$tspec4$spec5…`，
  撞编译期增长上限；develop 45.6 s CPU vs 翻转后 11m16s wall）。
- 数组下标丢了静态越界检查（h450 实测：`bounds/{index_const,index_declared_shape}` 只剩运行期 `IndexOutOfRange`）。
- `for i in range(a, b, step)` 的融合挂死（h444 实测；线索：`intervalSequenceValue` 的 `::Tuple::create(2, …)`）。