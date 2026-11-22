# 执行计划归位、`std::cpp` 改为静态规划（施工计划）

> 决策者 2026-11-17。依据：负责人同日的定位——执行计划不是编译器的东西，是特定后端的一步，可以写成公共模块共用，但不强制所有后端使用；`std::cpp` 追求性能，需要尽可能静态分配内存。原则 10.5 与 `docs/design/cpp-standalone/README.cn.md` 第 5.0 节已按此改。排期：甲、乙紧接在每日审查（11-17）第二节计数返工之后做（同一批文件，一起做免得来回改）；丙在甲、乙之后。

## 现状

- 执行计划 `ExecPlan` 由运行时的图对象拥有（`include/camel/runtime/graph.h` 的 `execPlan()`，按需构建）。它看起来像图的一部分，于是各处都从它取东西。
- `std::cpp`（`src/passes/trans/cpp/`）、`tns`、`cml` 三个翻译遍与宏阶段只从它读两样：`topo`（与边一致的一个线性次序）、`branchArmEntriesOfNode`（每条臂从哪个节点开始）。这两样是图上的事实。
- 计数行、槽位、`lastUseAt`、原地复用、循环布局只有虚拟机类后端（nodevm、fastvm、taskflow）用。
- `std::cpp` 生成的程序里，数组、字符串、函数值一律是带计数的句柄（`include/camel/standalone/`）。

## 甲：图上的两条公共查询

1. 在图一侧（`runtime` 的图查询，与 `executionPredecessorsOf` 同一处）提供：一个与边一致、确定的线性次序；分支每条臂的入口。实现从现有 `ExecPlan` 的构建代码里搬出来，不另写一份。
2. `std::cpp`、`tns`、`cml`、宏阶段改用这两条查询，不再碰 `execPlan()`。
3. 验收：`git grep -n "execPlan()" src/passes/trans src/execute/macro` 为空；四处的金标不变。

## 乙：执行计划移出图

1. 新建虚拟机类后端共用的模块（建议 `src/passes/sched/common/plan/`，头文件随之移出 `include/camel/runtime/`），`ExecPlan` 与 `exec_plan.cpp`、`storage_reuse.*` 搬进去；由用它的后端构建，按图的版本缓存在后端一侧。
2. 删掉 `GCGraph` 上的 `execPlan()` 与它拥有的计划字段；`loop_frame.h` 等执行层头文件通过后端传进来的计划取行，不经图。
3. `test::plan_refs`、`camel inspect --copies` 这类读计划的工具，标明读的是"虚拟机类后端的计划"，随模块走。
4. 验收：`git grep -n "execPlan" include/camel/runtime src/runtime` 为空；`include/camel/runtime` 不再包含计划的头文件；引用检查里没有新的反向边；四个后端的计数平衡与现有读数不变。

## 丙：`std::cpp` 的静态规划

按 `docs/design/cpp-standalone/README.cn.md` 第 5.0 节：
1. 本后端自己的逃逸与最后使用分析（在图上做，线性次序取甲的查询）。
2. 定长的值放栈上；不逃出调用的放调用的区域；只活一轮的放轮次的区域；循环状态原地或两块轮换；其余退回计数句柄。
3. 原地与否在生成时决定。
4. 读数：生成的程序按 `--trace` 区域统计堆分配次数与字节，默认关。
5. 验收：热循环第一轮之后零次堆分配；`fib`、`churn`、案例训练步与计数版本对比用时与峰值内存，结果写进第 5.0 节的表；`std::cpp` 的差分用例全绿。

## 不做的

- 不让翻译遍"为了方便"再读虚拟机的计划；需要图上的事实，就加图的查询（第 8 条规矩：在表示处改，不在用的地方补）。
- 丙不改语言、不改别的后端。
