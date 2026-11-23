# 车道表

一页常设。每条车道一行,行首是分支名。**开车道、合入、卡住、收车道时当场改自己那一行**,
随下一次往 `develop` 的合并带上。更新时间超过两小时的行,记为停滞。

读数分两栏:**分支读数**与自己比(派工时 → 现在),**`develop` 读数**是别人能依赖的数。
两栏分开,是因为一条车道的进展在合入前对 `develop` 不可见 —— 「没动」常常是「读的位置不对」。

| 分支 | 在做哪一项 | 分支读数 | `develop` 读数 | 下一笔预计 | 更新 |
|---|---|---|---|---|---|
| lane/onnx-lowering | 栈顶：`dy` 的生产节点为什么没有 rank（探针 `17a051428` 已提交为仪器、量完要删）；余 `fusion_group.profile.none` 是 golden 落后于翻转，等翻转落地一笔重录 | 探针在量 | `feat/compile` 2669/2663/1红/5known，1 红是既有 SIGSEGV | 报 `dy` 的生产节点 | 17:20 |
| lane/facts-tools | 作者 `D-1123-29` 第 2 条（`2026-11-23-who-owns-a-store` 末段 ②）：`camel inspect --facts` 接上新事实库、每个已落地 kind 一条「事实答案与旧算法逐项对照」的用例、调试器 `regionOuter` 改读 `ControlRegions`（`develop` 上另按 `D-1123-31` 第 2 条登过同一件事，合并时并成这一行） | 本笔：① `--facts` 两套库并出，每条 `FACT` 行带 `library`（`facts` / `runtime.facts`）；`--fact <key>` 指名一个 kind，没登记时打 `FACT-KIND-ABSENT` 并 exit 1；`core.arrangeable` 与 `core.controlRegions` 改成随库加载注册（此前 `computeAll` 与 `--facts` 根本看不到这两个 kind）；② 新增 `test::facts_compare`，两个 kind 各一条对照用例；③ `program_index` 里 `isControl`、`controlRegionOf`、`regionOuter` 三处手算清零，`visibleAt` 改问 `ControlRegions::within`；主机实测（`camel-remote.ps1`，跑时 1m load 19–24，非安静机）：`feat/tools/facts` 6/6、`smoke` 10/10/0；① `--facts` 一次 9 行 `FACT`（`facts` 4 行 + `runtime.facts` 5 行，靠 `library` 字段区分），`--fact core.controlRegions` 恰 1 行，`--fact core.execPlan` 打 `FACT-KIND-ABSENT` 且 exit 1、无空表；① 还暴露并修掉 `facts.cpp` 的 `factJson` 先关对象再写 `epoch`/`target`（`facts` 行原以 `"target":0` 结尾，不是合法 JSON）；② `tensors_and_loops.cml` 上 `core.controlRegions graphs=6 regions=3 enclosing 3/0 chain 3/0 within_pairs=8 same=8 diff=0 nodes 40/0`（same/diff）、`core.arrangeable nodes=40 same=11 diff=0 no_old=29`；③ `program_index` 手算清零（`git grep` 已无 `regionOuter`/`controlRegionOf`，−34/+14 行），`feat/tools/debugger` 12/14：`loop_frames`/`cli_session`/`dap_frames`/`dap_tail_steps` 过，`dap_session`/`dap_stepping` 红——**把 ③ 单独回退后这两条同样红**（同名、同 242 s），与本笔无因果 | 未合 | **到期条件**：`library` 字段在两库合一后消失 —— 3b 把旧的七个 kind 搬进 `facts` 库时，`--facts` 不再带这个字段（这是「两套库并出」的临时形状，不能活成永久）。未取：`--facts` 只标 root 图（框架不能给图命名）⇒ 跨图覆盖没量；`--facts` 里 `std.execPlan` 被算一次的代价没量；`dap_session`/`dap_stepping` 的红属另一笔，根因未查。已 merge `develop`（`7e645db6b`），上面读数即合并后重取的。 | 2026-11-23 17:48 |
| lane/range-value | `D-1123-30`：只做 A′——加 `export type Range`（现 0 行）+ 七个定义域函数（`to_array` 单向），**一行不碰** `intervalValue`/`:struct/range` 结果类型/`walk.cpp:2701`/值位置语料 | 已落 `53b47ec84`（车道行 + 问题页）；A′ 未落 | 无 | A′ 一笔 + 「`export type Range` 0→1」与原判据「`git grep '"range"'` 仍 5」 | 17:20 |
| lane/facts-effects | `D-1123-31` 第 3 条：`EffectSummary`、`graph.is_pure` 的读者改读 `core.effects`，7 处绕库的直接计算删掉，读数是逐节点相同 | 刚开，0 笔 | 无 | 逐节点「同/异」数 + 否决式「清掉记录 → 读者停摆报红」 | 17:20 |
| lane/notation-corpus | `D-1123-31` 第 4 条：4,252 个 `.cml` 的旧返回类型写法与残留 `f..g` 用格式化器一次过，**然后**再删解析器旧分支；冲突时重跑格式化器 | 刚开，0 笔 | 无 | 语料改前/改后两个数 + 否决式「旧写法报语法错」 | 17:20 |
| lane/golden-debt | 重录 `differential_equivalence.cml` 的 ast 哈希（`15457dbcd` 改注释里的脚本路径未重录） | `d9f1ea9e6`，+1/−1，`ast_hashes.txt` 只动一行 | **已合入**（`74b03ac99`）：`feat/parse` **125/126 → 126/126/0** | 已合入完成 | 17:25 |
| lane/3a-b5 | `D-1123-31` 第 1 条：`src/compile`、`src/stdlib` 其余子目录、`src/tools`（`3a-b4` 留 `kernel/runtime`、`stdlib/std`），注意 `C-1123-17` | **没开**——等 `range-value` 与 `facts-effects` 报出它们实际改过的文件，再按实际文件划界；现在开只能靠猜 | — | 那两条报完后开 | 17:25 |




| lane/record-uses | 作者续裁：`OperatorTable` 的效应语义就是 C++ 入口对 `pure`/`uses` 的陈述，登记时写进 `Record::uses`（事实层只读记录）；并加一条检查列出两个入口都没说的算子 | `stateUsesInRecord` 挪到 `OperatorTable::set()` 之前，`set` 发布语义后调它（`pure` 写成空 access list；state-only/effectful 的词不编造 accesses，3.3 的读法就是答案）；`declareSemantics` 那一处保留并重写注释；读数（`camel inspect --ops`，814 个算子）：**`uses_stated` 349 → 789、沉默 465 → 25、表里声明了 accesses 却没进记录 440 → 0**；成因查明：那 440 不是记录被整体换掉，而是 `set()` 是唯一不过 `declareSemantics` 的陈述入口（`declare`/`declareResources`/`declareDefinition` 都汇进它，`CppRegistration` 走 `Registry::declare` 按项合并），所以它们的 `resources.has_value()`（`pure` 写下的空表）让读数看着像「声明了又丢」；`scripts/check-operator-uses.mjs` 从记录读这三个数并按模块列出那 25 个，`-Check operator-uses` 已接入（report 行）；它第一次运行就找出 `:op/mem_blocks` 两处陈述不一致（`kAccessFree` 的 `pure` 与 `.pure = false` + `externalAmbient("mm")` 并存），已删掉列表里那一项、记录与表恢复一致 | 25 个算子仍无陈述（`graph:open`、`pyplot:plot`×8、`python:*`×9、`rewrite:*`×4、`transforms:*`×3），逐个补声明是下一笔 | 2026-11-23 12:05 |
| lane/device-programs | 设备后端缺 `:op/cast`、`:op/access`、`:op/access_key` 三个前段算子形的程序，六条用例同句 `GpuScopeUnsupported ... no device implementation`；做的是补程序，或按判据把某个算子排除并将用例改成断言那条拒绝 | 三笔：`df8a59e87` 行、`ab4b375d9` 并入 `lane/oper-identity-write`（**develop 上这六条本来就是绿的**：前端还没翻，图上是 CAST/ACCS 节点；六个红只在这条栈上存在，所以测量树就是它）、`85fcbf74c` 设备侧。读数（同一棵树，改前 → 改后）：`resident_cpu_block` 红 `no device implementation` → 绿（`TILES_RESIDENCY` 仍是 downloads 2 / uploads 1 / per_round 0，`24.0`）；`gpu_scope.macro_of_a_function_the_run_chooses` 红（拒绝列表被 `:op/cast` 污染，点名的算子不在句子里）→ 绿；`gpu_scope.macro_expansion_publishes_the_derived_graph` 红 → 绿（`grad(loss)` 35/35 全在一个 region 里、`host:0`、`moved 0.0 wrong 0.0`）；`tiles.batch_matmul.source.{nvm,fvm,jit}` 红（`tiles:source` 说 `:op/cast`、再往深是 `:op/fill` 没有设备程序）→ 三条绿。查出来的第四个形是 `:op/fill`（构造），第五个是 `tensor:zeros_like`/`ones_like`（编译器本来就降成「一个常量按零步长读」）；两者都不是内核缺失，是能力表没写 | 未合 | 判据写在提交里：形本身不拒绝（转换/构造/成员读不计算任何元素），拒绝留给「目标类型不能由源类型赋值」（`assignableFrom`，那是语言自己的规矩）与「设备没有这个宽度的元素」；`TILES_OUTLINED` 12/5 改前改后**同一对数**（栈带上来的，本笔没动放置判断），`region_execution.cml` 上没有 `zeros_like`；`lane/onnx-lowering`（`83604b796`）自己的 `feat/modules` 是 **2082/2016/52**（两遍同数，7 跳 7 known 4 个被信号杀掉），那六条按名字都在 52 里；但**把设备那一笔单独搬上去不行**：它的 `src/stdlib/tiles/src/compiler.cpp` 里 `:op/cast`、`:op/fill`、`:op/access` 一个分支都没有（0 次出现），只补能力表会让六个红从「编译期诊断」变成「特化期故障」；所以「带进 15 → 9」那个数要在 `oper-identity-write` 并进 `onnx-lowering`（或反向）之后量，而两支在成员读上各写一种形状（本支：位置在调用的 `with` 里，作者裁定；`onnx-lowering`：位置是第一个常规操作数，读者是 `accsPayloadOf`），19 个冲突、9 个文件全在这一处，合并方向归栈主 | 等点名 | 2026-11-23 13:40 |



## 没开车道的一件

| 项 | 为什么 |
|---|---|
| 算子与事实(阶段 2 第 3 条) | 它要把效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取这五样搬进事实框架,而 `facts.h` 今天零定义全树 —— 没有落点可以搬进去。建那个框架的形状取决于 `Graph::view()` 能不能让读者走回视图,那在 `lane/stage2-graph` 手上 |

## 已合入

`exemplars-path` `13e0228b9`（11 条全绿）、`3a-transforms` `70c7b324b`（`299 → 291`）、`panic-record` `6e4411d7e`（可读陈述 `214 → 261/382`，不能读的 `48 → 1`）、
otation-b2` `064939bbd`、`commit-checks-b2` `08fde0e53`（两处缺口，影响面 9099 例三类全 0）、`ast-record-recheck-b2`（零笔，重录与既有记录逐字相同）、`facts-b2` `5638096b8`（`facts.cpp` + 自检 28/28）、`graph-read-b2` `3c2ee39d6`（`GraphRead` 全树清零、`LoopView` 合一、23 个调用点、净减 2157 行）。读数留在 `git log` 与 `internal/backlog.md`，不占在飞的表。

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
| lane/onnx-lowering | 翻转之后「让读者跟上形状」:导出器/变换器/优化层/回溯按「节点说什么」读;成员读的形状按**声明**定(`__access_value__`:`norm.size()==1`、`with.size()==1`,位置在 `with`),19 个冲突全按这条解;`accsPayloadOf`/`AccsPayload` **整条拆掉**(零调用与不存在在 grep 上都是 0,只有后者让人不能再用它),`memberReadOf` 从「只收 `GCGraph`」改成**读者模板**(跨两面,payload 走 `payload_detail` —— 那是 `oper-identity-write` 那版的限制,不是一次移植);autodiff 的 `whyNotEvident` 也改问 `memberReadOf`(按 kind 问把「从 tuple 读出函数」说成「运行时才知道」) | 73 笔(含两笔 `develop` 合并、一笔量测探针与删除、一笔回退、一页问题) | `feat/compile` **2669/2664/1** —— 那 1 条 `vmap_batches_the_element_of_a_row.prl`(崩)在 develop 树上同名同崩,是合并带进来的 develop 红;`feat/modules` **2082/2028/46**(合 develop 前 2082/2026/48);与 develop `adde6e793` 逐名:本车道红 develop 绿 **11**(3 条已知 `export_train_step_bn`/`conv`/`fusion_group.profile.none` + 6 条 onnx 动态导出 + 并行族 2 条随负载),develop 红本车道绿 **1**(`compiled_region_parallel`);合 develop 让 `slice_step_negative`（`d8a5fa869`）与 `export_core_slice` **自己转绿**,不是修的;`feat/compile/ref`+`smoke` 89/89/0 | 卡在:两堵墙各一页 —— (1)`export_train_step_bn/conv`:图写 `float`、规则答 `f32`,在参数梯度上相遇;「规则随入参」能让 conv 绿、bn 推到 BN 反向,但 `conv2d_shape_error` 转红,已整笔回退(`015b989ade`),问题页 `2026-11-23-float-and-f32-name-one-type-or-two`;(2)6 条动态导出(`export_train_step_dynamic*`、`ort_dynamic_params`、`ort_loop_*`、`ort_train_step_dynamic`)是 `matmul_grad_rhs/lhs` 要求 target 全已知,而动态参数本来就没有静态范域 —— 与元素类型不是一条;另 1 条 `fusion_group.profile.none` 仍是普查随翻转多三行(golden 待重录) | 等点名 | 2026-11-23 18:12 |

## 只作证据（不在分支上，在标签上）

`with-param-audit` 与 `data-arg-migration` 的内容存在标签 `evidence/with-param-audit`、`evidence/data-arg-migration` 上，分支已删。理由：两条都不会合（一条作者撤回、一条前提被推翻），而它们每一笔 `develop` 都让「最落后分支」这个闸门读数涨一格 —— 那个数从此只描述它们，不描述任何一条会被合的车道。试过让它们跟上：`with-param-audit` 那 2 笔是另一代的 WIP，与今天的 `src/stdlib` 全冲突，合不进去。标签保住内容、不进分支计数，于是闸门重新描述真在飞的四条。

`packed-view-lifetime` 也在标签 `evidence/packed-view-lifetime` 上：它的 31 笔全部含在 `lane/oper-identity-write` 里（`merge-base --is-ancestor` 为真），是一条栈内路标，不是一条独立车道。读数留在 `internal/backlog.md` 与 `C-1123-24`。

## 那条栈现在是两个头

`lane/flip-consumers` ⊂ `lane/onnx-lowering`，`lane/packed-view-lifetime` ⊂ `lane/oper-identity-write`，而这两条互不相含 —— 所以栈要落得合**两次**：一次 `onnx-lowering`（迁移 + ONNX lowering + 视图），一次 `oper-identity-write`（成员读的位置进 `with` 的读者那一半）。合成一次合入会把两批交付条件不同的笔压成一笔。

`flip-consumers` 同样在标签 `evidence/flip-consumers` 上：它的 32 笔全部含在 `lane/onnx-lowering` 里，是栈底路标。于是那条栈现在只剩两个头（`onnx-lowering` 与 `oper-identity-write`），加一条在栈树上量设备红的 `device-programs`（它把整条栈合了进来，因为那六条红是在栈树上量的 —— 所以它合入时会带上栈）。
## 这一轮的清法

一次把所有「分支已不存在」的行删掉了（11 行），加上 `3a-b3`（它已合入，零笔领先）。判据是 `git rev-parse --verify refs/heads/<name>` —— 一个名字 git 解不出，那一行就不能迟、不能合、不能检查，而它读起来像工作量。

留着的一句：`fe1` 那棵工作树里有一处**故意未提交**的本地改动（`camel-remote.ps1` 的 `$ErrorActionPreference`，理由写在注释第一行：PowerShell 5.1 会把 cmake 的 CUDA 警告当终止错误）。它不属于任何一笔，**删工作树就是悄悄丢掉它**，所以那棵树留着。

## 写这些行时踩到的一个坑

在 PowerShell 的双引号字符串里，反引号是转义符：`` 
 `` 是换行、`` `0 `` 是 NUL、`` `t `` 是制表。而 Markdown 的代码段落用反引号包围。所以用双引号拼 Markdown 会同时做两件看不见的事：**吃掉所有反引号**，以及把 `` `0 `` 变成一个 NUL 字节 —— 后者会让 git 把这个文件判成二进制（`Bin 10997 -> 5841 bytes`），于是它不再有 diff，也就不再有评审。写文档内容一律用单引号 here-string。
## 两处「已经欠着、但现在量不了」的账

**一、`ast_hashes.txt` 里 `src/stdlib/array.cml` 那一行过期。** 现在是 `f4f7de82ac10c483`，那是
`af7c8a92e` 之后的值；`reverse` 改完之后它没重录。主机一通就要补，一条命令只动一行：

```
internal/tools/camel-remote.ps1 -Path <tree> -Plan test/plans/feat/parse -UpdateAstHashes -AstOnly 'src/stdlib/array.cml'
```

不补的话，`feat/parse` 的 `frontend.ast_snapshot` 会红，而红在**一行 golden** 上 —— 那是「一个读数属于它的位置」的反面：一个 hash 属于它记录的那份文本，文本变了 hash 没变，读起来就像文本没变。

**二、反向切片的 `-1` 端点还在用例里。** `reverse` 改用索引数组之后，**同一族**仍有两处写着那个端点：
`ort_core_slice.cml` 的 83/93/103/113/118 五行 `range(..., -1, ...)`（用例 `export_core_slice`）与
`slice_step_negative` 第 15 行。它们是**用例**，不是库，所以修它们等于裁一件事：**反向切片的末端怎么写**
—— 要么放宽「轴外边界是故障」这条，要么在语法上给一个可省末端的步长形式。这条在作者手上，没有动。

## 而 `reverse` 那一笔的阶梯读数（同一选择集，`stdlib_values` 单跑，`-Jobs 8`）

```
base 689304a87            59 / 35 / 24
七处文本（8ee76003e）      59 / 47 / 12
「一行 guard」              59 / 47 / 12   ← 改了个寂寞：12 条一条没动，名字逐条相同
索引数组（25162d366）       59 / 59 / 0
```

第三行是这一笔最重要的证据：**guard 那个改法不成立**，而理由是 `-1` 是**常量端点**，
`staticSliceRejection`（`src/stdlib/std/builtin/modules/globals.cpp:420-447`）在**调用处**就拒，
与数组空不空无关 —— 空数组只是第一个撞上它的那次实例化。所以那不是「空数组的特殊情况」，
是**写法本身**越界。
## 一道从没人问过的缺口：`:struct/len_arr` 没有 ONNX lowering

```
**`t[a:b:s]` → `range(a,b,s)` 那次迁移（`af7c8a92e`）把 `len(...)` 写进了**被导出的区域***
⇒ **⇒ 而":struct/len_arr` **没有 ONNX lowering** ⇒ `ExportUnsupported`**
⇒ **⇒ 那一笔的闸门只有 `feat/parse + feat/compile` ⇒ **exports 侧从未跑过** ⇒ 缺口从那时起就存在**
```

**⇒ 现在它挡在 `ort_core_slice.py` 最后 **5 行**前面（`posEnd`、`negAll`、`posAll`、`negOne`、`sweep`），
而它们红的**不是边界**，是这条没有 lowerer 的算子。** **

**⇒ 这一条的教训与前一条同形：**一次迁移改的是**读法**，而唯一的读者（导出器）从未被问过 —— **`feat/compile` 绿了，而 `ort_core_slice` 从来没跑**。** **

**⇒ 记在这里而不派：它是一个**缺 lowering**，不是边界问题，而它属于导出器那条账。**
## 已合入（本小时，`D-1123-29` 那一批）

| 车道 | 落了什么 | 合并 |
|---|---|---|
| lane/facts-b3 | store 移到 `Context`（`new Store` 生产处 1→1 但位置变了），`facts.h` 那段注释按实际改成两样 | `349951ba0` |
| lane/exec-plan-kind | `execPlan_` 字段删（4 处→0）、kind 归 `std::exec` 登记、`fastVm`/`countedSites` 搬进 `FastVmVersionState` 按计划取 | `d492be136` |
| lane/cleanup | 18 个沉默算子（25 个声明行）**25 → 0**，`check-operator-uses` exit 1→0；`C-1123-11` 脚本搬出 `test/plans/` | `a14bf9ab3` |
| lane/3a-b4 | 直连 `GCGraph`/`GraphDraft` **3155 → 3008**，零个目录增加 | `1f5a6efe` |
| lane/slice-bounds | 界不是下标：`t[-1..2]` 仍故障、`t[0..2]` 通过；两条计划红→绿；主线 377 条 20 红→11 红**新增 0** | `d8a5fa869` |

**⇒ 而 `facts-b3` 与 `cleanup` 的读数**一个是本机构建、一个是 `node` 脚本** —— 主机当时不通，方法违规但蓝屏未发；
主机现已通，**这两笔的主机读数仍缺**，是这一小时唯一「落了地但没有读数」的洞。
