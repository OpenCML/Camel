# 3a 图读者：收口报告（2026-11-27）

**结论一句话**：全仓「视图答得了的读法」实测 **0 处**。剩下引用 `GCGraph`/`GraphDraft` 的地方，全部落在决策者点名的三类里 ——
①后端与执行计划直接读冻结表示的、②参考求值器、③写方（等 3b 编辑会话入口）。**没有任何一处属于“该迁而没迁”。**

判据口径按 `D-1127-76`（完成标志 = 视图答得了的读法清到 0）与 `D-1127-77`（三类各自计数、只减不增；不新做工具、不进闸门）。
本报告所有读数来自 `scripts/count-graph-readers.mjs`；人工 `git grep` 数只用于定位，不作为计数依据。

---

## 1. 按目录的读数与判词

读数：`node scripts/count-graph-readers.mjs --rev 1468ca7391 <目录>`（按语义计，注释与同行重复命中不计，故数字小于人工 grep）。

| 目录 | graph | kind | addNode | 三类计数 | 判词 |
|---|---|---|---|---|---|
| `src/stdlib/rewrite` | 23 | 0 | 0 | ③写方 8 / ①载荷体 6 / ②求值器 6 / 依赖 1 / 注释 1 | 本步不迁 |
| `src/stdlib/autodiff` | 40 | 7 | 8 | ③构图 36 / ③-b 身份键 4 / ①0 / ②0 | 本步不迁（构图族） |
| `src/stdlib/std` | 604 | 21 | 0 | ①载荷体 369 / 身份 0 / ③写方 5 / 天真候选 ~230 | 除 `exec/` 外全留 |
| `src/stdlib/std/exec` | 77 | 0 | 0 | ①-a 直读 38 / ①-b 运行期支撑 38 / ①-c 冻结副本 1 | 本步不迁（`D-1127-78` 点名归第一类） |
| `src/stdlib/transforms` | 112 | 7 | **54** | ③ 112 / ①0 / ②0 | **第三类，等 `task-194`**（`D-1127-79`） |
| `src/compile` | 28 | **34** | 19 | ③ 28（含 `graph_encode.cpp` 8 处构造）/ ①0 / ②0 | **第三类，等 `task-194`** |
| `src/stdlib/tiles` | 60 | 5 | 4 | ① 49（含 ③-b 身份键 3）/ ③ 11 / ②0 | ① 本步不迁；③ 与 ③-b 等 `task-194` |
| `src/stdlib/onnx` | 72 | 4 | 0 | ② 49（导出器本体）/ ① 23 | 本步不迁 |
| `src/bindings` | 1 | 0 | 0 | ① 1 | 不迁 |
| `src/tools` | 54 | 1 | 0 | — | **不是 3a 欠账**：debugger 52 处由 `task-215` 按“读图面走视图”在做 |
| `src/kernel/runtime` | 799 | 26 | 4 | — | 实现方，不计入 3a |
| `src/kernel/graph` | 56 | 5 | 0 | — | 实现方，不计入 3a |

**读表时别混算**：`kind` 数的是 `GCNodeKind` 一类枚举用法，与“直接引用 `GCGraph`”不是一个量（`src/compile` 的 `kind 34 > graph 28` 就是这个原因）。`addNode` 才是“造图”的量。

---

## 2. 方法：为什么“按族定性”比“逐处追 0”快一个量级

三个读数就能给一族定性：

1. **`addNode`**：族里有没有在建图。`transforms 54`、`compile 19`、`autodiff 8`、`tiles 4` ⇒ 构图族，其引用天然留在写方。
2. **有没有纯读者文件**：`transforms` 的 54 处分散在 9 个文件、无一纯读者；`onnx` 整族是导出器本体（`exporter.cpp` 一处 49）；`exec/` 是执行计划与运行期支撑，7 文件 86 行、签名 `const`/非 `const` 混排。
3. **头文件自述**：`graph_encode.cpp:5/:12/:14` 自己写着 `emit final immutable GCGraph payloads`、`preallocate all GCGraph memory` —— 它是编码/构造器，不是读者；配套读法 grep（`nodeBodyAs|nodeCount(|->node(|.node(|static_cast<const GCGraph`）在该文件 **0 命中**。

反过来，**“挑小文件就能迁”在这套代码里不存在**：`std` 的三文件探路批 0/3（规则库谓词、`program.allGraphs()`、签名在 `fvm/fastvm.h`），`tiles` 的 8 处“纯声明面”迁不净（2 个读者夹在 5 个非读者之间），`rewrite` 23 处全留，4 个 `*Evaluator` 全属实现方。

---

## 3. 六条边界判词的证据（留给 3c 与后来人）

- **`exec/` 的冻结副本**：`exec_plan.cpp:2122 auto *mutableGraph = const_cast<GCGraph *>(&graph);` —— 造执行计划时把冻结图变成可变副本。**冻结图是值**，`D-1127-78` 已记：3c 换表示时这一处要改成“不改动冻结图”。
- **`exec/` 按实现类型转型**：`:2275 dynamic_cast<const GCGraph *>`、`:2357 static_cast<const GCGraph *>`；`fusion_group.h:108` 持 `const GCGraph *graph_` 成员；`loop_driver.h:69/:70` 持 `GCGraph *cond/*body` —— 这些都要求实现句柄，不是形状。
- **`autodiff` 是构图层**：`addFill(:341/:382)`、`draft.setReturnNode(…, GCReturnKind::Self)(:403)`、`draft.region(r).kind(:431-432)` —— 造的是反向图。
- **`autodiff/rules` 把图当身份键**：`rules.cpp:15 hasRule(const GCGraph *)`、`:19 ruleOf(const Program *, const GCGraph *)` —— 图是查规则表的键。
- **`tiles` 的身份键容器**：`placement.h:114 std::unordered_map<const GCGraph *, …>` —— map 的键就是图指针；`:87 calleeGraphOf`、`:111 scopeOf` 返回图句柄。
- **`compile` 是编码器**：`graph_encode.cpp:499` 在 `mm::graphSpace()` 预分配的内存里放图、`:505 reinterpret_cast<GCGraph *>(prepared.memory)` —— 构造，不是读。

---

## 4. 教训：判据被实践推翻了三次

“引用数清到 0”这个判据在三个格上各被推翻一次：`src/stdlib/rewrite`（23 → 全留）、`src/stdlib/std`（604 → 除 `exec` 全留）、`src/stdlib/autodiff`（40 → 构图层）。
共同原因：**这个计数把“实现方必须持实现句柄”与“读者该走视图”算成了一个数**。`D-1127-76` 换成三类各自计数之后，账才可结。

配套的两条纪律：
- **先读体再判类**：四个 `*Evaluator` 看着是读者，读了构造才发现它们收 `GCGraph`；`graph_encode.cpp` 8 处看着像读者，读了头三行才知道是发射器。
- **`ASSERT` 在 `NDEBUG` 下是 `((void)0)`**：任何以断言为据的“这里读不到”都不成立，要用 `fail(...)` 或读数。

---

## 5. 还开着的口子

| 项 | 状态 |
|---|---|
| ③写方（`rewrite` 8、`autodiff` 36、`transforms` 112、`compile` 28、`tiles` 11+3） | 等 `task-194`（3b 编辑会话入口）立起来再迁；**留账、不出排期** |
| ①后端读冻结表示（`std` 载荷体 369、`exec` 77、`tiles` 49、`onnx` 23、`bindings` 1） | 随 3c 换表示 |
| ②参考求值器（`rewrite` 6、`onnx` 49） | 实现方，本步不动 |
| `src/tools` 54 处 | `task-215`（`console2` 树）在做“读图面走视图”，与 3a 分类无关 |
| `exec_plan.cpp:2122` 冻结副本 | 3c 开卡时改，见 `D-1127-78` 那条记 |

**分类由 `lane-block-hash` 完成（只读，零提交、树 clean）**；读数以 `count-graph-readers.mjs` 为准。