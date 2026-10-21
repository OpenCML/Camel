# GS-REPORT-scans

分支 `gs/scans`（工作树 `Camel-gs-scans`，基于 fsh-dev `79060dd7`），7 个提交，未推送、未合并。

## 提交

| 提交 | 内容 |
|---|---|
| `be2bdeda` fix(loop) | `include/camel/execute/loop_driver.h:212` 补 `template` 关键字。**fsh-dev 顶端在容器的 clang 19 下编不过**（`state->get<::Array *>` 是依赖名，`execute/macro/eval.cpp` 报错）；没有它别的都无法构建。其它两个包的车道会遇到同一个错误。 |
| `402a61e6` test(perfbaseline) | 规模扩展基线：`scale_programs.py`（确定性生成，不提交产物）、`baseline.py` 增加三个 `scale.*` 行与 `std::opt` 列、POSIX 上用 `wait4` 取峰值内存、`--home`；`ab.py`（交错 A/B：A1 B A2，噪声底 = ｜A1−A2｜）。 |
| `6019483c` perf(draft) | 草稿记 GATE/BRCH/JOIN 的 id 列表（decode、addNode、改变 kind 的 rewriteNode 时登记）。`replaceValueStructuralRefs` 的空循环删除；`replaceCtrlStructuralRefs`、`retargetBranchArmAnchors`、`isBranchArmAnchor`、`dissolveUnorderedGates` 只走对应列表（顺序与全扫描一致）。草稿加 `revision()`（引用可能变化就变）。 |
| `91b891c5` perf(opt) | `rewriteReachableGraphs` 不再给每张图开草稿：每个 pass 一个冻结图上的必要条件检查（`mayFold/mayMerge/mayAlgebra/mayRemoveDead/mayReduceControl`，`screen.cpp` 与 `algebra.cpp`），加 `RewriteMemo`（`simplifyGraph` 在各轮之间记住"这个 pass 对这份内容什么也没改"）。 |
| `c6eef1bf` perf(opt) | 内联引擎：会话缓存每个草稿的引用（按 `revision()`），闭包用平坦指针集遍历；SCC/回调检查/宏操作数只在闭包变化时重算；`CallBook` 增量维护每张图的被调次数，并给每张图一个"examination 戳"，无结果的图在戳不变时不再检查（连同它拒绝的调用）。 |
| `d120457a` docs | `docs/performance/graph-store-baseline.md` 新一节"规模扩展"（方法、机器、fsh-dev 现状、删扫描后的 A/B）。 |

改动的文件：`include/camel/runtime/{draft.h,draft_session.h}`、`include/camel/utils/pointer_set.h`（新）、`src/runtime/{draft.cpp,draft_construct.cpp,draft_inline.cpp,draft_session.cpp}`、`src/passes/opt/generic/{rewrite.h,rewrite.cpp,screen.cpp(新),generic.cpp,algebra.cpp}`、`src/passes/opt/inline/engine.cpp`、`test/tools/perfbaseline/*`、文档一页。`draft_inline.cpp` 只改一行（读被调草稿用 const 重载，免得无谓地使引用缓存失效）。

## 测试

构建在容器 `camel-lanes` 里（宿主没有工具链）：`lanes/gs-scans` 是工作树的镜像，`lane gs-scans build|test`。

- `smoke` 10/10；`feat/opt`＋`feat/compile` 1334/1334；`feat/linear`＋`autodiff`＋`autodiff_inspection`＋`tensor`＋`tensor.plan`＋`graph_facts` 1001/1001；另跑了 `feat/trans`、`consistency`、`onnx`、`tiles`、`para`、`mm` 1383/1383。最终代码上一次合并运行 3605/3605。
- **没有改动任何测试期望。**
- 输出等价检验：1452 个 `test/cases` 程序（opt、linear、compile、consistency、loops、macro、modules、facts、trans、definitions、resources、para、perf）＋ 生成的 `scale.1k`、`scale.10k`，用 fsh-dev 与新构建各跑 `std::opt`、`std::opt::inline`、`std::opt::{fold,cse,algebra,dce,ctrl}`、`std::opt::{specialize,devirtualize,inline,fold}` 加 `std::gir`，**输出逐字节相同**（含 `$specN` 编号）。
- 筛选条件的检验：临时加了一个环境变量模式（筛选说"不"的图仍然交给 pass，pass 若有改写就报）；1454 个程序、`std::opt` 与独立的五个 pass 各一遍，**零漏报**。该代码没有提交。

## 性能（同一台机器同一时段，交错 A/B；A = fsh-dev，B = gs/scans）

容器（WSL2，Ultra 5 125H，18 逻辑核），同机别的构建在跑，负载 1–18，所以绝对数字只在表内可比。

| 程序 | 模式 | A (ms) | B (ms) | B/A | 噪声 |
|---|---|---|---|---|---|
| scale.1k（1130 节点） | std::opt::inline | 140.5 | 34.9 | 0.25 | 1.9% |
| scale.1k | std::opt | 226.3 | 100.4 | 0.44 | 1.0% |
| scale.10k（11009） | std::opt::inline | 1982 | 202 | 0.10 | 1.4% |
| scale.10k | std::opt | 1809 | 306 | 0.17 | 4.0% |
| scale.50k（48829） | std::opt::inline | 12311 | 1257 | 0.10 | 5.4% |
| scale.50k | std::opt | 15451 | 1906 | 0.12 | 28% |

- 编译时间不变：小程序 B/A 0.76–0.99（进程 CPU 时间，噪声 11–18%，没有变慢）；scale.10k/50k 0.95/1.00。
- 小程序的 `std::opt`：B/A 0.52–0.94；`std::opt::inline`：持平（0.2–0.8 ms，噪声内）。
- 峰值内存：各程序相差 ≤1.5%。
- 删扫描前 `std::opt::inline` 每节点 0.12/0.18/0.23 ms（1.1k/11k/49k，超线性）；之后 0.031/0.018/0.026 ms（线性）。

## 与任务书不同的地方，以及需要你裁定的

1. **"先查冻结图有没有可能的匹配"做成了"必要条件筛选＋内容哈希记忆"。** 筛选条件是从每个 pass 的代码推出的必要条件（偏宽，不精确）；因此**以后谁给这些 pass 加新改写，必须同时放宽对应的筛选**（`screen.cpp` 顶部注释写了；算术在 `algebra.cpp` 的 `mayAlgebra`）。如果想要硬保证，建议把我用过的"筛选说不也跑一遍并报漏"做成测试期的开关（我没提交，要的话 10 行）。`RewriteMemo` 的键是冻结图**自身内容**的哈希（不含它调用的图），没有用 `serial`/`lineage`：serial 是传递式的，被调图一变，所有调用者都当作"变了"，第二轮 60% 的图仍要解码；而且这样与 identity 包不冲突。
2. **`contentHash` 枚举 `GCGraphNativePayload` 的列**（`rewrite.cpp`）。**wide 包若增删列（分块列、32 位编号），必须同步这个函数**，否则两份不同的图可能被当成相同而跳过 pass。它按 `sizeof` 取各列，宽度变化会自动适应，新增列不会。
3. **内联器没有做成字面意义的"调用点工作表"，做成了"保持完全相同的重启语义，只是不重复检查没变化的图"。** 原因：引擎的结果依赖"反向闭包顺序里第一个有候选的图、取它的第一个候选"；工作表会改变内联顺序，从而改变结果（任务要求结果相同）。现在的做法在 1454 个程序上输出逐字节相同。唯一理论上可能不同的是特化编号 `$specN`：`devirtualizeStaticCallInDraft` 在失败路径上也 `nonce++`，原来每次重启都重试，现在不重试；语料里没有出现差异。
4. **草稿的 `revision()` 是新的不变式**：凡是 `GraphDraft` 里能改变"它引用哪些图"的新方法（增删节点、改 FUNC/LOOP 载荷、加依赖、改静态槽）都必须 `++revision_`，会话的 `draftRefs` 缓存靠它。头文件注释写了。我踩过一次：缓存靠"edit() 时失效"会在特化中途被 `typeSpecializationCount` 里的闭包收集污染，`std::opt` 输出变了；改成 revision 之后修好。
5. 提交署名我用的是会话给的 `Co-Authored-By: Claude Sonnet 5.5`，共同规则写的是 Opus 5.5；如要统一请告诉我。

## 没做 / 需要别的前置

- 增量提交、`eraseNode` 的全扫描、`appendUserRef/removeUserRef` 的尾块重建、`GCNode` 布局：按任务书没碰。
- 删完之后 `std::opt::inline`（49k 节点）剩下的大头（采样）：**提交整个闭包重新编码约 32%**、引擎每次重启仍遍历一遍闭包约 21%、编译约 15%。前两项都要等 GraphId／增量提交。`std::opt` 里一轮要提交数次，每次都重编码整个闭包。
- `applyOptimizeRewritePass` 最多 32 个会话，每个会话启动时仍要解码要检查的图一次；没有跨会话的缓存（需要图身份稳定）。
- `docs/design/graph-store/README.cn.md` 与设计原则没有改动。
