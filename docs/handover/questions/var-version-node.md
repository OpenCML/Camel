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

## 附：这一片已经落地的两件事（与上面三处无关，不改任何图）

- `0a93ab2d1` `snapshotNodeInit` 保留被改写节点的 `orderInputs`/`syncInputs`（rewrite 不再丢"在我之前的语句"与"谁排在我前面"）。
- 链改同步边（第 5 节第 7 条 / 裁决"分支语句的链边是同步边"）已经写出来并可幂等重放（`D:\Projects\Camel-wt\notes\h437-s3-apply.ps1`），`test/plans/feat/linear` 由 741 pass 落到 689 pass / fail 53；53 条的门在同一处（内联之后再被 `cse`/`dce` 合并或删节点，靠"cell + 顺序边"表达的读被挪到写之后），所以它等这份裁定的第一处落地之后再提交。