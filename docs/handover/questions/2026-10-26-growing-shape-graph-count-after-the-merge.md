# 特化出的图数是 4，不是 5：`Tensor` 拼写删掉之后少的那一个实例（实测，2026-10-26，h588 flip-merge lane）

本页最初是问作者的（合并后读到 4，两边各自钉的 5 与 `>5..40` 都不成立）。**翻转线自己已经回答了**
（`3980cbb55`「The growing-shape recursion's bound is read from the tree the deletion produces, and a
second array length pins that the count does not move with it」），并把钉子按 4 重写、把理由写进 desc。
合并进本 lane 的分支后，这条用例在本 lane 的树上读到 4 且**通过**。因此本页现在的用途是：说明
**develop 侧那条 `=== 5` 是删拼写之前的老读数**，develop 收这笔时应连钉子一起收。

## 一、实测：合并后的读数与来源（file:line）

`test/plans/feat/opt/generic.plan.toml:833`（用例）、`:846`（`assert(stats.graphs === 4, ...)`）；
读数本身由 `std::stats` 打印，`src/passes/trans/stats/stats.cpp:93`：
`os << "GRAPH_STATS " << collectGraphStats(*program, program->root()).toJson()`。合并后的 stdout：

```
GRAPH_STATS {"graphs":4,"nodes":39,"edges":41,"opers":10,"direct_calls":3,"indirect_calls":1,"closures":0,"branches":2,"loops":0,"bytes":1480,"specializations":1}
[12, 2]
36.0
```

即 `specializations === 1` 且 `graphs === 4`；程序自身输出与两边钉的零遍输出一致。

## 二、为什么是 4（翻转线的 desc 原话，`test/plans/feat/opt/generic.plan.toml:834`）

> holds exactly one type specialization of the recursion and exactly four graphs -- the root, `main`,
> the declaration and the one instance `main` calls -- and still prints the zero-pass output. Neither
> count moves with the array length: the declaration states the element type of `acc`, so a call inside
> the instance asks for the type the instance already states, binds no copy, and the recursion's fixed
> point is the declaration's own self-call. The five graphs this read before the bare `Tensor` spelling
> was deleted came from that spelling: the element type of `acc` was open in the declaration, and the
> element-typed argument of the recursive call refined it into a second instance (`grow$tspec1$spec2`),
> so the count moved with the level bound -- 5 at 12 levels, 4 at 24 -- which is why `bounded.rgir`
> names the graphs and `more_levels.bounded` reads the second bound.

即 5 不是"正确读数"，而是 `Tensor`（元素类型开放的）拼写带来的**第二个实例**：`acc` 的元素类型在声明里
开放，递归实参又把它收窄，于是多出一个键不同的实例，读数还随层数走（12 层 5、24 层 4）。删掉那个拼写
之后只剩声明与 `main` 调用的那一个实例，读数是 4 且与层数无关。翻转线同时用 `bounded.rgir`
（`:872`，把四个图逐个命名）与 `more_levels.bounded`（`:853`，第二条长度、同样 `=== 4`，`:865`）钉住这条。

## 三、两边各自的老钉子（原话）

- develop（`9a22ca77f`）：`assert(stats.specializations === 1, ...)`、`assert(stats.graphs === 5, ...)`。
- 翻转线 tip（`29a5df7d6`，本次合并之前）：`assert(stats.graphs > 5 && stats.graphs <= 40, ...)`，
  没有 `specializations` 那一条。
- 评审记录（`docs/handover/reviews/questions-2026-10-24-evening.md:401`）当时的核对是 5，并说"读数降到
  5 是修复本身"——那次核对的树仍有上面那个开放元素类型的拼写，所以 5 是**当时的**修复结果。

## 四、给 develop 的动作

1. 收本 lane 的合并时，把 `specialize_growing_shape.fold_specialize.bounded` 的钉子从 `=== 5` 改成
   `=== 4`，并采用翻转线的 desc（第二段原话）——这是"规则的结果"，不是放宽断言。
2. `bounded.rgir` 与 `more_levels.bounded` 两条一起收（它们把 4 与"不随层数变"分别钉住）。
3. 本 lane 未改这条钉子：合并树上它一度是红的，本次合并（把翻转线的 `3980cbb55` 收进来）之后它变绿。