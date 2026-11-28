# 405 判据台账：块体里读草稿（`edit d = f { … }`）

一句话：**三条判据，三版实现，没有一版全达。**放宽校验（B）让规则全绿但仍读不到；把 `pure` 改义（C）几乎不伤别处却仍读不到，因为它同时是"签名发布"的开关。结构原因见 §4：**`pure` 是一位，被四类读者用，而它们要两个不同的答案。**

> 行号基准：本页所有行号按 `pools-red` 尖端 `04d2353e31`（= `lane-editview-a` 落在 `hdr-channel` 的五笔：`63845ee572`、`45f096735c`、`af1a9d018a`、`1530aa420b`、`e29887b576`）。B、C 两版的读数是**我自己未提交的实验**（树上没有那五笔），见 §5 的说明。

---

## 0 判据（Lead 口径）

| 编号 | 判据 | 怎么算达 |
|---|---|---|
| ① | 块体三例读到值 | `graphs(d)`/`nodes(d)`、`erase`、`replace` 在块体里读到**值**（stdout 的具体答案），不是"没崩" |
| ② | 18 条 `rules.*` 保持绿 | `feat/modules` 的 `rules.extract_*` / `rules.quant_int8_*` / `rules.when_*` 族全绿 |
| ③ | 三层 A/B 甲 ∅ | 甲 = 本次红而两把尺子（本车道父尖端 46 名、75 表 141 名）都不红 |

红名词汇：**甲** 新红；**乙** 基线红本次绿；**丙** 两边都红。

## 1 两把尺子

- **父尖端基线跑**（`feat/compile` + `feat/trans` + `feat/modules`，`-Tier fast -Jobs 6`）：`[TREE] clean`，`[SUMMARY] total: 4926, pass: 4869, fail: 46, skip: 4, known failures: 7`（日志 `D:\Temp\t405-layer-head.log`）。46 个红名 = **29 个 modules** + 17 个 compile/trans。
- **75 表**：`internal/reports/2026-11-28-promotion-75-reds.tsv`，两列（plan、测试名），141 名。modules 层既有红（`rules.repeat/until/alternate`、`outline_delivers_*`、`op_stats.table.*`、`slice_runtime`、`types_and_node_kind`、`constant_folding_*`、`si_as_fails*`、`rename.*`、`export_*`、`as_refusal.*`、`npy_generic_wrapper_mismatch`、`fusion_group.profile.none`、`slice_step_zero`、`pass_export_refusal_located.request_in_arm_json`、`node_kinds_match_the_graph`）在两把尺子上都在。
- 背景：`rules.repeat/until/alternate` 那 5 条今天红是 **`schedulers` 库编不过**（`LoopSequenceNotIterable … '0 .. rounds' has type 'Range'`，`src/stdlib/rules.cml:551/564/576`，修点 `src/compile/src/gir/builder_control.cpp:3287-3303`），与本判据无关，一律记丙。

## 2 三版并列

### A 冲击版：把 39 个查询算子声明成 impure（`pure=false`）并声明对草稿的读
- **改了哪几处**：2 处。`src/stdlib/graph/module.cpp`（39 个算子 `{"<name>", false}` + `table.declareResources("graph", {…})` 39 条 `operandAccess(Handle, 0, Read, "graph::draft")`）；校验不必动（校验只拒"纯 + 有访问"）。
- **读数**：`BUILD_EXIT=0`；`[provenance] tip (dirty: 1 files) | binary built 2026-11-28T12:42:15+08:00 | tier fast | jobs 6`；`[SUMMARY] total: 4926, pass: 4851, fail: 64, skip: 4, known failures: 7`（日志 `D:\Temp\t405A-layer.log`）。
- **甲 18 / 乙 0 / 丙 46**。甲全部是 `rules.extract_keeps_no_unreachable_pure_node`、`rules.node_of_reaches_a_const_parameter(+.fvm/.nvm/.prl)`、`rules.quant_int8_*`(4)、`rules.when_*`(6)、`rules.when_passes_the_check`。
- **判据① 不达**（该版未跑块体三例；"声明了读也不成"由 B、C 的原文证明）。**判据② 不达**（18 条 rules 恰是甲）。**判据③ 不达**。

### B 窄口径：保留 `pure`，只放宽校验并声明读
- **改了哪几处**：2 处。`src/kernel/core/operator_table.cpp`（+`<algorithm>`；`onlyHandleReads` = `accesses` 非空且全为 `AccessMode::Read` 且 `ResourceClass::Handle`，仍拒 `allocates`/`externalEffect`/`functionOperand`）；`src/stdlib/graph/module.cpp`（39 条 `operandAccess(Handle, 0, Read, kDraft)`，纯度不动）。
- **读数**：`BUILD_EXIT=0`；`[provenance] tip (dirty: 5 files) | binary built 2026-11-28T12:49:03+08:00 | tree …pools-red | tier fast | jobs 6`；`[SUMMARY] total: 4928, pass: 4869, fail: 48, skip: 4, known failures: 7`（日志 `D:\Temp\t405A-narrow2.log`）。4928 = 基线 4926 + 2 条自建探针。
- **甲 2（`probe.block_reads_draft`、`probe.block_shape`，皆自建探针，不计入真用例）/ 乙 0 / 丙 46**。
- **判据① 不达**，原文：
  ```
  [stdout] fault={} { kind: InvalidArgument, message: Invalid argument of 'graph.nodes': the draft was committed or aborted already., location: block_shape.cml:11:18 }
  [stdout] answer= -1
  ```
- **判据② 达**（18 条全绿）。**判据③ 达**（真用例甲 ∅）。
- **结论**：放宽校验是**必要条件而非充分条件** —— 顺序边那一侧把它挡在门外：`nodeTakesAnOrder`（`src/compile/src/gir/builder_helpers.h:749`）对 OPER 节点的答案是 `!operatorArrangeable(id).all()`（同文件 `:785`），而 `all()` 要求四项全真，纯且不故障的节点恒为真 ⇒ 边不建（`src/compile/src/gir/builder_resources.cpp:1343`）。

### C 模型级：把 `pure` 的定义从"子句为空"改成"子句不写"，并给 `Arrangeable` 加第五项
- **改了哪几处**：5 处。① `src/kernel/ops/include/camel/ops/operator_record.h:482`（`pure()`：`uses` 已声明且 accesses 全非写、无 `allocates`/`externalEffect`/`functionOperand`）；② `src/kernel/facts/include/camel/facts/arrangeable.h`（第五项 `noAccesses`，并入 `all()`）；③ `src/kernel/facts/facts.cpp` `termsOfOperator`（`terms.noAccesses = record->uses.stated() && record->uses.value.accesses.empty()`）；④ 同文件 `andTerms`（第五项取交）；⑤ 外加 B 的两处。
- **读数**：`BUILD_EXIT=0`；`[provenance] tip (dirty: 9 files) | binary built 2026-11-28T13:05:57+08:00 | tree …pools-red | tier fast | jobs 6`；`[SUMMARY] total: 2094, pass: 2053, fail: 33, skip: 4, known failures: 4`（日志 `D:\Temp\t405A-full.log`；plan = `feat/modules` + 3 条自建块体探针）。
- **甲 4 = `resident_cpu_block` + 3 条自建探针（真新红只有 1 条）/ 乙 0 / 丙 29**（29 = modules 层全部既有红）。
- **判据① 不达**，原文：`block_erase` 仍 `… 'graph.nodes': the draft was committed or aborted already., location: block_erase.cml:20:18`；`block_reads_draft` 只打 `answer= -1`，连 `fault=` 都没打印。
- **判据② 达**。**判据③ 不达**（1 条真新红）。
- **为什么还不达**：`pure` 同时是**签名发布**的开关（`src/compile/src/gct/builder.cpp:1218-1231`：`clause->pure()` ⇒ 发布空签名，"the reads are the function's own business"）⇒ 读不进调用节点的 footprint ⇒ 没有键可以用来建顺序边。

**三版合计的教训**：不翻转 ⇒ 顺序看不见读（B）；翻转 ⇒ 承诺看见了 effect（A）；改义 ⇒ 承诺变了、发布还是空的（C）。一处改不动四类读者。

## 3 每条真新红一句原因

| 版本 | 名字 | 为什么被打到 |
|---|---|---|
| A | `rules.*` 18 条 | `EffectInRuleCondition`：把 `constant`/`tag_y` 这类调用标成 effectful 后，规则条件里出现 effect —— 「The call of 'constant' in the function 'isY' has an effect, and it is inside the condition of the rule 'tag_y'.」字串 `src/kernel/core/error/diagnostics/messages/semantic.cpp:1188`；触发 `src/compile/src/gir/builder_diagnostics.cpp:1799`、`:1884` |
| B | （无） | 2 条甲都是自建探针，不是回归 |
| C | `resident_cpu_block` | tiles 的区块 outline 用 `isFreelyArrangeable` 判"这条顺序边要不要留"（`src/stdlib/tiles/src/region_outline.cpp:166` 的 `keepsOrder`）；`Arrangeable` 加第五项后该事实换答案 ⇒ 分区/放置变 ⇒ `test/cases/modules/tiles/resident_cpu_block.cml:25` 的 `main` 带错误值退出（`expected exit 0, got exit 1`）。**这一条是第五项引的，不是 `pure()` 改义直接引的** —— C 版两者同时改，未分离 |

## 4 结构判断：`pure` 是一位，四类读者

| 读者 | 在哪一行 | 它需要的答案 |
|---|---|---|
| 1 声明语言的承诺（规则条件里不许有副作用） | `src/compile/src/gct/builder.cpp:1214-1231`（`PureAndUses` 的拒绝在 `:1218-1219`）；诊断 `src/compile/src/gir/builder_diagnostics.cpp:1799`、`:1884` + 字串 `src/kernel/core/error/diagnostics/messages/semantic.cpp:1188` | "这次调用**写**不写" ⇒ 只读应当算纯 |
| 2 效应摘要 / `is_pure` 查询 | `src/kernel/ops/include/camel/ops/operator_record.h:482`（`pure()` = `uses` 子句为空，**一位的源头**）；同文件 `:479` `effect()`；查询侧 `src/stdlib/graph/read.cpp:577-594` | 同上（"写不写"） |
| 3 `Arrangeable`（可摆性，决定顺序边能不能落在这个节点上） | `src/kernel/facts/include/camel/facts/arrangeable.h:60-82`（结构体与 `all()`；第五项 `noAccessToWrittenResource` 在 `:73`，`D-1128-117`）；`src/kernel/facts/facts.cpp:1018-1055`（`terms.pure = record->pure()` 在 `:1032`）；判定点 `facts.cpp:1333`（`if (arrangeable.all())`）；编译侧消费者 `src/compile/src/gir/builder_helpers.h:749-795`、`src/compile/src/gir/builder_resources.cpp:1343` 与 `:1376`；运行期同一判定 `src/kernel/runtime/graph.cpp:231` | "读没读一个**写得到**的资源" ⇒ 只读必须**不可**自由摆 |
| 4 签名发布（读进不进节点 footprint） | `src/compile/src/gct/builder.cpp:1222-1231`（`clause->pure()` ⇒ 空签名） | "读了**什么**" ⇒ 只读必须把读发布出来 |

**结论**：第 1、2 类要"写不写"，第 3、4 类要"读了什么"，一位给不出两个答案。`D-1128-117` 的第五项 `noAccessToWrittenResource`（只读"没有任何写到达的资源"不占用位置）已经走在拆位的方向上了。

## 5 落地版（`lane-editview-a`）状态

- 落在 `hdr-channel` 的 `lane/c4-round-source`：`63845ee572`（读声明）、`45f096735c`（体-序）、`af1a9d018a`（读在构建时取顺序边）、`1530aa420b`（facts 问语义记录）、`e29887b576`（第五项 `noAccessToWrittenResource`）。
- **尖端 `e29887b576` 编不过**：`../src/kernel/facts/include/camel/facts/arrangeable.h:77:9` 与 `:79:9` 报 `error: expected expression` —— 那两行里被写进了字面 `\n`（源码里就是反斜杠加 n，应为换行）。`BUILD_EXIT=1`，五次同样报错。**这笔提交不能进整合。**（对应日志 `D:\Temp\t405-landed.log`；`pools-red` 上是那五笔的 cherry-pick，`dirty=0`、未推。）
- **B、C 两版的读数不来自这份未修的树**：它们是我在 `pools-red` 上的**未提交实验**（`module.cpp` / `operator_table.cpp` / `arrangeable.h` / `facts.cpp` / `operator_record.h`），跑完已回滚 `dirty=0`。因此"未修的树"只影响 §5 的落地版，不影响 §2 的甲乙丙。
- 前置事实（早前量的）：只有 `63845ee572` + `45f096735c` 时块体仍报 `the draft was committed or aborted already` ⇒ 体-序那笔不是充分条件。

## 6 教训：`Compare-Object` 少了 `-IncludeEqual`，"丙"永远是 0

**症状**：我先用 `Compare-Object` 算"两边都红"的名字（丙），再拿"基线里也红"的去减，剩下的记甲 —— 结果甲 = 30，而且看起来完全自洽（30 + 16 = 46）。

**原因**：PowerShell 的 `Compare-Object` **默认不返回两边都有的项**（除非 `-IncludeEqual`）。用它做集合求交，`==` 一条也取不到，所以 `(Compare-Object a b | ? SideIndicator -eq '==').Count` **恒为 0**；而"甲 = 总差额"这条捷径会把丙全数吞进甲里，且差额与基线总数能对上，看不出破绽。

**最小复现**（三行，任何 PowerShell 都能跑）：
```powershell
$a = 'x','y','z'; $b = 'y','z','w'                       # y、z 两边都有
Compare-Object $a $b | ForEach-Object { $_.SideIndicator }  # 只出 <=(x) 与 =>(w)，y/z 不出现
(Compare-Object $a $b -IncludeEqual | Where-Object SideIndicator -eq '==').Count  # 加上才得 2；不加得 0
```

**正确做法**（本页用的就是它，不再依赖 `Compare-Object`）：
```powershell
$rx = [regex]'\[FAIL\]\s+(\S+)'                            # 去掉尾部耗时
function Names($p) { $s=[Collections.Generic.HashSet[string]]::new(); foreach($l in (Get-Content $p)){$m=$rx.Match($l); if($m.Success){[void]$s.Add($m.Groups[1].Value)}}; $s }
$base = Names 'D:\Temp\t405-layer-head.log'; $run = Names 'D:\Temp\t405A-full.log'
$jia = @($run | Where-Object { -not $base.Contains($_) })   # 甲
$bing = @($run | Where-Object { $base.Contains($_) })       # 丙
```

**一句规则**：凡是要"两边都红"的数，**不许在同一份日志的读数与基线之间用 `Compare-Object` 的默认输出推**；丙必须显式求交，甲只等于"跑里红且两把尺子都不红"。

## 7 若要拆两位：动哪几个文件、下游谁在读

**要动的文件（5 个 + 1 个可选）**

| 文件 | 为什么 |
|---|---|
| `src/kernel/ops/include/camel/ops/operator_record.h`（`pure()` `:482`、`effect()` `:479`）| `pure` 一位的源头：`uses.value.none()` = "子句为空"。要拆成"不写"（承诺）与"读了什么"（对顺序可见） |
| `src/kernel/core/resource_semantics.h`（`ResourceSpec`/`ResourceSignature`）| 让"读"在声明与发布里有一等表示，且不被 `pure` 吞掉 |
| `src/compile/src/gct/builder.cpp`（`:1218-1231`、`:1415`）| `pure` ⇒ 发布**空签名**的把关处；读要能出去 |
| `src/kernel/facts/facts.cpp`（`termsOfOperator` `:1018-1055`，`terms.pure` `:1032`）| `Arrangeable` 的 `pure` 项照抄 `record->pure()`；拆位后这一项要问"写了什么"，另立一项问"读了写得到的资源没有" |
| `src/compile/src/gir/builder_helpers.h`（`nodeTakesAnOrder` `:749-795`）+ `src/kernel/runtime/graph.cpp:231` | 两处同一定定：顺序边能不能落在这个节点上；应改问"节点读了写得到的资源没有"，而不是"算子四项全真" |
| （可选）`src/compile/src/gir/builder_diagnostics.cpp:198` `clauseSaysEffect` | 效应摘要的读法，若"写不写"口径落地这里也要跟 |

**下游谁在读这个事实**（`git grep -n 'nodeIsFreelyArrangeable' -- src` 的一次清单，`pools-red` 尖端 `04d2353e31`）

```
src/compile/src/gir/builder_helpers.h:787,793        nodeTakesAnOrder（顺序边闸门）
src/kernel/facts/facts.cpp:1262                      nodeIsFreelyArrangeable 定义；:1333 的 arrangeable.all() 决策点
src/kernel/runtime/draft_inline.cpp:1809             内联/改名时的同一判定
src/kernel/runtime/graph.cpp:1051,2343               图侧判定（seal / 边）
src/kernel/runtime/graph_edges.cpp:1070              边分类
src/stdlib/graph/read.cpp:639,641                    `is_pure`/arrangeable 查询（用户可见的答案）
src/stdlib/graph/txn.cpp:761                         事务里的放置
src/stdlib/std/cxx/opt/generic/rewrite.cpp:103,109,245,249   通用重写（抽写/融合）
src/stdlib/std/cxx/opt/inline/engine.cpp:220,227      内联引擎
src/stdlib/tiles/src/region_outline.cpp:166           tiles 区块 outline（C 版唯一真新红就落在这）
src/stdlib/test/edges.cpp:339                        std::test 的自检
src/stdlib/test/block_body.cpp:509                    块体自检（读 operatorArrangeable 的四项）
```
另有两处直接读 `operatorArrangeable(...).all()`：`src/compile/src/gir/builder_helpers.h:785`、`src/kernel/runtime/graph.cpp:231` —— 拆位时这两处是"顺序 vs 承诺"的分叉点。

**结论**：拆两位不是改一条校验，而是"一位 → 两份答案 + 处处按读者选一份"。最小可行切法（`D-1128-117` 的路子）：`pure()` 维持"子句为空"不动，另立"这次读的每个资源是否有写到达它"，只让第 3 类读者（顺序）看新项 —— 这样第 1、2、4 类读者的答案一字不变，只有"读一个会被写的资源"的节点从"可自由摆"变成"必须保位"。

## 8 待做

1. `lane-editview-a` 修掉 `arrangeable.h:77/79` 后，我在 `pools-red` 上 cherry-pick 取独立复核读数（三层甲 ∅ + 18 条 rules 绿 + 块体三例绿：`graphs(d)`/`erase`/`replace`，`erase` 那例用"本来就有、没人读的值节点"绕开 `erase(add_constant 造点)` 的既有 SIGSEGV）。
2. 若决策者要"两位分开"的正式口径，需要 `ResourceSignature`/效应模型的决定，不是车道能自定的。
3. 既有的独立缺陷（与本判据无关，别记在这一版头上）：块体里 `erase(add_constant 造的点)` SIGSEGV（`eraseKernel → replaceNode(transaction, ref, kInvalidNodeRef)`），父尖端同样崩、A/B 一致。