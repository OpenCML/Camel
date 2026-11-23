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
||| 已合入 (C++ 登记入口可读陈述 0→96;旧入口按 `blank()` 后的文本匹配,而它抹掉字符串字面量) |l|a|n|e|/|p|a|n|i|c|-|d|e|c|l|a|r|a|t|i|o|n|s| ||| |9|6| |项|写| |`|p|a|n|i|c|`|,|`|C|+|+|`| |侧|同|步|填|;|然|后| |`|g|r|a|p|h|.|c|p|p|`| |改|为|从|记|录|读|,|附|改|动|前|后|对|照|表|,|要|求|一|项|不|变| ||| |首|笔| |`|1|c|9|f|c|6|3|9|8|`|,|两|笔|合|计| |5| |文|件| |+|2|7|0|/|−|1|3|0|(|`|8|0|7|3|8|3|5|7|3|`| |是| |`|g|r|a|p|h|.|c|p|p|`| |那|一|笔|)|:|9|6| |项|写|成| |`|C|p|p|P|a|n|i|c|S|t|a|t|e|m|e|n|t|`| |字|面|量|表|(|`|b|u|i|l|t|i|n|_|s|e|m|a|n|t|i|c|s|.|c|p|p|`|)|,|循|环|把|同|一|对|交|给|空|故|障|表|与| |`|C|p|p|R|e|g|i|s|t|r|a|t|i|o|n|`|,|并|把|两|处|读|回|来|对|一|次|、|不|一|致|拒|绝|加|载|并|点|名|;|`|s|t|a|t|e|N|o|F|a|u|l|t|O|p|e|r|a|t|o|r|`| |连|声|明|带|两|处|调|用|删|除|,|`|d|e|c|l|a|r|e|(|)|`| |那|条|分|支|一|并|去|掉|。|检|查|的| |C|+|+| |入|口|原|来|拿| |`|b|l|a|n|k|(|)|`| |之|后|的|文|本|匹|配| |—|—| |*|*|字|面|量|被|抹|掉|,|这|条|路|从|没|匹|配|到|任|何|东|西|*|*|;|现|在|只|抹|注|释|、|按|条|目|类|型|读|表|、|认|不|出|条|目|就|退| |2|,|末|行|多|一|句|可|读|陈|述|的|算|子|数|。|读|数|:|C|+|+| |入|口|可|读|陈|述| |0| |→| |9|6| |条|(|9|6| |项|全|在|)|,|有|可|读|陈|述|的|算|子| |2|1|3| |→| |2|1|4|/|3|8|2|(|9|6| |项|里| |9|5| |项|此|前|就|有| |C|a|m|e|l| |陈|述|、|只|写| |`|f|a|l|s|e|`| |不|写| |`|p|a|n|i|c|`|,|`|:|c|o|l|l|/|e|q|`| |此|前|一|句|也|没|有|)|;|裁|决| |4|8| |红|/|3|3|1| |相|符|/|3| |条|不|抛|,|前|后|逐|行|相|同|。|远|程| |`|s|m|o|k|e|`|+|`|f|e|a|t|/|o|p|t|`|+|`|f|e|a|t|/|c|o|m|p|i|l|e|`|+|`|f|e|a|t|/|m|o|d|u|l|e|s|`|+|`|f|e|a|t|/|l|i|n|e|a|r|`|:|`|t|o|t|a|l| |7|2|4|6|,| |p|a|s|s| |7|2|1|7|,| |f|a|i|l| |6|`|(|`|d|o|c|s|.|*|`| |3|、|`|p|l|a|n|_|v|a|l|i|d|a|t|i|o|n|`|、|`|o|p|t|.|w|r|i|t|i|n|g|s|`| |2|,|与| |`|l|a|n|e|/|s|t|a|g|e|2|-|g|r|a|p|h|`| |记|在| |`|d|e|v|e|l|o|p|`| |上|同|红|的|那|批|同|名|)| ||| |未|合| ||| |等|点|名|;|余| |1|6|8| |个|算|子|没|有|任|何|可|读|陈|述|(|4|7| |红| |+| |1|2|1| |不|抛|却|没|人|说|)|,|不|在|这|一|笔| ||| |2|0|2|6|-|1|1|-|2|3| |0|3|:|5|2| |||
| lane/stage2-graphwrite | 已合入 (编辑会话两入口:记录编辑种类与碰过的区域、通知监听者、发布时按编辑记录分档检查) | 首笔 `cb647873f`(14 文件 +732/−65);本行所在的合并把 `develop` `fc2d7cae8` 合进来,无冲突;两个入口落在 `GraphDraft` 上(`NamedEdit` 关闭时记录、`noteEditedNode` 逐次记录),`editTier()` 喂 `commitViolation` 的两个 flag;`test::edit_record` 单计划合并后仍 1/1 绿,注掉通知即红(`notes=4 notified=0`) | 首笔已合(`9fcd1531b`);本行与两行拆分随下一笔带 | 收车道 | 2026-11-23 00:39 |
| lane/stage2-frontend-oper | 建图器不再产出 Cast/Fill/Accs，改产 OPER + GCOperBody —— **翻转栈的第一层** | 8 笔；栈整批 36 红 | **卡在:整条栈能否落地** —— 它不是独立的一笔,是翻转迁移的一层;层内单独合会让 `develop` 上出现「一半旧形状一半新形状」的树（`C-1123-30`）。**等:栈上红降到能当轮修完**（现 36）。`D-1123-23` 允许这样写清而不强合 | 栈内第 1 层；无独立可合的一笔 | 2026-11-23 09:05 |
| lane/cleanup-cml-comments | 已合入 (44 个 `.cml` 的注释去掉交接页路径,按计划目录重录 AST 记录) | 首笔 `cf8d1c342`;`git grep docs/handover -- '*.cml'` 44 文件 → 0;`feat/parse` 前后同组 126/125/1,`ast-hashes` 读 4479 行只剩 1 行旧账(`src/stdlib/prelude.cml`,合并 `9fcd1531b` 那一侧落下的);另两处红(`plan_validation`、`runner.backend_matrix`)在 `develop` `8ec3e301a` 的干净树上同样红 | 未合 | 等点名 | 2026-11-23 01:35 |
| lane/stats-macro | 已合入 | `std::stats` 每张可达图一个名字一个布尔 | 已合(`0699bc111`) | — | 2026-11-22 23:35 |
| lane/stage2-frontend | 已合入 | 四个算子声明+内核+注册;前端不翻 | 已合(`801e22a43`) | — | 2026-11-22 23:20 |
| lane/stage2-opsfacts | 已合入 | 图上 `#macro` 五处写 → 零处写 | 已合(`215574512`、`257f84c7e`) | — | 2026-11-22 23:10 |
| lane/axis-tangent-wrong | 已合入 (`jvp_max_axis`/`min` 交出的是「各道切线的极大值」而非「极大值那个元素的切线」;18 项里 12 项原本错) | 首笔 `4b06d3981`;新例 `max_min_axis_lane_tangent` 在并列与不并列上逐元素读切线,按手算的那组对上(修前 18 项期望里 12 项不符,并列读 `[1.0, 1.0]` 而选择值 `[2.0, 1.0]`);`-Changed`(基 `9680035a4`)`[SUMMARY] total: 4880, pass: 4859, fail: 2, skip: 7`、12 known;两红都不在本车道:`frontend.ast_snapshot` 只差 `src/stdlib/prelude.cml` 一行旧账(记录行与文件内容都不在本笔 diff 内,是并入时留下的),`plan_validation` 是主机 checkout 缺 `build/lib/tiles/Release/tiles-plan-check`;反向本就正确(两条既有梯度用例数字不变、`same=true`),只是走的法律由 `max_axis_stated_t` 变成 `sum_axis_stated_t`+`where_t` | 未合 | 等点名 | 2026-11-23 03:20 |
| lane/panic-declarations | 已合入 (C++ 登记入口可读陈述 0→96;旧入口按 `blank()` 后的文本匹配,而它抹掉字符串字面量,那条路一条也没读到过) | 214/382 可读 | 213/382 | 余 168 个算子仍无可读陈述 | 2026-11-23 04:30 |
| lane/3a-exec | 已合入，阶段 3a 执行侧：`src/stdlib/std/{nvm,fvm,prl,cpp,exec}` 与 `src/tools` 里直连 `GCGraph`/`GraphDraft` 的地方；六笔，每目录一笔 | 六笔 `3c168c424`、`581ab33fe`、`1a9eb9a80`、`4b08496cc`、`940391d32`、`8fca97a5e`；六目录 573 → 497（nvm 28→26、fvm 104→96、prl 149→129、cpp 127→94、exec 97→94、tools 68→58）；497 按类：声明/身份链 345、容器与键 89、载荷 23、空指针初始化 20、注释 9、using 6、`const_cast` 2、lambda 3；按编号枚举的循环一处也没搬（`exec:graph_runtime_support.cpp:874` 那处留在 `GCGraph` 上，理由与 JOIN 无关：下标是计划的位置）；`smoke` 10/10、`smoke+feat/para` 114/114 绿，`feat/trans` 122/119/3 与 `feat/tools` 81/64/6 的红都与基线同红 | 未合 | 等点名；账在 `internal/reviews/graph-view-stage3a-exec-ledger.cn.md` | 2026-11-23 05:35 |

| lane/stack-verify | 把六层叠起来**找问题**（D-1123-23：叠层只用来发现,不用来等） | 19 笔,866116f57 | 栈整批 **36 红**（onnx 55 已清） | **不再等整摞全绿**：每修好一层各自合；现由 oper-identity-write 与 onnx-lowering 在清 21 条 | 2026-11-23 09:05 |
| lane/flip-consumers | 翻转的下游读者跟上新形状 | 30 笔,dd497756,91 红 | 基线 129 → 91，转绿 38、反方向 0 | **红由 onnx-lowering（55 已清）与 oper-identity-write（21 在修）接手**；本层不再单独动手,等它们落完再合 | 2026-11-23 09:05 |
| lane/packed-view-lifetime | 修「一个闭包被当成 cell 塞进 ar int 槽」那族（C-1123-24） | 31 笔，5ae19df7，未推未合 | 旧症状全消失（不再 70.0/1046.0、不再 materializeAggregate、12 次逐字相同）。根因链已交:问题页 2026-11-23-an-operand-identity-is-a-uri-or-a-table-lookup.cn.md，**作者已答**（d56dc52ef，在本栈 `lane/flip-consumers` 上）；**ft2 里那份正文还是未跟踪副本**（`d56dc52ef` 未到 ft2 HEAD），刷新栈合进 ft2 时它会随 `d56dc52ef` 入账，不必再答或手抄 | **卡在:整条栈能否落地** —— 它不是独立的一笔（`C-1123-30`）。**等:栈上红降到能当轮修完**（`oper-identity-write` 已 94 → 86、`onnx-lowering` 已把 55 清零）。`D-1123-23` 允许这样写清而不强合 | 2026-11-23 10:30 |
| lane/facts-b2 | `facts.h` 缺的是**定义**而不是头文件：写注册表、`Store`、`computeAll`、`dump`，附 `camel-facts --self-check`；33 处只数不改 | `camel::facts` 有定义的入口 0 → **25**；`--self-check` **28/28**；`tools.facts` 1/1（本机要 `out/latest/libs` 在 `PATH` 上，与 `op_record` 同一条环境限制，不是这一笔带来的）；33 处仍 33 | 0（未合） | 等作者裁「可自由调度」归谁（`internal/questions/2026-11-23-who-owns-freely-arrangeable.cn.md`），再动 33 处 | 2026-11-23 09:30 |

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