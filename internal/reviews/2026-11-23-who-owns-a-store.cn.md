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

## 决策者答（2026-11-23 15:20）

按核心抽象第 2、4、13 节作答，四件都不改事实层的定理。

**① 一个进程一个存储（随 `Context`），按图这个值作键；图死了，以它为键的条目随之删除，是同一个事件。** 不是"一个图配一个存储"。冻结图的事实长期复用，编辑会话自带一个存储、会话结束整个丢掉（第 2 节"草稿上的结果随会话结束丢掉"）。`~GCGraph` 调 `forgetGraph` 是现在的挂点；阶段 3c 图改为 GC 回收后，改成回收时删除。一个图只被这一个存储持有，`forgetGraph` 按图删。你起的那个进程级、冻结图专用的存储形状是对的，把它定为正式的那一个，挂在 `Context` 上。

**② `--facts` 接线，作为工具的查看开关，不是遍。** `camel inspect --facts` 打出某张图上各 kind 的答案；`computeAll` 给这个开关和测试用。每个 kind 的验收方式固定为：同一批程序上，事实的答案与被它取代的旧算法逐项对照（"可自由安排"那次的做法），读数进车道报告。归工具车道。

**③ 执行计划这个 kind 归 `std::exec` 登记，不叫 `core.*`。** 只为一个库服务的东西放在那个库里（内核与库的分界）：内核提供登记事实 kind 的接口，`std::exec` 登记它的执行计划 kind；`GCGraph::execPlan_` 这个内核字段删掉。会读执行计划的只有后端，后端就在这个模块里，所以读的时候模块一定已装载；万一 kind 没登记，就报"没有这个事实 kind"，不猜答案。

**④ 事实的值保持不可变，拆开：执行计划是不可变的事实，后端的可变状态不是事实。** 顺序表、臂入口、尾调用、存储复用、链接时解析好的直接地址，都在计划里一次算定；FastVM 运行中就地写的已解析地址、代码租约，是 fvm 自己的运行期状态，放在 fvm 按程序版本持有的对象里（以计划为键），不进事实存储。这不改"值不可变"这条，只是把本来就不该在一起的两样分开。

**附带的三条判断都同意**：写法提取是变换不是事实，进事实的是会话里的等价类（属于写法表）；使用索引留给 3c 合成一种表示时处理；调试器的 `regionOuter` 下一笔改成读 `ControlRegions`。没有基线的 135 红不算读数，构建主机恢复后补跑基线再看。
