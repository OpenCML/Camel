# 模块 `graph`：把程序当作值

`graph` 是一个 C++ 写的模块（`modules/graph`），让 Camel 程序读取并改写另一个程序的图：读取图、在事务里编辑、提交成新的 `Program`；用 Camel 写的遍是一个 `(Program) => Program!` 函数，按 `<模块>::<函数>` 写进流水线。设计依据见 `docs/design/graph-store/README.cn.md` 第 4、5 节；本文说明实现里每一块放在哪里、行为是什么、有哪些限制。逐个算子的签名与例子见 [`docs/stdlib-reference.md`](../stdlib-reference.md) 的 `graph` 一节（由 `stdlib/signatures/graph.cml` 生成）。

## 类型

| 类型 | 是什么 | 实现 |
| --- | --- | --- |
| `Program` | 不可变的程序快照 | 运行时的 `Program` 对象本身，按身份比较 |
| `Graph`、`Node`、`Op`、`TypeRef`、`Region`、`Tag`、`Txn` | 命名某个 `Program` 里的东西 | 一个被追踪的 `Handle` 对象（`modules/graph/handle.h`）：种类、所属的 `Program`、`GraphId`、序号、`Type*`；`Txn` 是外部资源对象 |
| `Value` | 值；目前与 `Node` 同一个类型 | `Node` 的另一个名字 |
| `NodeKind` | `Data, Port, Cast, Copy, Fill, Accs, Brch, Join, Call, Func, Oper, Sync, Gate, Loop` | Camel 的 `enum`（`nodekind.cpp`）；`nodeKindTag` 对 `GCNodeKind` 做穷尽的 `switch`，图模型新增一种节点时这里编译不过，直到它被放进枚举或明确排除（`Bind`、`Dref`、`Dead` 不是程序里看得见的节点） |

句柄用 `==` / `!=` 比较（模块通过 `__eq__` / `__neq__` 算子组为自己的类型定义，设计原则 4.3）。两个句柄相等，当且仅当种类、`Program`、图与序号都相同。`id(h)` 给出图的 `GraphId`、节点的 `NodeId` 或算子的 `OperId`，在同一个 `Program` 里稳定，Map 的键用它。

一个句柄持有它读出时的 `Program`，所以经过它读到的永远是那个快照；`Handle::updateRefs` 追踪这个 `Program`，和函数值追踪它的 `Program` 一样。

## 读取

读取算子都是纯的，直接读冻结的图（`GCGraph`）：`graphs`、`root`、`name`、`nodes`（`NodeId` 序，跳过 `Dead`）、`inputs` / `with_inputs` / `ctrl_inputs`、`users` / `value_users`（经图缓存的使用者索引）、`kind`、`op` / `op_name`、`is_pure`、`is_anchor`、`is_constant`、`constant(n, 样本)`、`type_of` / `type_name`、`callee`、`tags`、`origin`、`show`。可能没有的结果是 `T?`，用 `match` 取出。`constant` 的第二个实参是类型的样本，值不被读取：原生算子的类型实参用这种形式（见 `json::decode_as`），`::<T>` 只属于 Camel 的泛型函数。

事实（`docs/technical/40_graph_facts.md`）经上下文的事实库读取：`effects(g)`（`external`、`writes_variables`、`may_fault`、`may_not_terminate`）、`shape(n)`、`permissions(n)`。第一次问时计算，之后只要它读过的图没有变就不重算。

## 事务

`open(p)` 在 `p` 上开一个 `RuntimeGraphDraftSession`，由一个外部资源对象持有；会话在提交或放弃时释放，没有人提交就由收集器在安全点释放。编辑只有两个，都带语义而不暴露原始邻接改写：

- `replace(t, old, new)`：把同一张图里的 `new` 放到 `old` 的位置并删除 `old`，用的是优化器的 `replaceNode`，所以 `old` 排过的顺序（控制前驱和它的值所来自的有序节点）保持不变。`old` 是图的锚（出口、输出、返回、分支臂首尾）、`new` 由 `old` 算出、或事务已结束，是故障。
- `erase(t, n)`：删除没有人读取其值的节点。

编辑不创建被处理程序的堆值：草稿把节点的静态值当作裸引用，NodeVM 的安全点可能在 Camel 代码运行时移动对象，所以现在没有“新建常量”的编辑，直到草稿有了根集规则。

`commit(t)` 用 `commitChecked()`：校验草稿（`docs/technical/17_graph_rewrite_api_contracts.md` 4.2 的边不变量），编码新 `Program`，它与旧的共享没有改的图。错误值的 `kind`：`GraphConflict`（事务读过或写过的图，在上下文的程序里已有别的版本）、`GraphInvalid`（编辑破坏了不变量，消息带图、节点与边）、`TxnUsed`（事务已结束）。`abort(t)` 丢弃事务。

## 函数到函数

`open_function(f)` 开一个只含函数值 `f` 的图及其可达子图的事务，`to_function(t, 样本)` 把它提交成**自己的** `Program`（运行中的程序不变）并返回样本类型的函数值。错误值除 `commit` 的以外有 `SignatureMismatch`（图的签名与样本不符）与 `ClosureNotEmpty`（函数捕获了值，新建的函数值没有闭包）。`program_of(f)` 与 `graph_of(f)` 读函数值。改写后的函数值在每个后端上都与原函数算同样的结果。

## 用 Camel 写遍

一个模块导出 `(p: Program) => Program!` 的函数，就是一个遍，流水线里写 `camel prog.cml mymod::fn std::nvm`。实现在 `src/execute/camel_pass.cpp`：

1. 遍的名字解析不到时，把模块当库编译（`Module::compileAsLibrary`，与算子定义的库同一条路），取出导出的函数，只接受唯一、非泛型、签名为 `(Program) => Program!` 的函数；
2. 函数连同它调用的图编码进**自己的** `Program`（工具程序，`DefinitionStore::libraryFunction`），由上下文持有，所以不需要额外的根；
3. `NodeVMSchedPass::callGraph` 在 NodeVM 上以被处理的程序为实参运行它；返回的 `Program` 与输入不同就 `installRuntimeProgram`；
4. 返回错误值是诊断 `GraphPassFailed`；模块编译不过、函数不是遍，是 `InvalidPassParameter` 带原因；模块不存在或没有这个函数，遍保持未知。

限制：库只能导入 C++ 写的模块，不能导入 Camel 写的库（例如 `array`）；遍参数还不能从命令行传入。

## 对照与测试

`test/cases/modules/graph_pass/` 里有两个用 Camel 写的遍，各与一个 C++ 遍做差分比较（`test/plans/feat/modules/graph.plan.toml`）：`neutral::integers`（`x + 0`、`0 + x`、`x - 0`、`x * 1`、`1 * x`）对 `std::opt::algebra(neutral_only=true)`，`unused::values`（无人读取的常量与纯运算）对 `std::opt::dce(values_only=true)`。两侧的 `std::jgir` 转储逐图相等（`normalize_origins` 去掉源码 origin 号，它们因编译过遍的库而不同）。两个参数是为这个对照加的、文档化的遍参数（`docs/cli.md`）。差分程序里没有 `var`：`std::opt::algebra` 对写过的 `var` 槽另有保守规则，Camel 遍不看这一点。

`node_kinds_match_the_graph` 把一个带分支、闭包与折叠的程序的每张图的节点种类序列与 `std::jgir` 的转储逐一对过。
