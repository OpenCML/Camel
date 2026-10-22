# 模块 `graph`：把程序当作值

`graph` 是一个 C++ 写的模块（`modules/graph`），让 Camel 程序读取并改写另一个程序的图：读取图、在事务里编辑、提交成新的 `Program`；用 Camel 写的遍是一个 `(Program) => Program!` 函数，按 `<模块>::<函数>` 写进流水线。设计依据见 `docs/design/graph-store/README.cn.md` 第 4、5 节；本文说明实现里每一块放在哪里、行为是什么、有哪些限制。逐个算子的签名与例子见 [`docs/stdlib-reference.md`](../stdlib-reference.md) 的 `graph` 一节（由 `stdlib/signatures/graph.cml` 生成）。

## 类型

| 类型 | 是什么 | 实现 |
| --- | --- | --- |
| `Program` | 不可变的程序快照 | 运行时的 `Program` 对象本身，按身份比较 |
| `Graph`、`Node`、`Op`、`TypeRef`、`Region`、`Tag`、`Txn` | 命名某个 `Program` 里的东西 | 一个被追踪的 `Handle` 对象（`modules/graph/handle.h`）：种类、所属的 `Program`、`GraphId`、序号、`Type*`；`Txn` 是外部资源对象 |
| `Value` | 值；目前与 `Node` 同一个类型 | `Node` 的另一个名字 |
| `NodeKind` | `Data, Port, Cast, Copy, Fill, Accs, Brch, Join, Call, Func, Oper, Sync, Gate, Loop` | Camel 的 `enum`（`nodekind.cpp`）；`nodeKindTag` 对 `GCNodeKind` 做穷尽的 `switch`，图模型新增一种节点时这里编译不过，直到它被放进枚举或明确排除（`Bind`、`Dref`、`Dead` 不是程序里看得见的节点） |
| `RegionKind` | `Root, Arm, LoopCond, LoopBody, Inline, Block, Dead` | Camel 的 `enum`（`regionkind.cpp`），与 `GCRegionKind` 一一对应；新增一种区域时 `regionKindTag` 编译不过 |

句柄用 `==` / `!=` 比较（模块通过 `__eq__` / `__neq__` 算子组为自己的类型定义，设计原则 4.3）。两个句柄相等，当且仅当种类、`Program`（或草稿）、图与序号都相同。`id(h)` 给出图的 `GraphId`、节点的 `NodeId` 或算子的 `OperId`，在同一个 `Program` 里稳定，Map 的键用它。

一个句柄持有它读出时的 `Program`，所以经过它读到的永远是那个快照；`Handle::updateRefs` 追踪这个 `Program`，和函数值追踪它的 `Program` 一样。指向草稿的句柄是唯一的例外：它持有草稿而不持有 `Program`，只在改写期间的 `when` 条件里出现（见“读取”）。

## 读取

读取算子都是纯的，直接读冻结的图（`GCGraph`）：`graphs`、`root`、`name`、`nodes`（`NodeId` 序，跳过 `Dead`）、`inputs` / `with_inputs` / `ctrl_inputs`、`users` / `value_users`（经图缓存的使用者索引）、`kind`、`op` / `op_name`、`is_pure`、`is_anchor`、`is_constant`、`constant(n, 样本)`、`type_of` / `type_name`、`callee`、`tags`、`origin`、`show`。可能没有的结果是 `T?`，用 `match` 取出。`constant` 的第二个实参是类型的样本，值不被读取：原生算子的类型实参用这种形式（见 `json::decode_as`），`::<T>` 只属于 Camel 的泛型函数。

区域：`region_of(n)` 给出节点所在的最内层区域，每个节点都有一个；区域 0 是函数体，只有体、没有区域表的图（`regionCount() == 0`）也返回区域 0，不会返回 `null`。`region_kind(r)` 给出 `RegionKind`，体的种类是 `Root`；`region_parent(r)` 给出父区域（体没有父区域，返回 `null`）。区域列在 R2b 落地后由分支臂与内联共同写入；区域规则（2）（3）（节点只读自己区域或祖先区域的节点、JOIN 的 with 输入可读本 BRCH 的 Arm 区域的结果、加边的默认区域取最深的输入区域）由分支臂校验执行（`src/runtime/branch_arms.cpp` 的 `verifyBranchArmRegions`），`EdgeChecker` 只检查等价写入在同一区域。

事实（`docs/technical/40_graph_facts.md`）经上下文的事实库读取：`effects(g)`（`external`、`writes_variables`、`may_fault`、`may_not_terminate`）、`shape(n)`、`permissions(n)`。第一次问时计算，之后只要它读过的图没有变就不重算。

句柄也可以指向**正在改写的草稿**（`Handle::createInDraft`，H38）：`when` 条件收到的 `Node` 就是匹配在当前草稿里的节点，不是冻结程序里的版本。读算子对两种来源给同样的答案（`NodeView`：冻结的 `GCNode` 或草稿的 `DraftNodeHeader`），包括 `inputs` / `with_inputs` / `ctrl_inputs`、`users` / `value_users`、`kind`、`op` / `op_name`、`is_pure`、`is_anchor`、`is_written_slot`、`is_constant` / `constant`、`type_of` / `type_name`、`tags`、`origin`、`show`。两处限制：`callee` 在草稿上返回 `null`（草稿不属于任何一个 `Program` 版本，没有函数值可读；今天也没有规则模式匹配 `CALL`）；`is_written_slot` 在草稿上每次现算（冻结的图按版本缓存，草稿按 `mutations()` 缓存是 H39 的事）。草稿句柄不持有 `Program`：草稿属于尚未提交的会话，它的静态值与编译进规则的常量由引擎在条件调用期间注册的根集重定位（`rewrite.engineRoots`）。

## 事务

`open(p)` 在 `p` 上开一个 `RuntimeGraphDraftSession`，由一个外部资源对象持有；会话在提交或放弃时释放，没有人提交就由收集器在安全点释放。编辑只有两个，都带语义而不暴露原始邻接改写：

- `replace(t, old, new)`：把同一张图里的 `new` 放到 `old` 的位置并删除 `old`，用的是优化器的 `replaceNode`，所以 `old` 排过的顺序（控制前驱和它的值所来自的有序节点）保持不变。`old` 是图的锚（出口、输出、返回、分支臂首尾）、`new` 由 `old` 算出、或事务已结束，是故障。
- `erase(t, n)`：删除没有人读取其值的节点。

`add_constant(t, g, 值)` 创建事务自己产生的值：把值放进图的静态槽并返回持有它的 `Node`（数值、布尔或字符串；字符串复制进 `permSpace`）。草稿的静态槽是裸引用，NodeVM 的安全点可能在 Camel 代码运行时移动对象，所以 `RuntimeGraphDraftSession::traceStaticSlots` 在每个事务被追踪时把草稿静态槽里的指针重定位；字符串还要独立于追踪活下来，因为旧世代的资源包装对象不一定在每个安全点被追踪。常量折叠（`fold_constants::fold`）就是这样把 `a + b` 的结果做成新值再 `replace`。

`commit(t)` 用 `commitChecked()`：校验草稿（`docs/technical/17_graph_rewrite_api_contracts.md` 4.2 的边不变量），编码新 `Program`，它与旧的共享没有改的图。错误值的 `kind`：`GraphConflict`（事务读过或写过的图，在上下文的程序里已有别的版本）、`GraphInvalid`（编辑破坏了不变量，消息带图、节点与边）、`TxnUsed`（事务已结束）。`abort(t)` 丢弃事务。

## 函数到函数

`open_function(f)` 开一个只含函数值 `f` 的图及其可达子图的事务，`to_function(t, 样本)` 把它提交成**自己的** `Program`（运行中的程序不变）并返回样本类型的函数值。错误值除 `commit` 的以外有 `SignatureMismatch`（图的签名与样本不符）与 `ClosureNotEmpty`（函数捕获了值，新建的函数值没有闭包）。`program_of(f)` 与 `graph_of(f)` 读函数值。改写后的函数值在每个后端上都与原函数算同样的结果。

## 用 Camel 写遍

一个模块导出 `(p: Program) => Program!` 的函数，就是一个遍，流水线里写 `camel prog.cml mymod::fn std::nvm`。实现在 `src/execute/camel_pass.cpp`：

1. 遍的名字解析不到时，把模块当**程序**编译（`Module::compileAsProgram`，`DefinitionStore::libraryFunction`）：程序以这个函数为入口，模块级的 `let` 在遍运行前算好（设计原则 10.1）；
2. 它只接受唯一、非泛型、签名为 `(p: Program, ...with 参数) => Program!` 的函数；
3. 模块可以导入 C++ 模块，也可以导入 Camel 写的库（例如 `array`）：`compileAsProgram` 把传递导入闭包里的 Camel 模块声明进这个程序自己的 builder 并一起构建。导出表是**程序的一份**（`ProgramExports`，`Builder::exports()`），不在模块对象上：同一个模块对象可能同时是两个程序的一部分（被运行的遍读同一个库），第一次构建留下的导出名不会被清掉，也不会挡住第二次声明；两次声明各写各的表，各拿到本程序建出的图，第二次声明也不会改写第一次程序里的图（任务 H42）。
4. 函数的 `with` 参数就是遍参数（设计原则 4.4、9.3）：流水线写 `mymod::p(label='x', n=21, scale=2.5, flag=true)`，布尔、整数、浮点与字符串按类型转换后绑定；源码里没有默认值，所以每个都是必填，缺一个就是 `InvalidPassParameter`，消息列出声明；
5. `NodeVMSchedPass::callGraph` 在 NodeVM 上以被处理的程序为实参、以遍参数为 with 实参运行它；返回的 `Program` 与输入不同就 `installRuntimeProgram`；
6. 返回错误值是诊断 `GraphPassFailed`；模块编译不过、函数不是遍，是 `InvalidPassParameter`，消息只说哪条遍规格、哪个库编译不过；模块自己的第一条诊断作为**从属位置**附上（文件、行、列与原行），文本格式里就是该位置下的源码片段。诊断没有位置时（例如导入的库编译不过），原因才留在消息里。模块不存在或没有这个函数，遍保持未知。模块自己解析不过（还在编辑的文件）也走这里：`compileAsProgram` 把 `compile()` 抛出的诊断留在模块的诊断表里并返回失败，调用方把它作为遍规格的问题报告，异常不会穿出流水线。诊断表停在错误上限（`--timeit` 的上下文上限是第一条错误）时得到的是上限异常而不是 `Diagnostic`：`DefinitionStore::libraryFunction` 把它同样当作"这个库没编过"，否则它会在遍解析之前穿出去，用户看到的是裸的解析诊断而不是 `InvalidPassParameter`（H210）。

## 一次编译问的是哪些模块

一次编译的"有没有错"只在命令所问的模块上回答，报告也只打印那些模块的诊断：`run`、`check` 与 `--timeit` 的一轮问的是程序的依赖闭包（入口模块加逐层 import），`inspect --ops` 问的是上下文里的全部用户模块，因为清单少任何一个模块的算子都不完整（H172）。pass 列表在编译前装载的 pass 库不在程序里，它的失败由遍解析报成 `InvalidPassParameter`；它不能因为"上下文里恰好有个坏模块"而出现在程序的诊断里，这正是 H198 的范围与 H210 的报告要一致的地方（文本与 JSON 是同一份答案）。`Context::dumpModuleDiagnostics` 接收要打印的模块集合，命令不再自行遍历上下文拼装渲染。语言服务器的按需分析不解析遍列表，因此不会装载 pass 库；依赖模块的错误仍作为程序自己的错误报在 import 处，不需要另一套作用域。

## 对照与测试

`test/cases/modules/graph_pass/` 里有两个用 Camel 写的遍，各与一个 C++ 遍做差分比较（`test/plans/feat/modules/graph.plan.toml`）：`neutral::integers`（`x + 0`、`0 + x`、`x - 0`、`x * 1`、`1 * x`）对 `std::opt::algebra(neutral_only=true)`，`unused::values`（无人读取的常量与纯运算）对 `std::opt::dce(values_only=true)`。两侧的 `std::jgir` 转储逐图相等（`normalize_origins` 去掉源码 origin 号，它们因编译过遍的库而不同）。两个参数是为这个对照加的、文档化的遍参数（`docs/cli.md`）。`is_written_slot(n)` 读出节点的值是否可能被原地写（传给 `var` 形参或改自身操作数的算子，经 GATE 传递），两个遍都跳过这样的节点，差分程序里有 `var`。

`node_kinds_match_the_graph` 把一个带分支、闭包与折叠的程序的每张图的节点种类序列与 `std::jgir` 的转储逐一对过。

其余覆盖面：`imports::count`（导入 Camel 库的泛型函数 `array::flat_map`）与 `monomorphic::show`（直接调用单态导入函数 `libvals::nine` / `addOne`，返回真值）；`sharedvals::run`（同一个模块既是被运行的遍，又是被处理的程序导入的库，两边各用各的建图器，泛型 `twice` 在两边各有一个实例）与 `chain::show`（导入闭包传递：`chainlib` 再导入 `innerlib`）；`unparsable_pass::run`（遍模块解析不过，报 `InvalidPassParameter`，解析诊断作为从属位置给出文件、行、列与原行，不是异常结束）；同样的 `unparsable_pass_lib::run` 与各条命令路径（H210）：`run` 的解析失败与编译失败报告只打印程序自己的模块、`--timeit` 的文本与 JSON 都报 `InvalidPassParameter`、`camel: N errors` 只数程序自己的模块；`params::report` 的 `with` 参数，缺参、未知参数（带列号并列出声明）与类型不符的 `with` 参数诊断；`fold_constants::fold` 与 `typed_constants::make` 用 `add_constant` 造常量并提交（后者造 int、f64、bool、字符串四种，提交后逐一数回），两者的加强版都在 `CAMEL_GC_STRESS_SAFEPOINT` 下跑 safepoint；`regionprobe` 读出没有区域表的图的体（`region_of` 为区域 0，`region_kind` 为 `Root`）与内联后的 `Inline` 区域及父区域。
