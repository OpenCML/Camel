# 遍边界效应顺序不变量：已就位，`develop` 上清单为空

lane `lane-inv`，基线 `origin/develop` = `0faa9d4bf`（会话开始时），期间合入 `e8d92a8ec`；头 = `d36506a38`。
改动只有 `src/execute/pass.cpp` 一个文件、纯新增。

## 结论

每个遍结束时检查一次：同一环境资源上带效应的节点，其写下的相对先后在遍前遍后必须一致；不一致就报
`RuntimeDiag::PassInternalError`，报文点名**哪个遍 + 哪两个效应（各自 `origin`、`oper uri`、图、位置）**，
并在两个效应落在两张图里时写出 `moved from graph <n> to graph <m>`。

在 `develop` 上它**一次也没响**：整套 fast 层 `total 7900, pass 7891, fail 0, skip 7, known failures 2`，
图快照类 golden 全照旧。所以这一页的结论只有一句：**规则在，清单空**。

## 逐类形状与它们为什么通过

按当时排查的顺序，每行的「结果」都是同一棵树上量出来的。

| 类 | 形状 | 为什么通过 | 结果 |
|---|---|---|---|
| 判据 | 效应 = `!OperatorTable::isPure(oper) \|\| hasExternalEffect(oper)`，照抄 `modules/tiles/src/graph_regions.cpp` 的 `"effect"` 排除 | 全树唯一判法，不另立第二套 | 0 |
| 顺序 | 每张图一条 Kahn 拓扑序（数据、控制、sync、order 前驱各一），应对 264 个效应 | 逐对可达性那一版要 20 s，单序 294 ms；据此定形 | 0 |
| 内联 | `older_effect_place` 报告里的最小形状，走 `std::nvm` / `std::opt` / `std::opt::inline` | 内联把调用者的链一起搬过去，链还在 | 0 |
| 区域提取 | `if_without_else.statement` 等：`std::opt` 把实参图抽成独立图 | 效应随图走，但运行位置仍是拥有它的那个调用，顺序由调用链保证 | 0 |
| 布局自由（55 条） | 遍前从来没有被边链排过序的一对效应（`sync_block_order.*`、`fold_keeps_arm_effect.*`、`loops_in_arms.*` 等） | 从来没有顺序可破：改写可以任意排布。**不要求「链」就会红这 55 条，输出却一字未变** | 55 红（保留「要求链」后 0） |
| 副本 | 内联产生的副本与原节点同 `origin` | 以 `(origin, 内联区域 callSite 的 origin, uri)` 作实例身份后，副本与原节点可区分；不加这条时报表会写成 `origin 4819 before origin 4819` | 0 |
| 跨图归属 | 两个效应被拆进两张图（`std::defunctionalize` 的 47/48 对 51） | 抬到「运行这张图的那个调用所在的图」再问链；`std::opt` 那对因此被调用链澄清 | 0 |
| Arm / Block 归属 | 同上，但来源是 `GCRegion{kind=Arm\|Block}` 的 `owner` / `callSite` | 抬到的位置确实是运行那张图的节点，链按写下的顺序仍然存在 | 0 |
| 「调用它的节点就拥有它」 | 上一行之外，再让任何调用某张图的节点都成为它的拥有者 | **不成立**：一张图可以被多处调用、也可以当函数值传，拥有者不唯一，抬上去的链是假的。整闸 `fail 186`（`sync_block_order.*`、`var_param.*`、`equal_branch.*` …），报文里还出现「效应在图 51 却说 in graph 47」这种混图 | 186 红，**已剔除，未进这一支** |

## `older_effect_place` 今天的颜色

作者问：`older_effect_place` 在 `develop` 上今天是绿是红，绿了是哪一笔修好的。

**绿。** 但要先把事实说清：**这个用例不在 `develop` 的树里**。全仓只有一笔加过它——`4af274872`
（"Hold the lane's remaining work in one place before measuring against develop"，作者 Zhenjie Wei），
它只在 `lane-eff-rule` 分支上，没合进 `develop`。`git log --all -- test/cases/consistency/if_without_else/older_effect_place.cml`
只有这一条，`origin/develop` 的树里既没有这个文件，也没有计划项。

所以量的是**那段程序**（与那一笔里的用例同文）在两棵树上跑出来的顺序：

| 树 | 三遍 | 输出 |
|---|---|---|
| `develop` 的 `abbc9c229`（10-29 21:10 的构建，主机 `camel-work/Camel`） | `std::nvm` / `std::opt std::nvm` / `std::opt::inline std::opt std::nvm` | `before / older / 1 1 / after`，三遍都对 |
| 本车道 `1636a849d`（基线 develop `e8d92a8ec` + 本页的五笔） | 同上三遍 | 同上，三遍都对 |

**没有可点名的「修好它的 sha」**：我能构建的最早一棵树（`abbc9c229`，10-29）已经是绿的，而那一笔加用例
的时间在 10-30，所以 `develop` 上不存在一段「红 → 后来绿」的区间可以二分。`lane-eff-rule` 那一笔自己
动了 `src/passes/opt/generic/generic.cpp`、`src/runtime/draft_inline.cpp`、`src/compile/gir/builder*` 与
`modules/tiles/src/graph_regions.cpp`——它更像**带着修复**的一笔，而它上面的红是「修复前」的状态。
要给出一个 sha，需要先有一棵能复现红的树（`lane-eff-rule` 修复前那一版即可，一次构建），这不是本车道
能独立回答的问题。

## 给作者的一句话

**这条不变量今天是「防将来」，与此前的环守卫同类**：它没有查出任何现存缺陷，是一次「某笔改写一旦把
同一资源上的两个效应换序、或把其中一个搬出那条唯一保证顺序的链，就立刻报错并点名那个遍」的保险。
**要不要保留它，取决于是否接受一条今天查不出东西、但一旦出错就会在 CI 上立刻指名道姓的规则。**

两个已知边界，写在报文和代码注释里，不是隐藏项：

1. 只比较**同一环境资源**（`stdout`、`stderr`、`stdin`、`clock`、`env`、`fs`）上的一对；命名不出资源
   的效应永不配对。
2. 每个效应记录自己的节点，抬过之后报文里的 `(图, 位置)` 仍是**确实装得下该节点**的位置（`placeHoldsEffect`）；位置装不下就不从它作答，也不拿它去报一个遍。正是这条检查在一个整闸之内认出了上面「186 红」那次的错。
3. 同一个 `origin` 在同一调用处被复制两次（同一调用被内联两遍）时无法区分，这类效应不参与比较——
   「程序里这个效应只出现一次（同一副本处）」这条守卫因此保留；去掉它，`if_without_else.statement`
   在 `std::opt::inline std::opt` 下会报一对输出未变的效应，**那不是待修的缺陷清单，是模型还不够**。