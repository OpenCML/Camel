# 车道队列（唯一来源）

**这一页是车道顺序的唯一来源。** 决策者的信、派工、backlog 行都不再各自维护一份顺序——那些只引用这一页。
（`C-1124-49`：此前顺序活在四处，其中两处会消失，而「一条规则写在两处」正是 `D-1123-125` 判过的那类缺陷。）

## 现在开着的（6 条）

| # | 车道 | 工作树 | 判据（可核的那一个） |
|---|---|---|---|
| 1 | `lane/view-gaps` | `Camel-wt\viewgaps` | **A→B→C 按序，D 接在 C 后进同一车道**（作者 `D-1123-137`）：A（`attrsOf` + `RegionRecord` tag set + 节点权限）、B（=B-2，载荷字节/原始 kind/帧布局不交给视图）、C（控制与顺序边、反向 `uses()`、按边种类全表）。**D = 视图 `typeOf` 对「类型在静态区」的节点答 nullptr，那是答错不是缺答**，修在 `typeOf` 里，**先量这种节点有多少**。**G 并入 C**；**F 随 3b**；**H 等 `kinds-to-oper` 清零**。判据用脚本 `node scripts/count-graph-readers.mjs src/stdlib/tiles`（不再手抄 `git grep`） |
| 2 | `lane/kinds-to-oper` | `Camel-wt\kindsoper` | `node scripts/count-graph-readers.mjs src` 的两个 kind 数到 0，外加枚举删四项、视图不再两步读法。**现 A 125 / B 96**（起点 180/129）；CAST/COPY 已清零，余下 FILL/ACCS 删了会改行为（稀疏 slots、`mayFault=false`），**属语言侧决定，须停下报** |
| 3 | `lane/3a-onnx-tiles` | 已合 `9f9c316d3` | `node scripts/count-graph-readers.mjs src/stdlib/onnx src/stdlib/tiles` → **84→70 / 74→71**；缺口页 `internal/questions/2026-11-24-what-the-graph-view-does-not-answer.cn.md` |
| 4 | `lane/3a-transforms-autodiff` | 已交回 `df5a1cc3c1`，未合 | `transforms` **118→112**、`autodiff` **40→40**（40 处全是图身份，**不是缺口**，已单列）；缺口页已成；**未合原因：`internal/questions/README.md` 生成表冲突** |
| 5 | `lane/names-layer` | 已交回 `16532a2f6c`，未合 | 层次序收进 `src/compile/src/name_layers.h` 一张表；`ns.p36_own_value_before_prelude`、`ns.p37_own_type_before_prelude` 各绿；类型名那一半等 `op-overload-merge` 落地再接 |
| 6 | `lane/op-overload-merge` | 既有树 `Camel-wt\op61` | **第五次读数：`fixed=309 new=0 changed=103 added=363 removed=3`**。改名已拆出（`e8447de7d6`，`--name-status` 0 个 `R`）。**卡在 `changed=103`：每条要么把期望带上，要么说明为什么换的是失败方式而不是红绿** |

## 排在后面的

| # | 车道 | 为什么排后面 |
|---|---|---|
| 7 | `:op/` URI 统一 | 排在 `kinds-to-oper` 清零之后（作者 `D-1123-131`） |
| 8 | `lane/view-edges`（E：程序侧收图句柄） | 排在 `view-gaps` 之后（作者 `D-1123-137`）：`Program` / `Context` / 事实层三个入口改成收图句柄；**判据就是这三个入口改收图句柄**。视图侧无解（视图不答「谁持有我」），而它是 `transforms` 里最大一块可回收（13 行） |
| 9 | `feat/compile` 归零的 B 组 | 等 `op-overload-merge` 落地（它改写约 369 条红，实为 `fixed=309`）；落地后在那时的 develop 上跑 feat/compile，剩下的红按 `D-1123-93` red-bisect 的口径归组：B 组 = 不属 61 链、每条指名一条带进它的车道；那张名单就是判据，每合一笔报红数（作者 `D-1123-133`） |
| 10 | 61 链收尾 | 接在 `op-overload-merge` 之后：四处 `to_array(0 .. n)` 换回 `0 .. n`，`random.cml:114` 一并；报 feat/compile 红数下降多少 |
| 11 | 合并前补测那三笔 | `D-1123-102`：d2c3f10d6、973868aec、4b528b928 在各自第一父上补测，有新红或换红就修或回退（**在跑**） |
| 12 | 3a 重写车道的余量 | `src/kernel/rewrite` 与 `cxx/opt`（**路径须先核，`src/stdlib/std/cxx/opt` 可能不存在**）；写图经编辑会话的入口；判据同 3a（**在跑**） |
| 13 | rewrite 分类里 N=1 那一处 | 行为有差异而没有测试：补一条能区分两种行为的用例，再定改法 |
| 14 | GPU 补测（`C-1123-65`） | 只在构建机空闲（无车道运行、负载低）时跑那 4 处按 NaN 比的 GPU 用例，不和车道抢 |
| 15 | `lane/count-script` | 把 3a 与 kinds 的计数做成 `scripts/count-graph-readers.mjs`：路径不存在 ⇒ **exit 2 并点名**，合法的零照常读成 0（**在跑**） |

## 三条对所有车道都成立的

1. **交回 = 已提交且已推车道分支**（铁律 13）。「完成、未提交、等点名」不算交回。
2. **读不了视图就写缺口页，按能力归类，交回时给页名**（作者 `D-1123-130`）。
3. **3a 类车道交回必须报本目录的单一前后数；数没降就说明为什么，缺口页就是那个为什么**（作者 `D-1123-127`）。