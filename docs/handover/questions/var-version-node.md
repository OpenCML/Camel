# H242a 的"写产出变量的新版本"：版本节点到底是什么（三处要裁定）

Author: 车道 `handover/h437-splice-verbatim`（H437 拼接/同步链 + H242a），2026-10-24。Status: open —— 需要作者对下面三处各给一句裁定。

裁定的出处是 `docs/handover/reviews/questions-2026-10-23.md`（`## h242a-capture-order-edge`，选 1 并把同一形态用到 `:var/get`；另有 `## h242a-var-initializer-write`），子句落在 `docs/design/effects/edges.cn.md` 第 5 节第 6 条。该节开头写着"施工中若发现与此冲突的用途，以问题文件提出"，这份就是那处冲突。读数都在 Linux 主机（`88zjwei`）上、同一窗口、同一命令形式取得（`RUN_EXIT` 是 `test/plans/feat/linear -Tier fast -Jobs 8` 的退出码，`EXIT` 是 `camel` 进程退出码）。

## 数字先（按第 6 条字面做出来的那一次）

基线（本车道已提交的 `0a93ab2d1`，即 H242a 之前）：`test/plans/feat/linear` = total 745 / pass 741 / fail 0 / known failures 4，`RUN_EXIT=0`。

按字面落地"读 = 从最近一次写来的数据边"：

- `Builder::readVariableThrough` 的数据输入从"cell"改成 `modifierOf(variable)`（最近一次写的节点），并删掉原来那条"写 → 读"的 `CTRL` 边；
- `Builder::recordWrite` 里给"上一次写 → 这一次写"连一条 `ORDER` 边。

结果：`test/plans/feat/linear` = total 745 / pass **443** / fail **298** / known failures 4，`RUN_EXIT=1`；`camel test/cases/modules/tensor/compound_assign.cml std::nvm` = **段错误**（`EXIT=139`，没有输出）；`edges.order_not_the_chain` 仍然报"the graph has none"；`edges.gate_has_no_control_input` 报"the frozen input must build at least one GATE"。

原因在代码里是确定的，不是取法问题：

- `src/builtin/operators/ops.cpp:42`：`variableOf(norm)` = `norm.get<Tuple *>(0)`，随后 `ASSERT(cell != nullptr && cell->size() == 1, "Operand is not a variable.")`。**`var` 的每个算子（`:var/get`、`:var/peek`、`:op/assn*`）的数据输入必须是 cell（单槽 `Tuple`）**，读的是 `cell->get<slot_t>(0)`。
- `src/builtin/operators/ops.cpp:218`：`__builtin__assn__` 的**产出是写入的值**（`slot_t rhs = norm.slot(1); … return rhs;`），`:op/assn_add_l`（`:271`）同理返回新的标量值。也就是说今天的**写节点本身不是"变量的新版本"**：它的值是被写入的那个值（`int`、张量……），不是 cell。
- 于是读把它当 cell 用就崩：`norm.type(0)` 是值类型，`get<Tuple *>` 拿着它读 slot 0。

## 要裁定的一：版本节点由谁产出

第 6 条说"每次写（赋值、写了捕获变量的调用）产出变量的新版本；每次读有一个数据端口接到最近一次写的产出"。今天没有这个"产出"。三种落法：

- **A：`:op/assn*` 改成产出新 cell**（结果类型 = 变量类型；运行时给一个新的 cell，或原地改并返回同一个 cell）。那么"赋值表达式的值"变了：今天是写入的值（`a = (b = 1)` 这类写法与 `:op/assn` 的返回类型都随它变）。
- **B：建图器在每次写后面加一个版本节点**（新算子或新节点种类，比如 `:var/version`），数据端口接 cell，值是"这一版的变量"；读接它。要一起说清它是不是快照：cell 是原地改的，若这一版不是快照，那么"读到最近一次写的产出"就要靠"写与写之间的顺序边"再保证读排在下一次写之前，而第 6 条只把顺序边放在**写与写之间**。
- **C：读不再包一层 `:var/get`，直接用写节点的值**（因为写节点的值已经是"那时的值"）。这最省，但它把 `:var/get`/`:var/peek` 的**所有权**作用一起丢了：`__builtin__var_get__`（`ops.cpp:201`）会 `cell->setHoldsUnsharedArray(false)`，而 `:var/take`（`:242`）、`:op/assn_owned`（`:253`）、`Builder::markNonRetainingReads` 与 `:var/peek` 全靠 cell 上的这个标记决定"能不能原地改"。今天 `var` 的原地优化（`xs = xs->sort`、`xs[i] = v`）就建立在这上面。

我倾向 **B（版本节点，且版本就是"写节点的值"）**——它让"读用到最近一次写、没被读的写不执行"（F3）直接由数据边表达，同时把 cell 留给所有权；但这需要一句裁定，因为 A 会改 `:op/assn*` 的返回类型与赋值表达式的值，C 会改原地优化的判据。

## 要裁定二：版本与 `h242a-var-initializer-write` 那一半的先后

`:var/new` 今天是**取初值**的（`ops.cpp:195`：`cell->set<slot_t>(0, norm.slot(0), TupleType::cell(norm.type(0)))`），而 `## h242a-var-initializer-write` 裁定它"不取值、元素类型是节点的静态类型"，`var a = 0` 降为"建 cell + 一次普通写"。第 6 条同时要求"没写过则接建 cell 处"，也就是**建 cell 处必须给得出一个值**。

问：这两半是否必须同一提交落地（今天"建 cell 处取初值"正好能当第一次读的来源，但那是旧模型的一半），还是先把"版本节点"落定、再单独落"建 cell 不取值"？

## 要裁定三："读了捕获变量的调用"那一半的端口角色

第 6 条说调用上这个端口的角色是"**捕获**"，来自被调函数的捕获表。今天 `PortRole` 只有 `Normal` 与 `Context`（`include/camel/runtime/draft.h:82` 的 `edgeSlotOf`），`src/compile/gir/builder_support.h:64` 的注释已经在等它（"the capture role is the callee's capture table's, not a `LinkType`"）。

问：这一半（`PortRole::Capture` + `CALL` 上的捕获端口 + 读了/写了捕获变量的调用）要不要与"版本节点"同一片做？我按"不发明"处理：本片只做直接读/直接写，调用那一半等裁定；`Builder::orderAfterLastWriter` 的注释已经写明它要等这条数据边才敢从 `CTRL` 改成 `ORDER`，所以我不动它。

## 补：读法 C 的实测（2026-10-25，为第一处的裁定提供数据）

协调者授权做一次只报数字的测量：按**读法 C**（读直接用写节点的值，不建 `:var/get`，于是也不再走 `cell->setHoldsUnsharedArray(false)`）。做法只有一处：`Builder::readVariableThrough` 里，若该 `var` 有写者且写者的种类是 `Oper`（赋值类，它的值就是新值），读直接返回那个写者节点；写者是调用（值是被调函数的结果，不是变量的新值）时仍走旧路。跑完即 `git checkout -- src/` 回退，未进提交。

读数（Linux 主机，`-Tier fast -Jobs 8`，与基线同一命令形式）：

- 基线（`8ee159466`）：`test/plans/feat/linear,test/plans/feat/opt` = total 1559 / pass 1555 / fail 0 / known failures 4，`RUN_EXIT=0`。
- 读法 C：**total 1559 / pass 1278 / fail 277 / known failures 4，`RUN_EXIT=1`** ⇒ 新增 277 条红。

红在哪（按用例族，括号是条数；族内多条是后端/遍的变体）：`var_capture`(33)、`wait_before_after`(33)、`var_tensor`(33)、`var_param`(33)、`var_grad`(33)、`var_rule_subpasses`(20)、`wait_direct_user`(18，输出变 `2001`)、`captured_write_order`(17，`10 | 10 | 100 | …` 这类顺序错)、`camel_passes`(12，金标)、`written_variable`(9)、`let_multi_binding`(6)、`var_writes_through_recursion`(6)、`var_read_before_callee_write`(5)、`path_assigned`(4)、`string_plus_assign`(4，输出 `abab`——原地追加别名了)、`nn`(2)、`nested_array_type`(2)、`var_tensor_read_after_write`(2)、`wait_graph`/`generic`/`element_assigned_copy`/`captured_write_graph`/`onnx` 各 1。

这条数据说明的事：**C 不是"省一层节点"的小改**——它把 cell 上的所有权标记整条拿掉，红的是 `var` 的捕获/写序/张量原地改与 `var/grad` 这些族（277 条里 165 条集中在五个 `33` 的族），`string_plus_assign` 的 `abab` 正是"读没清标记 ⇒ `:var/take` 把别人还refer着的数组交出去原地改"。所以第一处裁定若选 C，等于同时裁定"`var` 的原地优化换一套判据"；若选 A 或 B，所有权留在 cell 上，这批红不该出现。

**B 的估工（供裁定参考，按本车道已测到的边界列）**：①运行时/算子面——每次写要产出"变量的新版本"这个节点，而 `:op/assn*` 今天返回写入的值（`ops.cpp:218/271`），所以要么改它的结果类型/解析器，要么新增一个算子（新算子还要在 `src/builtin/executors/builtin.cpp` 与 `operators/ops.cpp` 注册）；②建图器面——`recordWrite` 把"最近一次写"指向版本节点、`readVariableThrough` 的数据端口接到它（本文件开头那次字面落地就是这两处，读数在场）、写与写之间的 `ORDER` 边（已写出过，约 10 行）；③消费面——读 `modifierOf`/"前一条语句"的遍（`writings.cpp`、`definitions.cpp`、`dce`/`gates`/`screen`）与封存检查（`graph_edges.cpp` 的 `checkGates`/`checkSyncEdges`）各要过一遍；④调用那一半（`PortRole::Capture`）与第 5 节第 4 条的"返回 `GATE` 去掉第二个输入"；⑤金标重生成（读法 C 的读数里 `camel_passes`(12) 已经是金标条目）。按本车道这一片的速度（改一处 + 一次整层 ≈ 15 分钟，且每次都要逐条比对红集合），①+②是一个提交、③+④是第二个提交。

- `0a93ab2d1` `snapshotNodeInit` 保留被改写节点的 `orderInputs`/`syncInputs`（rewrite 不再丢"在我之前的语句"与"谁排在我前面"）。
- 链改同步边（第 5 节第 7 条 / 裁决"分支语句的链边是同步边"）已经写出来并可幂等重放（`D:\Projects\Camel-wt\notes\h437-s3-apply.ps1`），`test/plans/feat/linear` 由 741 pass 落到 689 pass / fail 53；53 条的门在同一处（内联之后再被 `cse`/`dce` 合并或删节点，靠"cell + 顺序边"表达的读被挪到写之后），所以它等这份裁定的第一处落地之后再提交。

## 补二：裁定之后的重读 —— 277 红按"逃逸判定"分类，与内存规划要给出的东西

裁定（`docs/handover/reviews/questions-2026-10-24-evening.md` §`var-version-node`）把第一处一分为二：**不逃逸的 `var` 化成 SSA**（写产出新值、读直接用最近一次写的那个值、不建 cell、不经 `:var/get`；所有权与"能否原地"交给内存规划，不靠 `holdsUnsharedArray`），**逃逸的 `var`**（被闭包捕获，或作为 `var` 实参传出——它是资源）保留 cell 那一套（写产出"这一版的 cell"、读与读了捕获变量的调用接最近一次写的产出、调用上这个端口的角色是"捕获"、写与写之间顺序边）。所以"读法 C 的 277 红"要按这个划分重读，而不是当成 C 错了。

**分类（判据：用例源码里有没有 `var` 形参 / `wait` / 捕获；主机上的红清单见 `D:\Projects\Camel-wt\notes\h437-cv-fails.log`）**：

- **逃逸那一类（SSA 不该覆盖，保留 cell ⇒ 这 277 条里的大头本来就是"不能按 C 做"的）**：`var_capture`33、`wait_before_after`33、`var_tensor`33、`var_param`33、`var_grad`33、`var_rule_subpasses`20、`wait_direct_user`18、`captured_write_order`17、`written_variable`9、`var_writes_through_recursion`6、`var_read_before_callee_write`5、`string_plus_assign`4（`var` 形参）、`path_assigned_allocs`、`nested_array_type`、`var_tensor_read_after_write`、`wait_graph`、`onnx/export_var_parameter`、`export_var_append`、`nn/lr`、`nn/lrit` —— 各自的用例源码都带 `var` 形参（`func f(var c: int)`）、`wait`、或"captured/捕获"字样。**这不是说 SSA 落地后它们还红**：SSA 只碰不逃逸的 `var`，这些用例走的仍是今天的 cell 那条路，读数应当与今天一样（绿）。
- **金标（不是语义）**：`camel_passes` 12 条（`cse_subject`/`fold_subject`/`fold_fault`/`dce_subject` 等）。读法 C 让不逃逸 `var` 的图少了 cell/`:var/get` 若干节点，金标必然要按新图重生成 —— 这一步在 SSA 的提交里做，与红无关。
- **"内存规划该管"的（`holdsUnsharedArray` 那一族）**：`string_plus_assign`（输出 `abab`：`:var/take` 把一个别人还 refer 着的数组交出去原地追加）、`path_assigned`、`element_assigned_copy`、`nested_array_type` 里的 `[T]` 别名条目。这些要的不是"把标记加回来"，而是内存规划给出统一规则：**同一版本的值在什么时候可以原地改**（今天由 `:op/assn_owned` 置位、`:var/get` 清位、`:var/peek` 不清、`:var/take` 按位复制来表达）。SSA 之后这些标记没有 cell 可挂，判据必须来自"这个值还有没有别的使用者 / 它是不是本版本的最后一用"。这是我下一片要问的一整件事，先记在这里。

**两处撞点在 SSA 落地时的影响面**（按裁定不发明语义，先点出各自影响的用例）：`Builder::orderAfterLastWriter`（今天用 Ctrl 把读排在上一次写之后）影响的正是 `wait_direct_user`18、`captured_write_order`17、`var_rule_subpasses`20 这些**逃逸**族的排序条目——SSA 之后不逃逸的读是纯数据边，这条 Ctrl 应当退回 `ORDER`；`Builder::markNonRetainingReads` / `:var/peek` 影响的正是 `path_assigned`(4)、`element_assigned_copy`、`string_plus_assign`(4) 这些**原地改**的条目——SSA 之后它们不再是所有权标记，而是"路径赋值要读的值"。

**落在 SSA 前的一处要裁定（我不发明）——"逃逸判定"在哪一步做**：裁定说"按变量是否逃逸分两种"，但没说判定发生在哪。单遍建图器要在**发出读之前**就知道这个 `var` 会不会逃逸（逃逸点可能在读的后面出现，例如后面的 `wait f(c)` 把 `c` 传出去），所以要么在建图前先把函数体扫一遍收"被捕获/被作为 `var` 实参传出"的符号（`addCaptureName` 与调用点的 `var` 实参都在本函数体里看得见），要么保持单遍建图、在建完之后用一次**图级转换**做 SSA（那时"逃逸"由图的形状直接判定：cell 的用途只有 `:var/get`/`:var/peek`/`:op/assn*`，且不在任何捕获表里）。两条路对**最终图**是一样的，对**建图期的中间状态**与诊断（`builder_diagnostics.cpp` 里 `__var_peek__` 那条检查）不一样；我倾向后者（图级转换，判定就是图的性质，且能在同一处把不逃逸的 cell 一次收干净），但这一句要作者说。**这一片我没有落 SSA 代码**，等着这句。

## 补三：内存规划那条规则原文，与它缺的一句（2026-10-25，按裁定要求逐字引）

协调者要求先翻设计原则里"原地更新 / 内存规划"那条统一规则的原文再决定"别自己发明"。原文（`docs/design-principles.cn.md`，只读，逐字）：

- **§10.5 内（第 652 行）**："**计数为 1 时，"返回新值"的操作原地进行，这是有保证的行为。**"（同段还说：内存管理分三层——编译期规划、数据块（带计数）、分代 GC；"不含引用、地址固定、带计数"是数据块的定义。）
- **§3 内（第 167 行）**："**可以给以 `var` 开头的路径赋值**：`v.f = e`、`v[i] = e`、`v[i].f = e`、`v[i] += e` 等，含义是"用改好的新值整体替换 `v`"（等价于 `v = set(v, i, set(v[i], f, e))`），值语义不变：`let a = v` 之后再改 `v`，`a` 不变。**没有别人共享时原地修改（10.5 的保证）**。"
- **§7 内（第 257 行）**："**张量的 `t += u` 是算出新张量再重新绑定**，与数值一致，不原地修改。要原地修改用显式的算子。"（与裁决 `compound-assignment-on-a-tensored-array` 的路线 1"`a op= b` 就是 `a = a op b`"一致。）

**规则缺的那一句**：上面三条说的是**运行时的保证**（"计数为 1"由数据块的计数在运行期判定）。SSA 之后图上没有 cell、也没有计数，编译器在**图层面**许可"原地进行"要有自己的判据，而设计原则没有写这一句。今天这个判据挂在 cell 上（`:op/assn_owned` 置位、`:var/get` 清位、`:var/peek` 不清、`:var/take` 按位复制）；SSA 之后 cell 不在了，要作者补的是一句**图层面判据**，形如：

> 一个"返回新值"的操作可以在原地进行，当且仅当**它的结果（这一版的值）在图上没有别的使用者**，也不是函数的结果、捕获端口或导出端口；此时这一版的值被视为计数为 1（10.5 的运行时保证由此落到图上）。

这句话要作者给（我不发明它的措辞与边界）。**受影响的条目**（读法 C 里那一族，主机清单 `D:\Projects\Camel-wt\notes\h437-cv-fails.log` 可复算）：`string_plus_assign`4（输出 `abab`：`:var/take` 把别人还 refer 的数组交出去原地追加）、`path_assigned`4、`element_assigned_copy`1、`nested_array_type`2（`[T]` 别名条目）；它们今天靠 cell 上的标记，SSA 之后要么按上面那句图判据走，要么保留 cell（即归入"逃逸"以外的第三类）。