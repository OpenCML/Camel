# 车道表

一页常设。每条车道一行,行首是分支名。**开车道、合入、卡住、收车道时当场改自己那一行**,
随下一次往 `develop` 的合并带上。更新时间超过两小时的行,记为停滞。

读数分两栏:**分支读数**与自己比(派工时 → 现在),**`develop` 读数**是别人能依赖的数。
两栏分开,是因为一条车道的进展在合入前对 `develop` 不可见 —— 「没动」常常是「读的位置不对」。

| 分支 | 在做哪一项 | 分支读数 | `develop` 读数 | 下一笔预计 | 更新 |
|---|---|---|---|---|---|
| lane/stage2-graph | 收两处红:`region_roots.branch_statement`(`JOIN` 应答它那个 `BRCH` 所在区域)与 `stream.generators_shapes_consumed` 的编码器残差 | `GraphRead` 12 → **4** 个文件;`LoopDraftView` 7(卡 `GCFillBody::slots()`) | 12 / 7 | 再做一轮(约 10 分钟构建 + `feat/opt`/`trans`/`compile` 复核) | 2026-11-22 23:30 |
| lane/stage2-exec2 | 后端与运行时按 `Cast`/`Fill`/`Accs` 分支的地方改为问算子 | 已开,未提交 | 144 处 | 这一轮结束前 | 2026-11-22 23:35 |
| lane/stage2-trans2 | ① 已落:`node_operator.h` 的 OPER 分支从「答 `NotOne`」改为读 `GCOperBody` 的算子 URI;② 自己目录里直接读冻结图/草稿图的地方改读视图 —— **阻塞**:`develop` 上没有 `Graph::view()` | 首笔 `e83defe0b`;8 个文件 34 个调用点改问 URI,今天答案 0 变化(全树 0 个 OPER 带这五个 URI);三组与基线逐项相同:trans 111/109/2、compile 2668/2663/0(known 5)、opt 1623/1621/2,killed 0;另把 `fillBodyOf`/`accsBodyOf` 的闸门从家族改成 kind(翻转那天会读错 body) | 未合 | ② 等 `Graph::view()` 合入 | 2026-11-23 00:20 |
| lane/stage2-panic | 96 项写 `panic`,`C++` 侧同步填;然后 `graph.cpp` 改为从记录读,附改动前后对照表,要求一项不变 | 已开,未提交 | 50 处 / 96 项待写 | 需要一轮以上(对照表 729 项) | 2026-11-22 23:35 |
| lane/stage2-graphwrite | 编辑会话的两个入口:记录编辑的种类与碰过的区域、通知监听者、发布时按编辑记录分档检查 | 首笔 `cb647873f`(14 文件 +732/−65);本行所在的合并把 `develop` `fc2d7cae8` 合进来,无冲突;两个入口落在 `GraphDraft` 上(`NamedEdit` 关闭时记录、`noteEditedNode` 逐次记录),`editTier()` 喂 `commitViolation` 的两个 flag;`test::edit_record` 单计划合并后仍 1/1 绿,注掉通知即红(`notes=4 notified=0`) | 首笔已合(`9fcd1531b`);本行与两行拆分随下一笔带 | 收车道 | 2026-11-23 00:39 |
| lane/stage2-frontend-oper | 建图器不再产出 `Cast`/`Fill`/`Accs` 节点,改为产出 `OPER` + `GCOperBody` | 已开,未提交 | 146 处(其中 2 处是 `tr2` 的 kind 闸门) | 这一轮结束前 | 2026-11-23 00:30 |
| lane/stage3-facts | 五样分析搬进事实框架;33 处绕过事实层的纯度判断改问事实 | 已开,未提交;`facts.h` 全树零定义,先立起来 | — | 需要一轮以上 | 2026-11-23 00:30 |
| lane/cleanup-cml-comments | 44 个 `.cml` 用例的注释去掉交接页路径(只写测什么、为什么),按计划目录重录被注释移动的 AST 记录 | 首笔 `cf8d1c342`;`git grep docs/handover -- '*.cml'` 44 文件 → 0;`feat/parse` 前后同组 126/125/1,`ast-hashes` 读 4479 行只剩 1 行旧账(`src/stdlib/prelude.cml`,合并 `9fcd1531b` 那一侧落下的);另两处红(`plan_validation`、`runner.backend_matrix`)在 `develop` `8ec3e301a` 的干净树上同样红 | 未合 | 等点名 | 2026-11-23 01:35 |
| lane/stats-macro | 已合入 | `std::stats` 每张可达图一个名字一个布尔 | 已合(`0699bc111`) | — | 2026-11-22 23:35 |
| lane/stage2-frontend | 已合入 | 四个算子声明+内核+注册;前端不翻 | 已合(`801e22a43`) | — | 2026-11-22 23:20 |
| lane/stage2-opsfacts | 已合入 | 图上 `#macro` 五处写 → 零处写 | 已合(`215574512`、`257f84c7e`) | — | 2026-11-22 23:10 |

## 没开车道的一件

| 项 | 为什么 |
|---|---|
| 算子与事实(阶段 2 第 3 条) | 它要把效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取这五样搬进事实框架,而 `facts.h` 今天零定义全树 —— 没有落点可以搬进去。建那个框架的形状取决于 `Graph::view()` 能不能让读者走回视图,那在 `lane/stage2-graph` 手上 |