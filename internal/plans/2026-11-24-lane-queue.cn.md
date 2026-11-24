# 车道队列（唯一来源）

**这一页是车道顺序的唯一来源。** 决策者的信、派工、backlog 行都不再各自维护一份顺序——那些只引用这一页。
（`C-1124-49`：此前顺序活在四处，其中两处会消失，而「一条规则写在两处」正是 `D-1123-125` 判过的那类缺陷。）

## 现在开着的（6 条）

| # | 车道 | 工作树 | 判据（可核的那一个） |
|---|---|---|---|
| 1 | `lane/view-gaps` | `Camel-wt\viewgaps` | 补 A（`attrsOf` + `RegionRecord` tag set + 节点权限）、B（载荷/原始 kind/`dataIndex`+`staticSlots`/帧布局）、C（控制与顺序边、反向 `uses()`、按边种类全表），**D、E 后放**；硬判据：`src/stdlib/std/tiles` 的 `GCGraph`/`GraphDraft` 数必须实际下降（A 单独解释了它 74→74） |
| 2 | `lane/kinds-to-oper` | `Camel-wt\kindsoper` | `git grep -cE '(NodeKind|GCNodeKind|Kind)::(Cast|Copy|Fill|Accs|CAST|COPY|FILL|ACCS)\b' -- src` = 0（现 180）**且** `git grep -cE 'add(Cast|Copy|Fill|Accs)Node\b' -- src` = 0（现 129），外加枚举删四项、视图不再两步读法 |
| 3 | `lane/3a-onnx-tiles` | 既有树 | `src/stdlib/std/onnx`（84）与 `src/stdlib/std/tiles`（74）两个数往 0 走；读不了视图的部分写缺口页并给页名 |
| 4 | `lane/3a-transforms-autodiff` | 既有树 | `src/stdlib/transforms`（124）与 `src/stdlib/autodiff`（41）两个数往 0 走；**这条没有缺口页，须新写一页** |
| 5 | `lane/names-layer` | `Camel-wt\nameslayer` | 五层的次序在 `git grep` 里**只出现一处**；一条值名、一条类型名的「本模块遮住 prelude」用例各过；类型名那一半等 `op-overload-merge` 落地再接 |
| 6 | `lane/op-overload-merge` | 既有树 | 已量到 `values` 9 红→0、推导路径 `compilerNamedOperator` 0 处代码；**卡在第四次合并（选择集因改名不一致），正在把改名拆出这一笔** |

## 排在后面的

| # | 车道 | 为什么排后面 |
|---|---|---|
| 7 | `:op/` URI 统一 | 排在 `kinds-to-oper` 清零之后（作者 `D-1123-131`） |
| 8 | `feat/compile` 归零的 B 组 | 等 `op-overload-merge` 落地（它改写约 369 条红）；落地后在那时的 develop 上跑 feat/compile，剩下的红按 `D-1123-93` red-bisect 的口径归组：B 组 = 不属 61 链、每条指名一条带进它的车道；那张名单就是判据，每合一笔报红数（作者 `D-1123-133`） |
| 9 | 61 链收尾 | 接在 `op-overload-merge` 之后：四处 `to_array(0 .. n)` 换回 `0 .. n`，`random.cml:114` 一并；报 feat/compile 红数下降多少 |
| 10 | 合并前补测那三笔 | `D-1123-102`：d2c3f10d6、973868aec、4b528b928 在各自第一父上补测，有新红或换红就修或回退 |
| 11 | 3a 重写车道的余量 | `src/stdlib/std/cxx/opt` 与 `src/kernel/rewrite`；写图经编辑会话的入口；判据同 3a |
| 12 | rewrite 分类里 N=1 那一处 | 行为有差异而没有测试：补一条能区分两种行为的用例，再定改法 |
| 13 | GPU 补测（`C-1123-65`） | 只在构建机空闲（无车道运行、负载低）时跑那 4 处按 NaN 比的 GPU 用例，不和车道抢 |

## 三条对所有车道都成立的

1. **交回 = 已提交且已推车道分支**（铁律 13）。「完成、未提交、等点名」不算交回。
2. **读不了视图就写缺口页，按能力归类，交回时给页名**（作者 `D-1123-130`）。
3. **3a 类车道交回必须报本目录的单一前后数；数没降就说明为什么，缺口页就是那个为什么**（作者 `D-1123-127`）。