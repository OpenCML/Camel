# 块身份哈希车道：现在到哪（交接）

车道 `lane/editview-a`，工作树 `D:\Projects\Camel-wt\editview-a`。审查项：决策者 D-1126-113 第 3、8、9、14 条。

## 已落

- `e3d9219c7` — `src/kernel/runtime/block_form.cpp`：`hashGraph` 进体前把 `ports_` 挪进 `outerPorts`，体走完挪回。修的是「调用返回后外层节点按被调函数的形参位置算」。**没有用例，因此这笔改动没有读数**，提交说明里已写明。
- 区域枚举的三个只读入口（不新增结构）：`src/kernel/include/camel/runtime/graph.h:753` `regionCount()`、`:756` `region(id)`（0 是体本身）、`src/kernel/runtime/canonical.cpp:176` `nodeRegion(ref)`。

## 仪器形状（下一轮从这里下笔）

```
src/stdlib/test/block_body.cpp:305-322  blocks{ {.what=..., .function=..., .text=kBodyText}, ... }
· 块体是**编译进 C++ 的文本常量**（kBodyText / kScaledText / kBranchHotText / kBranchChillText …），不在用例文件里
· 每个体编成独立程序后打 `BLOCK_BODY built <what> graphs=… nodes=…`；uri = `block:{:016x}`（blockFormHash）
· test/cases/runtime/block_body.py 读这些行做断言
```
**⇒ 已存在的缺陷盲点：`hot` / `chill` 这对区域块体早就在仪器里，但没有任何断言在比它们的 uri。** 区域缺陷一直没有读数，原因就在这里。

## 下一步三处落点（本轮范围，只到第三步）

1. `block_body.cpp`：加 `kAfterCallLeftText` / `kAfterCallRightText` —— 两个体都先调用同一个函数，回来后一个用参数 0、一个用参数 1（差异只在调用之后用哪个形参）；再各加一条登记 `{what="after_call_left"/"after_call_right", .function=...}`。
2. `test/cases/runtime/block_body.py`：断言这两条 uri **改前相同、改后不同**。
3. 读数两读，同一命令、同一批节点：`git checkout da4fdc318` 到 detached 跑一次贴哈希原文（改前同名），回本分支再跑一次（改后不同名）。

**说明：以上是对既有仪器加文本与断言，不是新造会进哈希的结构。**

## 之后（下一轮，不在本轮范围）

- 区域走遍：用 `regionCount`/`region`/`nodeRegion` 把每个区域的节点也纳入哈希（现在只有 `armCount`/`loopKind` 进哈希）；给 `hot`/`chill` 补 uri 断言，同样两读。
- 第 8 条：删 `operator_record.h` 上没人读的 `mayNotTerminate`，改 `facts.cpp` 注释；改前基线 `git grep -n mayNotTerminate` = 45 行（代码里只有 `operator_table.cpp:96` 一处填 + 该声明 + 该注释；文档/报告 40 余处引用不动）。
- 第 9 条：性质位打包与 FNV 哈希两份走样（`block_form.cpp` 少 `callsGraphs`/`readsGraphs` 两位）收成一个函数两处调用；「逐位相同」若现有入口不能同时喂两边，就报「打包收在一处」的 diff + 一处调用点原文并说明原因。
- 第 14 条：`std::cpp` 不设调用算子编号 —— 设上，或明确拒绝块算子并写理由（二选一）。