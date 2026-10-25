# 合并后特化出来的图数是 4：两边各自钉的 5 与 `>5..40` 都不成立（实测，2026-10-26，h588 flip-merge lane）

`handover/h588-flip-merge-develop` 把 `develop` 并进翻转线（本文件所在树，合并提交见该分支）。
`specialize_growing_shape.fold_specialize.bounded` 这条用例在合并后的树上读到 4 个图，而两边各自
钉的读数都不是 4。这条红留在树上没有动钉子，按设计原则第 1 节（与测试结果冲突时记录下来，不改语义
让测试通过）写成本页。

## 一、实测：合并后的读数（`-Plan test/plans -Tier fast -Jobs 8`，linux 主机，2026-10-26）

用例：`test/cases/opt/generic/specialize_growing_shape.cml`，参数
`std::opt::fold std::opt::specialize std::stats std::fvm`。合并后的 stdout：

```
GRAPH_STATS {"graphs":4,"nodes":39,"edges":41,"opers":10,"direct_calls":3,"indirect_calls":1,"closures":0,"branches":2,"loops":0,"bytes":1480,"specializations":1}
[12, 2]
36.0
```

即 `specializations === 1` 成立，`graphs` 是 **4**；程序自己的输出（`[12, 2]`、`36.0`）与两边钉的
零遍输出一致。失败信息原文：`unexpected graph count: 4`。

## 二、两边各自的钉子（原话）

- `develop`（`9a22ca77f`）的这条用例：`assert(stats.specializations === 1, ...)`、
  `assert(stats.graphs === 5, ...)`，desc 写"holds exactly one type specialization of the
  recursion and exactly five graphs … the single instance is the recursion's fixed point"。
- 翻转线 tip（`29a5df7d6`）的同名用例：`assert(stats.graphs > 5 && stats.graphs <= 40, ...)`，
  desc 写"holds a bounded number of graphs (the 8 type specializations of the recursion's branch
  arm and what they call)"，并且**没有** `specializations` 这一条。
- 评审记录（`docs/handover/reviews/questions-2026-10-24-evening.md`，第 401 行）对 5 的解释是：
  "特化前是 3 个图，特化出的唯一一个实例连同它调用的图是 2 个，合起来 5 个，而且不随数组长度增长
  …… 旧下界 `> 5` 能通过，是因为类型身份没修好时，同一个类型被当成两个 …… 读数降到 5 是修复本身"。

合并树上两条钉子都不成立：4 既不 `=== 5`，也不 `> 5`。也就是说合并后的行为与**两边各自单独**都不
相同，不是"取了哪一边"的问题。

## 三、本 lane 的判断与推断（不是裁定）

- 合并这次动过的地方（赋值算子的存储运行时与"写给出格"约定、`:op/idx_take` 取行、tiles 报告的
  schema-1、`f32[..]` 拼写、文档与生成物）都不碰泛型实例化与类型身份，所以 4 与 5 的差不是这些
  裁决造成的；它来自合并后**类型身份/实例键**那部分代码的组合。
- 4 比 5 少一个图，两种读法都说得通：**(a)** 又多复用了一个实例，4 是 5 之后下一步的修复本身
  （与翻转线原来"同一个类型被当成两个"的缺陷同方向）；**(b)** 该出现的图少了一个，是合并引入的缺陷。
  本 lane 无法从读数分辨，也不该按读数改钉子。
- 本 lane 因此**保持** develop 的钉子原样（`=== 5`、`specializations === 1`），让这条用例红着，
  并把读数记在这里；没有把它放宽成 `=== 4`。

## 四、请裁定

1. `graphs` 应当是 4 还是 5？
2. 若是 4：这条用例的 desc 与钉子按 4 重写（一句"为什么是 4"写进 desc），本 lane 连同别的一起改。
3. 若是 5：请指个方向找那个少掉的图（本 lane 可以先查合并后实例键与"调用者参数 vs 副本"那一处），
   找到后修代码而不是改钉子。