# 块体编成算子表里那条算子的实现：树上的落点与施工顺序（2026-11-26）

裁决把「块体放哪份图」定成了下面这一种安排。这一页是它在树上的落点、已经有的那一半、
还缺的四件、以及六条判据今天各自的读数。**这一页没有写一行块的代码**，凡标「无」的都是无。

```
块体编成算子表里那条新算子的实现：
· 它的图放在一份独立的程序里 —— 与 library-program 同一个安排，
  也和用 Camel 写的遍一样：由同一个编译器编译、同一个虚拟机执行
· 实现由执行器解析按 Context 解析（算子表不存实现），记录里只有规范
· 外层图里只有一个 OPER 节点
· 任何遍都不往算子实现里走 ⇒ 「遍改遍」的递归因此切断
· 不需要在程序的图里造子图或隐藏图
```

第二行原先写的是「算子记录指向它」；那一句已被收回：记录里只留规范，实现走执行器，
见第 4 节（依据 `operator_table.h:33-34`）。据此重排的施工顺序在第 6 节。

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
3. **登记「程序 + 入口」、注册执行器、把外层图造对**：外层图只有一个 OPER 节点，算子按块体规范形式
   命名（哈希）；那份程序登记在上下文里，节点属性记它的编号；`block` 协议的执行器
   （第 4 节）在同一个虚拟机上跑它。VM 那条按节点解析原生函数指针的路径（`nodevm.cpp:292`）
   不需要认识这种算子：执行器是一个模块自己的实现，`finder` 一条路就够了。

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

## 4. 实现不进算子记录，走执行器（裁决 D-1126-19，以及它落在树上的哪几行）

算子表的文件头已经把这件事写死了：`src/kernel/include/camel/core/operator_table.h:33-34` ——

```
 * The implementation (the C++ function pointer) is deliberately not in the table: it is
 * resolved per Context by the executor manager.
```

所以块的实现**既不共用 `sem::Definition`，也不在算子记录上新增字段**，而是走执行器这条路：

| 树上已有的 | 位置 | 块用它做什么 |
|---|---|---|
| 执行器按**协议**（URI 冒号前那一段）找实现 | `src/stdlib/std/exec/executor.cpp:58-60`；工厂 `ExecutorManager::registerExecutorFactory`（`.../exec/include/camel/execute/executor.h:59`） | 块的 URI 用一个协议，注册一个工厂；一个执行器把该协议下每个块的 URI 映到同一个核 |
| 模块自己注册自己的执行器 | `src/stdlib/graph/module.cpp:171`（`graph` 模块注册 `GraphExecutor`）、`src/kernel/include/camel/core/context/context.h:440` | 同一个写法，不需要新机制 |
| **节点属性**就是「哪个实现跑这个节点」的通道 | `src/kernel/include/camel/core/operator.h:43-47`（「what a pass recorded about the node, e.g. which implementation runs it」）、`node_attrs.h`（键由库自己注册，核心不认识） | 块的 OPER 节点上记它那份程序的编号；核读自己的属性 |
| 每个调度器都把属性交给算子 | `src/stdlib/std/nvm/nodevm.cpp:1190-1191`、`src/stdlib/std/fvm/native_call.cpp:24-25`、`src/stdlib/std/macro/eval.cpp:693-694`、`src/stdlib/std/prl/taskflow.cpp:2226-2227` | 块体在任何调度器下都读得到编号，与「同一个虚拟机执行」不冲突 |
| 跑一份程序里的图 | `src/kernel/execute/graph_caller.h` | 核拿到那份程序后就在同一个虚拟机里跑它 |

于是三件事各有其位，谁也不借谁的字段：

- **记录是规范**：块对外是一个不透明算子，事实照图编辑设计第 5 节写（可能不终止——块体展开的是
  用户的代码；读写图资源，`graph::Txn` 就是那条资源）。
- **程序在上下文的登记处**：一份块体编出来的程序与它的函数图（`DefinitionGraph{graph, program}`，
  `src/kernel/include/camel/runtime/definitions.h:80` 已经是这个形状），登记在上下文里、
  与 `DefinitionStore` **分开**——那份是规范、只比对、不执行，两者的生命周期与用途不同。
- **实现只有执行器**：核从节点属性取编号 → 取那份程序 → `GraphCaller` 在同一个虚拟机里跑。
  新的一格里没有「记录里存程序」这件事。

裁决没有说、而树上已经有答案的一点：**执行器怎么知道自己是哪个块**。答案就在
`operator.h:43-45`：属性是「哪个实现跑这个节点」的通道，由算子自己的模块解释；核心不新增
「一个节点一个实现」的字段。所以「一个块一个核」不成立，也不需要——一个核按属性分派。

裁决与设计原则 9.12、图编辑设计第 5 节**逐字一致**：9.12 写的就是
「每个块是算子表里一个按块体规范形式命名的算子……块体不属于程序的图，任何遍看不到它」
（`docs/design-principles.cn.md:744-747`）。注意两份文档的节号：设计原则第 5 节是类型系统，
块体那一段在 **9.12**；`docs/design/graph-editing/README.cn.md` 的第 5 节才是
「编译之后：每个块是算子表里的一个算子」。

## 5. 六条判据今天各自的读数

| 判据 | 今天 | 缺什么 |
|---|---|---|
| ① 结构：块体只贡献 1 个节点、独立程序在上下文里（记录只有规范） | **一半**：文本编成一份自己的程序已量出（第 7 节）；块本身仍无 | 第 3 节 ①② |
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

## 6. 下一笔怎么开头（一步一格读数，按「实现走执行器」重排）

1. ~~**文本 → 一份程序**~~ **已做，读数见第 7 节**（`test::block_body`）。
2. **执行器 + 块节点**：给 `block` 协议注册一个工厂（`module.cpp:171` 的写法），核按
   `norm.attrs()` 取编号、取程序、`GraphCaller` 跑；手造一个 OPER 节点（C++ 的图原语，不经前端）。
   读数：`std::stats` 里块体只贡献 **1 个节点**并跑出块体的值，且 `std::stats`/`std::jgir` 的
   图数仍不增——判据①的一半与「同一个虚拟机执行」同时量出来。
3. **前端先 `view` 后 `edit`**：`view` 没有提交、没有错误值，只需要块头、β 与「体不出现在程序里」；
   判据③的五条里有两条（交出、闭包带走）在 `view` 上就能量。
4. `edit` 接上 ④ 的块尾提交与错误值；最后是 ② 的「遍改遍」用例。
## 7. 第一步做完了：一处读数

`test::block_body`（`src/stdlib/test/block_body.cpp`）把一段文本交给编译器，编成一份**自己的程序**，
并把三个数打出来。跑 `test/cases/loops/captures.cml`：

```
BLOCK_BODY built graphs=2 nodes=6
BLOCK_BODY graph __root__ nodes=3
BLOCK_BODY graph block_body::body nodes=3
BLOCK_BODY entry graphs 2 -> 2 nodes 76 -> 76
```

- 块体（这里写的是 `export func body(x: int) => int`）编成一份自己的程序：**2 张图 6 个节点**，
  根图 `__root__` 与函数图 `block_body::body` 各 3 个节点；
- 被编的那份程序**一张图没多、一个节点没多**：**2 张图 76 个节点**，编之前与编之后同数，与不跑
  这个遍的读数也同数；
- `std::jgir` 写出的图集合**同数**（2 张）。

机制落点：`UserDefinedModule(name, "", ctx, reader)`（路径空、reader 已经解析过文本，
`readSource` 因此不开文件）→ `compileAsProgram` 编成自己的程序（它自己开一个 `Builder`）→
装载器 `encodeProgram(..., freshIds=true)` 编码成一份独立的运行时程序 → `context->pinRuntimeProgram`
把它钉住（模块随这次调用结束，图会跟着走）。计划：`feat.runtime.block_body`，用例
`the_body_is_a_program_of_its_own`。

**这不是块。** 块还缺：算子记录与名字（按块体规范形式哈希）、外层那一个 OPER 节点、节点属性里
记程序编号、`block` 协议的执行器（第二步），以及前端、β、块尾提交（第三、四步）。

### 7.1 算子的那一半：一个内核，两个块

同一个遍接着把两份块体登记进上下文的块程序处（`camel/runtime/block_programs.h`，
`Context::blockPrograms()`），注册 `block` 协议的那一个执行器，然后**通过执行器管理器**取算子、
带上属性调用：

```
BLOCK_BODY built body graphs=2 nodes=6
BLOCK_BODY built second graphs=2 nodes=6
BLOCK_CALL block::body argument=41 value=42
BLOCK_CALL block::scaled argument=5 value=15
BLOCK_CALL block::body argument=1 value=2
BLOCK_BODY entry graphs 2 -> 2 nodes 76 -> 76
```

- **两个块，一个内核**：`runBlockOfItsProgram` 只有一份。`ExecutorManager::find("block::body")`
  取冒号前协议 `block`、造出那个执行器、**把 URI 去掉协议后**去它的算表里取（`uri.substr(pos+1)`，
  即 `:body`，这是树上每张算表的写法）——所以"一块一核"不成立，块之间的差别是程序，不是代码。
- **哪份程序由调用上的算子身份说**：内核读 `norm.oper()`——节点的 URI 在算表里的 id，由调度器随
  调用送进 `ArgsView`——到块程序处按同一个 id 取 program 与根图，再交给 `graphCaller()` 跑。
  算子记录里没有任何关于块体的东西，节点属性里也没有。
- **值是真的**：`41 → 42`、`5 → 15`；第三次调同一个块 `1 → 2`，发生在它自己第一次跑完之后，
  "钉住了"因此是一条值，不是"没崩"。

两条结构性事实（设计时就该写下来的）：

- **一个身份，不是两个**：块的 URI 与它在算表里的 id 是同一个身份的两种写法，`BlockPrograms` 以
  id 为键，`byUri` 只是经算表查到同一个 id 再看一次，两者不可能分歧。下文最初写的是"序数记在
  节点属性里"，并以属性去留表的一行登记它；那一行与那条通道随身份改到调用上一起撤掉了——一份
  实现登记给许多算子时（块执行器、以后的桥、按名字分派的模块算子），它得知道自己这次是作为哪个
  算子被调的，而身份本来就在节点上，随调用送到就不必在属性里再存一份。
- `Executor::find` **不是虚函数**，所以动态的块 URI 集合只能在执行器构造时铺进算表——顺序因此是
  "先编块体、后造执行器、再跑"，这与管线天然一致，但不能反过来。

**还差 ①的那一格**：外层图里那一个 OPER 节点还没插，所以 `std::stats` 的 nodes 现在仍是
`76`（这个遍不动入口程序，读数只证明"块体不进来"）。接上它的路已经查清：`RuntimeGraphDraftSession`
+ `draft.addOperNode(type, nullptr, uri)`（算子指针留空，VM 按节点自己的 URI 解析实现，
`camel_pass.cpp` 的 `splicePrint` 就是这么插节点的）+ 把函数的出口改为读它。
届时 nodes `76 -> 77`；`operators` 会 +1，因为 OPER 节点本身就是算子节点——这一项没法"不变"。

### 7.2 那一个节点：插进去，逐项对账

同一个遍接着把块放进**外层图**：拿 `<main>` 图里出口原本读的那颗值当块入参，把出口改成读块的结果。
（`RuntimeGraphDraftSession` + `draft.addOperNode(type, nullptr, uri)`，算子指针留空，VM 按节点上的
URI 解析实现；节点的 URI 就是它的身份，不另写属性；出口的 `setNormInputs` 指向新节点。）读数：

```
BLOCK_BODY inserted block::body into the exit of <main>
BLOCK_BODY entry graphs 2 -> 2 nodes 76 -> 77
```

`std::stats` 七项逐项对账（不跑这个遍 vs 跑）：

| 项 | 变化 | 为什么这一项只能这样 |
|---|---|---|
| nodes | 76 → **77** | 块在外层图里就是 1 个节点 |
| opers | **+1** | 一个 OPER 节点就是一个算子：同一个东西的两种数法 |
| edges | 81 → **82** | 节点插在链中间：一条链多一个节点就多一条边 |
| bytes | 2860 → 2908 | 程序的字节数，节点带记录，必然变；它不是"数了什么" |
| graphs / direct_calls / indirect_calls / closures / branches / loops | **不变** | 没有调用、没有闭包、没有分支、没有图 |

三条因此是**结构性**的、不是"偏差"：`opers` +1、`edges` +1、`bytes` 变，任何"只加一个节点"的
改法都躲不开；能要求不动的只有 graphs 与后四类计数。

### 7.3 插这一格时，树上三次拒绝了它（都不是格式问题）

1. **节点的类型必须是值的类型**：第一版给了 `Int32`，提交拒绝
   `graph 'main' node 73 (13): forwards a i32 as a int` —— Camel 的 `int` 不是 `i32`。改成取出口那颗
   值的类型（`draft.header(arg)->dataType`）才对。
2. **没人声明的算子按"有副作用"对待**（`operator_table.h` 的保守读法），而一个带着图里没声明的
   副作用的节点，提交也拒绝：`reaches past the effect its own type declares`。于是块算子的记录里
   必须写清它算什么、会不会抛：这个夹具的块体只有算术，记录就写 `pure`。
3. 这两条合起来正好是**记录里要有什么**：块的算子记录不是"块体在哪"，而是**块体带来的语义**
   （副作用的行、会不会终止、会不会抛）。真块要把这些**从块体程序自己的图里推出来**
   （`OperatorTraits`，设计原则 3.3/3.9），这是下一步。

### 7.4 一条约束管线顺序的事实（不是实现细节）

`Executor::find` **不是虚函数**（`camel/execute/executor.h`），执行器的算表在它被造出来时就定死；
而 `ExecutorManager::find(uri)` 是先取协议、再造执行器、再**把 URI 去掉协议**丢给它（`uri.substr(pos+1)`，
即 `:body`）。于是：

> **块的程序必须先注册进上下文的块程序处，执行器才可能被造出来，算子才可能取到。**
> 顺序被钉死为"先编块体 → 登记 → 造执行器 → 跑"。反过来（先跑、后登记）拿不到实现，
> 且不会以"找不到算子"的形态出现，而是执行器算表里没有这一项。

同一个道理的反面：一个块在跑之前被再次编译（同名不同体），块程序处的 `add` 会保留第一次的条目
（一个块，一份程序），不会悄悄换成第二份。

### 8 名字：块体规范形式的哈希

块算子的 URI 由**块体自己的规范形式**算出，不由夹具书写：

```
uri = "block:" + hex16(fact::portableGraphHash(program, bodyGraph))
```

**输入就是树上已有的那一条规约**（`src/kernel/include/camel/runtime/fact_persist.h#portableGraphHash`），
不是本层另写的一套规范化：

- 图的**函数类型、端口、数值权限**；
- 每个节点的 **kind、flags、类型（mangled）、slot、输入、载荷**；
- **被它引用的图**（任意深度）按**名字 + 它自己的内容**计；
- **算子**按 URI + 语义键（含版本）+ traits 计；
- **静态常量**按类型与值计（标量按位，对象按打印形式）；
- **源码位置不进**哈希。

"按什么顺序规范化"因此不是这里排的：这一条规约在树里已经存在，是缓存和事实键用的那一份
（`definitions.cpp:479` 用它填 `contentHash`）。块名直接用它——另写一套规范化就是第二个形状来源，
正是"同一件事只有一个形状"要避免的。

读数：

- 四个计算不同东西的块体 → 四个不同的名字；
- **同一个块体、换一个模块名 → 名字不同**：`block:4f8cc9d670086474` 与 `block:cbd2b9368a519e15`。

第二条是**缺陷**，不是特性：`portableGraphHash` 把**图自己的名字**也算进去，于是块的身份取决于
模块给图起的名。块的名字应当只取决于块体算什么。修法方向（本轮没做）：哈希前把块体图的名字规范化，
或对"去掉名字的图"取哈希；case 已经把这个事实钉在今天的位置上
（`assert uris["twin"] != uris["body"]`），修好之后反过来写就是验收。

同一个机制还有一个正面读数：块程序处**按名字去重**。五个块体、五个名字，`store blocks=5`；
两个内容相同的块体本该并成一份——现在因为名字里带着模块名而没并上，这条修好会一起变。

### 9 记录：从块体自己的图里读出来

块的记录不再由夹具声明，而是从块体程序的图里读：

```
EffectSummary e = fact::graphEffects(facts, *program, bodyGraph);   // 走它调用的图、它跑的循环
traits.pure      = e.pure();                                        // 无外部副作用、不写变量
traits.stateOnly = !e.external && e.writesVariables;
traits.mayFault  = e.mayFault;
traits.foldable  = !e.mayNotTerminate;                              // 可能不终止的调用绝不在编译期算
```

五个块体的读数（打印为 `BLOCK_TRAITS`，由 case 断言并由 plan 钉住三行）：

| 块体 | 体是什么 | pure | mayFault | mayNotTerminate | foldable |
|---|---|---|---|---|---|
| `body` | `x + 1` | true | false | false | true |
| `second` | `x * 3` | true | false | false | true |
| `effectful` | `println(x); x * 2` | **false** | false | false | true |
| `fallible` | `10 / x` | true | **true** | false | true |

这就是 §7.3 里那两次拒绝的落点：记录里的 pure / mayFault 是**读出来的**，夹具只提供块体文本。

**一处空白要记下**：`OperatorTraits`（`src/kernel/include/camel/core/operator_table.h`）没有"会不会
终止"的字段——九个字段是 pure / elementwise / stateOnly / mayFault / callsGraphs / readsGraphs /
macro / foldable / withAfterNorm。"可能不终止"只体现在 `foldable = false` 这一处；作为记录的一项它
在 traits 里没有直接位置（`EffectSummary` 里有 `mayNotTerminate`）。这是表本身的空白，本车不擅自
加字段，先记在这里。

### 10 终止性：记录里有了自己的字段

上一节末尾那处空白已经补上。`OperatorTraits`（`src/kernel/include/camel/core/operator_table.h`）新增
`bool mayNotTerminate = false;`，与 `mayFault` **并列**、默认 `false`。理由写在字段注释里：

- 原则 3.4 的"可自由安排"是**纯、不出故障、会终止**三件，前两件在算子记录上都有位置，
  第三件原来没有，事实层就只能从 `foldable` 里猜；
- **让一个字段藏两个意思，才是要避免的形状**：`foldable` 从今天起只说"它的结果可以当值留下"，
  "可不可能不终止"由它自己的字段说；
- 这也不是新概念：效应层的 `EffectSummary` 早就有 `mayNotTerminate`，本步只是让算子层也有一个位置。

一个字段要活起来，得把**读它的每一处**都改过来，否则它只是记录里的一句话：

| 位置 | 改了什么 |
|---|---|
| `operator_semantics.h` / `operator_record.h` | 声明它的地方：模块与内置算子都能写；不写就是会终止 |
| `operator_table.cpp`（`traitsOf` 与反向写回） | 记录 ↔ traits 两个方向都带上它 |
| `fact_persist.cpp`（算子 traits 的打包） | 进哈希的第 7 位，声明了它的算子不再与没声明时同名 |
| `facts.cpp`（`termsOfOperator`） | `arrangeable` 的**终止项**改读它——这一项以前是常量 `true` |
| `operator_table.cpp`（`mayBeRecomputed`） | 可能不终止的算子，遍既不能搬、不能复制，也不能提前算 |
| `src/stdlib/test/block_body.cpp` | 块的记录由块体自己的 `EffectSummary` 填，不再兼用 `foldable` |

读数（打印为 `BLOCK_TRAITS`，case 断言、plan 钉住）：

| 块体 | 体 | pure | mayFault | **mayNotTerminate** | foldable |
|---|---|---|---|---|---|
| `body` | `x + 1` | true | false | false | true |
| `second` | `x * 3` | true | false | false | true |
| `effectful` | `println(x); x * 2` | false | false | false | true |
| `fallible` | `10 / x` | true | **true** | false | true |
| `spin` | `return spin(x)` | true | false | **true** | **true** |

最后一行是这次的关键：`spin` **会终止这件事为假**，而同一条记录里 `foldable` 仍然是 true ——
两个字段各说一件事，谁也不再替另一个人说。`mayBeRecomputed` 因此对它是 false（遍不能提前算一个
不会停的调用），而 `foldable` 那句话本身没有被偷偷改写。

`feat/tools/facts` 整目录 11/11 不变：已声明的算子都没写这个字段，所以 `arrangeable` 的四项对它们
一个也没变——这次改动对既有算子是**惰性**的，这正是"默认会终止"该有的样子。

### 11 名字里的"谁给它起名"

块体改由**块自己的模块名**编译（`kBlockModule`，一个常量），`compileBody` 不再接受调用方的模块名。
于是 `portableGraphHash` 里那个"图自己的名字"这一项不再能带进**调用方**的名字：同一个块体被两个
不同的外层模块交出，编译出来仍叫 `block::body`，哈希相同。

- 同一个块体两次 ⇒ 同一个名字（case 反过来写：`uris["twin"] == uris["body"]`）；
- 六个块体、两个是同一个 ⇒ `store blocks=5`（去重落到数上）。

**还没做到的**：哈希里剩下的那部分拼写是**块体自己**的函数名（`block::<function>`）。要让块名只由
"块体算什么"决定，需要设计 9.5 意义上的**规范形式**——结构顺序、不看名字与创建顺序——而
`portableGraphHash` 恰恰按名字和节点编号算（`ownContentHash` 第一句就是 `h.str(graph.name())`）。
这不是一次替换能完成的：需要一个按规范顺序走图、不带拼写的哈希，本步没有做，也不假装做了。

### 12 两个哈希，两种规约

块名的来源换成了新写的**规范形式哈希**（`src/kernel/include/camel/runtime/block_form.h`，
`blockFormHash`），与 `fact::portableGraphHash` 并存，谁也不替代谁：

| | `fact::portableGraphHash` | `runtime::blockFormHash` |
|---|---|---|
| 服务谁 | 缓存与事实键（`definitions.cpp` 的 `contentHash`） | 块的名字 |
| 要回答 | 两个程序/两张图**是不是同一个东西**，差一点也要分开 | 两个块体**算的是不是同一件事** |
| 因此必须含 | 图的名字、节点编号（创建顺序）、算子 traits 原样、源码位置的相邻信息 | **不含**名字、**不含**创建顺序；只含结构 |
| 参数 | 按编号 | 按**签名里的位置**（名字不进），遍历按**第一次使用的先后**到达 |
| 调用 | 按被调图的名字 | 按被调图的**规范形式**递归；回到栈上的图时记"再次调用"与距离 |

两条规约方向相反，所以是**两个函数**：把名字从缓存键里拿掉，缓存就不再能区分"同内容不同名的两份"；
把名字留在块名里，块的身份就取决于谁给它起名。把一个函数改成另一个的用途，就是把两件事并成一件。

遍历怎么走（都写在头文件里）：函数类型（mangled，参数名已被 mangle 去掉）、端口数、返回种类；
然后从**出口**出发，按端口顺序（`normInputsOf` → `withInputsOf` → `ctrlInputsOf`）深度优先，
每个节点按 kind、flags、结果类型、数值权限、属性；算子按 URI + traits，常量按位或打印形式，
循环按 loop 种类，分支按臂数。

三条判据的读数（case 断言）：

| 判据 | 读数 |
|---|---|
| ① 同一个块体换函数名（`body` 与 `renamed`，参数名也不同） | **同一个名字**（`uris["renamed"] == uris["body"]`），而 `portableGraphHash` 两者**不同**（case 断言它不同，正是"两种规约方向相反"的证据） |
| ② 不同结构不同名，同名不同体不许出现 | 九个块体 ⇒ 七个名字；`diff`（`a - b`）与 `mirror`（`left - right`，形参序 `(right, left)`）**不同名**（`uris["mirror"] != uris["diff"]`） |
| ③ 参数按规范形式里第一次使用的先后 | 遍历就是从出口沿端口走，参数在**第一次被用到**的位置进入序列；参数的身份是签名位置，所以 `a - b` 与 `b - a` 不会被并成一个 |

第二条判据在实现过程中真的抓到了一个缺陷：最初参数的载荷被当成身份，`diff` 与 `mirror` 因此并成同一个名字
（`store` 只有 6）。改成"端口位置即身份"（`graph->normPorts()`/`withPorts()` 的序号）之后，两者分开。
这正是判据 ② 存在的理由——**同名不同体**是最坏的一种错，它悄悄让两个块共用一份程序。

**已知缺口（写在头文件里，没有藏）**：区域内部（分支的臂、循环体）的节点没有走。区域里的节点表是按创建
顺序寻址的，那是拼写；这里没有它的结构入口。因此差别只在区域内、且外部节点与循环种类相同的两个块体会
同哈希。补上它需要设计 9.5 真正的规范顺序（含跨程序的编号与并列消解），不是本轮能诚实完成的。

### 13 族层的读数：`oper_not_free` 的那一项

记录层（`BLOCK_TRAITS`）证明不了"事实层读的是它"，所以另取一族读数：**事实层自己的函数**
（`camel::facts::operatorArrangeable` + 它自己的 `toString`），不是本遍写下的记录。

```
BLOCK_ARRANGE body  family=may fault all=0 terminates=1 foldable_field=1
BLOCK_ARRANGE spin  family=may fault, may not terminate all=0 terminates=0 foldable_field=1
BLOCK_ARRANGE stops family=may fault all=0 terminates=1 foldable_field=1
```

- `spin`（`return spin(x)`）⇒ 不可自由安排，族名里**点名了终止那一项**；`terminates=0`。
- `stops` 是 `spin` **把调用拿掉**的同一份体（`return x`）⇒ 同一族读数**翻了过来**（`terminates=1`，
  族名里没有终止那一项）。**"拿掉就变"** 说明这一项是从块体自己的图（`graphEffects`）读出来的，
  不是夹具声明的一个常量。
- 三条都打印 `foldable_field=1`：终止性是它自己的字段，`foldable` 没有替它说话。

`family=may fault` 是另一件事：块的记录声明了 `mayFault=false`，但算子的**缺省故障表**没被写成
"永不故障"（`set(uri, traits)` 只在显式声明时才写），所以族层照保守读法说"可能故障"。这条不影响
终止那一项，也不把两者混在一起——族名是**逐项**报的，这正是它比一个布尔值有用的地方。

### 14 两种缺失不是一种

`body` 与 `stops` 的族名里原来有 `may fault`，而块的记录明明白白写着 `mayFault=false`。原因不是"保守"，
是**把两种缺失当成了同一种**：

| 记录里 | 读者的读法 | 该读成 |
|---|---|---|
| 没写 `panic` | `mayFault() == true` | **不知道** |
| 写了 `panic = false` | `mayFault() == false` | **确定不会** |

写记录的那一处（`OperatorTable::set`）只为**显式声明**写了故障表，没声明的什么都不写；于是"确定不会"
落进了"不知道"那一格。修法与效应那一侧完全对称：`set` 在 `mayFault=false` 时把 `panic = false` **写进
记录**（`stateFaultsInRecord`，`byCpp = true`），`mayFault=true` 时**不写**——"不知道"就还是"不知道"。
一句话：**没写就写"没写"，写了就写"写了"**。

改后的读数（族名逐项报，所以两种缺失在输出里分得开）：

```
BLOCK_ARRANGE body  family=freely arrangeable all=1 terminates=1 foldable_field=1
BLOCK_ARRANGE spin  family=may not terminate all=0 terminates=0 foldable_field=1
BLOCK_ARRANGE stops family=freely arrangeable all=1 terminates=1 foldable_field=1
```

- `body`/`stops`：族名里**没有** `may fault` 了——它们的体只有算术，记录现在把"不会故障"写了出来。
- `spin`：只剩 `may not terminate`。原来那条 `may fault` 也消失了，而这**不是漏**：`spin` 的体是一次
  自调用，效应走查给它的 `mayFault` 就是 `false`——它不会失败，只是不会停。**族名跟着体走，不跟着
  直觉走**；上一版把它读成"可能故障"才是错的。
- 三条 `terminates` 读数与改前**一致**（1/0/1），`foldable_field` 三条都 1。

### 15 `cost` 的 1.0，也是同一个病

`Record::relativeCostOf()` 在"没人声明代价"时返回 1.0，于是**沉默读成了一个值**：声明 1.0 的算子和没声明的
算子在读数上完全一样。改成 `relativeCostStated()` 返回 `std::optional<double>`——"没写"就是"没写"。

需要数字的读者自己选，并且在选的地方说明是它在选：`graph::relative_cost` 用 `value_or(1.0)`，注释写明
那是**这个读数**对"没声明"的处理，不是记录的答案。

三条读数（只有 `stated` 位能把后两条分开，这就是这一笔的全部）：

```
BLOCK_COST declared_eight stated=1 value=8.000000   成本函数里声明了 8.0 的算子（div）
BLOCK_COST declared_one   stated=1 value=1.000000   显式声明 1.0 的块算子：这个 1.0 是**读数**
BLOCK_COST silent         stated=0 value=1.000000   什么都没声明的算子：这个 1.0 是**读数的策略**
```

### 16 不终止不等于会失败

`spin` 的族名里没有 `may fault`，不是漏判：设计原则在"故障只从故障源产生"那一节讲明，递归过深与耗尽内存
是**运行时的资源极限**，而"会不会故障是从图推出的事实……资源极限不计入"。所以一个可能不结束的算子，
仍然是一个不会失败的算子——**它不会失败，只是不会停**。这条写进了 `termsOfOperator` 的注释里，没有写成
"暂时没算"，因为那不是欠账。

### 17 输出也要能分开：数字跟着它的来源一起印

记录里分得清、读者分得清，还差一格：**印出来的人分不清**。`camel inspect --ops` 现在每一行都带着代价和
它的来源（`src/kernel/core/module/introspect.cpp`）：

```
"cost_stated":true,"cost":8        ← 算子说的就是 8
"cost_stated":false,"cost":1       ← 没人说，这是读者的缺省
```

同一行、字面不同，不用回头查记录。`cost_stated` 不是新字段：它就是 `relativeCostStated()` 有没有值，
即"写了没有"与"没写"的区别被搬到了看得见的地方。判据：`feat/modules/op_record.plan.toml` 2/2 绿，
用例断言同时存在 `cost_stated` 真与假两类——少了任一类，说明这个字段什么都没说。
