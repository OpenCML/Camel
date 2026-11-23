# 事实层在生产里没有商店：一个 Store 的形状该由谁定

## 事实

事实框架已经落了几个 kind（`core.effects`、`core.arrangeable`、`core.controlRegions`），
而把它们用起来的一笔刚刚做完（`lane/facts-b3` 的第一笔）：`ControlRegions` 的计算从
**13 处**收成 **1 处**，全树唯一的建树者是 `src/kernel/facts/control_regions.cpp#buildControlRegionTree`。

量的方式是「谁还自己算」：

| 位置 | 改前 | 改后 |
| --- | --- | --- |
| `FunctionEmitter::regions_`（按图缓存） | 1 | 删除 |
| `branch_arms`、`cml`、`graph_runtime_support`（5 处）、`tiles`（2 处）、`emitter`（2 处）、3 个 test | 12 处每次重算 | 都改成查一次表 + 持共享树 |
| 合计 | **13** | **1** |

否定式读数（这一条是这个框架唯一能自证的方式）：`ControlRegions` 不再有 `build()`，读者造不出树，
构造时拿不到树即抛 `logic_error`。**清掉记录，依赖它的判断停摆报红，而不是猜一个答案。**

## 而那件事本身暴露了一个更大的事实

```
**新事实框架在生产里没有任何 Store 实例。`computeAll` 与 `--facts` 都没有接线。**
```

`ControlRegions` 这一笔为了能跑，起了一个**冻结图专用、进程级、`~GCGraph` 时调
`facts::forgetGraph` 结束**的 store。它是全仓**第一个**生产用的 store，而它不是设计出来的，
是这一笔为了让读数成立而起的。

## 要裁的四件

**① Store 的生命周期是不是这个形状。** 一个冻结图配一个进程级 store，`~GCGraph` 时注销；
那么「一个图死了」与「一个 store 停了」是两件事还是一个事件？若图可以被多个 store 持有，
`forgetGraph` 该按图还是按 store 计数？

**② `computeAll` / `--facts` 要不要接线。** 不接线的话，第一个证框架是对的人只能是某条车道；
接线的话，它是一个 pass 还是一个开关？—— 而这一条决定后面所有 kind 的验收方式。

**③ 模块没装载时，kind 该报什么红。** 具体是 `core.execPlan`：`buildExecPlan` 定义在
`std::exec` 模块里，而缓存在内核 `GCGraph::execPlan_`。挪进事实层之前必须先回答
「谁登记 `core.execPlan`」，以及模块不在时 `execPlan()` 答什么。**这是接口问题，不是搬代码。**

**④ 一个 kind 里能不能放 `mutable` 的值。** 事实层的值是 `shared_ptr<const T>`，而
`ExecPlan` 的 `fastVm` 与 `countedSites` 是 `mutable` 的——FastVM 就地写已解析地址与代码租约
（`fastvm.cpp` 6 处写）。所以要么每次读拷一份，要么把「顺序表（不可变）」与「后端地址（可变）」
拆成两个 kind。**后者会改变事实层「值是不可变的」这条定理。**

## 为什么问作者

因为 ④ 会改事实层的定理，而 ①②③ 都是接口形状。按「接口合入后只能加、不能改」，
先定形状再写实现；而这四件都不是一条车道能自己定的。

## 附带：三样**不该**挪，理由各自成立（不是遗漏）

- **执行计划**：不是图的不可变派生值（见 ④），且 compute 在模块、缓存在内核。
- **写法提取**：`extractWritings` 交出的是**一个新 Program**（还会装进 context），输入里有代价模型
  ——那是改写加效应，不是「图的一个函数」。该进事实的是 `DraftWritings` 的等价类，那属于会话库。
- **使用索引**：改前就 1 处，且冻结图内容不变所以永不过期；挪进去家数不变，只多一次版本比较。
  真正的两处家是「冻结图 / 草稿」两种表示各一份（`rewrite/fact.cpp:206` 的三目），那是 3c 的活。

## 还欠一处手工副本

debugger `program_index.cpp#regionOuter` 自己算出「每个区域的外层控制区域」喂 `visibleAt`。
本笔未转：它是工具车道的文件，且要 `GraphInfo` 持一个 `ControlRegions` 句柄。列下一笔。

## 一处读数没有基线，因此它不是结果

整层 fast 跑出 `total: 9844, pass: 9644, fail: 135`，而**基线那一跑没能送出**（构建主机
`10.126.62.88:22` 自 15:07 起超时）。135 这个数在没有基线时不成其为读数，只能按类看它落在
本笔不碰的地方（docs、lsp、grammar、capacity、记号解析 `This '<' touches 'map'`），
以及 2 个 gpu 红是主机 checkout 缺 `build/lib/tiles/Release/{tiles-module-lease,tiles-async-check}`
（二进制建在 `src/stdlib/tiles/Release/`，而 plan 找 `build/lib/tiles/`），与既有的
`plan_validation` 同因。**主机回来后要补这一次基线。**
## 决策者答（2026-11-23 15:20）

按核心抽象第 2、4、13 节作答，四件都不改事实层的定理。

**① 一个进程一个存储（随 `Context`），按图这个值作键；图死了，以它为键的条目随之删除，是同一个事件。** 不是"一个图配一个存储"。冻结图的事实长期复用，编辑会话自带一个存储、会话结束整个丢掉（第 2 节"草稿上的结果随会话结束丢掉"）。`~GCGraph` 调 `forgetGraph` 是现在的挂点；阶段 3c 图改为 GC 回收后，改成回收时删除。一个图只被这一个存储持有，`forgetGraph` 按图删。你起的那个进程级、冻结图专用的存储形状是对的，把它定为正式的那一个，挂在 `Context` 上。

**② `--facts` 接线，作为工具的查看开关，不是遍。** `camel inspect --facts` 打出某张图上各 kind 的答案；`computeAll` 给这个开关和测试用。每个 kind 的验收方式固定为：同一批程序上，事实的答案与被它取代的旧算法逐项对照（"可自由安排"那次的做法），读数进车道报告。归工具车道。

**③ 执行计划这个 kind 归 `std::exec` 登记，不叫 `core.*`。** 只为一个库服务的东西放在那个库里（内核与库的分界）：内核提供登记事实 kind 的接口，`std::exec` 登记它的执行计划 kind；`GCGraph::execPlan_` 这个内核字段删掉。会读执行计划的只有后端，后端就在这个模块里，所以读的时候模块一定已装载；万一 kind 没登记，就报"没有这个事实 kind"，不猜答案。

**④ 事实的值保持不可变，拆开：执行计划是不可变的事实，后端的可变状态不是事实。** 顺序表、臂入口、尾调用、存储复用、链接时解析好的直接地址，都在计划里一次算定；FastVM 运行中就地写的已解析地址、代码租约，是 fvm 自己的运行期状态，放在 fvm 按程序版本持有的对象里（以计划为键），不进事实存储。这不改"值不可变"这条，只是把本来就不该在一起的两样分开。

**附带的三条判断都同意**：写法提取是变换不是事实，进事实的是会话里的等价类（属于写法表）；使用索引留给 3c 合成一种表示时处理；调试器的 `regionOuter` 下一笔改成读 `ControlRegions`。没有基线的 135 红不算读数，构建主机恢复后补跑基线再看。
