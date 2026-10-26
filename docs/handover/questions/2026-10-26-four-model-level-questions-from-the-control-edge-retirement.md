# 控制边退役撞上的四条模型级问题（两条不变量、一条不一致、一条前置）

这批是"把控制边这个种类退役"施工时**实测撞到的**、我们不敢单方面定的事。清点与施工细节在车道报告里；这里只列**必须由你定口径**的四条，每条带逐字读数与 `文件:行`。

## 1. 纯节点上的"等待"该用哪种边
把产出方从 ctrl 改成顺序边后，出现新的不变量报错：逐字 `order edge 12 -> 7 is on a pure node, which nothing orders`（`docs/design/edges.cn.md` 4.6 第三款）。我们在 `appendOrderDependency` 里加了"纯节点不接序边 ⇒ 回退 Ctrl"，但**同一形状仍报**，来源是**内联器自己写的序边**（`rules::combineSameSign<f32>` 那条），说明回退规则要覆盖到内联器。
- **问题**：纯节点（无效应）的"等待"你要用哪种边？顺序边被 4.6 禁；同步边受 5.3 与链语义限制（"GATE 一个同步前驱"，而纯节点不在语句链上）。
- 我们的**临时取法**（等你的话，行不行）：纯节点继续用 Ctrl，并把该回退规则扩到内联器的序边站点。

## 2. 两种拼写并存期的"混合二环"
逐字：`node 6(11) <- 7(11) <- 6`。`findCycle` 现在**同时**走 ctrl 与 order 两组边，而旧写法下同一形状只有一种边。也就是说：当图上"一半是 ctrl、一半是 order"时，会出现旧模型里不存在的环。
- **问题**：并存期这类环应当**禁止**（findCycle 只走一种）、还是**允许**（两种拼写语义等价、环按并集判）？我们倾向"禁止并按顺序边收敛"，但那等于要求两类边不能混用于同一对节点。

## 3. 同一形状：一处溶解、一处内部错误
- **拒绝**：`src/runtime/graph.cpp:512-535`（GATE 分支，消息在 `:523`）：`"Runtime graph '{}' has GATE node {} without a wait: no sync predecessor and no control input."`；前端冻结/encode 提交走 `src/runtime/draft_session.cpp:1878 validateRuntimeGraphPayload`（提交路径 `:1726`）。实测该诊断之后进程常**随 SIGSEGV**（`F02 [std::opt std::prl]` 修前 `exit=139`）。
- **溶解**：`src/runtime/draft.cpp:1942-1982 GraphDraft::dissolveUnorderedGates()`（条件 `syncInputsOf(id).empty() && ctrlInputsOf(id).empty() && !normInputsOf(id).empty()`，做 `replaceAllValueUses` `:1976` ＋ `eraseNode`），由**运行时提交** `src/runtime/draft_session.cpp:1549` 对闭包内每张图调用，并置 `kGraphFlagGatesDissolved`。
- **问题**：**同一个形状**（GATE 没有等待、有 norm 使用者）为什么运行时提交当"纯转发"溶解、前端冻结却当**内部错误**并拒绝（还经常崩）？要统一成哪一种？

## 4. "读出来喂给指定种类的写入"这类站点
把"读"换成联合读（`sync ∪ order ∪ ctrl`）后出现逐字：`Draft payload planning found dead node ref … in ctrl-input list`（`std::macro`）。已定位到 `src/runtime/draft_inline.cpp:351`、`modules/autodiff/src/macro/rewrite.cpp:27` 这类"**读出来喂给一个指定种类（ctrl）的写入**"的站点。
- **问题**：这类站点在并存期应当**只读它要写的那种边**（我们临时这么回退的），还是你要它们也改成联合读（那就得同时改写入端的种类）？

## 附：一处与本次退役无关、但同一族的缺口（等你排期）
`src/compile/gir/builder_calls.cpp:884` 的 `requireMutableArgument` 分不清"把 cell 交给另一个图"和"这次调用就是写本身"，把**赋值目标**也插进 `escapingVariables_`；而 `src/compile/gir/builder_expr.cpp:1354` 的 `collapseNonEscapingVariableReads` 对逃逸 cell 直接跳过 ⇒ 原则 3.5 第 184 行"不逃逸 var 化成 SSA"**对任何被写过的 `var` 都不生效**。最小复现 `var a = 0; a = 1; println(a)`：判据收窄前 `std::gir` 计 `:var/new|:var/get|:op/assn` = **3**；收窄后 = **0**（真 SSA、`a = 1` 不执行，合乎 F3）**但**多一条假警告 `warning[SemanticDiag::EffectNotExecuted]: … and it is not in a sync block.`（该函数体本身就是 sync 块）且快层冒出 **66 条红**（写方失去可达性 ＋ `GATE` 没有 wait 的内部错误）⇒ 这是独立一件大活，要不要排期也请一并说。

## 附二：今天 develop 上就能复现的一处误编译（与本页第 1 条同根）
`func main(): int sync { var a: int = 1; println(a); println(a + 1); return 0 }` **只打印 `2`**，并报一条 `warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.`（指 `3:5`）。`std::gir` 里图**只剩后一个** `:io/println`，`println(a)` 的节点整个不在，链的形状是 `println(a+1) -CTRL-> GATE -> EXIT`；把 `var` 换成 `let`（或后一条语句不读 `a`）则两个 `:io/println` 都在、`println1 -CTRL-> println2 -CTRL-> GATE -> EXIT`。
- **根因（一句）**：`Builder::collapseNonEscapingVariableReads`（`src/compile/gir/builder_expr.cpp:1452-1453`）**先** `replaceAllValueUses(readId, version)` **再** `eraseNode(readId)`；而 sync 体里这次 `var` 读取本身是链条的一环（`readVariableThrough` `builder_expr.cpp:1334-1342` 把 `lastSyncedNode_` 置为它、并写下"上一条语句 `-CTRL->` read"），紧跟的语句却只靠 **data** 口读它（`shouldAddSyncCtrlLink` 的"已是直接数据输入就不再写控制边"）⇒ 值口改指 version 后，draft 节点**没有任何 ctrl/sync 用户**，`eraseNode → GraphDraft::closeChainOver` 无"链位"可交，那条 `上一条语句 -> read` 的 CTRL 边随节点消失 ⇒ 前一条语句失去通往 exit 的唯一联系，`reportDroppedEffects` 的 liveness 走不到它 ⇒ 报 `EffectNotExecuted` 且调度器不执行。
- **条件**：sync 体里有 `var`，且后面有语句读它（读之前紧邻的效应语句成片丢，只留链尾）。三条预测全部命中：`var a=1; println(a); println(a+1); println(a+2)` → 只打印 `3`、两条警告；无 `var` 的三个 `println` → `1,2,3`、零警告；闭包捕获让 cell 逃逸（关掉折叠）→ `1,2`、零警告。
- **"两版警告文案"是同一根因的两面**：`func main(): int { var a: int = 1; sync { println(a); println(a + 1); } return 0 }` 同一次编译里两条并存——`3:12` 是 `nothing depends on it and it is not in a sync block`（被折叠从链上摘下来的那条）、`3:24` 是 `the sync block it is in is not used`（GATE 仍可达的那条）；无 `var` 的同类只报后者。
- **它与本页第 1 条是同一个决定**：`handover/h242-edges` 的 diff 恰好把这一行改写成 `linkSyncStatement(lastSyncedNode_, value)`（就是"读在链上怎么写等待/控制边"那一处），并重写了 `draft.cpp` 的 `closeChainOver`（新增 `statementRegionOf`/`sameSyncBlock`/`controlRegionOf`）与 `takeStatementPlace`、以及 `passes/opt/generic/gates.cpp` ⇒ 任何修法（折叠时保留该读／把链位交给继承其值的节点／换边种类）都在等你这条口径，所以我们**一行都没改**。