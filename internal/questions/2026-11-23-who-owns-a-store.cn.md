# 「一个 Store」由谁造、由谁结束、由谁证它对

## 事实

`facts.h` 声明了一套事实框架：`Store`（`obtain<T>` 交回 `shared_ptr<const T>`、`forget`、
`invalidateGraph`）、kind 注册表、`GraphAccess{view, version}`、`ReadLog` 与
`Dependence::Reported/Anchors`，还有 `computeAll` 与 `dump` 两个读者侧入口。`facts.cpp` 有它们的
定义。但是：

- **生产里只有一个 `facts::Store` 实例**：本车道在 `kernel/facts/control_regions.cpp:46` 起的那个，
  `static Store *store = new Store(...)`，进程级、故意泄漏，`GraphAccess.version` 恒为 `0`（冻结图
  的内容不变，所以"版本"这件事在它上面不成立），图的死讯由 `GCGraph::~GCGraph` 调
  `facts::forgetGraph(this)` 送到（`kernel/runtime/graph.cpp:3544`）。全树 `new Store` 的生产处只有
  这一处，另外两处在 `test/tools/facts/main.cpp:314`、`:414`，是测试自己造、自己调。
- **生产里注册的新框架 kind 只有两个**：`core.arrangeable`（`facts.cpp:893`，上一个车道）与本车道的
  `core.controlRegions`。`facts.h` 第 49–54 行说"效应汇总、使用索引、执行计划、控制区域、写法提取
  …它们在这里注册成 kind，compute 调已有的代码"——**树上不是这样**：这五样一个都没注册。那句话
  写的是意图，不是现状；这本身就是"一个没有实例的框架与一个没写的框架在读数上一样"的一个样本。
- **`computeAll` 与 `dump` 只有一个调用者**：`test/tools/facts/main.cpp:415`、`:428`。
- **`camel inspect --facts` 打的是旧库**：`tools/camel-cli/main.cpp:480–484` 调
  `camel::runtime::fact::dumpFacts(Context::facts(), ...)`。新库的事实今天没有任何命令能打印。
- 生产里读新框架的只有两处：`graph.cpp:208`（`operatorArrangeable`，算子级）与本车道的
  `control_regions.cpp`（图级）。
- 旧库 `runtime::fact::FactStore`（`Context::facts()`）已经是"有 owner、有命令、有七种 kind"的库：
  `core.shape`、`core.instance`、`core.reorder`、`core.effects`、`core.loopDomain`、
  `core.permissions`、`core.resources`（`kernel/runtime/core_facts.cpp:296–355`）。

于是「事实」在树上有两套互不相识的库：旧库有 owner、有 `--facts`、七种 kind；新库有
`ReadLog`/`Dependence`/`Anchors`/`merge`/`monotone` 的形状，没有 owner、没有一个命令、两个 kind。
3b 说的「五样挪进事实框架」预设了新框架能用，而它现在只有本车道借出去的那一个实例。

## 需要裁的四件

### ① Store 的生命周期：`~GCGraph` 调 `facts::forgetGraph` 这条路，是不是框架的正式形状？

现在的形状把「一个图死了」当成「一个 store 的一部分过期了」：store 还活着，只把这个图的事实丢掉。
三条路今天都有话说：

- **A 图自己宣布**（现状）：冻结图与进程级 store 是两件事，所以 `GCGraph` 的析构是唯一知道图死掉的
  地方；代价是**每个持有图的类都得记得宣布**，忘一处就漏一片事实（内存与错误答案各一半）。
- **B store 归 owner**：`Program` 或 `Context` 持有 store，store 随 owner 一起消失，图的死不必通知
  谁；代价是「跨 owner 问一个事实」不再可能，而 `stdlib/graph/read.cpp` 那些给用户看的查询正是跨
  owner 的。
- **C store 自己记活图**：`Anchor` 带 owner 的弱引用，读者发现图已死就报红；代价是每次读要问一次
  所有权，且"报红"必须是一个明确的答案而不是 `nullopt`。

若图是**冻结**的而 store 是**进程级**的，那么 A 的形状实际上是"一个进程级的东西记住了一群会死的图"；
是否把这条路写进 `facts.h`，要一个裁决。

### ② `computeAll` / `--facts` 要不要接线；若不接线，谁第一个证它是对的？

- 不接线：`facts.h` 的合同（"每个可达图的每个锚点都算到"）只有 `test/tools/facts/main.cpp` 走过
  一遍，而那是**自己造 store 自己调**——它证不了生产里那个 store 是对的，因为那个 store 的
  `GraphAccess` 是另一份。
- 接线：`inspect --facts` 一个命令只能打一套。打新库，就要把旧库七个 kind 搬过来（3b 的其余四样
  正是这件事）；打旧库，新库继续没有读者。
- 第三种：`--facts` 打两套（`FACT` 行里加一个 `library` 字段），代价是用户可见的输出里出现一个
  本来该在内部消掉的区分。

我倾向的读数（不是结论）：**没有实例的框架与没写的框架在读数上一样**，所以接不接线之前，任何
"我们已经有一层事实框架"的说法都不能被引用为读数。

### ③ 模块没装载时（`core.execPlan` 这种情况），`execPlan()` 报什么红？

执行计划的 compute 在模块里（`stdlib/std/exec/exec_plan.cpp:2043` 的 `buildExecPlan`），缓存在内核
（`GCGraph::execPlan_`）；今天能成立是因为两者编进同一个 `libcamel`。若这个 kind 由 `std::exec`
登记，模块不在时就**没有这个 kind**。读者的三种可能：

- 抛（`ControlRegions` 现在的样子）：红最响，但一个不需要执行计划的读者（例如 `inspect --facts`
  遍历所有 kind）会因为模块没装载而变红；
- `nullopt`：更安静的、也更容易被读成"没有"的红——这正是"读不到东西时默认必须是红"要防的形状；
- 调用者自己算：等于把家搬回去。

这一条正好是"执行计划不该挪"的理由：它是**接口问题**，不是搬代码。但没有裁决，`ControlRegions`
的抛是否就是标准答案，我不知道。

### ④ `shared_ptr<const T>` 与「可变的后端地址」要不要拆成两个 kind？

`obtain<T>` 交回 `shared_ptr<const T>`；`ExecPlan` 有两个 `mutable` 成员：`FastVmState fastVm`
（`codeLease`、`entryPc`、间接调用处）与 `CountedSites countedSites`，FastVM 就地写它们。两种拆法：

- **A 拆成两个 kind**：一个放"是图的函数、只读"的表（`topo`、臂入口、顺序），一个放"后端为这个
  版本造的代码"的记录。这也顺带回答 ③：模块不在时表仍然答得出，红就不必是全局的。
- **B kind 的值允许可变**：事实层不再是"算一次、只读"，`merge`/`monotone`/`stamp` 三条合同都要
  跟着改——尤其是"版本变了就重算"与"值会被写"同时成立时，谁赢。

## 本车道怎么处理

- 什么都没实现。`src/kernel/facts/include/camel/facts/store_owner.h` 是一份草案：只有声明与注释，
  没有 `.cpp`，没有一处 `#include` 它，四个问题各占一段注释。它不决定任何一条。
- `control_regions.cpp` 里那个 store 不动：它是今天唯一在工作的事实层实例，也是这四件的样本。
- 读数不变：`ControlRegions` 13 处计算 → 1；`smoke` 10/10/0；`feat/parse` 126/126/0；
  `feat/compile` `total 2669 / pass 2664 / fail 0 / known 5`。

## 不裁会怎样

- 3b 的其余四样（效应汇总、使用索引、执行计划、写法提取）**一件也挪不动**：挪哪一件都要先回答
  「它进哪个 store、store 谁造、图死了谁通知、模块不在时报什么」。第一样卡住的不是它的形状，是
  这个问题。
- 两套库继续各自长 kind，名字已经撞过一次：旧库有 `core.effects`，新库的效应汇总也要叫
  `core.effects`；撞名之后，`--facts` 打出来的两行 `FACT` 无法区分来自哪套库。
- `facts.h` 第 49–54 行继续说着一个不存在的注册表，而下一个读这行的人会以为五样已经在框架里。

## 决策者答（待作者）