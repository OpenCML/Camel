# 块体编成算子表里那条算子的实现：树上的落点与施工顺序（2026-11-26）

裁决把「块体放哪份图」定成了下面这一种安排。这一页是它在树上的落点、已经有的那一半、
还缺的四件、以及六条判据今天各自的读数。**这一页没有写一行块的代码**，凡标「无」的都是无。

```
块体编成算子表里那条新算子的实现：
· 它的图放在一份独立的程序里 —— 与 library-program 同一个安排，
  也和用 Camel 写的遍一样：由同一个编译器编译、同一个虚拟机执行
· 算子记录指向它（记录里有它那份程序的编号/入口）
· 外层图里只有一个 OPER 节点
· 任何遍都不往算子实现里走 ⇒ 「遍改遍」的递归因此切断
· 不需要在程序的图里造子图或隐藏图
```

## 1. 三条计划的读数

树 `D:\Projects\Camel-wt\edit-block`，分支 `lane/cc-edit-block`，基点 `origin/integration`
（`b1ba268416`；本树原先落后 128 笔，`graph::add_param` 一条都没有，现已前移）。

| 计划 | 用例 | 结果 | tip | 构建 | 主机 1m load |
|---|---|---|---|---|---|
| `feat.runtime.add_param` | 3 | 3 pass / 0 fail | `73be55e106` | `2026-11-25T21:58:52-05:00` | 21.93 |
| `feat.runtime.edit_record` | 1 | 1 pass / 0 fail | `73be55e106` | `2026-11-25T21:59:35-05:00` | 20.67 |
| `feat.runtime.program_graphs`（新增） | 1 | 1 pass / 0 fail | `73be55e106` + 两处未提交 | `2026-11-25T22:03:14-05:00` | 21.62 |

三条都是 **pass/fail 读数**，不是计时读数：load 20–22，与安静主机的时间不可比（读数只用于
「过没过」，不用于时间基线）。`runtime.add_param` 的三个用例名：
`the_port_without_its_declaration_is_refused`、`the_added_parameter_runs`、
`the_declaration_is_the_other_half`。

## 2. 这个安排树上已经有一半（带落点）

| 组件 | 位置 | 今天做什么 | 对块意味着什么 |
|---|---|---|---|
| `sem::Definition{module, function, params}` | `src/kernel/include/camel/core/operator_semantics.h` | 记录上唯一「指向别处」的指针：**模块搜索路径上一个 Camel 文件**的一个导出函数；只被 `expandDefinition` 读来做规范比对，不执行 | 「记录里有它那份程序」有一半：指着文件+名字，不是程序 |
| `DefinitionGraph{oper, key, definition, graph, program, contentHash}` | `src/kernel/include/camel/runtime/definitions.h:80` | 已经是「记录 → 一份程序」的形状：函数图、它所在的那份程序、内容哈希 | 把它当块体的落点，形状是对的 |
| `DefinitionStore::build` | `src/kernel/runtime/definitions.cpp:358`（库在 `:242` `compileAsLibrary`，程序在 `:426` `pinRuntimeProgram`） | 用**自己的 builder** 编译库，编完把那份程序钉住 | 块体要的「一份独立的程序」就是这一条 |
| `Builder::freezeProgram` / `Builder::pruneToTheProgram` | `src/compile/src/gir/program.cpp:628`、`:640`，声明在 `src/compile/src/gir/builder.h:98`、`:105` | 一次 build 链接了什么就是那份程序的组成，之后挂上去的图被剪掉 | 「任何遍都不往算子实现里走」的机制面 |
| `SourceModuleLoader::attach(module, source)` | `src/kernel/include/camel/core/module/source_loader.h:56` | 模块的前端可以由调用者交进来（`std::shared_ptr<void>`，实现里转成 `CamelParser`） | 合成的块体文本可以被同一个编译器吃掉 |
| `readSource` 只在交来的 reader 还没有 AST 时才读文件 | `src/compile/src/module_source.cpp:142`、`:148` | 调用者自己解析过一段文本，`readSource` 就不再开文件；另一条路是 `context->sourceOverride(path)`（`context.h:99`、`:280`，给了就用它代替文件内容） | 文本 → 模块 → 程序这条路今天就不需要新管道 |
| `UserDefinedModule(name, path, ctx, source)` | `src/kernel/include/camel/core/module/userdef.h:77` | 路径可以空（`:46-48` 的 `<in-memory>`），`source` 是调用者造的 reader | 一份没有文件的块体模块今天就是构造得出来的 |
| `CamelParser::parse(std::istream&)` | `src/parse/include/camel/parse/parse.h:49` | 解析器吃一个 `istream`，不要求文件 | 同上 |
| `<in-memory>` 这一支 | `src/compile/src/module_source.cpp:131`、`src/kernel/core/module/userdef.cpp:46-48` | 路径为空时模块仍能读、仍有诊断 | 「没有文件的模块」已经在代码里有名字 |
| `GraphCaller` | `src/kernel/execute/graph_caller.h` | 用普通实参与 `with` 实参跑一份程序里的一张图，返回它的值 | 跑块体那一步已经有了 |
| `NodeVMSchedPass::resolvedOpersOf` | `src/stdlib/std/nvm/nodevm.cpp:292` | VM 对每个节点解析出一个**原生函数指针** | 今天没有「按记录里的程序跑」这条路径：块算子没有核可解析 |
| 图引用的角色 | `src/kernel/include/camel/runtime/graph.h:484-487`（`kGraphRefCallee/Nested/Static/Rule/LiftedFrom`）、`:935-937` | 一份程序里哪张图是谁的，按角色分得开 | 判据⑥的只读检查读的就是这一层 |

结论：**「一份独立的程序，遍看不到」这条机制在树上，并且今天就在用**（算子的规范定义、用 Camel 写的遍）。
缺的不是安排，是把**一个用户的块体**送进这条安排的入口。

## 3. 还缺的四件，按依赖排序

### ① 块体 → 新算子的实现

三步，前两步在机制上已经通了，第三步是新的：

1. **合成模块文本**：块头绑定 + 块体语句 + 捕获值作形参（次序按块体规范形式里第一次使用的先后，
   图编辑设计 5）。这一半不需要新管道：`UserDefinedModule(name, "", ctx, reader)` 的路径可以空，
   `readSource` 在交来的 reader 已经有 AST 时不再读文件（`module_source.cpp:142`、`:148`），
   另一条路是 `context->sourceOverride(path)`。缺的是**造这个 reader 的那一处**：今天的造法都从文件来。
2. **编成自己的程序**：`compileAsProgram`（`src/kernel/core/module/userdef.cpp:250`）已经是
   「库编成自己的程序」，与遍程序同一个安排。
3. **登记「程序 + 入口」并把外层图造对**：外层图只有一个 OPER 节点，算子按块体规范形式命名
   （哈希），记录指向那份程序；运行时一个通用核（下称 `:block/run`）从记录取程序与入口，
   用 `GraphCaller` 带实参跑。VM 那条按节点解析原生函数指针的路径（`nodevm.cpp:292`）不认识
   这种算子，`:block/run` 是它唯一需要的核——不需要每个块一个核。

### ② 关键字与产生式

今天语法是准的：`check-grammar --check` 报 **127 个产生式 / 33 个上下文关键字**，与解析器、token 表、
语言参考三者一致。加 `edit` / `view` 要动的是：`scripts/context-keywords.mjs` 两个条目（各带一条
`atWord` 证据行，`--check` 要求那行还在解析器里）、两条产生式、`token_kinds.def` 与 `node_kinds.def`
的记账、AST 的一个 `Load` 类、AST 构建器分支、GCT 一个节点、GIR 一个分支、
`check-grammar --write` 重生成两语言的参考表，以及 `-UpdateAstHashes` 重记 `ast_hashes.txt`。

### ③ 块标签 β

句柄类型今天没有标签：`src/stdlib/graph.cml:36-45` 的 `Graph` / `Node<T>` / `Region` / `TypeRef` /
`Op` / `Tag` / `Txn<F>` / `NodeKind` / `RegionKind` / `Effects`，`Node<T>` 只有一个元素类型参数。
图编辑设计第 4 节（`:74-87`）要的五条是：不能作为块的值交出、不能存到块外、不能被离开块的闭包
带走、帮助函数对 β 泛型、普通值（字符串、整数）可以交出。前三条各要一条诊断，第四条是实例化规则，
第五条保证 β 不是「什么都关住」。

### ④ 块尾自动提交与错误值

今天提交是**显式**的：`commit` 在 `src/stdlib/graph.cml:447`，交 `F!`，三种错误值
`GraphConflict` / `GraphInvalid` / `TxnUsed` 写在 `:445` 的文档里、由 `src/stdlib/graph/txn.cpp` 造。
提交诊断已经能指出「是哪一步造成的」：`CommitViolation` 带 `origin`（出错节点所在的源码位置）
与 `editOrigin`（改了它所在区域的那次编辑的位置），见
`src/kernel/include/camel/runtime/resources.h:419-431`，`EditNote::origin` 由 `NamedEdit` 记。
所以块尾要补的是**时序与失败语义**：正常走到块尾自动提交一次；任何一步失败（当场检查、体里
未处理的错误值或故障、提交被拒）放弃整份草稿、块的值是一个错误值。这一格不是从零开始，
但「放弃整份草稿、程序里不留下改了一半的图」这一条今天没有块去触发它。

**顺带记下一处真缺口**：图编辑设计第 7 节（`:122`）把 `replace(exit_of(d), v)` 当作「换出口的值」
的写法，而 `replace` 今天拒绝锚点（`src/stdlib/graph/txn.cpp:245-250`：「the node anchors its graph
(an exit, an output, a return or the end of a branch arm)」）；图模块导出的算子表
（`src/stdlib/graph/module.cpp:231-242`）里也没有第二个写出口的算子，`exit_of`
（`src/stdlib/graph.cml:83`）只读。所以「换出口的值」这一行设计今天没有实现，而 `edit` 块要换出口值
就得先给它一条真写法。

## 4. 一处形状问题，值得你知道（不是与设计原则的冲突）

裁决说「算子记录指向它」，而记录上今天那处指针（`sem::Definition`）是**规范**：它说一个算子算
什么，规范只做比对，任何算子都不能靠定义执行（`docs/design/op-semantics` 第 9 节、
设计原则 7.1「定义图是规范」）。块的实现程序**要执行**。两者共用一处指针，就把「定义图是规范」
改成了「定义就是实现」。

所以我按**另加一处指针**施工：记录上区分「规范性定义」与「可执行的实现程序」。
这一格会动到算子记录的形状，它不是 9.12 与图编辑设计第 5 节能推出或否掉的东西，故在这里点出来。

除此之外，裁决与设计原则 9.12、图编辑设计第 5 节**逐字一致**：9.12 写的就是
「每个块是算子表里一个按块体规范形式命名的算子……块体不属于程序的图，任何遍看不到它」
（`docs/design-principles.cn.md:744-747`）。注意两份文档的节号：设计原则第 5 节是类型系统，
块体那一段在 **9.12**；`docs/design/graph-editing/README.cn.md` 的第 5 节才是
「编译之后：每个块是算子表里的一个算子」。

## 5. 六条判据今天各自的读数

| 判据 | 今天 | 缺什么 |
|---|---|---|
| ① 结构：块体只贡献 1 个节点、独立程序在记录里 | **量不出**：没有块，也没有写块体的语法 | 第 3 节 ①② |
| ② 「遍里写块、块里再写遍」递归切断 | **量不出** | 同上，且要两条块（外层一条、遍里一条） |
| ③ 句柄出不了块，越界必须是诊断 | **量不出**：今天块外拿句柄是允许的写法（`graph_of`、`graphs_of`，`src/stdlib/graph.cml:52`、`:57`） | ③ |
| ④ 块尾自动提交与错误值 | **半**：显式 `commit` 的三种错误值有实现与文档，提交诊断已带两步位置；块尾自动提交、以及「失败即放弃整份草稿」无 | ④ |
| ⑤ 不回退：`add_param` 的用例仍全绿 | **绿**：`feat.runtime.add_param` 3/3（第 1 节） | — |
| ⑥ 程序里没有子图/隐藏图 | **新增一条只读检查**：`feat.runtime.program_graphs`，见下 | 块体那一半要等 ① |

`feat.runtime.program_graphs` 在同一份程序上读三样东西：

- `std::stats` 数出的图：`__root__`、`main` 两张、76 个节点；
- 把**一个用 Camel 写的遍的名字**写进流水线（`std::stats std::nop opt::cse`：名字要解析、遍不执行）
  之后的同一个数：**2 张图、76 个节点，不增**，图名逐个相同；
- `std::jgir` 文档里的图集合：与上面同一个数，且没有一张是库图（`opt::` / `rules::` / `types::`）。

它的红是**另一棵树上的读数**：同一条流水线在库图挂到入口程序上的时候是 210 张而不是 5 张。
本树复现不出那个红（修已经在里面），我也没有为了看它红去拆别的车道的修。所以这条检查的立场是：
它是块体那条安排**将来必须保持**的那一格，今天先钉在「2 张图」上——块体若被编成子图或隐藏图，
这个数会变，而它变就是红。

## 6. 下一笔怎么开头（一步一格读数）

1. **合成的模块走 in-memory 那条路**：一个 `test::` 遍把一段文本交给模块加载器，编成**自己的程序**。
   读数两条：`std::stats` 的图数不变，`std::jgir` 里找不到它。这一步不碰前端。
2. **记录上的第二处指针 + `:block/run` 核**：手造一个 OPER 节点（C++ 的图原语，不经前端）。
   读数：`std::stats` 里块体只贡献 **1 个节点**，程序跑出块体的值——把判据①的一半与
   「同一个虚拟机执行」同时量出来。
3. **前端先 `view` 后 `edit`**：`view` 没有提交、没有错误值，只需要块头、β 与「体不出现在程序里」；
   判据③的五条里有两条（交出、闭包带走）在 `view` 上就能量。
4. `edit` 接上 ④ 的块尾提交与错误值；最后是 ② 的「遍改遍」用例。