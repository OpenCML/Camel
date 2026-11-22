# 外部锚：`created` 与 `produced` 数的不是同一件事

日期：2026-11-15。范围：`lane-refcount`。这一页是一次**测量**，结论是「按你给的形式做不成」，附带能做成的
形式和它的数字。判据：`分配器计数 == produced`，逐用例。

## 1. 锚写好了，量出来不等

`scripts/check-plan-refs-anchor.mjs`：对 `test/cases/consistency/values` 的 36 个用例 × 四个后端各跑一次
（`camel --gc-check foreign <case> test::plan_refs std::<backend>`），把 CLI 退出时打的
`FOREIGN_RESOURCES created=`（分配器侧，盒子由谁 `new`）与表里所有图的 `produced` 相加相比。

```
PLAN_REFS_ANCHOR cases=36 runs=144 equal=132 unequal=12 skipped=0
ANCHOR_BAD test/cases/consistency/values/counter_closure.cml fvm produced=17 created=2
ANCHOR_BAD test/cases/consistency/values/content_equality_trees.cml prl produced=105 created=0
ANCHOR_BAD test/cases/consistency/values/deep_json_depth.cml nvm produced=26 created=0
ANCHOR_BAD test/cases/consistency/values/deep_nested_enum.cml nvm produced=22 created=0
ANCHOR_BAD test/cases/consistency/values/effect_not_used.cml nvm produced=5 created=0
...（12 条，全是 created < produced）
```

## 2. 为什么不等：两个数量的不是一件事

- `produced` 是**计划数的值**：图里每个不转发、非常量、非端口的产出位置都算一个。`deep_json_depth`
  有 26 个，`content_equality_trees` 有 105 个——这些值里绝大多数是整数、tuple、数组，不是盒子。
- `created` 是**盒子**：只有 `state::ref` 真的执行了才有一个。`deep_json_depth` 一个盒子都不造，
  所以 `created=0` 而 `produced=26`；`counter_closure` 造两个盒子，表数 17 个值。

`created == produced` 只在「这张图里每个值都是盒子」时成立，那不是任何一张图的形状。**132/144 相等是巧合**
（那些程序里 `produced` 恰好等于 `created` 的用例），不是锚在工作。

## 3. 能成立的锚，两种写法

1. **按盒子数对齐**：表里再打一列 `refs <n>`——产出位置中**输出类型是 `Ref`** 的那些——锚就是
   `created == refs`。环里跑一次以上仍然会多（`boxes_made_per_call_ten` 造 11 个盒子、静态 1 个位置），
   所以这一列要配 `--gc-check foreign` 的 `created` 一起读：`created >= refs`，多出来的是循环次数。
2. **按次对齐（更强的那个）**：盒子造出来的那一刻，拿当时的节点去查表的 `refProducedAt[位置]`，
   **每次造盒子都必须查到一行**，查不到就当场报。这样漏掉的不是一对（一拿一交），而是**一次造盒子**，
   独立于循环次数，也独立于表的自洽。代价是造盒子的地方要知道当前节点（`Frame` 知道图与节点，
   `state::ref` 的算子体不知道），要在算子调用处把它带进去。

我建议第二种：它才是「外部锚」，第一种只是把同一个静态计数换个名字再比一次。

## 4. 这一轮没有做的事

没有加 `refs` 列，也没有把锚接进算子调用处（那是上面的第 2 条，要动 `state::ref` 的签名）。
`scripts/check-plan-refs-anchor.mjs` 留在树里，但没有计划条目跑它：它是一个会红的量具，红线该由你
裁下形式之后才钉。`test/plan-selection.json` 的规则已经把它记在名下，所以它不是一个没人认领的脚本。