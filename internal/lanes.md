# 车道表

一页常设。每条车道一行,行首是分支名。**开车道、合入、卡住、收车道时当场改自己那一行**,
随下一次往 `develop` 的合并带上。更新时间超过两小时的行,记为停滞。

读数分两栏:**分支读数**与自己比(派工时 → 现在),**`develop` 读数**是别人能依赖的数。
两栏分开,是因为一条车道的进展在合入前对 `develop` 不可见 —— 「没动」常常是「读的位置不对」。

| 分支 | 在做哪一项 | 分支读数 | `develop` 读数 | 下一笔预计 | 更新 |
|---|---|---|---|---|---|
| lane/canonical-order-independent | 已合入 (并列不再回落到 `stable_sort`;切不开的如实进 `inspect --ties` 的 `unresolved`) | 首笔 `c1f6a093a`；`canon_order` 新增第 6 个读数 `same`（八个同型同值的捕获，第二种写法把声明次序与捕获次序同时反转）：修前 `--ties` 说 `a known limitation`、用例红，修后该并列全部由结构定下、用例绿；`canon_order`+`canonical_gir`+`canonical_opt_export`+`same_graph`+`spelling`+`jgir`+`print_determinism` 11/11；`feat/trans`+`feat/compile`+`smoke` `[SUMMARY] total: 2790, pass: 2782, fail: 3, known failures: 5` | 未合 | 三红（`trans.pass_dump`、`trans.effect_trace`、`trans.cml`）把 src 暂存掉后在基线 `9680035a4` 上逐项同红；可合入，等合并位 | 2026-11-23 04:05 |
| lane/stage2-exec2 | 已合入 (按 kind 分派的 8 处改问算子,149→147;余 147 按类留在原处) | 已开,未提交 | 144 处 | 这一轮结束前 | 2026-11-22 23:35 |
| lane/stage2-trans2 | 已合入 (①`node_operator.h` 的 OPER 分支读 `GCOperBody` 的算子 URI;②视图改造随 `Graph::view()` 已合 `64ac86003` 解锁,待重开) | 首笔 `e83defe0b`;8 个文件 34 个调用点改问 URI,今天答案 0 变化(全树 0 个 OPER 带这五个 URI);三组与基线逐项相同:trans 111/109/2、compile 2668/2663/0(known 5)、opt 1623/1621/2,killed 0;另把 `fillBodyOf`/`accsBodyOf` 的闸门从家族改成 kind(翻转那天会读错 body) | 未合 | ② 等 `Graph::view()` 合入 | 2026-11-23 00:20 |
| lane/stage2-graphwrite | 已合入 (编辑会话两入口:记录编辑种类与碰过的区域、通知监听者、发布时按编辑记录分档检查) | 首笔 `cb647873f`(14 文件 +732/−65);本行所在的合并把 `develop` `fc2d7cae8` 合进来,无冲突;两个入口落在 `GraphDraft` 上(`NamedEdit` 关闭时记录、`noteEditedNode` 逐次记录),`editTier()` 喂 `commitViolation` 的两个 flag;`test::edit_record` 单计划合并后仍 1/1 绿,注掉通知即红(`notes=4 notified=0`) | 首笔已合(`9fcd1531b`);本行与两行拆分随下一笔带 | 收车道 | 2026-11-23 00:39 |
| lane/cleanup-cml-comments | 已合入 (44 个 `.cml` 的注释去掉交接页路径,按计划目录重录 AST 记录) | 首笔 `cf8d1c342`;`git grep docs/handover -- '*.cml'` 44 文件 → 0;`feat/parse` 前后同组 126/125/1,`ast-hashes` 读 4479 行只剩 1 行旧账(`src/stdlib/prelude.cml`,合并 `9fcd1531b` 那一侧落下的);另两处红(`plan_validation`、`runner.backend_matrix`)在 `develop` `8ec3e301a` 的干净树上同样红 | 未合 | 等点名 | 2026-11-23 01:35 |
| lane/stats-macro | 已合入 | `std::stats` 每张可达图一个名字一个布尔 | 已合(`0699bc111`) | — | 2026-11-22 23:35 |
| lane/stage2-frontend | 已合入 | 四个算子声明+内核+注册;前端不翻 | 已合(`801e22a43`) | — | 2026-11-22 23:20 |
| lane/stage2-opsfacts | 已合入 | 图上 `#macro` 五处写 → 零处写 | 已合(`215574512`、`257f84c7e`) | — | 2026-11-22 23:10 |
| lane/axis-tangent-wrong | 已合入 (`jvp_max_axis`/`min` 交出的是「各道切线的极大值」而非「极大值那个元素的切线」;18 项里 12 项原本错) | 首笔 `4b06d3981`;新例 `max_min_axis_lane_tangent` 在并列与不并列上逐元素读切线,按手算的那组对上(修前 18 项期望里 12 项不符,并列读 `[1.0, 1.0]` 而选择值 `[2.0, 1.0]`);`-Changed`(基 `9680035a4`)`[SUMMARY] total: 4880, pass: 4859, fail: 2, skip: 7`、12 known;两红都不在本车道:`frontend.ast_snapshot` 只差 `src/stdlib/prelude.cml` 一行旧账(记录行与文件内容都不在本笔 diff 内,是并入时留下的),`plan_validation` 是主机 checkout 缺 `build/lib/tiles/Release/tiles-plan-check`;反向本就正确(两条既有梯度用例数字不变、`same=true`),只是走的法律由 `max_axis_stated_t` 变成 `sum_axis_stated_t`+`where_t` | 未合 | 等点名 | 2026-11-23 03:20 |
| lane/panic-declarations | 已合入 (C++ 登记入口可读陈述 0→96;旧入口按 `blank()` 后的文本匹配,而它抹掉字符串字面量,那条路一条也没读到过) | 214/382 可读 | 213/382 | 余 168 个算子仍无可读陈述 | 2026-11-23 04:30 |
| lane/3a-exec | 已合入，阶段 3a 执行侧：`src/stdlib/std/{nvm,fvm,prl,cpp,exec}` 与 `src/tools` 里直连 `GCGraph`/`GraphDraft` 的地方；六笔，每目录一笔 | 六笔 `3c168c424`、`581ab33fe`、`1a9eb9a80`、`4b08496cc`、`940391d32`、`8fca97a5e`；六目录 573 → 497（nvm 28→26、fvm 104→96、prl 149→129、cpp 127→94、exec 97→94、tools 68→58）；497 按类：声明/身份链 345、容器与键 89、载荷 23、空指针初始化 20、注释 9、using 6、`const_cast` 2、lambda 3；按编号枚举的循环一处也没搬（`exec:graph_runtime_support.cpp:874` 那处留在 `GCGraph` 上，理由与 JOIN 无关：下标是计划的位置）；`smoke` 10/10、`smoke+feat/para` 114/114 绿，`feat/trans` 122/119/3 与 `feat/tools` 81/64/6 的红都与基线同红 | 未合 | 等点名；账在 `internal/reviews/graph-view-stage3a-exec-ledger.cn.md` | 2026-11-23 05:35 |
| lane/3a-b3 | 两件，按序：`C-1123-33` 把语料与库里剩下的 `[a:b]` 写成 `[a..b]`（一笔）；随后阶段 3a 第三批，`src/stdlib/{onnx,tiles}` 里直连 `GCGraph`/`GraphDraft` 的引用继续减（一笔，与 `3a-b2` 分目录） | 开在 `689304a87`（`develop` 头）。改前 `stdlib_values` `[SUMMARY] total: 59, pass: 35, fail: 24`，`[provenance] tip 689304a8770d90a6c8331a1a4512e5d6d4f8711e`、`clean`、`jobs 8`；24 红分两族：`str_unicode`/`str_chars` 各 6 报 `Expected ']' before ':'`，`array_basic`/`array_large` 各 6 报 `src/stdlib/array.cml:32` 的 `a slice bound must not be negative, and -1 is` | 未合 | ①文本笔 → ②结构笔 | 2026-11-23 14:00 |




## 没开车道的一件

| 项 | 为什么 |
|---|---|
| 算子与事实(阶段 2 第 3 条) | 它要把效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取这五样搬进事实框架,而 `facts.h` 今天零定义全树 —— 没有落点可以搬进去。建那个框架的形状取决于 `Graph::view()` 能不能让读者走回视图,那在 `lane/stage2-graph` 手上 |

## 已合入

`exemplars-path` `13e0228b9`（11 条全绿）、`3a-transforms` `70c7b324b`（`299 → 291`）、`panic-record` `6e4411d7e`（可读陈述 `214 → 261/382`，不能读的 `48 → 1`）、`notation-b2` `064939bbd`、`commit-checks-b2` `08fde0e53`（两处缺口，影响面 9099 例三类全 0）、`ast-record-recheck-b2`（零笔，重录与既有记录逐字相同）、`facts-b2` `5638096b8`（`facts.cpp` + 自检 28/28）、`graph-read-b2` `3c2ee39d6`（`GraphRead` 全树清零、`LoopView` 合一、23 个调用点、净减 2157 行）。读数留在 `git log` 与 `internal/backlog.md`，不占在飞的表。

## 已停 / 不续

| 分支 | 停的原因 | 处置 |
| --- | --- | --- |
| `stage2-graph` | WIP 落后 139 笔，是源搬进 `internal/` 之前的树（`C-1123-14`） | 作者已裁：从 `develop` 重开 —— 见 `lane/graph-read-b2` |
| `stage3-facts` | 同上；`facts.h` 有声明无定义 | 作者已裁：从 `develop` 重开 —— 见 `lane/facts-b2`（**已合入**） |
| `commit-checks` | 被 `commit-checks-b2` 取代（后者已合入） | 保留作证据，不合 |
| `data-arg-migration` | 作者撤回（`D-1123-20`）：`with` 的判据被重新裁定，那次迁移的前提不成立 | 保留作证据，**不合** |
| `with-param-audit` | 是上面那次迁移的底，前提同样被推翻 | 待作者重裁，不合 |
| lane/onnx-lowering | 55 条 ONNX 无 lowering 清零（零条假映射）+ shape 20 条 + residue 10 处；现做移植 | 39 笔，`ex2`（临时借 `tmp/base-measure` 做旧树对照） | `feat/compile` 2669/2662/2（2 条既存） | **卡在:6 笔在给已删除的 `GraphRead` 面重塑读法。**等:把 24 个调用点移植到 `GraphView` 单面 | 2026-11-23 12:00 |

## 只作证据（不在分支上，在标签上）

`with-param-audit` 与 `data-arg-migration` 的内容存在标签 `evidence/with-param-audit`、`evidence/data-arg-migration` 上，分支已删。理由：两条都不会合（一条作者撤回、一条前提被推翻），而它们每一笔 `develop` 都让「最落后分支」这个闸门读数涨一格 —— 那个数从此只描述它们，不描述任何一条会被合的车道。试过让它们跟上：`with-param-audit` 那 2 笔是另一代的 WIP，与今天的 `src/stdlib` 全冲突，合不进去。标签保住内容、不进分支计数，于是闸门重新描述真在飞的四条。

`packed-view-lifetime` 也在标签 `evidence/packed-view-lifetime` 上：它的 31 笔全部含在 `lane/oper-identity-write` 里（`merge-base --is-ancestor` 为真），是一条栈内路标，不是一条独立车道。读数留在 `internal/backlog.md` 与 `C-1123-24`。

## 那条栈现在是两个头

`lane/flip-consumers` ⊂ `lane/onnx-lowering`，`lane/packed-view-lifetime` ⊂ `lane/oper-identity-write`，而这两条互不相含 —— 所以栈要落得合**两次**：一次 `onnx-lowering`（迁移 + ONNX lowering + 视图），一次 `oper-identity-write`（成员读的位置进 `with` 的读者那一半）。合成一次合入会把两批交付条件不同的笔压成一笔。

`flip-consumers` 同样在标签 `evidence/flip-consumers` 上：它的 32 笔全部含在 `lane/onnx-lowering` 里，是栈底路标。于是那条栈现在只剩两个头（`onnx-lowering` 与 `oper-identity-write`），加一条在栈树上量设备红的 `device-programs`（它把整条栈合了进来，因为那六条红是在栈树上量的 —— 所以它合入时会带上栈）。
| lane/oper-identity-write | 成员读的身份一刀切：只写 `body.oper`，URI 不入载荷；成员读位置成为编译期参数，内核从 `with` 读 | 40 笔，未推未合 | `feat/compile` 2669/2662/2；`ref` 已修 | **卡在:设备侧要三个算子形的程序。**等:`lane/device-programs`（六条已全绿） |
| lane/device-programs | 给 `:op/cast`/`:op/access`/`:op/access_key`/`:op/fill` 补设备程序 | 43 笔（含整条栈，作为测量树），未推未合 | 六条设备红**全绿**，没有一条靠放宽检查 | **下一笔:`merge lane/onnx-lowering` 后重量 `feat/modules`，看「带进 15」降到 9** |
