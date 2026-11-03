# 一个归约要不要有「整轴」算子：`sum`/`mean` 有，`max`/`min`/`argmax`/`argmin` 没有

> **裁定（作者，`5fd6fd902`）——选 B：四条整轴形态要补。** 依据 `docs/design-principles.cn.md:548`：
> 整轴归约与沿轴归约**同名**，沿轴那条带 `with <axis, keepdims>`——所以 `max(x)` 这四个名字本来就该能写，
> 今天写不出来是实现落后于原则，不是语言里没有这个东西。语义不由实现另定：空张量是 `InvalidArgument` 故障
> （`:575`），`NaN`/`-0` 的次序与 `argsort` 同一条，整轴 `argmax`/`argmin` 答**行优先展开的位置**、并列取最小。
> 已落地：`modules/tensor/ops/reduction_ops.cpp` 的四个 OpDef 与 kernel、`stdlib/signatures/tensor.cml`、
> `vjp` 规则、`vmap` 的整轴批规则（`max`/`min` 有；`argmax`/`argmin` 缺扁平视图，另有一页在等作者：
> `questions-2026-11-01-a-batched-argmax-needs-a-flat-view.md`）。下面三节是裁定前的事实与三条路，留作记录。

> **问什么**：`sum` 与 `mean` 有一个实参的整轴形态（`sum(x)`、`mean(x)`，URI `tensor:sum` / `tensor:mean`），
> `max` / `min` / `argmax` / `argmin` **没有**——它们只有带 `axis` 的那一条。要不要给这四个补整轴算子？
> **堵着谁**：今天**没有人被它堵住**。`vmap` 的覆盖表里这四行只能写「没有这个算子」，不是「规则缺」；
> 谁要批一个含 `max(x)` 的函数，得自己写 `max_axis(xs, 0, false)` 再折，或者从 `max_axis` 起手。
> **我没能自己定的理由**：这不是缺规则，是**语言里没有这个东西**——要么承认它不该有，要么就得新开一个算子
> （OpDef、kernel、infer、vjp、签名、导出各来一遍），那是一条会影响 `tensor:*` 整体形状的决定，不该由一条车道顺手做掉。

## 事实：今天是什么样

- 声明（`stdlib/signatures/tensor.cml`）：`168` 行 `sum::<T>(t: T[..]): T`、`186` 行 `mean::<T>(t: T[..]): T` 是一实参的整轴；
  `#op('tensor:max_axis')` 那一条（`201` 行）起，`max`/`min`/`argmax`/`argmin` **只有**带 `axis` 的声明。
- 注册（`modules/tensor/ops/reduction_ops.cpp#meanAxisVjp`）：`reductionOps()` 里只有 `sum` 与 `mean` 各推一个整轴 `OpDef`；
  `max`/`min`/`argmax`/`argmin` 走 `reduceAxisDef<…>("max_axis", "max")` 这样的**沿轴注册**，没有 `tensor:max` 整轴 OpDef。
- 实测原文（一个 `float[2, 3]` 的行上写一实参调用）：

  ```
  No matching function found for argument types: <> (float[2, 3]).
  ```

  `max(x)`、`min(x)`、`argmax(x)`、`argmin(x)` 四条同一句。也就是说这四个名字在源码里连**写法**都没有。
- 对比：`sum(x)` 在同一把尺子下答得出来——
  `REINTERP vmap function=f graph=… asked=1 found=1 missing=0 rules=vmap_rule_sum elements=float`；
  `max_axis(x, 0, false)` 也答得出来——`… missing=0 rules=vmap_rule_max_axis elements=float`。

## 三条路

### A（默认建议）——不新增整轴算子

- 理由：`sum`/`mean` 的整轴形态本来就是「折掉所有行轴」，而 `max`/`min` 的整轴就是
  `max_axis(xs, 0, false)` 折到秩 0；差别只在调用者的写法上，不在算子的能力上。新增四个算子要让
  「每条归约两种形态」变成不变式：导数（`reduceAxisDef` 那条线已有整轴 `sum`/`mean` 的 vjp）、
  ONNX 导出、签名页、`docs/stdlib-reference.md` 各来一遍，代价不小，换来的只是写法短一点。
- 代价：写 `max(x)` 依旧不行；批处理那条路上，「整轴」这一组永远是 2+4 的形状（两条有形态、四条没有），
  覆盖表要一直这么记。用户要么写 `max_axis(x, 0, false)`，要么先知道秩。

### B——补 `tensor:max`/`tensor:min` 的整轴 OpDef（`argmax`/`argmin` 视 A 的结论顺带或另开）

- 好处：四个名字与 `sum`/`mean` 同形；`vmap` 里就多四条整轴规则，与 `vmap_sum`/`vmap_mean` 同一个折法。
- 代价：四个新 OpDef（kernel、infer、`resultDoc`）、四条 vjp、签名与文档；`f32` 的整轴 `max` 与沿轴版
  在 `NaN`、`-0` 上必须给出同一句话（沿轴版今天有「split 的 extent 为 0 就拒」这一条，整轴版要不要同样拒空张量，
  又是一句话），这些语义现在只能猜。

### C——只在库层写一个 Camel 函数，不加算子

- 好处：不动算子表；`max::<T>(t: T[..])` 用 `max_axis` 按 `shape` 折秩就能写（`vmap_sum` 的折法就是它）。
- 代价：它得**运行期**读秩，类型只能写 `T[..]` 而不是 `T`，于是它不像 `sum`/`mean` 那样是「回一个标量」的
  声明；而且这是把「整轴」这件事从算子层挪到库层，`vmap` 的规则得跟到库函数上——那是另一种形状的规则（批一个调用，
  不是批一个原语），今天 `vmap` 明确按名拒绝调用。

## 判据

**改完能删掉什么**：A 什么都不删、也什么都不加，代价是这四条永远只有沿轴形态；B 能删掉「用户要自己折秩」这件事，
但要新写四个算子的语义；C 能删掉「整轴必须是算子」这个假设，但会把规则从原语层挪到调用层，
与今天「按名拒绝调用」的判据冲突。所以只有 B 的收益是干净的，B 的代价也是最大的。

## 默认建议与正在做的事

> **以下是裁定前的建议（保留作记录，已被上面的裁定推翻）。**

**按 A 走：不新增整轴算子。** 这条车道（`lane-m5`）已经把 `tensor:sum`/`tensor:mean` 的整轴批规则写上，
`max`/`min`/`argmax`/`argmin` 的整轴**不写**——因为写不出来，不是拒绝。若作者认为该有，
一句「要」就能把这页翻成 B，那是四个算子的一次性工作，不影响已经落地的九条批规则。