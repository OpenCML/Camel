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
| lane/3a-b2 | 阶段 3a 第二批：变换、重写、执行三处直连 `GCGraph`/`GraphDraft` 的读者继续迁，加上测试里的遍（`src/stdlib/test`，stdlib 最后一块大的直读区）；四笔，每类一笔 | 四笔 `aaa2f8a90`、`8de478cee`、`f9189ff32`、`5066fa6fc`；`src/stdlib` 1772 → 1586（变换 192→159、重写 40→32、测试 434→292、执行侧五目录 439→436），新增 0 处；145 处声明丢掉类型名（另六个纯形状谓词改问视图），其中九行仍带着 `GCGraphBuildAccess`/`GCGraphPayloadArena` 这类同前缀的另一个类型名，所以按行数只掉 142；循环：两处「找控制台写」改走 `nodes()`、`portIndexIn` 改问端口表，其余按编号的逐一留了理由（JOIN、控制输入、载荷、`kernelArgumentsOf` 的实参次序）；`smoke` 10/10/0、`feat/parse` 126/126/0、`feat/trans` 112/106/6、`feat/compile` 2669/2668/1，干净树上 `[provenance] tip 98deec31d`；七条红在基点 `234dfb0a4` 上用同样五个计划文件逐名同红（73/66/7）；**工具 `src/tools` 58 处一处未动**：那里是写路径的形参（debugger 改图并存帧、遍钩子回调签名、`execPlan()`），只读的面答不了，等 3c 的引用模型 | 未合 | 等点名；账在 `internal/reviews/graph-view-stage3a-b2-ledger.cn.md` | 2026-11-23 10:55 |

| lane/tooling | 工具欠账三条（`C-1123-15` 远端只带回一份基准、`C-1123-16` 构建戳不标识构建、`C-1123-7` `-AstOnly` 参数绑定），作者说这是「一个读数不可信，有三种不可信的方式」 | 首笔 `bf11b50cb`；①`-UpdateGolden` 改前：主机重录了 1 份、本地仍是旧的那份（`git diff --numstat` 1 行），改后：`goldens: 2 rewritten on the host, 1 written here, 1 already identical, 0 left stale`，`annotated_dump` 不带 `-UpdateGolden` 再跑 3/3 绿；②`node scripts/check-build-stamp.mjs` 对旧规则 3 项红（`261123_040107` 无内容标签、同内容两次全新配置戳不同、不同内容戳区分不出），新规则全绿，主机 `-Check build-stamp` 1 run 0 failed；③`-AstOnly 'a,b'` 旧脚本红（`--only selected none of the tree's 4256 .cml file(s)`）、新脚本 `re-hashed 229 / kept 4254` 且记录 sha256 与整批重录逐字节相同，`feat/parse` 两次 126/126/0 | 未合 | 等点名 | 2026-11-23 12:04 |
| lane/flip-consumers | 五层迁移的栈顶（建图器、C++、autodiff、优化、下游读者） | 32 笔，刷到 17a11804d（已合 91a3fd5cc 的 96 笔），未推未合 | smoke 0、eat/parse 0、eat/compile 11 红 | **卡在:栈上剩 5 条红,根在 72df5049c（成员读写成 ccess）—— 即作者那条裁定的**后半**。**等:位置被搬进 with（oper-identity-write 做完了读者那一半,3 条已绿） | 2026-11-23 11:20 |
| lane/packed-view-lifetime | 修「一个闭包被当成 cell 塞进 ar int 槽」那族（C-1123-24） | 31 笔，5ae19df7，未推未合 | 旧症状全消失（不再 70.0/1046.0、不再 materializeAggregate、12 次逐字相同）。根因链已交:问题页 2026-11-23-an-operand-identity-is-a-uri-or-a-table-lookup.cn.md，**作者已答**（d56dc52ef） | **卡在:整条栈能否落地** —— 它不是独立的一笔（`C-1123-30`）。**等:栈上红降到能当轮修完**（`oper-identity-write` 已 94 → 86、`onnx-lowering` 已把 55 清零）。`D-1123-23` 允许这样写清而不强合 | 2026-11-23 10:30 |

| lane/facts-arrangeable | 作者裁 `6fe8c9bbb`：「可自由安排」是一件事（四项合取，终止按节点判），事实层给 `core.arrangeable`，各处改问它；续裁：读 `:var` 是依赖不是效应，效应声明一律写进 `Record::uses`，事实层只读记录、删回落 | 15 处改问事实层（`is_chain_statement`、`nodeHasEffect`、`--ops` 的 `external`、tiles 四处放置判断、`graph.is_pure`）；检查器 33 处拼答案 → **0**（18 处只读一个原子）；`stateUsesInRecord` 在 `OperatorTable::declareSemantics` 里把声明过的 `ResourceSpec` 写进记录（`builtin_semantics.cpp` 的建内算子也在内）；`camel inspect --ops` 加 `record`/`uses_stated`/`clause_pure`，`test/cases/feat/modules/oprecord_uses.py` 把两个读法扣在一起；读数：814 个算子全部有记录、**349 个记录里写了 `uses`**、465 个仍沉默，其中 **440 个表里声明了 accesses 却没进记录**；smoke 10/10/0、feat/parse 126/126/0、op_record 2/2（138/138/0）；gpu 层 17 红（13 已记录，4 未记录：`module_lease`、`async_native_submissions` 是本机缺 `build/lib/tiles/Release/tiles-module-lease` 与 `tiles-async-check`；`nofallback.*` 两条是改后新增） | ① 那 13 处 `:var/get` 的答案变化是纠正，写进报告与提交信息；② `TILES_OUTLINED` 一处读到 12（原 10），`nofallback.*` 两条新增红：有算子按记录判成可上设备而设备层没有它的程序，落在诊断列表里 —— 这是「答案变了」，按规矩停在这里，等作者说读记录还是读表 | 2026-11-23 11:55（裁 `6fe8c9bbb`） |

| lane/record-uses | 作者续裁：`OperatorTable` 的效应语义就是 C++ 入口对 `pure`/`uses` 的陈述，登记时写进 `Record::uses`（事实层只读记录）；并加一条检查列出两个入口都没说的算子 | `stateUsesInRecord` 挪到 `OperatorTable::set()` 之前，`set` 发布语义后调它（`pure` 写成空 access list；state-only/effectful 的词不编造 accesses，3.3 的读法就是答案）；`declareSemantics` 那一处保留并重写注释；读数（`camel inspect --ops`，814 个算子）：**`uses_stated` 349 → 789、沉默 465 → 25、表里声明了 accesses 却没进记录 440 → 0**；成因查明：那 440 不是记录被整体换掉，而是 `set()` 是唯一不过 `declareSemantics` 的陈述入口（`declare`/`declareResources`/`declareDefinition` 都汇进它，`CppRegistration` 走 `Registry::declare` 按项合并），所以它们的 `resources.has_value()`（`pure` 写下的空表）让读数看着像「声明了又丢」；`scripts/check-operator-uses.mjs` 从记录读这三个数并按模块列出那 25 个，`-Check operator-uses` 已接入（report 行）；它第一次运行就找出 `:op/mem_blocks` 两处陈述不一致（`kAccessFree` 的 `pure` 与 `.pure = false` + `externalAmbient("mm")` 并存），已删掉列表里那一项、记录与表恢复一致 | 25 个算子仍无陈述（`graph:open`、`pyplot:plot`×8、`python:*`×9、`rewrite:*`×4、`transforms:*`×3），逐个补声明是下一笔 | 2026-11-23 12:05 |

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
| lane/facts-arrangeable | 「可自由安排」成为一个事实 kind（`Arrangeable` 四项合取）+ 效应语义写进 `Record::uses` | 5 笔，`6f6fa4ac3`，未推未合 | 814 算子全有记录，**349 个有 `uses`**（改前 0），465 沉默 | **卡在:那 440 个「表里声明了却进不了记录」—— 作者判为**记录在装载时被整体替换**（`447b7a2ed`）。**等:按项合并（`lane/record-uses` 在做） | 2026-11-23 12:00 |
| lane/record-uses | 记录按项合并、已陈述项不被未陈述覆盖；加检查列「两个入口都没说」的算子 | 5 笔，未推未合 | 基线 `9237d52ed` | **下一笔:定位整体替换那一步；读数应为 **789 有 `uses` / 25 真沉默**** | 2026-11-23 12:00 |
| lane/tooling | `C-1123-15` 远端带回一份基准 / `C-1123-16` 构建戳不标识构建 / `C-1123-7` `-AstOnly` 参数绑定 | 新建，基线 `9237d52ed` | 未测 | 三条各有一个「改前会红的用例」 | 2026-11-23 12:00 |
| lane/onnx-lowering | 55 条 ONNX 无 lowering 清零（零条假映射）+ shape 20 条 + residue 10 处；现做移植 | 39 笔，`ex2`（临时借 `tmp/base-measure` 做旧树对照） | `feat/compile` 2669/2662/2（2 条既存） | **卡在:6 笔在给已删除的 `GraphRead` 面重塑读法。**等:把 24 个调用点移植到 `GraphView` 单面 | 2026-11-23 12:00 |

## 只作证据（不在分支上，在标签上）

`with-param-audit` 与 `data-arg-migration` 的内容存在标签 `evidence/with-param-audit`、`evidence/data-arg-migration` 上，分支已删。理由：两条都不会合（一条作者撤回、一条前提被推翻），而它们每一笔 `develop` 都让「最落后分支」这个闸门读数涨一格 —— 那个数从此只描述它们，不描述任何一条会被合的车道。试过让它们跟上：`with-param-audit` 那 2 笔是另一代的 WIP，与今天的 `src/stdlib` 全冲突，合不进去。标签保住内容、不进分支计数，于是闸门重新描述真在飞的四条。