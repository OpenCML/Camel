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

句柄也可以指向**正在改写的草稿**（`Handle::createInDraft`）：`when` 条件收到的 `Node` 就是匹配在当前草稿里的节点，不是冻结程序里的版本。读算子对两种来源给同样的答案（`NodeView`：冻结的 `GCNode` 或草稿的 `DraftNodeHeader`），包括 `inputs` / `with_inputs` / `ctrl_inputs`、`users` / `value_users`、`kind`、`op` / `op_name`、`is_pure`、`is_anchor`、`is_written_slot`、`is_constant` / `constant`、`type_of` / `type_name`、`tags`、`origin`、`show`。两处限制：`callee` 在草稿上返回 `null`（草稿不属于任何一个 `Program` 版本，没有函数值可读；今天也没有规则模式匹配 `CALL`）；`is_written_slot` 在草稿上每次现算（冻结的图按版本缓存，草稿按 `mutations()` 缓存是以后的事）。草稿句柄不持有 `Program`：草稿属于尚未提交的会话，它的静态值与编译进规则的常量由引擎在条件调用期间注册的根集重定位（`rewrite.engineRoots`）。

## 事务

`open(p)` 在 `p` 上开一个 `RuntimeGraphDraftSession`，由一个外部资源对象持有；会话在提交或放弃时释放，没有人提交就由收集器在安全点释放。编辑有三个，其中两个整节点，都带语义而不暴露原始邻接改写：

- `replace(t, old, new)`：把同一张图里的 `new` 放到 `old` 的位置并删除 `old`，用的是优化器的 `replaceNode`，所以 `old` 排过的顺序（控制前驱和它的值所来自的有序节点）保持不变。`old` 是图的锚（出口、输出、返回、分支臂首尾）、`new` 由 `old` 算出、或事务已结束，是故障。
- `erase(t, n)`：删除没有人读取其值的节点。

第三个不是一个节点，而是一个值。

`eval(t, n)` 把节点 `n` 的纯算子放在它自己的常量操作数上求值，返回持有结果的那个节点；没有值可算时返回 `null`。它要的是"在值上求值一个算子"这件事：`constant` 只读一个常量节点的值，`add_constant` 只把遍已经拿在手里的值造成节点，两个都不算。`null` 覆盖四种情形：`n` 不是纯运算（转换、拷贝、访问、填充不在这一步求值，C++ 的折叠每一种都有一个分支）、某个操作数不是图里的常量、算子没有内核、求值出故障。求值出故障不是错误值：必然出故障的常量表达式按设计原则 3.9 留在原处，所以 `eval` 报一条与 C++ 折叠同一条的 `ConstantExpressionFaults` 警告（`raisePassWarning`，位置是节点的 origin）再返回 `null`，程序有没有这个遍都一个样。结果节点是**图的**值：`eval` 与 `add_constant` 一样把值放进图的静态槽，草稿的静态槽是裸引用，所以这个值同样要由 `RuntimeGraphDraftSession::traceStaticSlots` 在安全点重定位。

`add_constant(t, g, 值)` 创建事务自己产生的值：把值放进图的静态槽并返回持有它的 `Node`（数值、布尔或字符串；字符串复制进 `permSpace`）。草稿的静态槽是裸引用，NodeVM 的安全点可能在 Camel 代码运行时移动对象，所以 `RuntimeGraphDraftSession::traceStaticSlots` 在每个事务被追踪时把草稿静态槽里的指针重定位；字符串还要独立于追踪活下来，因为旧世代的资源包装对象不一定在每个安全点被追踪。常量折叠（`fold_constants::fold`）就是这样把 `a + b` 的结果做成新值再 `replace`。

`commit(t)` 用 `commitChecked()`：校验草稿（`docs/technical/17_graph_rewrite_api_contracts.md` 4.2 的边不变量），编码新 `Program`，它与旧的共享没有改的图。错误值的 `kind`：`GraphConflict`（事务读过或写过的图，在上下文的程序里已有别的版本）、`GraphInvalid`（编辑破坏了不变量，消息带图、节点与边）、`TxnUsed`（事务已结束）。`abort(t)` 丢弃事务。

## 函数到函数

`open_function(f)` 开一个只含函数值 `f` 的图及其可达子图的事务，`to_function(t, 样本)` 把它提交成**自己的** `Program`（运行中的程序不变）并返回样本类型的函数值。错误值除 `commit` 的以外有 `SignatureMismatch`（图的签名与样本不符）与 `ClosureNotEmpty`（函数捕获了值，新建的函数值没有闭包）。`program_of(f)` 与 `graph_of(f)` 读函数值。改写后的函数值在每个后端上都与原函数算同样的结果。

## 用 Camel 写遍

一个模块导出 `(p: Program) => Program!` 的函数，就是一个遍，流水线里写 `camel prog.cml mymod::fn std::nvm`。实现在 `src/execute/camel_pass.cpp`：

1. 遍的名字解析不到时，把模块当**程序**编译（`Module::compileAsProgram`，`DefinitionStore::libraryFunction`）：程序以这个函数为入口，模块级的 `let` 在遍运行前算好（设计原则 10.1）；
2. 它只接受唯一、非泛型、签名为 `(p: Program, ...with 参数) => Program!` 的函数；
3. 模块可以导入 C++ 模块，也可以导入 Camel 写的库（例如 `array`）：`compileAsProgram` 把传递导入闭包里的 Camel 模块声明进这个程序自己的 builder 并一起构建。导出表是**程序的一份**（`ProgramExports`，`Builder::exports()`），不在模块对象上：同一个模块对象可能同时是两个程序的一部分（被运行的遍读同一个库），第一次构建留下的导出名不会被清掉，也不会挡住第二次声明；两次声明各写各的表，各拿到本程序建出的图，第二次声明也不会改写第一次程序里的图（任务 H42）。
4. 函数的 `with` 参数就是遍参数（设计原则 4.4、9.3）：流水线写 `mymod::p label='x' n=21 scale=2.5 flag=true`，布尔、整数、浮点与字符串按类型转换后绑定；源码里没有默认值，所以每个都是必填，缺一个就是 `InvalidPassParameter`，消息列出声明；
5. `NodeVMSchedPass::callGraph` 在 NodeVM 上以被处理的程序为实参、以遍参数为 with 实参运行它；返回的 `Program` 与输入不同就 `installRuntimeProgram`；
6. 返回错误值是诊断 `GraphPassFailed`；模块编译不过、函数不是遍，是 `InvalidPassParameter`，消息只说哪条遍规格、哪个库编译不过；模块自己的第一条诊断作为**从属位置**附上（文件、行、列与原行），文本格式里就是该位置下的源码片段。诊断没有位置时（例如导入的库编译不过），原因才留在消息里。模块不存在或没有这个函数，遍保持未知。模块自己解析不过（还在编辑的文件）也走这里：`compileAsProgram` 把 `compile()` 抛出的诊断留在模块的诊断表里并返回失败，调用方把它作为遍规格的问题报告，异常不会穿出流水线。诊断表停在错误上限（`--timeit` 的上下文上限是第一条错误）时得到的是上限异常而不是 `Diagnostic`：`DefinitionStore::libraryFunction` 把它同样当作"这个库没编过"，否则它会在遍解析之前穿出去，用户看到的是裸的解析诊断而不是 `InvalidPassParameter`（H210）。

## 标准库里的 Camel 遍

`stdlib/opt.cml`（模块 `opt`）是标准库里用 Camel 写的优化遍：`opt::fold`、`opt::cse`、`opt::dce` 分别与 C++ 的 `std::opt::fold`、`std::opt::cse`、`std::opt::dce values_only=true` 做同一件事，流水线里按模块名写作 `opt::fold`（遍名与遍参数的写法见 `docs/cli.md`）。`opt::fold` 把操作数全是常量的纯算子换成它的算子算出的值（`eval`），逐轮重复到没有可折的为止（`chain` 要把 `let b = a + 4` 的加法先折成常量，才能折 `let c = b * 2` 的乘法）。`opt::cse` 合并同类型同值的常量（`int`、`i32`、`bool`）与算子、类型、输入、区域、权限都相同的纯运算，每一组留下第一个节点；`opt::dce` 删除没人读取的常量、转换、拷贝、访问、填充与纯运算。三个遍都跳过值可能被原地写的节点与读取这种值的节点，并按轮次重复到没有可改的为止：合并两个常量会让它们上面的运算相等，删掉一个值会让读它的值变成没人读，折掉一个算子会让它上面的运算成为常量，所以一轮不够。

每轮一次事务。`cse` 与 `dce` 里同一轮每个待改的节点都被换成"输入完全相同且更早的那个节点"，而这样的两个节点不可能互为祖先——若 `b` 是 `a` 的传递使用者，`b` 的某个输入必在 `a` 的使用者闭包里，于是 `b` 的输入列不可能与 `a` 的相同——所以一轮一次 `commit` 就够，不需要 C++ 的 `reaches` 守卫（`replace` 自带的成环检查是第二道防线）。`fold` 没有这条保证，折出来的值是事务新造的节点而不是更早的节点，所以它只在真有可求值的节点时才 `open`，并且一轮里一个都没折成（必然出故障的常量表达式就是这种）时 `abort` 掉、原样返回：不然事务会造出一个与原来一模一样的图，递归就停不下来。

`fold` 求值的范围比它能替换的范围大：函数返回的那个常量表达式是图的 return、output 与 exit 锚点，而 `replace` 拒绝替换锚点（下一节的第三条）。C++ 的折叠在这里给 return 节点留了口子——没有控制输入时 `isFoldable` 放行，`replaceFoldResult` 再把三个锚点搬到折出来的值上——事务没有搬锚点的操作，于是 `opt::fold` 只求值、只为在必然出故障时报出警告，然后原样留下。两者对"必然出故障"的表达式都是这样：都求值、都报同一条 `ConstantExpressionFaults`、都原样留下，程序有没有这个遍都一样（设计原则 3.9）。

### `opt::cse` 与 `opt::fold` 对着 C++ 遍少了什么

上表之外，两个遍各自还有几处"只有 C++ 侧有"的分支：

- `opt::cse` 只合并 `int`、`i32`、`bool` 的常量（`bitEqualType`）：Camel 读不到一个标量的位（`bytes` 编解码的是数组与张量，`math` 没有 `signbit`），`0.0` 与 `-0.0` 用 `==` 相等却是图里两个值，按 `==` 合并会改程序，所以任何浮点常量都不合并。合并节点时它也不合并属性：`tags(n)` 只把属性读成文本、没有写的出口，属性文本不同的两个节点因此不合并，而 C++ 遍会把被合并节点的属性并到一起。
- `opt::fold` 只折叠**算子**（`NodeKind::Oper`）上的常量：转换、拷贝、访问与填充在 C++ 侧各有一个分支（`isFoldable`），而 `eval(t, n)` 要的是"在值上求值一个算子"，这四种不是算子；按操作数类型就能定出结果的算子（C++ 的 `typeFolder`）也没有出口，`graph` 模块求值的是拿在手里的值。
- `opt::fold` 不折**分支**上的常量：判定是常量时 C++ 遍会挑一条臂并把结构折掉，而在值上求值一个算子不是折它外层的分支结构。
- `opt::fold` 不折**返回位置**的常量表达式：C++ 遍的 `isFoldable` 在 return 节点没有控制输入时放行它，`replaceFoldResult` 再把图的 exit、output 与 return 锚点搬到折出来的值上；事务没有搬锚点的操作（`replace` 自带"替换锚点"的拒绝），所以这里只求值、只为必然出故障时报出警告，然后原样留下。`opt::fold` 与 `opt::order` 的其余差别见上表（`opt::order` 要删一条控制边，是事务没有的边手术）。

对照在 `test/plans/feat/opt/camel_passes.plan.toml`：C++ 一侧的图记成 golden，Camel 一侧交给快照测试已有的同构比较（`gir_dot_isomorphic`，`test/tools/gir-dot.mjs`）——节点是种类、类型、形状与出入度，边是两端种类、连接类型与标签，也就是"同样的值、同样的类型、同样的区域结构、同样的接线"。`opt::fold` 另有一条把两侧的转储按文本比的条目：把 origin 号归一化之后两份转储逐字符相同，比同构更强。五个语料（`cse_subject`、`cse_written_variable`、`dce_subject`、`fold_subject`、`fold_fault`）各有一条"遍之前"的条目，断言要合并的重复、要删的死值与要折的运算确实在（没看任何东西的守卫不会通过），一组 `std::jgir` 属性条目（合并的不动点、写过一次的 cell 两侧不合并、程序用到的值还在、没有算子还留在常量上）与四条后端上的结果条目。`fold_fault` 是"必然出故障的常量表达式"那个语料：它在返回位置放一个 `1 / 0`（锚点，不能替换）、在旁边放一个可折的乘法，于是同一轮里既求值又替换，两侧的 jgir 与两侧的警告文本都逐字符相同（`stdout_normalized` 与 `stderr_normalized` 一起比），并且在 `std::nvm` 上跑到那条运行期故障时的结果也与没有这个遍时相同。

**缺口即任务**。下表记着哪些缺口已关闭、哪些还在：

| C++ 遍做什么 | 模块没有提供什么 | 今天的结果 |
| --- | --- | --- |
| `opt::fold`：把输入全是常量的纯算子求值 | **已补**：`eval(t, n)` 在值上求值一个算子，返回持有结果的节点，求值出故障就报同一警告并返回 `null` | 写出来了（`opt::fold`） |
| `opt::fold` 的其余分支：转换、拷贝、访问、填充，以及按操作数类型就能定出结果的算子（`typeFolder`） | `eval` 求值的是**算子**（`NodeKind::Oper`），`Cast`／`Copy`／`Accs`／`Fill` 不是算子，`typeFolder` 也没有出口 | 这些分支仍只在 C++ 侧；`fold_subject` 只用算子的形状，所以这一层没有被两侧比过 |
| `opt::fold` 折**返回位置**的常量表达式：`isFoldable` 在 return 节点没有控制输入时放行它，`replaceFoldResult` 把图的 exit、output 与 return 锚点搬到折出来的值上 | 搬锚点：事务的 `replace` 拒绝替换锚点（`isReplaceable` 那条），没有别的操作能移动一个锚点 | 只求值（为了必然出故障时报出那条警告）然后原样留下；`fold_subject` 里没有返回位置的常量表达式，所以这一层没有被两侧比过 |
| `opt::order`：删掉已被必然的数据或控制路径蕴涵的一条控制边 | 删一条边、加一条边：事务只有整节点的 `replace` 与 `erase`，对应 C++ 的 `unlinkInput(Ctrl, …)` 与 `appendControlDependency` | 写不出来 |
| `std::opt::dce` 的 `values_only=false` 那一半：绕过只转发值的 GATE，把它的等待移到消费者 | 同上，这是边手术 | 只落了 `values_only=true` 那一半 |
| 合并任何非 GC 追踪类型的常量 | 读一个标量的位（`bytes` 编解码的是数组与张量，`math` 没有 `signbit`） | 只合并 `int`、`i32`、`bool`：`0.0` 与 `-0.0` 用 `==` 相等却是图里两个值，按 `==` 合并会改程序 |
| 合并节点时把属性一起合并 | 写属性（`tags(n)` 只把属性读成文本；`add_constant` 造的是新节点，`replace` 不搬运属性） | 属性文本不同的两个节点不合并 |
| 按类型问"是不是 GC 追踪"（`type::isGCTraced`） | 节点类型上没有这个查询 | 按类型名排除 |
| 以区域为键的 Map | `id(Region)` 没有导出，只有 `region_of`、`region_kind`、`region_parent` 与 `==` | 区域只能用 `==` 比较 |
| 遍参数带默认值（`std::opt safe_fp=false`） | Camel 遍的源码没有默认值，每个 `with` 参数都必填（上一节第 4 条） | 每个参数都要写在流水线里 |

计划里说的"`opt::cse` 与 `std::opt::cse` 互换"今天只在结果上成立（同构，四个后端一致）：两个名字仍是两条实现。把 `opt::cse` 的 Camel 定义用 `#impl(cse_native)` 接到 C++ 实现落不下来——`#impl` 的内核需要一个既声明算子又持有函数体的模块，而 `tensor:log_softmax`、`nn:adam` 这类算子没有这样的位置；这已记为问题并**已有定论**（设计原则 10.4）：声明模块可以有函数体、泛型函数可以带 `#impl`，这条从此不再是障碍，`#impl(cse_native)` 的实现尚未做。

**一页结论**。行数按各自的语言量：`opt::cse` 的定义与它用到的比较（`stdlib/opt.cml` 的 `bitEqualType` 到 `cse`）126 行，对 C++ 的 `cseDraft`（`src/passes/opt/generic/generic.cpp`）130 行；`opt::dce`（同一文件的 `valueOnly` 到 `dce`）26 行，对 `dceDraft` 38 行。这是"同一件工作在同一抽象层上的长度"，不是等价的工作量：草稿、使用者索引与提交在 C++ 侧要自己维护，在 Camel 侧交给了 `graph` 模块。同构率：三个语料（`cse_subject`、`written_variable` 的 cse、`dce_subject` 的 dce）全部同构，四个后端上的结果逐字一致。速度不在本项作门槛，这里也没有计时——计时会把 NodeVM 跑一个 Camel 遍的固定成本与遍本身混在一起，而本项回答的是"写得出来吗"。

## 一次编译问的是哪些模块

一次编译的"有没有错"只在命令所问的模块上回答，报告也只打印那些模块的诊断：`run`、`check` 与 `--timeit` 的一轮问的是程序的依赖闭包（入口模块加逐层 import），`inspect --ops` 问的是上下文里的全部用户模块，因为清单少任何一个模块的算子都不完整。pass 列表在编译前装载的 pass 库不在程序里，它的失败由遍解析报成 `InvalidPassParameter`；它不能因为"上下文里恰好有个坏模块"而出现在程序的诊断里，这正是范围划分与报告要一致的地方（文本与 JSON 是同一份答案）。`Context::dumpModuleDiagnostics` 接收要打印的模块集合，命令不再自行遍历上下文拼装渲染。语言服务器的按需分析不解析遍列表，因此不会装载 pass 库；依赖模块的错误仍作为程序自己的错误报在 import 处，不需要另一套作用域。

## 对照与测试

`test/cases/modules/graph_pass/` 里有两个用 Camel 写的遍，各与一个 C++ 遍做差分比较（`test/plans/feat/modules/graph.plan.toml`）：`neutral::integers`（`x + 0`、`0 + x`、`x - 0`、`x * 1`、`1 * x`）对 `std::opt::algebra neutral_only=true`，`unused::values`（无人读取的常量与纯运算）对 `std::opt::dce values_only=true`。两侧的 `std::jgir` 转储逐图相等（`normalize_origins` 去掉源码 origin 号，它们因编译过遍的库而不同）。两个参数是为这个对照加的、文档化的遍参数（`docs/cli.md`）。`is_written_slot(n)` 读出节点的值是否可能被原地写（传给 `var` 形参或改自身操作数的算子，经 GATE 传递），两个遍都跳过这样的节点，差分程序里有 `var`。

`node_kinds_match_the_graph` 把一个带分支、闭包与折叠的程序的每张图的节点种类序列与 `std::jgir` 的转储逐一对过。

标准库的 `opt::cse` 与 `opt::dce` 是同一套对照的库里版本，语料、性质断言与缺口清单见上一节"标准库里的 Camel 遍"。

其余覆盖面：`imports::count`（导入 Camel 库的泛型函数 `array::flat_map`）与 `monomorphic::show`（直接调用单态导入函数 `libvals::nine` / `addOne`，返回真值）；`sharedvals::run`（同一个模块既是被运行的遍，又是被处理的程序导入的库，两边各用各的建图器，泛型 `twice` 在两边各有一个实例）与 `chain::show`（导入闭包传递：`chainlib` 再导入 `innerlib`）；`unparsable_pass::run`（遍模块解析不过，报 `InvalidPassParameter`，解析诊断作为从属位置给出文件、行、列与原行，不是异常结束）；同样的 `unparsable_pass_lib::run` 与各条命令路径（H210）：`run` 的解析失败与编译失败报告只打印程序自己的模块、`--timeit` 的文本与 JSON 都报 `InvalidPassParameter`、`camel: N errors` 只数程序自己的模块；`params::report` 的 `with` 参数，缺参、未知参数（带列号并列出声明）与类型不符的 `with` 参数诊断；`fold_constants::fold` 与 `typed_constants::make` 用 `add_constant` 造常量并提交（后者造 int、f64、bool、字符串四种，提交后逐一数回），两者的加强版都在 `CAMEL_GC_STRESS_SAFEPOINT` 下跑 safepoint；`regionprobe` 读出没有区域表的图的体（`region_of` 为区域 0，`region_kind` 为 `Root`）与内联后的 `Inline` 区域及父区域。
