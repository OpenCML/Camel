# 第 6 步计划：Camel 一侧的图 API（只读调研，未写代码）

基线：fsh-dev `226e9e66`。依据：图存储第二版 §3–§5、`docs/design/rewrite/README.cn.md`（已定稿）、设计原则 §9，以及下面"现状"里逐条读到的代码。区域（R0/R1）在 gs/wide，本计划不依赖它，只在 §7 标出接口。

## 0. 结论（先读这一节）

1. **用原生算子 + 不透明句柄类型实现，不改语言核心。** 新建原生模块 `graph`（与 `tensor`、`tree` 同一机制：`BuiltinModule` + `OperatorGroup` + `OtherType`）。Camel 写的遍调用的是普通算子，每次调用直接读冻结图或事务里的草稿，不复制。第一阶段**不需要**新语法；`rule`/`analysis` 是第三阶段，在这套底层 API 上做编译器前端。
2. **遍的类型是 `(Program) => Program!`**，与 C++ 的 `apply(const Program*) -> const Program*` 一一对应；`!` 是已有的错误值。三种登记：C++ 遍不变；Camel 遍由模块导出、用 `graph::pass(name, fn)` 登记，流水线上写 `mymod::simplify` 与 C++ 的模块遍同名空间（`registerModulePass` 路径）；函数到函数的变换 `graph::of(f)…graph::to::<F>` 是同一套的语法糖。
3. **事务 = 现有的 `RuntimeGraphDraftSession`**，提交 = 它的 `commit`，校验 = 它已有的 `validateRuntimeGraphPayload/Calls`；Camel 里提交返回 `Program!`，校验失败是错误值，不是崩溃。**缺口**：现行校验没有类型相容、无环、区域合法性检查，也没有乐观并发的冲突检测（见 §4、§7）；"Camel 造不出坏图"的红线要先补这两样才成立，列为 C2。
4. **第一个证明用的 Camel 遍：整数中性元消除（`x+0`、`0+x`、`x-0`、`x*1`、`1*x`）**，与 C++ `std::opt::algebra` 的整数中性元分支逐字节比对输出；第二个是"无使用者的纯节点删除"。理由见 §8，不选完整 DCE（它含 gate 旁路等，不是"小"）。
5. 提交顺序 C0–C9 见 §9，每步可单独合并、可测。

## 1. 现状（已核实）

| 事实 | 位置 |
|---|---|
| 遍是 `GraphIRPass::apply(const Program*, ostream&) -> const Program*`，nullptr 表示消费 | `include/camel/execute/pass/base.h` |
| 模块遍：`registerModulePass("tensor::fuse", factory/descriptor)`；查找先查内置 `std`，不可遮蔽；参数 `name(k=v)` 是流水线通用语法 | `src/execute/pass.cpp` 886–980，`pass/params.h` |
| 改写入口：`RuntimeGraphDraftSession(ctx, program)`，`edit(g)` 取草稿，`commit()` 返回新 `Program*`；同一会话内编辑、整体提交 | `include/camel/runtime/draft_session.h` |
| 提交只重编码改过的图并与未改的共享版本（identity 的增量提交）；新旧图用 `sameContent` 判断是否真的变了 | `src/runtime/draft_session.cpp` 1040–1190 |
| 提交时校验：`validateRuntimeGraphPayload`（引用有效、邻接一致、BRCH/JOIN、GATE、臂、类型表）、`validateRuntimeGraphCalls`（实参数、LOOP 接口）；**没有**类型相容检查、无环检查（无环在取执行序时才暴露）；**没有**读写集冲突检测 | 同上 1134–1140；调研报告 §1.4 |
| 草稿 API 很宽：`addNode/eraseNode/rewriteNode/setNorm/With/CtrlInputs/replaceAll*Uses/materializeStaticValue/…`；邻接（使用者表）随改动维护；`revision()` 标识引用变化 | `include/camel/runtime/draft.h` |
| 冻结图只读访问器：`node(ref)`、`normInputsOf/withInputsOf/ctrlInputsOf`、`uses()`（CSR 使用者索引，缓存在版本上）、`branchArmsOf`、`staticSlots`；引用按 `GraphId`，经 `Program::calleeOf/loopCondOf/…` 解析；`ExecPlan` 缓存在版本上 | `graph.h`、`program.h`、`exec_plan.h` |
| 事实库：`registerKind<T>(KindSpec)`，键 `Anchor{program,graph,node}`，戳校验，持久化文本 | `include/camel/runtime/facts.h`、`core_facts.h` |
| 节点属性（标签）：驻留的标签集合编号 | `include/camel/core/node_attrs.h` |
| 原生模块可以导出算子组和不透明类型（`Tensor`、`Map`、`Set` 是 `OtherType`），算子带类型解析器、纯度/资源声明 | `modules/tree/module.cpp`、`modules/tensor` |
| 运行 Camel 代码于另一个程序之上的先例：`std::macro` 的 `MacroEvaluator(context, program)` 在遍里求值 Camel 函数，并要求求值与草稿不同时存在（safepoint 会移动对象） | `src/execute/macro/macro.cpp` 开头注释 |
| 枚举（H1）已实现：`type E = enum {…}`、`match`；联合类型 `A\|B` 与 `T?` 已退役 | `handover note ⟨d80a283⟩ (H1)` |
| 现有遍都是 C++：`std::opt` 等通过 `RuntimeGraphDraftSession` + 草稿；没有任何地方以 Camel 值的形式暴露图 | `src/passes/` |

## 2. 暴露的值类型

全部是不透明的原生类型（`OtherType`，由图/事务/程序的生命周期保证有效），**可比较、可作 `Map`/`Set` 的键**（设计 §5.3）。

| Camel 类型 | 含义 | 实现 |
|---|---|---|
| `graph::Program` | 不可变快照（`Program*`） | 句柄；由 GC 追踪的函数值/事务保活 |
| `graph::Graph` | 某个程序里的一张图 = `(Program*, GraphId)`；**身份是 `GraphId`**，相等比较 id（同一 id 的不同版本相等） | 两个字 |
| `graph::Node` | `(Graph, NodeId)`；`NodeId` 跨版本稳定 | 三个字 |
| `graph::Value` | 值（设计 §3.1：输入指向值）。当前"一值一写法"，`Value` 与 `Node` 同表示；多写法落地（R2 以后）再分开。**现在就定类型名**，避免以后改调用方 | 同 Node |
| `graph::Region` | 区域（R0/R1，gs/wide）。**第一阶段只读**：`region_of(n)`、`kind`、`parent` | `(Graph, RegionId)` |
| `graph::TypeRef` | 被分析程序里的类型，与写遍程序自己的类型分开（设计 §5.3） | 指向驻留的 `Type*`；`type_of::<T>()` 取 |
| `graph::NodeKind` | `enum`：`Data, Port, Cast, Copy, Fill, Accs, Brch, Join, Call, Func, Oper, Sync, Gate, Loop`（不含保留的 `Bind`/`Dref`） | 在 `graph` 模块里声明的 Camel `enum`，与 C++ `GCNodeKind` 一一映射；映射表放在一处并有测试对拍 |
| `graph::Op` | 算子的声明身份。`graph::op(n) == matmul` 比较 `OperId`，不比较 URI；`matmul` 是模块导出的算子组值 | `OperId` |
| `graph::Txn` | 事务（草稿会话）；线性使用，提交后失效（再用是错误值） | 句柄 |
| `graph::Tag` | 节点标签（`#reassociate` 等）的集合/成员 | `AttrSetId` |

读取 API（**纯函数，不需要事务**，直接读冻结图）：`graphs(p)`、`root(p)`、`name(g)`、`nodes(g)`（按 `NodeId` 序，**跳过 Dead**）、`kind(n)`、`op(n)`、`type_of(n)`、`inputs(n)`/`with_inputs(n)`/`ctrl_inputs(n)`、`users(n)`（用 `uses()` 索引，不扫描）、`constant::<T>(n)`、`callee(n)`（经 `Program` 解析）、`tags(n)`、`origin(n)`（源码位置）、`is_pure(n)`、`fact::<K>(n)`。不返回字符串 URI；打印用 `graph::show(n)`，只用于诊断。

可选值：设计稿写 `f32?`，但 `T?` 已退役（`ReservedTypeQuestionRetired`）。**问题 Q1**：返回"可能没有"用什么？建议标准库的 `Option<T>`（H1 的枚举）；错误用 `T!`。

## 3. 遍的写法、登记与运行

### 3.1 写法

```camel
import { Program, Txn, NodeKind, commit, nodes, kind, op, inputs, replace_uses } from graph
import { Add, Mul } from std::ops          // 算子组值，比较身份

export func neutral(p: Program): Program! {
    let t = Txn::open(p)
    for g in graph::graphs(p) do
        for n in graph::nodes(g) do
            if graph::kind(n) == NodeKind::Oper && graph::op(n) == Add {
                match graph::constant::<int>(graph::inputs(n)[1]) {
                    Option::Some(0) => t->replace_uses(n, graph::inputs(n)[0])
                    _ => ()
                }
            }
    return t->commit()          // Program!
}
export let pass = graph::pass<neutral>
```

函数到函数（设计 §5.1 第 1 层）：`graph::of(f)` 打开只含 `f` 及其可达子图的事务，`graph::to::<F>(g)` 提交并取回同类型函数值，签名不符是错误值。命令行遍作用于入口函数，用同一对函数；装饰器、`#macro` 将来复用。

### 3.2 登记

1. **C++ 遍不变**（`std::` 与 `registerModulePass`）。
2. **Camel 遍**由 `graph::pass<fn>` 得到一个遍值；模块加载时把导出的遍值登记为 `mymod::name`（走现有 `registerModulePass(descriptor)`，参数声明用函数的 `with<…>` 参数，沿用 `name(k=v)` 语法与"遍参数是通用语法"的原则）。优先级与现状一致：内置 `std::` 先于模块遍，不可遮蔽。
3. 流水线：`camel prog.cml std::macro mymod::neutral std::opt std::nvm`。`mymod` 是 Camel 模块，**与被处理的程序是两个程序**（见 3.3）。

### 3.3 怎样运行 Camel 遍（关键难点）

遍的 Camel 代码需要一个**可执行的程序**，被处理的程序是**数据**。两者不能是同一个 `Program`（遍运行时不能被自己的事务改写）。方案：

- 加载 `mymod` 时把它编译成**自己的 `Program`**（工具程序），登记时只持有这个程序；遍的 `apply(program)` 创建 `NodeVM` 调用（或 `MacroEvaluator`），把被处理的 `Program*` 作为实参句柄传入，返回值是新 `Program*`。
- 调用用 **NodeVM**（参考实现，有断点钩子，便于调试遍）；高级用户可选 `fvm`（遍很大时）。接口沿用 `macro` 的做法：求值与草稿不同时存在；在安全点（会移动对象）之间，遍只持有句柄（`Program*`/`GraphId`/`NodeId` 都不是堆对象引用，不被移动影响），**读取不拿草稿指针**，写入只发生在 `Txn` 内。
- **问题 Q2**：现在一个 `Context` 只有一个"当前运行时程序"（`currentRuntimeProgram`）。是否允许同一 `Context` 同时加载工具程序？建议：工具程序由 `Context::reserveProgram()`（已存在）取得，不安装为当前程序，跟随上下文生命周期。需要确认 `materializeRuntimeProgram` 对一个模块（而不是入口）能产出独立程序；若不能，C3 需要先做这件事。

## 4. 事务与提交

| 操作 | 语义 | 落到哪里 |
|---|---|---|
| `Txn::open(p)` | 基于快照 `p` 的私有工作区 | 新建 `RuntimeGraphDraftSession(ctx, p)` |
| `t->add(g, kind, …)` / `t->replace_uses(old, new)` / `t->erase(n)` / `t->set_inputs(n, …)` / `t->set_tag(n, …)` / `t->materialize(g, constant)` | 按图编辑；返回新建的 `Node`；**节点编号在事务内稳定**（墓碑，不复用） | `GraphDraft` 对应方法；用 `replaceNode`/`appendControlDependency` 那一层的**安全组合**（见下） |
| `t->commit()` | 整体校验，成功得新 `Program`，失败得错误值，快照不变 | `session.commit()`，异常转换为错误值 |
| `t->abort()` / 离开作用域 | 丢弃 | 析构 |

要点：

- **只暴露带语义的编辑，不暴露原始邻接改写。** 例如 `replace_uses` 不是 `replaceAllValueUses`，而是 `replaceNode`（把被替换节点的控制前驱与"有序来源"转给使用者，C++ 的 dce/cse/algebra 都用它，保证"规则永远不会悄悄丢掉顺序约束"，rewrite 设计 §2.4）。低层 `set_inputs` 保留，校验兜底。
- **读写集记录**：`Txn` 记录读过/写过的 `GraphId`（读 API 在事务内也记；纯 `Program` 读不记）。用于乐观并发（下）。
- **提交的校验范围**：改过的图 + 受影响的直接依赖者（签名变化时的调用者）——增量提交已有"被替换的图"集合与反向引用，复用，不新增扫描。
- **乐观并发（设计 §4.3.1）**：现状没有。冲突检测所需的只是"事务开始时的快照版本 + 读写集 + 提交时在最新快照上比较 `GCGraph::revision()`"；`Program::version()` 与逐图 `revision()` 都已存在。**C4** 加：`commit` 在 `program_ != context.currentRuntimeProgram()` 的情形下，用读写集对最新快照做比较，冲突返回 `Conflict` 错误值，由调用者重试；并行组合子 `graph::par_map` 按列表位置顺序提交，冲突者在前面结果之上重做（确定性）。**问题 Q3**：是否第一阶段就要并行？建议不要：C4 只做检测与重试，`par_map` 放第二阶段。
- **错误值**：`GraphError` 枚举 `Invalid(reasons)`、`Conflict(graphs)`、`UsedAfterCommit`、`Capacity`；`reasons` 是带源码位置的诊断列表，直接来自校验器。

## 5. 事实与分析

- **查询**：`graph::fact::<K>(n)`，`K` 是已注册的事实种类（`registerKind`）。第一阶段暴露现有内建种类（常量、形状、纯度/效应摘要等，见 `core_facts.h`），按需计算、戳校验，Camel 侧只看到值。
- **事务内读事实**：事务中途改过的图，其事实视为无效，重新计算（`Stamp` 已含 `closureRevision`）；不暴露脏读。
- **自定义事实/分析**属第三阶段（`analysis` 声明，rewrite 设计 §4）：编译成 `registerKind` 的 `KindSpec`（依赖由引擎自动记录）。第一阶段不开放 Camel 侧 `registerKind`，避免过早固定戳/依赖语义。
- **等价饱和/多写法的事实**依赖 R2 之后的存储，第一阶段不涉及。

## 6. C++ 遍与 Camel 遍共存

- 流水线里同一序列混用，`Program` 是唯一的接口，**不存在"Camel 图"与"C++ 图"**。
- **同一份语义的 C++ 实现是黄金参照**：每个用 Camel 重写的遍保留 C++ 版本，测试比对输出（§10）。标准库里 Camel 版本成熟后是否替换 C++ 版本，需另行决定（**Q4**：建议不替换热遍 `std::opt`，只替换"策略/胶水"）。
- **事实与缓存**：Camel 遍经 `Txn` 提交，走同一个增量提交与同一个事实戳，所以 `ExecPlan`、`GCUseIndex`、事实库对两种遍同等有效。
- **诊断**：Camel 遍抛出的错误值带遍名与节点的源码位置；C++ 遍沿用现有。`std::inspect` 能列出登记的 Camel 遍。
- **装饰器/`#macro`**：设计宣称它们复用同一套（§5.1）。现在 `std::macro` 是 C++ 的求值器；复用留给第二阶段，不在本计划内。

## 7. 安全：Camel 遍造不出坏图

红线（设计 §5.4）由**提交校验器**强制，Camel 侧没有任何绕过它的接口。校验器分层：

| 层 | 内容 | 现状 |
|---|---|---|
| 结构 | 引用有效、邻接一致、BRCH/JOIN/GATE 形状、臂 | 已有（`validateRuntimeGraphPayload`） |
| 调用 | 实参数、LOOP 接口 | 已有（`validateRuntimeGraphCalls`） |
| 类型 | 每条输入与算子/节点期望的类型相容；节点值类型与它的输入推出的一致 | **缺**。C++ 遍靠自己小心。需新增，增量地只查改过的节点 |
| 无环 | 数据/控制/with 输入无环（执行序存在） | **缺**（暴露在 `computeExecutionOrder` 抛异常）。提交时对改过的图跑一次，复用同一实现 |
| 区域（R0/R1） | 会出错/有副作用的节点在它应在的控制区域内；区域不部分重叠 | gs/wide 落地后接入；接口先留 |
| 效应摘要 | 摘要与节点内容相符（原则 9.7） | 已有（`effects.h`），确认走提交路径 |
| 许可 | 新写法在许可集合内（3.4）| 仅规则引擎层，第三阶段 |

- 校验失败**不抛异常到 Camel 程序之外**：转成 `Invalid` 错误值；C++ 内部的 `ASSERT`（程序员错误）仍是崩溃，但 `graph` 模块的每个入口先检查句柄有效性（越界 `NodeId`、已提交的 `Txn`、跨程序混用），返回错误值。
- **资源上限**：遍里的死循环或超大结果由现有的递归深度/内存限制处理，不另设；提交的容量错误已是 32 位后的"耗尽"类诊断。
- **"改写不改语义"不保证**（原则 9），所以不做运行等价性检查；规则引擎的随机检验（rewrite §2.8）属第三阶段。

## 8. 第一批 Camel 遍（证明）

**遍 A：整数中性元消除。** 范围与 C++ `algebra.cpp` 的整数分支完全一致：对 `int32/int64` 的 `add/sub/mul`，`x+0 → x`、`0+x → x`、`x-0 → x`、`x*1 → x`、`1*x → x`（中性元取算子声明的 `neutralBits`，即 `graph::neutral(op, type)`，不写死 0/1）；只在节点可替换（不是出口/臂锚）且不被原地写时生效；替换用 `replace_uses`（= C++ `replaceNode`）。
- **为什么选它**：规则小而完整，涉及模式（算子身份、常量、类型）、替换、控制依赖转移三件事，足以检验 API；C++ 参照是独立函数，边界清楚。
- **逐字节比对**：在专门构造的程序集上（只含这类节点，及少量不应命中的反例：浮点、written-in-place、arm 锚），`camel p.cml mymod::neutral std::gir` 与 `camel p.cml std::opt::algebra std::gir` 输出相同。注意 `std::opt::algebra` 还会做浮点/乘积合并，所以语料不含它们；这一点写进测试说明。另加 `progen` 随机程序的差分：两边只比"无浮点"的子集，并把 C++ 一侧限定在整数中性元（给 C++ 遍加一个只开整数分支的内部开关，仅测试用）——**Q5**：是否接受给 `algebraDraft` 加这个测试开关？不接受则只比专门语料。

**遍 B：无使用者的纯节点删除**（`dce` 的"值-only 无使用者"一半，不含 gate 旁路/SYNC）。检验 `users(n)` 索引、`erase`、控制依赖转移（`replace_uses` 的空替换形式 = C++ `replaceNode(draft, id, kInvalidNodeRef)`）与"出口/臂锚不可删"。对照 `std::opt::dce` 在无 gate/sync 语料上的输出。

两个遍合起来约 80–120 行 Camel，用来出 API 的缺口清单（比文档推演可靠）。

## 9. 提交顺序

| # | 内容 | 依赖 | 可单独合并 |
|---|---|---|---|
| C0 | `graph` 模块骨架：`BuiltinModule`、类型 `Program/Graph/Node/Value/TypeRef/NodeKind/Op/Tag`（句柄、比较、可作键）、`NodeKind` 与 `GCNodeKind` 对拍测试 | 无 | 是 |
| C1 | 只读 API：`graphs/nodes/kind/op/type_of/inputs/users/constant/callee/tags/origin/is_pure/show`，直接读冻结图；`Option<T>` 若 Q1 采纳先入标准库 | C0 | 是 |
| C2 | **校验器补全**：类型相容、无环（提交时只查改过的图），C++ 单元测试 + 对现有遍的回归（所有 C++ 遍通过新校验）| 无（独立于 Camel） | 是，**先合** |
| C3 | 运行 Camel 遍：工具程序加载（Q2）、`graph::pass`、`registerModulePass` 路径、NodeVM 调用、错误值传出；空遍与恒等遍 | C0–C1 | 是 |
| C4 | `Txn`：`open/commit/abort`、读写集、冲突检测（比较 `revision()`）、`GraphError`；编辑 API 的第一批（`replace_uses/erase/set_inputs/set_tag/materialize`）| C2、C3 | 是 |
| C5 | 遍 A（整数中性元）+ 差分测试 | C4 | 是 |
| C6 | 遍 B（无使用者纯节点删除）+ 对照 | C5 | 是 |
| C7 | `graph::of/to`（函数到函数）、`seq/fixpoint/try` 等策略组合子（标准库普通函数）| C4 | 是 |
| C8 | 事实查询 `fact::<K>`、事务内失效 | C1、C4 | 是 |
| C9 | 文档：语言参考"写遍"一节、`docs/design/graph-store` 的 5.x 与实现对齐；API 缺口清单（来自 C5/C6）| — | 是 |

**第二阶段**（本计划之外，各自出计划）：`rule` 声明（编译成匹配程序，本机引擎；rewrite 设计 §2）、`analysis` 声明（§4）、`par_map` 并行提交、饱和与提取（依赖多写法存储）、装饰器/`#macro` 复用。

## 10. 测试

- **C0**：类型可比较/作键、`NodeKind` 对拍（枚举两边遍历，数量与名字逐一相等，防止 C++ 加了种类而 Camel 没跟）。
- **C1**：对一组程序，Camel 读出的节点/边/使用者与 `std::jgir` 输出一致（把 JSON 导出当作黄金参照，解析后逐项比）。
- **C2**：构造非法图（输入类型不符、环、悬空引用）经草稿 API 提交，期望 `Invalid` 且列出位置；全部 C++ 遍的回归套（`feat/opt`、`linear`、`modules`）零新增失败——这是该步的风险所在。
- **C3/C4**：遍里故意造坏图 → 错误值而非崩溃；提交后再用 `Txn` → `UsedAfterCommit`；两个事务基于同一快照改同一张图 → 后提交者 `Conflict`；改不同图 → 都成功且合并。
- **C5/C6**：与 C++ 遍逐字节差分（§8）；随机程序（`progen`）的子集差分；反例集（应不命中）。
- **全程**：每步 `smoke`、`feat/opt`、`feat/compile`、`feat/modules` 快层；`feat/opt/screens.plan.toml` 不受影响（C++ 遍路径不变）。
- **确定性**：同一输入连跑两次，输出相同（遍里不得依赖哈希迭代序；`graphs()`/`nodes()` 按编号序，`Map` 遍历序见语言规定）。

## 11. 性能预期

- **读 API**：每次调用是一个原生算子调用，nvm 上约 100–200 ns；O(1) 的访问器直接读冻结图/使用者索引，无复制。遍 A 在 50k 节点程序上：约 5 万节点 × 每节点 ~6 次调用 ≈ 60 ms 一次扫描（C++ 约 2 ms）。**可接受**：热的化简应是 `rule`（原生匹配程序，第二阶段），Camel 底层 API 留给分析/降低/切分这类"少量节点、复杂逻辑"的遍。
- 事务提交成本与 C++ 遍完全相同（同一个增量提交），Camel 额外成本只在遍体内。
- 不变量：**不使用 Camel 遍的程序零开销**（`graph` 模块按需加载；无登记则无工具程序）。红线：C2 的校验器增加后，`feat/opt` 与规模程序的 `std::opt` 时间在噪声内（校验只查改过的图；先测 `ab.py`）。
- fvm 运行 Camel 遍（大遍）可再快数倍，非第一阶段目标。

## 12. 风险与待定问题

**风险**
1. **C2 的校验器可能暴露现有 C++ 遍的隐性违规**（类型不相容的中间态、环）。缓解：C2 先以"只记录不拒绝"模式跑全套，列出违规再决定修遍还是放宽校验。
2. **对象移动与句柄**：Camel 遍的 safepoint 会移动堆对象，而 `Graph/Node` 句柄是整数对，不受影响；`Txn` 内部持有的草稿在堆外。需要在 C3 的测试里用 GC 压力构建确认（"不要在本机跑 GC 压力"→请施工方在里程碑时跑）。
3. **工具程序的生命周期**（Q2）与 `Function` 持 `(Program, GraphId)` 的关系：遍值是函数值，指向工具程序；工具程序必须在流水线期间存活。
4. **NodeVM 对大遍的递归深度**：用 `for` 循环写遍避免深递归；文档注明。
5. **区域接入**（R0/R1）会改变 `Node` 的"所在区域"语义和校验层；§2、§7 已留位，但 C4 的编辑 API 在区域上线后需要补"节点放在哪个区域"参数。

**需要你裁定的问题**
- **Q1** 可选值怎么写：标准库 `Option<T>`（建议）还是在 `graph` 模块里用 `T!`/哨兵？设计稿的 `f32?` 与已退役的 `T?` 冲突，是否要更新设计稿 §5.3？
- **Q2** 工具程序：同一 `Context` 里是否允许同时加载（建议用 `reserveProgram`，不安装为当前）？是否接受 `materializeRuntimeProgram` 先支持"以模块为根"？
- **Q3** 第一阶段是否包含并行（`par_map`）：建议只做冲突检测与重试，并行放第二阶段。
- **Q4** Camel 版成熟后是否替换 C++ 版：建议不替换 `std::opt` 的热遍，只替换策略/胶水。
- **Q5** 给 `algebraDraft` 加仅测试用的"只开整数中性元"开关，以便与遍 A 做随机差分？
- **Q6** C2（校验器补全）是否单独作为一个包先做、由别的车道并行？它不依赖 Camel 侧，且是红线的前提。
- **Q7** 流水线里 Camel 遍的名字空间：`mymod::name` 沿用模块遍写法（建议），还是加显式前缀如 `camel::mymod::name` 以免与 C++ 模块遍混淆？
