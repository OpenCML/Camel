# 卡面（板上待建）：`resolveGroupedCall` 的候选集 —— 内建声明与 group 规则进同一张表

**来源**：`task-379` 收口时留下的"另一处没收敛"（backlog 里那条 `task-379 … done` 的末句：`resolveGroupedCall` 的候选仍只有 group 的 resolver，要动返回类型、牵 4 个调用点）。任务板已满 256（`team_task_create` 回 `Team task limit 256 reached`）⇒ 卡面先落这一页，腾出槽位再建卡。

**基**：`08864abb46`（读源时的 integration 尖端，已含 `task-379` 的三笔 `lane/call-candidates @ a301bb2031`：`callCandidateSignatures` 已在 `builder_call.cpp`，`selectOverloadFor` 与 `writtenParameterTakes` 都已 **0 处**）。基一动，下面每个符号名与处数都要重核。

## 现状（读源）

- **一次调用有两个候选集，其中一个只装了一半。** `Builder::resolveGroupedCall`（`src/compile/src/gir/builder_operators.cpp`）只遍历 `ops->resolvers()`：这是 group 自己的规则表，`operatorOverloads_`（`builder.h`）里的内建 `graphs` / `templates` **一条也不进**。`foreach` 在 `Range` 上是 prelude 的声明（一个带体的内建函数），在数组上是 group 的 `:mark/foreach_arr` 规则 —— 同一个名字的两半，走 `resolveGroupedCall` 时只看得到后一半。
- **签名层已经合了，答案层没有。** `task-379` 那笔引入的 `callCandidateSignatures`（现在就在 integration 的 `builder_call.cpp` 里）已经把两个来源合成**一张签名表**，顺序是规则：group 注册序 → 内建 `graphs` 原序 → `templates` 原序，`operatorOverloads_` 是 `unordered_map`、只按调用节点索引、**从不被遍历来排序**。但它给的是 `FunctionType *`，够回答"哪个签名收得下这组实参"，**回答不了"这次调用落到哪一条"**。
- **症结是答案的类型不同。** group 规则能被打包成算子索引 `oper_idx_ptr_t`（`OperatorIndex`：名字 + uri + `operAt`），而内建声明是**一张已成形的图**（`compile_graph_ptr_t`）：`Builder::operatorValueGraphOf` 造的是"一个只调这个算子的函数值"，是"把这个算子当值用"的答案，**不是"调用一条声明"**。`resolveGroupedCall` 今天返回 `std::optional<oper_idx_ptr_t>`，所以它连"选中的是内建声明"这件事都表达不出来 —— 这就是返回类型必须动的原因，也是 4 个落点要一起动的原因。
- **4 个落点（都只读 group 的候选）**：
  1. `visitLinkNode` 主路那一处 `resolveGroupedCall`；
  2. 同一函数里 `with` 实参适配后的**重试**那一处（自己的 `unmet`，答案失败就丢）；
  3. 同一函数里 entrance/change 扫描那一处（`normInputTypes` 临时换成 `enteredTypes`）。
  4. `Builder::overloadValueFor`（`builder_operators.cpp`）：按期望函数类型挑一条，**自己走一遍 `ops->resolvers()`**，与 `resolveGroupedCall` 问的是同一个问题。
  另：拿到答案之后，主路还要按 `(*res)->uri()` 回 `ops->resolvers()` 里再找一遍那条规则（补 `with` 默认值、`constas` 检查）。内建声明不在那张表里 ⇒ 这两处也跟着要改，别只改"挑"，漏掉"挑完之后按 uri 回查"。
- `selectOverloadFor` **0 处**、`writtenParameterTakes` **0 处**（`git grep -c` 在这个基上都空）。这两个 0 是 `task-379` 那笔的结果，**不是**本卡的结果 —— 本卡只负责**守住**它们（见判据 ④）。

## 目标

一次调用**只有一个候选集**：内建声明（`graphs` / `templates`）与 group 规则进**同一张表**，4 个落点都读它；"调用一条声明"与"调用一条规则"是同一张表里的两种答案，不是两条路。

## 判据

① **`std::gir` 阳性对照（两形都要 EXIT=0）**：一条 `Range` 上的 `foreach`（走内建声明 `prelude.cml` 的 `foreach(r: Range)`）与一条数组上的 `foreach`（走 group 规则 `:mark/foreach_arr`），各自 `camel <case>.cml std::gir std::nop` **exit 0**，且 dot 里点名的是对应那一个。这是"两个来源都得进同一张表"的最小读数：今天两形都能过，**改坏了最先红在这里**。
② **`probe.foreach_lambda*` 6/6**（`test/plans/probe/foreach_lambda.plan.toml` 六条：`array_form` / `array_value_form` / `range_form` / `array_filter` / `range_filter` / `range_typed`）**+ `docs.snippets_compile` 27/27**（`test/plans/feat/docs_links.plan.toml`）。这两项是 `task-379` 的读数，本卡不许退回去。
③ **`feat/compile` + `feat/modules` 对当时最新红表逐名比，表外 0**。口径见 `coordinator.md`「基线口径」与 `baseline-74.md`：按 `plan_id` + 用例名逐名，不拿总数相减；红表取 `newestRedTable()` 当时指向的那张（今天 = 73 表 141 名；注意 `transforms` 8 行 / `stated_extents` 1 行写的是裸名，前缀过滤会漏掉这 9 行）。
④ **`selectOverloadFor` 0 处、`writtenParameterTakes` 0 处**（`git grep -c` 各一次，贴原文；基上已经是 `0 / 0`，所以这一条是**守**不是**等**）。**变回非 0 就是本卡把 `task-379` 的回退带回来了 —— 时报出来，不顺手删。**
⑤ **`numstat` + `[provenance]`**（tip 与 `dirty=0` 原文），交回写**枝名@sha**。

## 顺序规则

沿用 `callCandidateSignatures` 开头那句注释：**group 注册序 → 内建 `graphs` 原序 → `templates` 原序**；`operatorOverloads_` 只按节点索引、不参与排序。新表按这条写，并**证明**"改前改后选中同一条"（造一条"两入口都匹配"的用例，贴两侧选中者；不依赖 `unordered_map` 序）。

## 边界（不动）

- **不动"唯一候选"的判定**：今天写作 `settleOverloaded`（`builder_generics.cpp` 里 `Builder::settleGenericOperatorArguments` 的 lambda）的 `graphs.size() + operators.size() != 1`。`task-379` 换基后是它换成的形态 —— 判的是同一件事，不去动它。
- **不动 `resolveGroupedCall` 现有的选择策略**："第一个吃下的赢；两条以上写 `when` 且条件成立才报歧义"（`holding` 那段，排序去重后报 `AmbiguousCall`）。本卡只把候选集合成一张、让内建的也能当答案，**不重排"谁赢"**。
- **不碰 `jvp.cml` / `deriv-tails`**（`task-379` 那笔一字未动，这里也不动）。

## 备注

- **板上待建，等腾位**；建卡时把这一页的链接写进卡面，别把内容抄两遍。
- 卡面里的处数与符号名是 `08864abb46` 上的读数；动手前第一件事是重核（尤其 4 个落点各自所在函数是否还叫那个名字 —— `resolveGroupedCall` 的三处调用已在 `builder_call.cpp:1873` / `:1985` / `:2018`，第 4 处是 `builder_operators.cpp` 的 `Builder::overloadValueFor`）。