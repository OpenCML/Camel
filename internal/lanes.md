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
| lane/stage2-frontend-oper | 建图器不再产出 `Cast`/`Accs`/`Fill` 节点,改为产出 `OPER` + 算子 URI。**blocked**:这一笔压在分支上不合 —— 翻完才第一次能说话的 18 处红分属三条别的活(`std::cpp` 加算子的规则、autodiff 加成员读的导数规则、按 kind 数成员读的校验改判据),一轮修不完 | `blocked`,不推、不合 `develop`;首笔 `060091eb5`(`cast` 4 处),`72df5049c`(`access`/`access_key` 30 处,只剩 2 处兜底),本笔 `8d0e4fea8` + `ea4557211`(`fill`:内核改读 `norm` 四处,声明不动;16 处构造点改成 `:op/fill` 的 OPER 节点,读回构造的 6 处改成两种形状都认);合 `develop` `5a63a3b52` 后同一棵树,`feat/parse,compile` 2800 例 2768/21/5 → 2754/35/5 —— **14 处由绿转红**(autodiff 10、`std::cpp` 1、按 kind 的校验 2、ref 诊断措辞 1),其余 2786 例逐项相同,无一处反向变化、无用例增减,killed 15 两组相同;加 `feat/trans` 那 4 处共 18 处新红;`fill` 那一笔自己 **0 红 0 绿**:`feat/parse,compile,trans` 2906 例 2848/53/5(翻转前 2848/53/5,差的是被删掉的 4 例探针),逐项相同;印出的图里 `type="CAST"`、`type="ACCS"`、`type="FILL"` 各 0 处 | 未合 | 等 `std::cpp`/autodiff/kind 校验三条下游;`fill` 声明按作者裁定未动,读 `with` 取字段的其余构造算子已列成清单待编号 `C-` | 2026-11-23 01:45 |
| lane/stage3-facts | 五样分析搬进事实框架;33 处绕过事实层的纯度判断改问事实 | 已开,未提交;`facts.h` 全树零定义,先立起来 | — | 需要一轮以上 | 2026-11-23 00:30 |
| lane/cpp-knows-access | 摞在 `lane/stage2-frontend-oper` 上:`std::cpp` 按算子名认那四个(转换、拷贝、构造、成员读)——`lowerNode` 不再只看节点 kind,先问算子 URI,四个各按自己那种写法取参数(构造的值就是模板之后的普通参数;成员读的位置/名字是常量) | 首笔 `09891bd37`(代码,`function_emitter.cpp` 一处)、`452a8ab3b`(DOT `type=` 列的问题页);同一棵树上改前改后:`1b5b932e5` `[SUMMARY] total: 283, pass: 262, fail: 21` → 加本笔 `total: 283, pass: 276, fail: 7`,14 处 `[FAIL]`→`[PASS]`,零新红 | `feat/trans` `total: 112, pass: 109, fail: 3`(`dump_after_each_pass`、`effect_order_across_backends`、`print_then_run_agrees_with_the_origin`,三处在干净 `develop` `b8b7b7787` 上逐项同红) | 剩下的 7 处:3 处红在 DOT 的 `type=` 列(`port_names_and_fill_types`、`comprehension_graph`、`static_tuple_function_materializes`),不是本遍;`print_and_recompile` 红在 `std::cml` 印出的成员读调用重新过不了类型检查(`develop` 上不红);另 3 处 `develop` 上本就红 | 等点名 | 2026-11-23 03:02 |
| lane/autodiff-knows-access | 导数表补上成员读(`:op/access`/`:op/access_key`)——一条成员读的反向是把这个成员的梯度写回它在聚合体梯度里的那个位置、其余位置写零切向,不是零梯度也不是恒等;读的是哪个成员由第一操作数的常量与**它的类型**决定(位置是 int、名字是 string,与 `__access_value__` 同一判据)。摞在 `lane/stage2-frontend-oper` 上(基线 `1b5b932e5` + 合 `941efb82c`),不是从 `develop` 开 | 首笔 `6bace0f53`(合 `cpp-knows-access`),本笔 `b9aa2359a`;同一棵树同一计划改前改后 `feat/modules/autodiff` `290 例 232/58` → `290 例 276/13`(known 1、killed 1),13 处都不是这条规则(`:op/cast` 缺规则 3、捕获 `var` 4、拒绝措辞 2、benchmark 4);新用例 `struct_member_read_grad` 六次全绿,断言印出 `dw=10.0 db=2.0` 与 `dp=26.0 dq=2.0`;gpu 层 6 红 → 2 红,剩下两处改由「设备端没有翻转后结构算子的程序」拒绝(`:op/access_key`、`:op/fill`、`tensor:zeros_like` 都点名列在拒绝里),那是翻转自己的余量;`feat/trans` 改前改后同为 `112/107/5`,同样那五处(其中两处是翻转的余量:rgir 不再有 `ACCS` 行、成员读调用的 cml 往返) | 未合 | 等点名 | 2026-11-23 03:26 |
| lane/stats-macro | 已合入 | `std::stats` 每张可达图一个名字一个布尔 | 已合(`0699bc111`) | — | 2026-11-22 23:35 |
| lane/stage2-frontend | 已合入 | 四个算子声明+内核+注册;前端不翻 | 已合(`801e22a43`) | — | 2026-11-22 23:20 |
| lane/stage2-opsfacts | 已合入 | 图上 `#macro` 五处写 → 零处写 | 已合(`215574512`、`257f84c7e`) | — | 2026-11-22 23:10 |

## 没开车道的一件

| 项 | 为什么 |
|---|---|
| 算子与事实(阶段 2 第 3 条) | 它要把效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取这五样搬进事实框架,而 `facts.h` 今天零定义全树 —— 没有落点可以搬进去。建那个框架的形状取决于 `Graph::view()` 能不能让读者走回视图,那在 `lane/stage2-graph` 手上 |