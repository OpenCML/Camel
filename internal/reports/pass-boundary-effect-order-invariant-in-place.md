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

## 一条尚未覆盖的形状，今天量过了：同一个 `var`（cell）上的两次写

状态表里最靠前的一条「尚未覆盖」是边界 1——**只比较环境资源**。挑它的原因不是它新，而是它今天仍
留在覆盖之外，而判据可以一句话指出来：

- **守哪个判据**：设计原则 3.3 的「同一资源上的效应保持写下的顺序」。**判据在哪一行**：
  `src/execute/pass.cpp` 的 `collectEffects` 里把非 `ResourceClass::Ambient` 的访问丢掉的这段
  （`if (access.resource.cls != ...Ambient) continue;`），以及 `sharedResourceOf` 只认 ambient 名字。
- **今天的颜色**：**绿**。最小程序（一个模块级 `var cell`，`cell += bump()` 之后 `println(cell)`，
  再 `cell += 5`、`println(cell)`）在 `std::nvm`、`std::opt std::nvm`、`std::opt::inline std::opt std::nvm`
  三遍下都是 `bump / 2 / 7`，顺序没坏。
- **为什么它本来就对，以及为什么仍不覆盖**：cell 的身份写在程序里（一个 `var` 的名字不是资源，是一格
  存储），**两张图之间没有可比较的名字**——两个 cell 是不是同一格，只有「谁拥有它」知道，而这正是
  上一次「调用节点即所有者」那条规则踩坏的地方（见上表 186 红）。所以这条边界是**判据本身的边界**，
  不是漏写：要覆盖它，得先有一条跨图的 cell 身份模型，那是另一条判据，不属于这张表。

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

As stated at the time: **the colour "on develop" is undefined** — a case that is not in the tree has no
colour there, and that is why the request "name the commit that fixed it" could only be answered with
"none".

Two follow-ups closed it.

The case is now on a lane of its own, based on `origin/develop` = `45bc25f47`, carrying only the case
file and its plan entry (no source change from the lane it came from): `5cdcb9692`. The whole fast gate
on that tree is `total 7928, pass 7917, fail 0, skip 9, known failures 2`, and
`feat.consistency.if_without_else` is 17 of 17.

The bisect was done as promised, and the answer is that **the red is not in any commit**. `4af274872`'s
parent `d86f50660` ("Judge the rewritten graph as a whole, and drop the session when it has a cycle")
was built and run against the same program: `std::nvm`, `std::opt` and `std::opt::inline std::opt` all
print `before / older / 1 1 / after`. Every tree that can be built — `d86f50660`, `abbc9c229`,
`1636a849d`, `45bc25f47` — is green, so **there is no commit to name as the fix**: the red lived only
inside that lane's working tree while the fix was being written, and the commit that carries the case
carries the fix with it.

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