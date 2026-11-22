# 不可达节点在规范顺序里该有什么位置，以及草稿的节点为什么没有 `id`

- 日期：2026-11-18
- 车道：`lane-unreach`（分支 `lane-unreach`，从 `origin/develop` = `29f751a7e` 开出）
- 范围：`internal/reports/2026-11-18-commit-checks-and-spelling-spelling.cn.md` 的 ① 与 ②。③（`graphs_of` 按 GraphId 序）明确留给下一轮，本车道没碰。

## 1. 两句结论

1. **不可达节点是规范顺序里的「一节」，不是「一个洞」。** 它在 `nodes` 里，排在走查（`CanonicalWalk`）够得到的一切之后，并且在那一节内部用**和全图同一个并列规则**排：节点键、等键部分按最小编号表、超过枚举上限的部分按每个节点自己的片段切（`include/camel/runtime/canonical.h`）。于是那一节是图自己决定的全序 —— 同一张图的两种写法给出一节，`same_graph` 因此对一张带不可达节点的图也有意义。
2. **草稿的节点没有 `id`，读它是 `RuntimeDiag::InvalidArgument`。** 一个会变的数是「一个坏了的工具返回一个正常形状的结果」；拒绝读、并且说清为什么和该改读什么，才是那个读数的正确形状。

## 2. ① 的问题与三种读数

问题不是「补一个规则」，而是「不可达节点在规范顺序里该有什么位置」—— 这是规范顺序的定义。三种可能，各一句读数：

- **(a) 按它们自己的 kind/参数做次级全序**：两种写法之间仍可比，但那一节只是个**前序**——两张**不同**的图，只要它们的尾部节点 kind 和参数相同，就会读成相等，`same_graph` 于是会错答 `true`。代价：规范顺序旁边住进第二条更弱的排序规则，一份文档带两种顺序。
- **(b) 排除在 `nodes` 之外、另给一个 `unreachable` 视图**：`nodes` 精确地成为「可达部分」，但 `nodes` 从此不再是「图的每一个节点」；每个走 `nodes` 看整张图的消费者（`std::jgir`、`opt` 的清扫、`node_kinds_match_the_graph`）都得问第二次，节点数不再等于图的节点数，而且第二个视图仍然需要一套顺序 —— **(b) 把问题搬走了，没有消掉它**。
- **(c) 保持「排最后」但在最后内部也定全序** ← **选了这个**。顺序从一头到另一头只有一条规则，`same_graph` 立刻有意义。代价：位置本身不携带信息 —— 光看顺序，读者分不出哪些节点是不可达的。

**为什么 (c) 便宜**：测下来的事实是那一节**已经**是按并列规则排的（每个区域对剩下的节点调用 `arrange`），(c) 大体上已经存在。缺的只有两件事：(i) 创造顺序还能渗进序列的那一处 —— 收尾的 `for (ref : refs_) place(ref);` 是按 NodeId 走的；以及 (ii) 这个规则的**陈述**和一个测它的用例。两件都补了。

## 3. 读数（主机 `88zjwei`，`~/camel-work/unreach`）

- 4037 个 `.cml`（`test/cases`、`examples`、`stdlib`、`benchmarks`）里，**207 个**至少有一个不可达节点。
- 那 207 个里的每一个不可达节点都是 **kind `k1` = `Port`，一个没有任何节点读的区域端口**。成因是可复现的：循环的 `while` 条件只读状态的一部分，于是另一个状态变量在 `LoopCond` 区域的端口没有读者。`loop a = 0, b = 0 while b < n do (a + 1, b + 1)` 就是这个形状。所以这一节是常见现象，不是模型的残余。
- 收尾那个 NodeId 兜底 `for (ref : refs_) place(ref);` 在 4037 个程序里**触发 0 次** —— 它是一个没有现行证人的洞（死区 region 的节点才会走进去）。改成 `arrange` 之后它仍然只是最后一道守卫（`arrange` 不落只有「走查正卡在环里」的节点）。
- **① 的判据对**：`loop total = 0, step = 0 while step < scale do (total + twice, step + 1)`，两种写法（两条语句对调、每个局部改名），`std::jgir canonical=true` 两次给出**同一个** sha256 `004eefb6603d668a2c375775eb51540450a416ec00b6e72a7cbe9c8bafcebc68`，`cmp` 干净；各自恰好 1 个不可达节点。反事实（把一个常数 `* 3` 改成 `* 4`）读成两张，且 `test/tools/same_graph.py` 逐个点名差在哪几个节点和哪几条边 —— 没有这一条，上面的 `true` 是空的。
- 用例：`test/cases/trans/canon_unreachable.py`，挂在 `test/plans/feat/trans/canon_order.plan.toml`。它从 dump 出来的文档本身读出不可达节点（某区域 `ports` 里列了、但没有任何节点的 `inputs` 指向的端口），所以读数是对着那张图读的，不是对着本文件的相信读的。

## 4. ② 的诊断原话

`modules/graph/read.cpp` 的 `nodeCanonicalNumber` 在 `handle->transaction() != nullptr` 时抛：

```
error[RuntimeDiag::InvalidArgument]: Invalid argument of 'graph.id': the node belongs to a draft, and a draft has no canonical number: its nodes' places are the order they were made in, which the commit replaces. Read `id` after the commit publishes the program, or read `creation_id` for the draft's own order.
  --> <file>:7:13
   |
 7 |     println(id(added))
   |             ^^
```

- 位置落在 `id(...)` 这个调用上，所以它不是「一个数」，是一个诊断。
- 没有新增诊断登记行：复用的是已有的 `RuntimeDiag::InvalidArgument`（`0x000034`，`diagnostics/registry.tsv` 里现成的行）。这是有意的 —— 本车道不许碰生成物，而新增一行要重生成 `registry.tsv` + 头文件 + 跑七项。代价是渲染出来是「`Invalid argument of 'graph.id': <message>.`」这个通用外框。
- `id` **故意不改成 `int!`**：`stdlib/opt.cml:120` 写 `id(m) < id(n)`，而且它读的是冻结图（`graphs_of(f)` 来的），不受影响；把 `id` 变成可失败会把标准库连带改一遍，超出本车道。
- `stdlib/graph.cml` 的 `id` 文档、`modules/graph/read.h`、`modules/graph/module.cpp` 的注释都改成说这件事；`id` 仍然**不**声明 `spelling`（它读的是规范号，不是拼写；`creation_id` 才是拼写，且它已经声明了）。
- 用例：`test/cases/modules/graph_draft_id.py`，挂在 `test/plans/feat/modules/graph.plan.toml`。同一个函数的两次运行只差一行（commit 在 `id` 之前还是之后）：之前是上面那条诊断（exit 1，且定位到 `id` 调用），之后是 `nodes` 里每个节点的位置，读回来和 `0..18` 逐项相等 —— 是定义，不是取样。

## 5. 三处不属于本车道、但测到了的读数

1. **`region` 的编号是拼写。** 两张**互相独立**的循环图，只是两条语句对调，`std::jgir canonical=true` 给出**两个**哈希；差异**只在**节点的 `region` 字段和 `regions[]` 的 `owner` 次序 —— 区域 id 跟着源序走。这是比尾部更大的已有泄漏，落点在打印文档的那一侧，不在本车道允许的文件里。① 的判据对因此刻意只用**一个**循环，不让这个混淆混进来。给下一轮：`region` 该不该也是规范号。
2. **`docs/stdlib-reference.md` 在基线就与源不符。** `stdlib/vjp.cml` / `grad` 的两段 2026-11-18 裁定（`split` 在哪儿切）在源里有、文档里没有，所以基线上 `node scripts/stdlib-reference.mjs --verify-doc` 是红的。本车道改了 `stdlib/graph.cml` 的文档注释，必须重生成，于是那次重生成**必然**把这两段一起带上（生成物的差异不手解）。结果是 `--verify-doc` 从红变绿。副作用：`check-doc-links --citations` 多了 1 条未验证引用 `internal/reports/2026-11-18-nested-transforms.cn.md`（那份报告属于另一条车道，本树里还没有）。基线本来就是 15 条，现在是 17 条 —— 一条来自上面这次重生成，一条是本报告（本报告写入后消失）。
3. **`include/camel/runtime/region_roots.h` 的头不是规范形式**（第 3 行 `#pragma once` 前没有空行），基线就是这样，`headers --check` 在基线上也红。

## 6. 门禁读数

基线 `29f751a7e`：`compile 2630/2613/0/17`｜`linear 869/864/0/3/2`｜`opt 1599/1599/0`｜`parse 121`｜`para 104`｜`tools 70`，`[SUMMARY] 5393 / 5371 / 0 / 2 / 20`。

本车道（`-Plan` 用正斜杠，`-Jobs 16`，逐计划各跑一次以便逐条读数）：

| 计划 | 读数 |
|---|---|
| `test/plans/feat/compile` | `[SUMMARY] total: 2630, pass: 2613, fail: 0, known failures: 17` |
| `test/plans/feat/linear` | `[SUMMARY] total: 869, pass: 864, fail: 0, skip: 2, known failures: 3` |
| `test/plans/feat/opt` | `[SUMMARY] total: 1599, pass: 1599, fail: 0` |
| `test/plans/feat/parse` | `[SUMMARY] total: 121, pass: 121, fail: 0` |
| `test/plans/feat/para` | `[SUMMARY] total: 104, pass: 104, fail: 0` |
| `test/plans/feat/tools` | `[SUMMARY] total: 70, pass: 70, fail: 0` |

合计 **5393 / 5371 / 0 / 2 / 20**，与基线 `29f751a7e` 逐项相同。

中间过程有一件事要说清：第一次把这六个计划一起跑，读数是 `total: 5393, pass: 5370, fail: 1`，唯一的红是 `frontend.ast_snapshot` —— 因为 `stdlib/graph.cml` 的**注释**也在快照里（`was d18c0005cc2591d8 ok` / `now 3314284e5333a70d ok`）。按 `-UpdateAstHashes -AstOnly 'stdlib/graph.cml'` 只重记这一行（记录 4424 行，1 加 1 减，都是 `stdlib/graph.cml`），parse 随之 121/121/0。

`feat/trans/spelling.plan.toml` + `feat/trans/canon_order.plan.toml` + `feat/modules/graph.plan.toml` + `feat/modules/rules_conditions.plan.toml` 一起跑：`[SUMMARY] total: 96, pass: 96, fail: 0`，其中 `unreachable_nodes_have_a_place` 与 `draft_id_is_refused_and_the_published_id_is_a_position` 都是 `[PASS]`。

静态检查（本机，非构建）：`diagnostics-catalog.mjs` 的七项全绿；`check-grammar`、`plan-dir-markers`、`test/tools/check-plan-fields.mjs` 绿。基线上就红、本车道同样红的：`camel-for-ai --check`（三个问题，逐字与基线相同）、`headers --check`（`region_roots.h`）、`check-doc-links --citations`（基线 15 条，本车道 16 条 —— 多的一条来自 §5.2 那次重生成）。`stdlib-reference --verify-doc` 在基线上红，重生成后**绿**。

## 7. 没做

- ③（`graphs_of` 按 GraphId 序）：下一轮。
- ⑤（`x*2+y*3+x*y` 验收程序没有同积对、多写一个 `x*2`）：不是本车道的判据，没碰。
- ⑥（`sem::covers` 没加拼写规则）：A 已经量过并放弃，没有重做。
- `region` 编号是拼写这件事：本车道只报，不修（不在允许的文件里）。