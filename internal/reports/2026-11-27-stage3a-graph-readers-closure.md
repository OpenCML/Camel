# 3a 图读者：收口报告（2026-11-27，定稿）

**结论一句话**：全仓「视图答得了的读法」实测 **0 处**。剩下引用 `GCGraph`/`GraphDraft` 的地方，全部落在决策者点名的三类里 ——
**①**后端与执行计划直接读冻结表示的（含按图指针挂的旁表，按 `D-1127-82` 并入①，随 3c 的“按名字或按值引用”一起换）、
**②**参考求值器、**③**写方（等 `task-194` 的编辑会话入口）。**没有任何一处属于“该迁而没迁”。**

口径：`D-1127-76`（完成标志 = 视图答得了的读法清到 0）、`D-1127-77`（三类各自计数、只减不增；不新做工具、不进闸门）、
`D-1127-78`（`exec/` 归第一类，类名“后端与执行计划直接读冻结表示的”）、`D-1127-79`（`transforms`/`compile` 的构图处记第三类等 `task-194`）、
`D-1127-80`（逐文件归类，不许整目录自动归类）、`D-1127-82`（旁表并入①，不另立一类）。
本报告所有计数以 `scripts/count-graph-readers.mjs` 为准；人工 `git grep` 只用于定位。

---

## 1. 按目录的读数与判词

`node scripts/count-graph-readers.mjs --rev 1468ca7391 <目录>`

| 目录 | graph | kind | addNode | 三类计数 | 判词 |
|---|---|---|---|---|---|
| `src/stdlib/rewrite` | 23 | 0 | 0 | ③写方 8 / ① 6 / ② 6 / 依赖 1 / 注释 1 | 本步不迁 |
| `src/stdlib/autodiff` | 40 | 7 | 8 | ③构图 36 / ①(旁表身份键) 4 / ②0 | 本步不迁（构图族） |
| `src/stdlib/std` | 604 | 21 | 0 | ① 298（190 载荷 + 108 旁表）· ③ 45 · 头文件面 206（逐行给类）· 注释/别名 3 | 本步不迁，逐文件表见 §2 |
| `src/stdlib/transforms` | 112 | 7 | **54** | ③ 112 | **第三类，等 `task-194`** |
| `src/compile` | 28 | **34** | 19 | ③ 28（含 `graph_encode.cpp` 8 处构造） | **第三类，等 `task-194`** |
| `src/stdlib/tiles` | 60 | 5 | 4 | ① 49 · ③ 11 | 本步不迁 |
| `src/stdlib/onnx` | 72 | 4 | 0 | ② 49（导出器本体）/ ① 23 | 本步不迁 |
| `src/bindings` | 1 | 0 | 0 | ① 1 | 不迁 |
| `src/tools` | 54 | 1 | 0 | — | **不是 3a 欠账**：debugger 52 处由 `task-215` 按“读图面走视图”在做 |
| `src/kernel/runtime` | 799 | 26 | 4 | — | 实现方，不计入 3a |
| `src/kernel/graph` | 56 | 5 | 0 | — | 实现方，不计入 3a |

**读表时别混算**：`kind` 是 `GCNodeKind` 一类枚举用法，与“直接引用 `GCGraph`”不是一个量（`compile` 的 `kind 34 > graph 28` 即此）；`addNode` 才是“造图”的量。

---

## 2. `src/stdlib/std` 的 62 文件逐行表（`task-224`，决策者认）

**对账**：实现文件 42 个 = **398** 处；头文件 20 个 = **206** 处；合计 **604** ✓（逐文件求和 = 脚本 `graph=604`）。

| 模式 | 文件数 | 处数 |
|---|---|---|
| ① 载荷/形状直读（`nodeBodyAs`、`node(ref)->字段`） | 22 | 190 |
| ① 旁表身份键（图指针当 `set/map` 键） | 14 | 108 |
| ③ 写方会话/强制（`session`/`draft`/`const_cast`） | 8 | 45 |
| 头文件签名面（类 = 其实现文件的类） | 20 | 206 |
| 注释/别名 | 3 | 3 |
| **视图可答** | **0** | **0** |

**头文件判法（逐行，不用“整族迁办”当类名）：头文件的类 = 其实现文件的类** —— 声明与实现必须同面，不可能只改一半。
例：`fvm/fastvm.h` ①（`fastvm.cpp` 旁表）· `cpp/function_emitter.h` ①（载荷 58 行）· `prl/taskflow.h` ①（`taskflow.cpp` ①+旁表）·
`exec/graph_runtime_support.h` ① · `nvm/nodevm.h` ①。两个无独立实现的（`exec/loop_driver.h`、`macro/runtime.h`）写明依据。

**三个“同一文件三种面并存”的实证**（正是“不许整目录归类”的理由）：
- `fvm/fastvm.cpp` 34 处全为旁表：`:53 fastVmEntryPcOf`、`:62 setFastVmEntryPcOf`、`:82 jitEntry`、`:93 flags` —— 值 = JIT 元数据。
- `cpp/program_emitter.cpp` 31 处：`:219 unordered_set<const GCGraph*> seen`、`:267/:268/:285/:321 unordered_map<const GCGraph*,…>` —— 把图收进容器做 **SCC 与尾调用分组**，图是“图的图”里的节点身份（`:279` 的 `const_cast` 是**原有**，非本轮引入）。
- `cml/cml.cpp` 29 处 = ①11 + 旁表 11 + ③3 + 注释/别名 2；`gir/json/gir_canonical.cpp` 23 处 = 旁表 12 + ①11。

---

## 3. 方法：三读数给一族定性

1. **`addNode`**：族里有没有在建图。`transforms 54`、`compile 19`、`autodiff 8`、`tiles 4` ⇒ 构图族。
2. **有没有纯读者文件**：`transforms` 的 54 处分散在 9 个文件、无一纯读者；`onnx` 整族是导出器本体（`exporter.cpp` 一处 49）。
3. **头文件自述**：`compile/graph_encode.cpp:5/:12/:14` 自己写着 `emit final immutable GCGraph payloads`、`preallocate all GCGraph memory`；
   配套读法 grep（`nodeBodyAs|nodeCount(|->node(|.node(|static_cast<const GCGraph`）在该文件 **0 命中** ⇒ 它是编码/构造器，不是读者。

**“挑小文件就能迁”在这套代码里不存在**（四次反例）：`std` 三文件探路 0/3（规则库谓词、`program.allGraphs()`、签名在 `fvm/fastvm.h`）、
`tiles` 8 处“纯声明面”迁不净（2 个读者夹在 5 个非读者之间）、`rewrite` 23 处全留、4 个 `*Evaluator` 全属实现方（**接口请求为 0**）。

---

## 4. 留给 3c 的证据（换表示时要看这几处）

- **冻结副本**：`exec/exec_plan.cpp:2122 auto *mutableGraph = const_cast<GCGraph *>(&graph);` —— 造执行计划时把冻结图变成可变副本。**冻结图是值**，`D-1127-78` 已记：3c 要改成“不改动冻结图”。
- **按实现类型转型**：`exec/exec_plan.cpp:2275 dynamic_cast<const GCGraph *>`、`:2357 static_cast<const GCGraph *>`；`fusion_group.h:108` 持 `const GCGraph *graph_`；`loop_driver.h:69/:70` 持 `GCGraph *cond/*body`。
- **按图指针挂的旁表**（`D-1127-82` 并入①）：`fvm/fastvm.cpp` 的 PC/长度/JIT 入口/标志、`cpp/program_emitter.cpp` 的 SCC 分组、`gir/gir_metadata.cpp:16 lookupRecord(GCGraph *)`、`prl/taskflow_frame_pool.cpp:93` 帧池、`fvm/version_state.cpp:36 fastVmStateOf`、`gir/json/gir_canonical.cpp:131` 起的一族 map。
- **写入方向的强制**：`exec_plan.cpp:2122`、`program_emitter.cpp:279`（均为原有）——3c 换“按名字或按值引用”时一并处理。

---

## 5. 教训

“引用数清到 0”这个判据在三个格上各被推翻一次（`rewrite` 23、`std` 604、`autodiff` 40）。共同原因：**这个计数把“实现方必须持实现句柄”与“读者该走视图”算成了一个数**。
换成三类各自计数之后，账才可结。配套两条纪律：
- **先读体再判类**：四个 `*Evaluator` 看着像读者，读了构造才发现收 `GCGraph`；`graph_encode.cpp` 看着像读者，读了头三行才知道是发射器。
- **`ASSERT` 在 `NDEBUG` 下是 `((void)0)`**：任何以断言为据的“这里读不到”都不成立，要用 `fail(...)` 或读数。

---

## 6. 还开着的口子

| 项 | 状态 |
|---|---|
| ③ 写方（`rewrite` 8、`autodiff` 36、`std` 45、`transforms` 112、`compile` 28、`tiles` 11） | 等 `task-194`（3b 编辑会话入口）立起来再迁；**留账、不出排期** |
| ① 后端/执行计划读冻结表示（`std` 298、`exec` 77、`tiles` 49、`onnx` 23、`bindings` 1、`autodiff` 4） | 随 3c 换表示（含旁表按名字或按值引用） |
| ② 参考求值器（`rewrite` 6、`onnx` 49） | 实现方，本步不动 |
| `src/tools` 54 处 | `task-215`（`console2` 树）在做“读图面走视图”，与 3a 分类无关 |
| `exec_plan.cpp:2122` 冻结副本 | 3c 开卡时改，见 `D-1127-78` |

**分类由 `lane-block-hash` 完成（只读，零提交、树 clean）**；`task-223`（`compile/graph_encode.cpp` 8 处）判“无一处属读、整片第三类”，**无代码可合**，故 3a 以此报告收口。