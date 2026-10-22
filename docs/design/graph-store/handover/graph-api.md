# 图 API（模块 `graph`）：已做与交接

分支 `gs/graphapi`（工作树 `Camel-gs-scans`），头 `ac2402d1`，已含 fsh-dev `a9cfeca6`。未推送。技术说明在 `docs/technical/45_graph_module.md`，算子签名在 `stdlib/signatures/graph.cml`（`docs/stdlib-reference.md` 由它生成）。

## 做完的

| 提交 | 内容 |
|---|---|
| C0 `e9a6ca01` | 模块骨架：`Handle` 对象、类型 Program/Graph/Node/Region/TypeRef/Op/Tag/Txn、枚举 `NodeKind`（对 `GCNodeKind` 的穷尽 `switch`）、`current()` |
| C1 `21a38fd8` `8eea0f25` | 读取：graphs/root/name/nodes/inputs/with_inputs/ctrl_inputs/users/value_users/kind/op/op_name/is_pure/is_anchor/is_constant/constant/type_of/type_name/callee/tags/origin/show/id；`==`/`!=`（`__eq__`/`__neq__`）；`Value` |
| C3 `0ee79587` | 用 Camel 写的遍：`(Program) => Program!` 的导出函数即遍；`DefinitionStore::libraryFunction`、`NodeVMSchedPass::callGraph`、`src/execute/camel_pass.cpp`、诊断 `GraphPassFailed`(0x61) |
| C4 `101f71d2` | 事务：open/replace/erase/commit/abort；错误值 GraphConflict/GraphInvalid/TxnUsed |
| C5 `7d9f3225` | `neutral::integers`（Camel）对 `std::opt::algebra(neutral_only=true)` 的差分；测试规范化器 `normalize_origins` |
| C6 `7133e983` | `unused::values`（Camel）对 `std::opt::dce(values_only=true)` 的差分 |
| C7 `50174f1c` | `open_function` / `to_function` / `graph_of` / `program_of`（`of`、`to` 是关键字，故改名）；四个后端一致 |
| C8 `9d80934e` | `effects` / `shape` / `permissions`（经事实库）；新增的 libcamel 符号写进 `cmake/libcamel_exports.def`（手工写的 MSVC 名，未在 Windows 链接） |
| C9 `a99bbe9d` | 签名、参考文档、技术说明 |
| `ac2402d1` | `is_written_slot`；`neutral::integers` 跳过写过的 var 槽；差分测试含 var 程序 |

测试：`test/plans/feat/modules/graph.plan.toml`（约 26 条）；合并后跑 smoke、feat/modules、feat/opt、feat/consistency、feat/compile、feat/mm/writings 全过，唯一失败是 `fs.remove_permissions`（容器以 root 运行；没有在干净的 fsh-dev 上重建确认）。

## 剩下的（交接给实现组）

1. **pass 库导入 Camel 模块**（未做）。`compileAsLibrary` 刻意拒绝导入 Camel 写的模块，因为那样的模块会被声明进程序的图构造器。入口：`DefinitionStore::library`（`src/runtime/definitions.cpp`）。两条路：（a）让库自己的构造器也能编译它导入的 Camel 模块（库用独立的 `libraryBuilder_`，导入链要递归地 `compileAsLibrary`，并缓存到同一个库构造器，避免同一模块被编译两次，要处理循环导入）；（b）把遍所在模块当作一个小程序整个编译（含 Camel 依赖），再从该程序里取函数。测试：`neutral.cml` 里的 `flatten`（`algo`）换成 `array::flat_map` 应能编过。现在只能用 C++ 模块（`algo`、`str` 等）与内建函数。
2. **遍参数**（未做）。`mymod::p(k=v)` 应映射到函数的 `with` 参数（原则 4.4）。入口：`registerCamelPass`（`src/execute/camel_pass.cpp`）现在用 `registerModulePass(path, factory)`；改用带 `PassDescriptor` 的重载，由函数的 with 参数生成 `PassParam`，`CamelPass::apply` 把值作为 with 实参传给 `callGraph`（`callGraph` 目前只绑定 norm 端口，要加 with 端口）。
3. **新建常量的编辑与根集规则**（未做，最大的一块）。`replace`/`erase` 不创建堆值，因为草稿把节点的静态值当裸引用，而 NodeVM 的安全点会在 Camel 代码运行时移动对象。有用的改写（折叠、物化常量）都要“加一个常量节点”。先定规则：草稿的静态槽在事务存活期间作为根（例如事务外部资源对象的 `trace` 里对草稿静态槽 `relocate`），或新常量一律放进不移动的空间。入口：`modules/graph/txn.cpp` 的 `txnDescriptor().trace`、`GraphDraft::materializeStaticValue`。再加编辑 `add_constant(t, g, value)`（先限标量与字符串）。
4. **`constant::<T>(n)`**：等 H19“一个事实一处”把原生函数声明放进 `.cml` 后，原生算子才能有类型参数；到那时把 `constant(n, sample)` 换成 `constant::<T>(n)`（技术债；签名在 `stdlib/signatures/graph.cml`，实现 `constantKernel` 与类型规则在 `modules/graph/read.cpp`）。
5. **组合子**（C7 的余项）：现在只有 `open_function` / `to_function`。想要的 `rewrite(f, pass)` 之类最好写成 Camel 库（`stdlib/graph.cml`），依赖第 1 项。
6. **事实**（C8 的余项）：只暴露了 effects/shape/permissions；`core.instance`、`core.reorder`、`core.loopDomain`、`core.resources` 和遍自己注册的事实没有暴露。
7. **区域读取**（H36 已落地）：`Region` 类型、`RegionKind` 枚举与 `region_of(n)`、`region_kind(r)`、`region_parent(r)`；每个节点都属于一个区域，区域 0 是函数体，只有体的图没有区域表时 `region_of` 也返回区域 0，`region_kind` 为 `Root`。测试 `regions_of_a_program_without_regions`、`regions_after_inlining`。
8. **区域规则（2）(3)**：由分支臂校验覆盖（`src/runtime/branch_arms.cpp` 的 `verifyBranchArmRegions`，参考 `src/runtime/arm_members.h`）——“节点只读自己区域或祖先区域的节点”“JOIN 的 with 输入可读本 BRCH 的 Arm 区域的结果”“加边的默认区域取各输入区域里最深”；`EdgeChecker` 本身只检查等价写入同区域，一个 BRCH 只有一个 JOIN。
9. **Map/Set 键**：句柄用 `id(h)` 做键；“库类型作键”的通用协议是核心的另一个条目。

## 陷阱

- **语言**：`of`、`type` 是关键字，不能作算子名或变量名；语言是表达式式的，没有语句 `if`，用 `if c then a else b`；`!x->f<...>` 解析成 `(!x)->f`，要加括号；`match` 不能在 `&&` 右边裸写；`throw` 不是表达式（传递错误用 `x ?-> f`）。
- **库里的错误**：pass 模块有语法错误时 `importModule` 抛 `Diagnostic`，“找不到”与“编不过”靠 `DefinitionStore::absentLibraries_` 区分（只有 `ModuleNotFound` 算找不到）。
- **GC**：事务是 `ForeignResourceObject`（删除由收集器在安全点推迟执行），不要在 `finalize` 里直接析构会话；Handle 的 `updateRefs` 先看 `graphSpace().isAllocated(program)`。`Function::create` 的闭包元组类型不能为空指针（Release 下 ASSERT 被去掉，会段错误）：无闭包用 `TupleType::create()`。
- **jgir 对照**：两个遍的转储只在源码 origin 号上不同（库编译占用 origin），所以加了 `normalize_origins`；`std::jgir` 的图顺序是遍历序，`graphs(p)` 是 GraphId 序，只逐图比较。
- **Windows 导出**：新调用 libcamel 的符号要写进 `cmake/libcamel_exports.def`。我写的 13 个 MSVC 名是手工推出来的，没有在 Windows 链接过；实现组的 Windows 构建已为这个模块补过一次，以他们的版本为准。
- **Bash 工具**：heredoc 里的反斜杠和单引号会被改写；含 `\n` 或撇号的文本用 Write/Edit 工具写文件。`robocopy` 在 Git Bash 里要加 `MSYS_NO_PATHCONV=1`。
- **环境变量开关**：没有加；`neutral_only`、`values_only` 是文档化的遍参数（`docs/cli.md`、`docs/cli.cn.md`）。

## 关键文件

`modules/graph/{module,handle,nodekind,read,txn,facts}.{cpp,h}`；`src/execute/camel_pass.{cpp,h}`；`src/runtime/definitions.cpp`（`libraryFunction`）；`src/passes/sched/nodevm/nodevm.cpp`（`callGraph`）；`src/passes/opt/generic/{algebra,generic}.cpp`、`rewrite.h`、`include/camel/passes/opt/generic.h`、`src/execute/pass.cpp`（两个新遍参数）；`test/plans/feat/modules/graph.plan.toml`、`test/cases/modules/graph_*.cml`、`test/cases/modules/graph_pass/*.cml`；`test/tools/run-tests.mjs`（`normalize_origins`）。
