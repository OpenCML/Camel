# 车道表 · 2026-11-22 23:30

> 常设车道表已移到 [internal/lanes.md](../lanes.md),每条车道自己更新自己那一行。这一页只留作那一晚的快照。


一页四行,每条车道按作者催办要的形状:在做哪一项、分支名、预计第一笔什么时候合。
读数分两栏:**分支上的读数**与 **`develop` 上的读数** —— 作者的催办表读的是后者,
而每条车道在合入前都只改动前者,所以「没动」常常是「读的位置不对」。

## 四条阶段 2 车道

| 车道 | 在做哪一项 | 分支 | 预计合入 | 分支读数 | `develop` 读数 |
|---|---|---|---|---|---|
| 图 | 收两处红:`region_roots.branch_statement`(`JOIN` 应答它那个 `BRCH` 所在区域,不是 arm)与 `stream.generators_shapes_consumed` 的编码器残差 | `lane/stage2-graph` | 再做一轮(约 10 分钟构建 + `feat/opt`/`trans`/`compile` 复核) | `GraphRead` **4 个文件**(派工时 12);`LoopDraftView` 7 | 12 / 7 |
| 前端 | 四个算子(`copy`/`cast`/`fill`/`access`)的**声明与内核**已落,读者未迁;翻与不翻的差分已量 | `lane/stage2-frontend` | 读者清单 144 处分段后按段合 | `GCNodeKind::{Cast,Fill,Accs}` 已在声明侧清零 | 144 处 |
| 算子与事实 | 已合入 | — | 已合(`215574512`、`257f84c7e`) | — | — |
| 新派:`std::stats` 的 `"macro"` 键 | 给 `isMacro()` 一个读者 —— 算子记录说 macro 这条路今天没有可观察面 | `lane/stats-macro` | 这一轮结束前 | 未提交 | — |

## 两件作者点名而没动的

| 项 | 现状 |
|---|---|
| `panic` 车道 | 库里 50 处;`mayFault` 表 96 项待写。**没有车道在跑这件事** —— 它需要 C++ 登记侧先能填,在那之前两边不一致是作者裁的「对的红,列待办,不消」 |
| 合并位 / 根目录整理 | `editors/`、`lib/*/signatures/`、`pypi/`→`packaging/`、`promo/` `exemplars/` `ROADMAP.md` `REPRODUCE.md`→`internal/`、`logo.png`、`export_matrix.*`、`hello.cml`、`CHANGELOG.md` —— 未做,且**今天先做了搬家**(`kernel/` `parse/` `compile/` `bindings/` `lib/` → `src/`),那件事已经把根目录的主要部分收进 `src/` |

## 一条结构性问题

作者的催办表以 `develop` 头为读数。**分支上的读数在合入前不可见**,所以那张表量的是
「已经合进来的进展」,不是「已经做出来的进展」。今晚的实例:图车道的 `GraphRead`
从 12 降到 4,而表上写的是 12。

这不是谁读错了 —— 表本来就该读 `develop`,否则它量的不是可以依赖的东西。
问题是**回报的格式**:一条车道要能说清「我做到了 X,它现在在分支上,合入后会变成 Y」,
而不是只报一个数。今晚四条车道里只有图车道这么说了。