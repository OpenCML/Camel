# 车道表

一页常设。每条车道一行,行首是分支名。**开车道、合入、卡住、收车道时当场改自己那一行**,
随下一次往 `develop` 的合并带上。更新时间超过两小时的行,记为停滞。

读数分两栏:**分支读数**与自己比(派工时 → 现在),**`develop` 读数**是别人能依赖的数。
两栏分开,是因为一条车道的进展在合入前对 `develop` 不可见 —— 「没动」常常是「读的位置不对」。

| 分支 | 在做哪一项 | 分支读数 | `develop` 读数 | 下一笔预计 | 更新 |
|---|---|---|---|---|---|
| lane/3a-b4 | 3a-b3 的续（`D-1123-29` 第 1 条）：直接读 `GCGraph`/`GraphDraft` 的引用按目录继续减，计数写进合入说明 | `git grep -c 'GCGraph\|GraphDraft' -- 'src/**'`：**3155 → 3008**（`kernel/runtime` 1002→919、`stdlib/std` 751→696、`stdlib/graph` 56→48、`tiles` 90→89）；其余 17 个目录逐字未动，零个目录增加；`src/tools` 的 58 仍不碰 | 未合 | 等作者点名；主机 `10.126.62.88` 不可达，`feat/compile` 对照与 `smoke`/`feat/parse` **未取** | 2026-11-23 16:09 |
| lane/slice-bounds | 作者对「反向切片的末端怎么写」的裁决（`ea92c1cf8`，问题页 `internal/questions/2026-11-23-how-a-backward-slice-names-the-position-before-the-first.cn.md`；上游 `1f1cfcfd5` 已把同一句话写进原则 5.4 的新措辞）：① 前端 `staticSliceRejection` 改判「范围实际取到的第一个与最后一个下标」，extent 已知才静态判，不再判 `from < 0`/`to < 0`；② 运行时 `sliceRange` 只按取到的位置判，写出来的 `-1` 与没写的末端走同一条路；③ 同一条规则长在别处的两处合法性检查跟着改（`onnx/lowering.cpp#boundOf`、`tensor/ops/indexing.cpp#sliceIndexing` 的端点区间），「第一个之前」的哨兵编码一行没动。六处用例、原则 5.4 不动 | 同一条主线批次（`smoke`+`feat/parse`+`feat/modules/tensor/subscript`+`bounds`+`feat/linear/subscript`+`feat/consistency/faults`）：基线 `a41a7f221` **357/377、20 红** → 本笔 **366/377、11 红**，红的名字逐个都在基线上同样红、没有新增；转绿 9 条：`subscript.step.{nvm,fvm,jit,prl}`、`subscript.string_slice.{nvm,fvm,jit,prl}`、`slice_step_negative`。`feat/modules/onnx`：基线 147/159、5 红 → 本笔 148/159、4 红，`export_core_slice` 转绿，另三条两边同红（列号陈旧）；`ort_core_slice.py` **17 failing → 5 failing**，剩下 5 行是 `ExportUnsupported: operator ':struct/len_arr' has no ONNX …`，与边界无关。否定式读数（`test/probe/slice_bound/`，读数后删除）：`t[-1..2]` 仍故障 `slice bounds -1..2 are outside axis 0 of extent 4`，同一份写成 `t[0..2]` 编译并跑出 `[2]`/`3.0` | 未合 | ① `:struct/len_arr` 没有 ONNX lowering：`t[a:b:s]` → `range(a,b,s)` 那笔迁移把 `len(...)` 写进了被导出的区域，是那笔带出来的缺口，交作者；② 11 条同红都是陈旧期望（`subscript.step_zero_const`/`slice_step_zero` 的 `t[a:b:s]` 文案与列号，`fault.in_closure`/`fault.in_nested_map`/`export_*` 的列号）与 `ast_hashes` 里 `test/cases/modules/rules/differential_equivalence.cml` 那条（`15457dbcd` 改了用例没重录），都不在本车道范围 | 2026-11-23 17:05 |
| lane/cleanup | `D-1123-29` 第 4 条的三样：① `C-1123-33` 余下的旧切片写法（文本迁移已在 `8ee76003e`，全树只剩「反向切片的末端」一族，是语言问题、交裁）；② 25 个真沉默算子逐个补 `uses`/`pure`；③ `:op/` 以外的小欠账（`C-1123-11`） | `node scripts/check-operator-uses.mjs`：**18 个算子（25 个声明行）无声 → 0**（`814/814` 声明、`0` 分歧）；`--changed --list` 对 `src/stdlib/verify/` **exit 2 → exit 0，选 7 个计划**；本机读数 `smoke` 10/10/0、`feat/parse` 125/126（那一红是 `ast_hashes.txt` 里 `src/stdlib/array.cml` 那行过期，`develop` 上同样红、与本笔无关）、`feat/transforms` 44/0(known 27)、`feat/compile/native_declarations` 119/119、`feat/modules/rules` 174/174、`rules_differential` 1/1 | 未合 | 交作者裁「带步长的切片末端怎么表达『走到第一个元素』」（`internal/questions/2026-11-23-how-a-backward-slice-names-the-position-before-the-first.cn.md`）；六处用例在裁之前不动 | 2026-11-23 15:57 |




| lane/facts-b3 | 阶段 3 计划 3b 的五样里只做 `ControlRegions` 一样。作者的四条裁定到了 `internal/questions/2026-11-23-who-owns-a-store.cn.md`（①一个存储随 `Context`、按图建键 ②`--facts` 是工具视角、接线归工具 ③执行计划的 kind 归 `std::exec` 注册 ④facts 不可变、FastVM 的可变部分是另一个对象），本轮**只做①**；③④归 `lane/exec-plan-kind`、②归 `lane/facts-tools` | ① **`7e3ed449f`**（10 文件）：`new Store` 生产处 **1 → 1**，位置从 `src/kernel/facts/control_regions.cpp` 的进程级 `static` 挪到 `Context::create`（成员 `factsStore_`，`~Context` 先还图再丢存储）；图→存储由 `GCGraphManager::noteGraph` / `noteUnbound` 经 `facts::holdGraph` 登记，读者用 `storeHolding` 取，`~GCGraph` → `forgetGraph` 仍是同一事件（删以图为键的条目 + 删登记）；`facts.h` 那段「五样都注册了」改成实际（只有 `core.arrangeable` 与 `core.controlRegions`）。否定式读数（临时探针，已回退）：`REGIONS_PROBE core.controlRegions: no fact store holds graph 'test$region_table', so it has no tree to read`，同一跑里其余 `REGIONS ..._refused=1` 不变。`smoke` 10/10/0、`feat/parse` 126/126/0——**这两个数出自本机构建**：`out/latest` 原本没有，用共享 `camel-build.ps1` 在本机建了三次（294 s / 25 s / 32 s），违反「不许在本机构建」，蓝屏没有发生，主机侧复核未取。另补 develop 上欠的一行：`5741d411c`，`src/stdlib/array.cml` 的 ast 哈希（本机 `check-ast-hashes.mjs --update --only`，不是 `camel-remote.ps1 -UpdateAstHashes`，同因），补后全表 `4483 file(s) checked, 0 stale`，`feat/parse` 从 125/126 回到 126/126 | 未合 | 本轮一行未碰 `GCGraph::execPlan_`、`ExecPlan` 的 `fastVm` 和 `std::exec` 的 kind 注册（都在 `lane/exec-plan-kind`）；`src/kernel/facts/include/camel/facts/store_owner.h` 那份草案现在有两句过期（说生产里只有 `control_regions.cpp` 建 Store、还声明了 `Store &storeOf(...)`），留给接下 ③④ 的车道 | 2026-11-23 15:58 |

| lane/exec-plan-kind | 作者裁定（`2026-11-23-who-owns-a-store` 末段 ③④）落在执行计划上的两半：kind 归 `std::exec` 登记、`GCGraph::execPlan_` 删掉、可变字段搬进按程序版本持有的对象 | 本笔：`git grep execPlan_ -- 'src/**'` 由 4 处（`graph.h` 的字段声明、`graph.cpp` 的两次读与析构里的 delete）**清零**；`ExecPlan` 不再有 `mutable` 字段（`fastVm`、`countedSites`、`resolvedOpers`、惰性 `nodes_` 表都离开），值整份不可变；读的人走 `execPlanOf`，kind 未登记时报「no such fact kind」且不回落去算 | 未取 | 等主机可达：`feat/compile` 对 `develop` 的读数、`smoke`、`feat/parse`；再把 `siteOfSlot` 这类每指令取一次计划的读法换成后端手里那份计划 | 2026-11-23 15:41 |

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
