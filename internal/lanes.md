# 车道表

一页常设。每条车道一行,行首是分支名。**开车道、合入、卡住、收车道时当场改自己那一行**,
随下一次往 `develop` 的合并带上。更新时间超过两小时的行,记为停滞。

读数分两栏:**分支读数**与自己比(派工时 → 现在),**`develop` 读数**是别人能依赖的数。
两栏分开,是因为一条车道的进展在合入前对 `develop` 不可见 —— 「没动」常常是「读的位置不对」。

| 分支 | 在做哪一项 | 分支读数 | `develop` 读数 | 下一笔预计 | 更新 |
|---|---|---|---|---|---|
| lane/fact-slots-grow | `D-1123-42` 返工槽表:不定死 32 格。`GCGraph` 只留一个原子指针指向 `{容量, 槽[]}`,首次有事实写入才分配,登记新种类时分配更大的表、复制、原子换指针,旧表留到图销毁时释放,删掉 `kFactSlotCount` 与「超出 32 就走存储」 | 建在 `975918aa4`;未报读数 | 未合入 | 扩容后旧指针仍可读的那条交错测 | 21:00 |
| lane/nn-kernels | `D-1123-38` 三件:内核按元素类型模板化(f64 能算)、声明改 `::<T>` 并真正编译检查、删掉 C++ 推导规则;加 f64 卷积与 BN 训练一步的用例 | 6 ahead / 2 behind;已合过一次 `develop` | 未合入 | 三件各自的判据与读数 | 21:00 |
| lane/range-rn | `D-1123-41` 2(a) 名字解析:同层 `#op` 声明与同名有体声明合成一个重载集;prelude 导出的类型免导入 | `len([1,2,3])`=3、`"xy"->len()`=2、`r->len()`=3、`r->map<dbl>`=[0,2,4]、`[1,2]->map<dbl>`=[2,4];`prelude::len: (r: Range) => int` 那条红字消失;`feat/parse` 因 prelude 加了四个函数体报 125/126,已准本笔带 `-AstOnly src/stdlib/prelude.cml` 重录 | 未合入 | 重录后 `feat/parse` 126/126/0 | 21:00 |
| lane/range-ri | `D-1123-41` 2(b) `a..b` 产 `Range`、`for` 走一个 `__iter__`、转置反向循环、C++ 后端、`cml` 打印 | 建在 `6b844be42`;未报读数 | 未合入 | 接收 `range-rx` 交来的三处(range 名字判断、`sliceIndices`、静态 for 的 `callee->name()`) | 21:00 |
| lane/range-rx（已合） | `D-1123-41` 2(c) 与 `D-1123-35`:切片规则收成一个函数、删掉按 `range` 名字的判断 | `feat/compile` 2672/2666/1红/5known/1信号、`feat/parse` 125/126、`smoke` 10/10/0、四计划合一 2920/2894/21/5/1 跑前跑后逐项相同 | `74fdda145` | 下标逐维按类型分派与删除解析器切片路径,**转 `range-ri`** —— `a..b` 仍落成 `int[]`,今天删了 `t[a..b]` 会退化成 `take` | 21:01 |
## 已合入（本小时，18:00 之后）

| 分支 | 合入 | 读数 | 备注 |
|---|---|---|---|
| lane/facts-tools | `adde6e793` | `feat/tools/facts` 6/6、`smoke` 10/10/0、`feat/tools/debugger` 12/14 | `--facts` 打两库、每条 `FACT` 行带 `library`；逐项对照 `enclosing 3/0`、`chain 3/0`、`within 8/8`、`nodes 40/0`；顺带修掉 `factJson` 先关对象再写 `epoch`/`target`（原以 `"target":0` 结尾，不是合法 JSON）。**到期条件**：`library` 字段在两库合一后消失（3b 把旧的七个 kind 搬进 `facts` 库时）。`dap_session`/`dap_stepping` 两条红经单独回退证明与本笔无因果 |
| lane/golden-debt | `74b03ac99` | `feat/parse` **125/126 → 126/126/0** | 重录 `differential_equivalence.cml` 的 ast 哈希（`15457dbcd` 改注释里的脚本路径未重录） |
| lane/3a-b5 | `74d9cf433` | `git grep -c 'GCGraph\|GraphDraft' -- 'src/compile' 'src/stdlib' 'src/tools'` **1576 → 1535**（compile 34→33、stdlib 1483→1443、tools 59→59）；`smoke` 10/10/0、`feat/parse` 126/126/0、`feat/compile` 2669/2663/1红/5known | 5 处 `C-1123-17`（视图里 JOIN 与墓碑不占位）全不改并报出；`src/tools` 实测 59 不是 58（`facts-tools` 合并给 `camel-cli/main.cpp:555` 加了一行 `GCGraph *root`），**派工里的那个数在写它的时候是对的** |
| lane/range-value | `28e3cb377` | `export type Range` **0→1**、三个函数；`smoke` 10/10/0、`stream` 150/150/0、`feat/parse` 126/126/0、`feat/compile` 2672/2666/1红/5known | 两条否决式：`let r: Range = 1..5` 报 `DeclaredTypeMismatch` 0x03000110；`map(1..5, f)` 仍走**算子组**。乙（五条 `:range/…` 算子 + C++ 内核）留作下一笔 |
| lane/onnx-lowering（含 `device-programs`、`oper-identity-write` 一摞） | `ce7f79bd9` | 61 文件 +2798/−1016；`feat/modules` **2026/48 → 2082/2032/40**；与 `develop` 逐名 13 红 → **5**（3 已知 + 并行族 2 条随负载）；`feat/compile` 2669/2663/1红/5known | `dy` 无 rank 的根在 `tensor:shape` 的 `inferenceTypeOf` 答开口 `int[]`（丢掉「列表有 2 项」，而 `sum_to` 的 rank 取自该长度），同族另补两处桥；「动态参数没有静态范域」那个前提**被读代码推翻**（`lowerSumTo` 早有两条路），修的是本车道自相矛盾，**4 条真绿**、2 条重钉到 ONNX Loop 的固定 dtype/shape 墙；两堵墙现在只剩 `float`/`f32` 一堵，在那页问题里等作者 |
| lane/facts-effects | `a82121b23` | 8 处 → **1**（私头可达，读者自算即编译错）；逐节点 **2,467,119 + 341,975 读 / 0 异**、图 43 / 区 68 / 算符 14 全 0 异；`smoke` 10/10/0、`feat/parse` 126/126/0、`feat/compile` 2669/2663/1红/5known | 四个 kind（图/节点/区域/算符子句）；`valid=` 多出**正好是 OPER 节点数**（main 11、layer 1、total 2、count 2、twice 1）—— 因为边界检查现在会读因而会存一条 `core.nodeEffects`。探针已删（`src/` 下 0 行） |
| lane/notation-corpus | `fb8399904f` | 语料 **4,254 → 0**、解析器旧分支已删（旧写法报 `Unexpected ':'; expected a declaration or the end of the file`，`:` 在第 15 列）；`feat/compile` **2808/2802/1红/5known**（那 1 红 = 既有 SIGSEGV）、`feat/parse` 126/126/0、`smoke` 10/10/0；131 条判完 **甲 130 / 乙 1**，乙那条是格式化器丢 `StrayPositional`（`f(a: 1, 2)` → `f(a: 1)`），已修 | **`camel-format` 不改语法，只改排版** ⇒ 「冲突时重跑格式化器」**不足以**恢复语料，必须**先**重跑 `): T → => T` 那支改写，再格式化 —— 这次 5 个文件就是照这条补回来的（`e7ade53e72`）。另：21 个 harness 仍写旧写法（生成程序 10、LSP 5、探针 1、被当源码读 1、只写不解析 1、仅注释 3），下一跑会红 |
| lane/nn-element-type | `72f4e764c` | nn 一族注册规则的元素类型改为**随入参**（`conv2dInfer`/`poolInfer` 取 x；`shapedLike` 取被照形的入参，含 conv 三个梯度、pool 梯度、`batch_norm`、`embedding` 表梯度；`batchNormTrainInfer` 的 y 取 x、两个 running stat 各取自己；`batchNormTrainGradInfer` 的 dx 取 x、dgamma/dbeta 取 gamma；两个 `softmax_cross_entropy*_grad` 取 logits；未知退 f32），**内核未动**；`export_train_step_conv` 红→绿、`conv2d_shape_error` 绿（声明改问几何，`f32[...]`→`f64[...]`）。`[SUMMARY]` **2808/2802/1红/5known/1信号**（那 1 红 = 既有 `vmap_batches_the_element_of_a_row.prl` SIGSEGV） | 作者裁定 `bbf78d63b1`。`export_train_step_bn` **仍红**，原样 `the two tensor operands have different element types ('float' and 'f32')` —— **BN 反向那一堵**，即反向发出 f32 而答案要 f64；本笔不动内核，故不动它。`pass_export_equals_export_model` 的到期条件已从「当卷积随入参时」改为「当 BN 反向也随入参时」——**一条到期条件的一半成立时，剩下那一半才是真正的条件**。`feat/modules` base **2082/2028/44/7**（不是派工里的 40，差额是语料余波），逐名唯一差就是 `conv` 那一条 |



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
| lane/onnx-lowering | 翻转之后「让读者跟上形状」:导出器/变换器/优化层/回溯按「节点说什么」读;成员读的形状按**声明**定(`__access_value__`:`norm.size()==1`、`with.size()==1`,位置在 `with`),19 个冲突全按这条解;`accsPayloadOf`/`AccsPayload` **整条拆掉**(零调用与不存在在 grep 上都是 0,只有后者让人不能再用它),`memberReadOf` 从「只收 `GCGraph`」改成**读者模板**(跨两面,payload 走 `payload_detail` —— 那是 `oper-identity-write` 那版的限制,不是一次移植);autodiff 的 `whyNotEvident` 也改问 `memberReadOf`(按 kind 问把「从 tuple 读出函数」说成「运行时才知道」) | 76 笔(含两笔 `develop` 合并、一笔量测探针与删除、一笔回退、一页问题) | `feat/compile` **2669/2664/1** —— 那 1 条 `vmap_batches_the_element_of_a_row.prl`(崩)在 develop 树上同名同崩,是合并带进来的 develop 红;`feat/modules` **2082/2032/40**(合 develop 后 2028/46;onnx 计划 159 条由 **145/14** 到 **149/7 + 3 标记**);与 develop `adde6e793` 逐名:本车道红 develop 绿 **5**(3 条已知 `export_train_step_bn`/`conv`/`fusion_group.profile.none` + 并行族 2 条随负载),develop 红本车道绿 **1**(`compiled_region_parallel`);合 develop 让 `slice_step_negative` 与 `export_core_slice` 自己转绿,4 条动态导出由本轮修好后转绿;`feat/compile/ref`+`smoke` 89/89/0 | 卡在:两堵墙 —— (1)`export_train_step_bn/conv` 与 `pass_export_equals_export_model` 的内联合:图写 `float`、规则答 `f32`,在参数梯度上相遇;「规则随入参」能让 conv 绿、bn 推到 BN 反向,但 `conv2d_shape_error` 转红,已回退(`015b989ade`),问题页 `2026-11-23-float-and-f32-name-one-type-or-two`;(2)2 条 loop 用例已不在范域上 —— 它们撞的是 `a loop can only carry values of a fixed dtype and shape`(ONNX Loop 的固定形状要求),标记已按新句子改;另 1 条 `fusion_group.profile.none` 仍是普查随翻转多三行(golden 待重录) | 等点名 | 2026-11-23 18:35 |

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
