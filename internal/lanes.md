# 车道表

一页常设。每条车道一行,行首是分支名。**开车道、合入、卡住、收车道时当场改自己那一行**,
随下一次往 `develop` 的合并带上。更新时间超过两小时的行,记为停滞。

读数分两栏:**分支读数**与自己比(派工时 → 现在),**`develop` 读数**是别人能依赖的数。
两栏分开,是因为一条车道的进展在合入前对 `develop` 不可见 —— 「没动」常常是「读的位置不对」。

| 分支 | 在做哪一项 | 分支读数 | `develop` 读数 | 下一笔预计 | 更新 |
|---|---|---|---|---|---|




| lane/facts-b3 | 阶段 3 计划 3b 的余下五样：效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取。**只做了一样**：`ControlRegions`；其余四样各有一份「改前」读数与不挪的理由 | 首笔 **`38d24c183`**：`ControlRegions` 构造 13 处不变、**计算 13 → 1**（事实层 kind `core.controlRegions`），emitter 的按图缓存 `regions_` 删除，`ControlRegions` 只剩薄读；`smoke` 10/10/0、`feat/parse` 126/126/0、`feat/compile` **2669/2664/0（known 5）是本轮唯一站得住的读数**（选择集内、可复现）；整层 fast 报 `9844/9644/135` skip 24，**这 135 是孤立数、不作读数引用**（基线那一跑 15:07 起主机超时，没送出去；按类看在本笔不碰的 docs/lsp/grammar/capacity/记号解析，另 2 个 gpu 红是主机 checkout 缺 `build/lib/tiles/Release/*`，与 `plan_validation` 同因）；事实层无回落：全树只有 `facts:control_regions.cpp#buildControlRegionTree` 会建树，`ControlRegions` 造不出来，拿不到树即抛 | 未合 | 第二笔（本行同笔）：形状先于第二样——`internal/questions/2026-11-23-who-owns-a-store.cn.md` 立四个问题（Store 生命周期 / `--facts` 接不接线 / 模块不在时报什么红 / 可变值与不可变表要不要拆 kind），头文件草案 `src/kernel/facts/include/camel/facts/store_owner.h` 只有声明、无 `.cpp`、无人 include，交作者裁；四件不裁，3b 其余四样一件也挪不动 | 2026-11-23 15:35 |

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
| lane/onnx-lowering | 55 条 ONNX 无 lowering 清零（零条假映射）+ shape 20 条 + residue 10 处；现做移植 | 39 笔，`ex2`（临时借 `tmp/base-measure` 做旧树对照） | `feat/compile` 2669/2662/2（2 条既存） | **卡在:6 笔在给已删除的 `GraphRead` 面重塑读法。**等:把 24 个调用点移植到 `GraphView` 单面 | 2026-11-23 12:00 |

## 只作证据（不在分支上，在标签上）

`with-param-audit` 与 `data-arg-migration` 的内容存在标签 `evidence/with-param-audit`、`evidence/data-arg-migration` 上，分支已删。理由：两条都不会合（一条作者撤回、一条前提被推翻），而它们每一笔 `develop` 都让「最落后分支」这个闸门读数涨一格 —— 那个数从此只描述它们，不描述任何一条会被合的车道。试过让它们跟上：`with-param-audit` 那 2 笔是另一代的 WIP，与今天的 `src/stdlib` 全冲突，合不进去。标签保住内容、不进分支计数，于是闸门重新描述真在飞的四条。

`packed-view-lifetime` 也在标签 `evidence/packed-view-lifetime` 上：它的 31 笔全部含在 `lane/oper-identity-write` 里（`merge-base --is-ancestor` 为真），是一条栈内路标，不是一条独立车道。读数留在 `internal/backlog.md` 与 `C-1123-24`。

## 那条栈现在是两个头

`lane/flip-consumers` ⊂ `lane/onnx-lowering`，`lane/packed-view-lifetime` ⊂ `lane/oper-identity-write`，而这两条互不相含 —— 所以栈要落得合**两次**：一次 `onnx-lowering`（迁移 + ONNX lowering + 视图），一次 `oper-identity-write`（成员读的位置进 `with` 的读者那一半）。合成一次合入会把两批交付条件不同的笔压成一笔。

`flip-consumers` 同样在标签 `evidence/flip-consumers` 上：它的 32 笔全部含在 `lane/onnx-lowering` 里，是栈底路标。于是那条栈现在只剩两个头（`onnx-lowering` 与 `oper-identity-write`），加一条在栈树上量设备红的 `device-programs`（它把整条栈合了进来，因为那六条红是在栈树上量的 —— 所以它合入时会带上栈）。
| lane/oper-identity-write | 成员读的身份一刀切：只写 `body.oper`，URI 不入载荷；成员读位置成为编译期参数，内核从 `with` 读 | 40 笔，未推未合 | `feat/compile` 2669/2662/2；`ref` 已修 | **卡在:设备侧要三个算子形的程序。**等:`lane/device-programs`（六条已全绿） |
| lane/device-programs | 给 `:op/cast`/`:op/access`/`:op/access_key`/`:op/fill` 补设备程序 | 43 笔（含整条栈，作为测量树），未推未合 | 六条设备红**全绿**，没有一条靠放宽检查 | **下一笔:`merge lane/onnx-lowering` 后重量 `feat/modules`，看「带进 15」降到 9** |
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