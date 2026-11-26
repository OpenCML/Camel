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
- **哪份程序由属性说**：内核读 `norm.attrs()` 里的 `block.program`（Int，序号），到块程序处取
  program 与根图，再交给 `graphCaller()` 跑。算子记录里没有任何关于块体的东西。
- **值是真的**：`41 → 42`、`5 → 15`；第三次调同一个块 `1 → 2`，发生在它自己第一次跑完之后，
  "钉住了"因此是一条值，不是"没崩"。

两条结构性事实（设计时就该写下来的）：

- 新属性键要进**属性去留表**（`node_attrs.cpp`，`inspect --attrs` 的类守卫会查）：`block.program`
  按 Int 注册，解释器与 dump 记为 Carried，ONNX 与 Tiles 记为 Refused（模型装不下"跑那份 Camel
  程序"这件事，宁可拒绝这个节点）。
- `Executor::find` **不是虚函数**，所以动态的块 URI 集合只能在执行器构造时铺进算表——顺序因此是
  "先编块体、后造执行器、再跑"，这与管线天然一致，但不能反过来。

**还差 ①的那一格**：外层图里那一个 OPER 节点还没插，所以 `std::stats` 的 nodes 现在仍是
`76`（这个遍不动入口程序，读数只证明"块体不进来"）。接上它的路已经查清：`RuntimeGraphDraftSession`
+ `draft.addOperNode(type, nullptr, uri)`（算子指针留空，VM 按节点自己的 URI 解析实现，
`camel_pass.cpp` 的 `splicePrint` 就是这么插节点的）+ 节点属性写序号 + 把函数的出口改为读它。
届时 nodes `76 -> 77`；`operators` 会 +1，因为 OPER 节点本身就是算子节点——这一项没法"不变"。

### 7.2 那一个节点：插进去，逐项对账

同一个遍接着把块放进**外层图**：拿 `<main>` 图里出口原本读的那颗值当块入参，把出口改成读块的结果。
（`RuntimeGraphDraftSession` + `draft.addOperNode(type, nullptr, uri)`，算子指针留空，VM 按节点上的
URI 解析实现；属性写序号，出口的 `setNormInputs` 指向新节点。）读数：

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
