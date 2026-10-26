# `graph` 重写说明

稿件：`graph.cml`。**未编译**。目标态（H233 剩余部分，`reviews/stdlib-rewrite.md`、设计原则 9.1）：图 API 以函数为中心，`Program` 不出现在公开接口上。`stdlib/signatures/graph.cml` **整个删除**：没有计算签名了（见下），`scripts/stdlib-reference.mjs` 里 `graph` 条目的 `computed:` 字段一并删。

## 函数一览

| 名字 | 处理 | 理由 |
|---|---|---|
| `current()`、`root(p)`、`graphs(p)`、`program_of(f)` | **删除** | `Program` 退出公开接口；程序级信息经函数读取 |
| `graph_of(f)` | 保留，改为声明 `graph_of::<F>(f: F): Graph when is_function::<F>()` | 原先是计算签名，只因声明泛型于 `F` 会放过任何类型；`when is_function` 正好堵住 |
| `graphs_of(f)` | **新增**：函数的图加它直接或间接调用的所有图，按编号，各一次 | `fold`/`cse`/`dce` 原来遍历 `graphs(p)`，函数中心的接口需要“这个函数用到的图”（Q1） |
| `open(f)` | 改为 `open::<F>(f: F): Txn<F>`；`open_function(f)` 删除 | 事务由函数打开，类型里带着 `F` |
| `commit(t)` | 改为 `commit::<F>(t: Txn<F>): F!`；`to_function(t, sample)` 删除 | 提交直接给出同类型的函数，闭包捕获随之带上，所以 `SignatureMismatch`、`ClosureNotEmpty` 两个错误种类不再产生 |
| `replace` `erase` `eval` `abort` `add_constant` | 改为对 `Txn<F>` 泛型；`uses t` 取代 `@effect` | 事务是句柄资源（设计原则 3.3），对同一个事务的操作按书写顺序 |
| `add_constant` | 六条按类型的重载并为一条 `add_constant::<F, T>(…, value: T) when is_ordered::<T>()` | 一个定义覆盖所有类型 |
| `constant(n, sample)` | 改为 `constant::<T>(n): T?`，类型实参写出（`constant::<int>(n)`） | 原先靠一个“样本”值传类型，是绕路；六条重载并为一条 |
| `ctrl_inputs(n)` | **删除**，换为 `sync_inputs(n)`、`order_inputs(n)` | H242 删除控制边，剩同步边与顺序边（设计原则 3.1）；新名字的 C++ 读法随 H242 |
| `shape(n)` | 改名 `shape_text(n)` | 与 `array::shape`（返回 `int[]`）同名异义，返回的是文本 |
| `==` of `Program`、`Txn` | **删除** | `Program` 已不在接口上；事务从不比较 |
| 其余读取函数（`name`、`nodes`、`inputs`、`with_inputs`、`users`、`value_users`、`kind`、`op`、`op_name`、`is_pure`、`is_anchor`、`is_written_slot`、`is_constant`、`type_of`、`type_name`、`callee`、`tags`、`origin`、区域三个、`show`、`id`、`effects`、`permissions`） | 保留 | |

`@pure` 与 `@effect` 注释全部删除；纯度与资源在 C++ 的 `declare`/`declareResources` 一处。示例改用函数值（`graph_of((x: int): int => x + 1)`），需要外部节点的写 `@example-fragment`。

## 需要的 C++ 改动

1. `modules/graph`：算子签名按上表；`Txn` 成为带类型参数的句柄类型 `Txn<F>`（`commit` 的结果类型取自 `F`）；`current`/`root`/`graphs`/`program_of`/`open_function`/`to_function` 删除；`graphs_of`、`sync_inputs`、`order_inputs` 新增；`shape` 导出名改 `shape_text`。
2. `commit` 在函数上提交：新函数共享未改的图；被改的图里的捕获端口保持，闭包的捕获值带上（H233 第 3 条）。
3. `graph:eq` 去掉 `Program`、`Txn` 两条。
4. `Program` 类型本身留在内部（C++ 遍仍按快照工作），不再导出给 Camel。

## 调用点迁移

调用 `graph` 的文件：`test/cases/modules/rules/`（63 个）、`test/cases/modules/graph_pass/`（21）、`test/cases/compile/native/`（9）、`test/cases/modules/`（6）、`stdlib/{opt,rewrite,rules}.cml`、`test/cases/compile/module_imports/**`（6）、`test/cases/modules/pass_not_found/`（2）、`docs/language/{05_modules,en/05_modules}.md`、`docs/stdlib-reference.md`。其中：`current()` 约 25 个文件、`graphs(…)` 约 44、`root(…)` 约 19、`open_function`/`to_function` 4、`program_of` 6、`ctrl_inputs` 4。写法对应：`graphs(current())` → `graphs_of(f)`（其中 `f` 是被变换的函数，不是全局）；`root(p)` → `graph_of(f)`；`open(p)`…`commit(t)` 对 `Program` → 对 `f`；`constant(n, 0)` → `constant::<int>(n)`；`add_constant(t, g, 7)` 不变（`T` 由值推出）。

## 开放问题

**Q1. `graphs_of(f)`。** 遍要看见“这个函数用到的所有图”，函数中心的接口里需要有它。推荐保留这个名字与语义（函数及其传递被调用的图）；备选是 `open(f)` 之后只通过 `callee` 手走。

**Q2. 对根函数以外的图改写。** `replace`/`erase` 作用于 `graphs_of(f)` 里任何一张图，不只 `graph_of(f)`；`commit` 给出的 `F` 是同一个函数入口加改过的被调函数。推荐如此（`cse` 需要跨图合并），文档已写。

**Q3. `uses t`。** 事务算子的资源写成 `uses t`，依赖 `Txn<F>` 带 `#resource` 标签。若句柄资源还没落地，退回不写 `uses`（视为全局资源，只是更保守）。

**Q4. 同步边/顺序边的读法。** `sync_inputs`、`order_inputs` 的名字与返回（只返回两端都执行时才有序的节点）是我按设计原则 3.1 的定义拟的，请 H242 车道确认。
