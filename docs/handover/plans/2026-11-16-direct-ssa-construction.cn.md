# 直接构造 SSA：分阶段、接缝、回退

> 作者的裁定见 `54b501f3a`：`var` 不再先建成单元再折叠，**建图时就按「名字到当前值」构造 SSA**。
> 分母（不要重测）：`:var/new` **348**（census `list_sha=ac8288b4…`，files=3853 counted=2688），
> A 生成器控制槽 0／B 闭包捕获 117（机器 64 + 程序 53）／C 普通局部 81／D `var` 形参 79／E 其他 71。
> 直接构造之后 **270 消失**（C81 + D79 + E67 + B 只读程序捕获 43），**剩 78**（64 机器 + 10 模块级 + 4 判不了）。
> 分类明细在 `docs/technical/63_what_the_348_units_are.md`（已合入本树；本页只用上面这几个数）。

> **本页末尾有作者 2026-11-16 的批复，其中四条改变了下表，读表之前先读它：**
> 1. S6 里「`FUNC` 上加生成器体标记」**不做**（建图器用自己的状态栈知道）；
>    「留格子」只指生成器自己跨 `yield` 的 64 个机器单元，生成器体里程序写的闭包照常按值捕获。
> 2. S3 之后加 **S3b**（就地更新，判据 `camel inspect --copies` 报「原地」）；S3b 不过，第一件不算完成。
> 3. S5 里 4 个「判不了」按**函数值的类型**判，不留「判不了」。
> 4. 模块级 10 个 `var` 不进 `Ref<T>`，作为 **S8**（原则 3.5：顶层 `var` 是编译错误）；
>    **第一件终值 64，不是 78。**

## 1. 终局形状

- **区域**：`ArmBuild`（`builder.h#ArmBuild`）与图本身都是区域；`armStack_` 已是区域栈，`ports`/`kind` 已在里面。
- **映射**：每区域一张「声明 → 当前值」的表，随 `buildArm` / `enterBlockScope` 压弹。**键是声明的 GCT 绑定，不是名字字符串**——按名字键的旁表正是要消失的东西。
- **声明** `var x = e`：不建 `:var/new`，把声明绑到 `e` 的值。
- **赋值** `x = e`：把声明重绑到新值；不留 store、不留顺序边、不进 `writesOf_`、不按节点编号判先后。
- **读**：直接返回表里的值；不建 `:var/get`，不建读节点，`syncedReads_` 里没有它。
- **JOIN**：`builder_control.cpp#mergeArmVariables` 处，凡各 arm 结束时值不同的声明，join 上取一个值——JOIN 本来就是值的合并点（`collapseNonEscapingVariableReads` 的 `foldTookJoin_` 已在用它），不需要新节点种类。判据：**该声明至少有一个 arm 写过**才放 φ；不写的 arm 传它原来的值。
- **循环头**：`loopStatePorts_`/`loopStateWrites_` 已经是「这一轮写进去的值」，把端口从「格子」改成「值」即得 φ。判据：**body 里写过**才进 carried state。
- **`var` 形参**：值进（绑定成初值）+ 值出（随返回值打包，`callWriteMasks_`/`callValueTypeOf` 已经会打包被写单元）；调用处把实参的声明重绑到那个新值。**写回就是调用处的一次重绑**，没有回写单元、没有 `:var/after`、没有 `:var/take`。

## 2. 分阶段

**切法的不变量（三段硬要求都靠它满足）**：每一段**只改一个判据**——`ssaEligible(decl)`：「这个声明走 SSA 还是留格子」，
在**声明处一次判定**，判据由一趟 GCT 预扫描给出（「这个名字的读/写有没有落在嵌套区域」，与 `scanProgramWriters` 同一类扫描）。
因此**每个声明从头到尾只有一种做法**，不存在「既不是旧做法也不是新做法」的名字，也没有需要重建的区域。
每段一个提交，段末跑一次构建 + 一次闸门；闸门 `G` 指三件事同时成立：

**`G` = `node scripts/test.js test/plans/feat/compile/stream` 仍 147/147 ｜ 全量红数 ≤ 76（`docs/handover/red-baseline.cn.md`）｜ `scripts/check-capture-census.mjs` 的 `var_new` 等于下表「剩」列。**

| 段 | 这一段删掉什么 / 加上什么 | 348 → 剩 | 这一段结束时的判据 |
|---|---|---|---|
| **S1** | 加值环境（`ValueMap`，键=声明）+ 直线局部的声明/赋值/读改写。判据只放「读写同区、都在本区域内」 | **293**（−55） | `G`，`var_new` 293；`new_values/*` 与 `linear/*` 里同区下标量 `var` 的用例逐条绿 |
| **S2** | 加分支 JOIN 的 φ（`mergeArmVariables` 上取值）；判据加「只在 arm 里被写」 | **275**（−18） | `G`，`var_new` 275；`warnings_visible.cml` 里 `inc(c); if c > 0 then { c += 10 }` 读到 11 |
| **S3** | 加循环头 φ（端口从格改值，`loopStateWrites_` 一般化）；判据加「只在 loop body 里被写」 | **267**（−8） | `G`，`var_new` 267；`write_in_loop.cml`（`x = x + 1`）读到 3，C 的 81 到此清零 |
| **S4a** | 加**被调侧**：`var` 形参的新值随返回值打包输出，格子仍在、读数不变（纯准备段） | **267（本段不改变这个数）** | `G`，且 `var_new` 严格等于 267（用这条钉住「本段没有顺手改语义」） |
| **S4b** | 删 `var` 形参的单元；加调用处重绑，写回不再经过格子 | **188**（−79） | `G`，`var_new` 188；`feat/linear/byref` 正例全绿；另钉一条 `f(a, a)`（同一次调用里同一个名字坐两个 `var` 位）的写回顺序 |
| **S5** | E 的其余 67 个形状：按 63 的分类逐个往同一套判据上搬；**4 个判不了的留着** | **121**（−67） | `G`，`var_new` 121；若 63 把 E 拆成两种以上形状，就按形状再切，每段数从 63 取，合计仍是 67 |
| **S6** | 删 `captures.cpp#decideCaptureByValue` 的 `programMayWriteThroughCall` 门与 `captureByValueOf_`；`__gen_` 前缀换成 `FUNC` 上的生成器体标记（**作者 2026-11-16 批复第 1 条：不做，见页末**）；B 的只读程序捕获按值 | **78**（−43） | `G`，`var_new` 78；`feat/linear/byref`、`feat/compile/function_values` 绿；`var_param_reference.cml#write_then_read` 的期望按问题页改（第 4 节） |
| **S7** | 纯删：`collapseNonEscapingVariableReads`、`variableReadSource_`、`knownCellUsers_`、`writesOf_`、`writeOnlyEscaping_`、`carriedVersions_`、`dropCarriedVariableUnits`、`kVarFoldDebug` 与 `[DBG-FOLD]`/`[DBG-W]` 打印 | **78（本段不改变这个数）** | `G`，且 `var_new`/总节点数/红集**逐字节不变**（纯删段唯一可接受的读数就是「什么都没变」） |

工作量：S1、S2、S3、S4a、S6 各 ≤250 行、一次构建；**S4b 是唯一可能吃两次构建的一段**（协议换了，调用点与返回打包要对齐），
所以它单独成段、且 S4a 已经把被调侧先做完了。没有一段需要三次以上构建。

## 3. 与既有工作的接缝

- `captureSites_` / `builder.h#CaptureSite`：**留着**。它是「这次调用还欠几个 `with` 输入」的簿记，与格子无关；S6 不碰它。
- 门一，`nodeKindOf(origin) != Oper`（`var` 形参不是捕获候选）：**随 S4b 消失**——形参不再是格子。
- 门二，`!localCell && programWrites(name)`：**留着**，只剩模块级那 10 个的用途；随模块 `var`/`Ref<T>` 线走。
- 门三，`programMayWriteThroughCall(name)`：**随 S6 消失**，被「调用处重绑 + 被调者静态未知的名字留格子」取代。
- `__gen_` 前缀豁免（`captures.cpp#compilerMadeClosure`）：**前缀判据删掉**，换成 `gct/node.h` 的 `FUNC` 上一个生成器体标记（`visitGeneratorBody` 处打上）（**作者 2026-11-16 批复第 1 条：这个标记不做，建图器用自己的状态栈知道，见页末**）；**「生成器体捕获留格子」这条规则留着**，所以 64 不动。
- `markNonRetainingReads`：**留着**，只剩 78 里那些格子的读；随模块 `var` 线走。
- `kVarFoldDebug`：**随 S7 删除**（含两处 `fprintf`）。
- `handedOver_` / `programWriters_`：**留着**。S4b 之后 `handedOver_` 的消费者只剩 `builder_operators.cpp` 里那段「被调者静态未知的保守写集」，随 10 + 4 那条线走。
- `:var/after`：**留着**（就是上面那段保守写集）；`:var/take`：**随 S1–S3 消失**（就地改写要格子，SSA 局部没有格子可取）；`:var/peek`：同 `markNonRetainingReads`，留着给 78。

## 4. 风险与回退

- 最可能动读数的两段：**S4b**（调用协议换了）和 **S6**（捕获规则换了）。S1–S3 只把「这个声明用哪种做法」的判据往外放一格，动读数的面最小。
- **S4b 的预期红**：`ImmutablePassedToVarParam` / `AssignmentTargetNotVariable` 的负例仍红。**切错了**：任何 `ok_*var*` 正例转红，或 `f(a, a)` 的写回顺序与形参顺序混了。
- **S6 的预期红**：`var_param_reference.cml#write_then_read` 一条（读数从 7 变成造闭包那一刻的值）。这条要作者裁，问题页已写死默认建议；**保持 7 就是保留那道门，B 少减一个，终值 79 不是 78**。**切错了**：`feat/compile/stream` 掉出 147/147。
- **S1 的隐性代价**：局部 `var` 的 `v = v->append(x)` 不能再就地改写（`:var/take` 无格子可取），每次 append 出新数组。语义不变，**`perf/**` 与基准读数会动**——那是性能线的账，不是本线切错，但要在提交信息里写明，别让它以后被当成回归。
- **回退**：S1/S2/S3/S4a/S5/S7 各自单独回退（都是「一个判据 / 一段纯删」）；**S4b 与 S6 不能单独回退到中段**（协议已改），只能整段回退到段前提交。

## 5. 不做的事

- 不顺手改 `std::cpp` 后端。
- 不顺手动 `StreamType::stepType`。
- 不做生成器状态按值（64 个单元、以及 `__gen_` 结构标记的**删除**，都在那条线）。
- 不做模块级 `var` → `Ref<T>`（10 个）。
- 不碰 4 个「判不了」的单元。
- 不重建局部的就地 append 优化（第 4 节那笔性能账另开一条）。
## 决策者批复（2026-11-16）

计划批准，按 S1–S7 的切法做，每段一个提交、一次闸门。四处修改：

1. **生成器体不在节点上加标记**：S6 里"`__gen_` 前缀换成 `gct/node.h` 的 `FUNC` 上一个生成器体标记"不做。建图器在 `visitGeneratorBody` 时自己知道正在建生成器体（建图器状态里的一个栈项），不必把这件事写到节点上（11-16 对生成器一页的答复第 3 条）。另外，"生成器体捕获留格子"只指生成器**自己跨 `yield` 的状态**（64 个机器单元，实现层面）；生成器体里**程序写的闭包**照常按值捕获，不留格子。
2. **就地更新不能丢**：S1 写着"`v = v->append(x)` 不再就地改写，每次出新数组，是性能线的账"。这条不能放到另一条线：负责人定 `var` 方案时的前提就是"`params.w -= …` 与训练循环的写法不变，而且原地进行"（原则 3.5）。就地靠的是值的计数为 1 时原地更新（10.5），执行计划的 `planStorageReuse` 已经在回答"这次更新能不能复用入参的存储"。所以在 S3 之后加一段 **S3b**：SSA 的值在最后一次使用处交给 `append`、路径赋值、`sgd` 一类更新时，复用入参存储；判据是 `camel inspect --copies`（没有就先做一个最小版）对 `v = v->append(x)` 循环与案例训练步报"原地"。S3b 不过，第一件不算完成。
3. **4 个"判不了"按函数值的类型判**（见问题页 (b) 的答复），不留"判不了"。
4. **模块级 `var` 的 10 个**：原则 3.5 规定顶层 `var` 是编译错误。它们不进 `Ref<T>` 的"以后"，作为 S8 跟在 S7 之后：用例改成 `let` 或 `Ref`，加上编译错误，门二与 `handedOver_`/`programWriters_` 随之删除。第一件的终值因此是 64（只剩生成器自己的状态），不是 78。
