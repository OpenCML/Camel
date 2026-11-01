# `remat` 改的是 `split` 的残差数，还是 `pullback` 的捕获表

> **问什么**：`remat(f)` 在值上是恒等，这一点三份文件都写死了；但"反向时不保存残差"这件事**落在哪一处**
> ——是 `split` 交出的残差元组（今天可读：`residuals=`），还是 `modules/autodiff/pullback.cpp` 里
> 建回传图时的捕获表（今天不可读）？两处都自称知道"哪些值是残差"，而今天没有任何一处读 `remat`。
> **堵着谁**：M6 的 `remat` 验收第二半（残差数下降）。第一半（数不变）已经落地并钉住
> （`stdlib/autodiff.cml:82-84`、`test/plans/feat/modules/remat.plan.toml`）；第二半没有落点，
> 所以"`remat` 已经能用"这句话今天只对一半。
> **为什么我不自己定**：它不是写法选择，而是**一个名字该由哪个模块读**——决定 `remat` 是图变换
> （`split` 认标记）还是引擎开关（`pullback` 认标记），两条路的读数不同，验收断言也不同。

## 今天的事实（读数逐条）

- **值那一半已经是恒等且被钉住**：`stdlib/autodiff.cml:82-84` 的 `remat(f) = f`；
  `test/cases/modules/autodiff/remat_identity.cml` 在 `f(x, y) = x * x * y`（x=3, y=2）上两条路都打印
  `18.0 / 12.0 / 9.0`。这条是 M6 表里的"数值与不加 `remat` 逐位相同"。
- **`remat` 在 `src/`、`modules/` 里零命中**：`modules/transforms`（`jvp`/`split`/`transpose`/`vmap` 四个算子）
  与 `modules/autodiff` 都不认识这个词。所以今天"反向时重算"一句没有实现它的地方。
- **残差数今天已经读得出来**，读在 `split` 上：`test/plans/feat/transforms/split.plan.toml:74-80` 钉着
  同一个 `x*x*y` 的 `residuals=3`，并写明"the arity is the graph's, not the signature's"。
  `docs/design/program-transforms/m6-combination-numbers.cn.md:112` 因此说：`split` 落地之后，
  "残差数等于 `split` 结果的元数"，`remat` 的"数变少了"可以断言成"`split` 的输出少了几个"。
- **另一处也知道残差**：`modules/autodiff/pullback.cpp:1241` 的 `captures_`（`:1117` 的 `residuals()` 直接返回它）。
  它是全树唯一同时知道"这是残差、不是普通捕获"的地方，但只在建回传图那一刻存在，图走一遍读不出来
  （同一页 4.1 量过 `std::stats` 的 12 个字段，没有一个与残差有关）。

## 三条路

### A（默认建议）标记由 `split` 读：被 `remat` 覆盖的那一段，跨切只留入口值

- 做法：`split` 在切图时认出标记段，把该段的跨切残差压到入口值（极端情形 0 项）。
- 代价：`modules/transforms` 要认一个标记。今天的标记是标签 `#`（第 6.2 节，贴在**声明**上），而
  `remat` 是**值**、贴的是**调用**——所以要么给调用上的区域标签找到写法，要么让库函数在图上留下一个
  被 `split` 认出的节点。另外 `who-owns-the-count-of-a-splits-residuals` 那张表（`residuals=`/`slots=`/`agree=`）
  会多出第三种 case：类型给 2 槽、图算 3 项、标记后 1 项。
- 好处：**读数已经在**（`residuals=`），"数变少了"今天就能断言；`split`+`transpose` 的 `vjp` 与
  `pullback` 的 `grad` 于是走同一个判据——一个名字一种含义。

### B 标记由 `pullback` 读：`split` 的残差数不动，只有引擎不捕获那一段

- 做法：`remat` 落在 `modules/autodiff` 建回传图的那一处，被标记的调用不做捕获、反向时重挂一份。
- 代价：`split` 侧读不出来（`residuals=` 不变），于是同一个 `remat` 在 `vjp`（`split`+`transpose`）与
  `grad`（`pullback`）里做两件不同的事；还要给求导自己加一行残差数，因为 `std::stats` 放不下它。
- 好处：不动 `modules/transforms`，只在一处读标记；与 JAX"重算发生在反向"最贴近。

### C 只承认值那一半，把残差数下降推到 `split` 收口之后

- 代价：M6 只落一半；而且**危险**——文档说这个标记意味着重算，实际反向一次也不重算，用户看不出区别。
- 好处：今天不动编译流程；等 `who-owns-the-count-of-a-splits-residuals` 裁定完，A/B 的选择面会变窄。

## 判据

**哪条路能让"数变少了"成为一条真断言**：A 今天就能（`residuals=` 已在），B 不能（得先造读数），
C 放弃这条断言。**哪条路删掉的东西多**：A 删掉"同一个名字两种含义"，B 删掉对 `modules/transforms` 的改动，
C 什么都删不掉。

## 默认与正在做的事

**按 A 走**：标记归 `split`，`remat` 的输出照今天的恒等不动；在 A 落地之前，`remat` 就是值上的恒等，
`split` 与 `pullback` 都不读它——这一点写在库里（`stdlib/autodiff.cml:73-79`），不写成"待办"。
B 的入口留好：`pullback.cpp` 的 `captures_` 与 `residualTypes` 已经集中在建图那一处，`split` 也已经有
`residualsOf(GCGraph)`，两条路将来都只需要在各自那一处加一个判断。

## 要作者答的一句

**`remat` 的"不保存残差"由 `split` 读（A，残差数下降，`vjp` 与 `grad` 一个判据），还是由 `pullback` 读
（B，`split` 的读数不动，只改引擎的捕获）？** 若选 A，还要答一句：标记写在**调用**上（`remat(f)(x)` 的那次调用）
用什么写法——这是今天没有的（标签只贴声明），A 的第一行代码就等这一句。