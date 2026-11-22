# 第 1 项：commit 与规范化的墙钟时间、图规模

**树**：`D:\Projects\Camel-wt\eval`，`lane-eval`，`2d7c5c3a1`（干净）。
**构建**：Release `Camel v0.2.45 (Build 261119_051158_2d7c5c3a14)`，revision `2d7c5c3a1` clean，
源指纹 `0244fca0cbac`，选项 `09afb0dfe7d8`（宿主 `88zjwei` 上经 `camel-remote.ps1` 构建；本机不构建）。
**时刻**：2026-11-19 05:15–05:35 UTC（宿主时间）。
**主机负载（作者要求写明）**：32 核，`/proc/loadavg` 在测量期间从 **23.72 走到 29.48**（1m），即每核约
0.74–0.92；**这是"有别的车道在跑"的负载，不是静读**。每条读数的 wall 与 CPU 中位数几乎相等
（差 <1%），例外是 `std::cxx::saturate` 的个别轮次（wall 280/346/766 ms，CPU 仍 116/134/130 ms），
那是负载抖动，本页以 **CPU 与 wall 中位数**为准，并把该组标为「高负载下，不与静读可比」。

## 1. 三张图的三件套

| 图 | 文件 | 图数 | 节点数 | 边数 | 区域数 | 规范文档字节 |
| --- | --- | --- | --- | --- | --- | --- |
| 案例程序 | `examples/case-study/train.cml` | 5 | 63 | 93 | 2 | 29945 |
| MLP 训练步 | `showcase/c2-mlp-mnist/train.cml` | 24 | 355 | 519 | 18 | 161076 |
| 小 transformer 训练步 | `showcase/c3-char-transformer/train.cml` | 79 | 647 | 709 | 14 | 273255 |

- 三个都是**仓库已有的例子**（MLP = `c2-mlp-mnist`，transformer = `c3-char-transformer`），没有自建。
- 节点 = `graphs[*].nodes` 求和；边 = 节点 `inputs[]` 引用求和（规范文档无 `edges` 数组）；区域 =
  `graphs[*].regions` 的条数求和（区域表本身：`Root`/`Arm`/`LoopBody`…，不是 DOT 的 egraph cluster）；
  字节 = `std::jgir canonical=true` 写到 stdout 的整份文档。
- 命令：`camel <file> std::jgir canonical=true`。

## 2. 两组墙钟（全新进程，各 9 次，报中位数与区间）

单位 ms；`中位数 [最小, 最大]`。**同一份构建、同一时段、交替测量**；每次都是一条独立的 `camel` 进程，
计时窗口只有那一个 pass（`--time-phases` 的 `pass` 阶段记录，`wall_ns`/`cpu_ns`）。

| 图 | ① 规范化：一次（`nodes(graph_of)`） | ② 规范化：每图一次（`graphs_of` 全走） | ③ commit：`open`+`commit` 无编辑 |
| --- | --- | --- | --- |
| train.cml | **1.14 [1.11, 1.40]** | **16.39 [16.13, 16.98]** | **1.24 [1.16, 1.39]** |
| mlp-mnist | **1.57 [1.51, 1.72]** | **17.15 [16.72, 17.96]** | **1.56 [1.49, 3.47]** |
| char-transformer | **1.85 [1.77, 2.31]** | **17.45 [16.91, 18.19]** | **1.94 [1.88, 2.36]** |

读数与前提（缺一不可）：

1. **①②走的就是 `CanonicalWalk`/`same_graph` 那条路**：`graph::nodes` 在 `modules/graph/read.cpp` 里问
   `canonicalNodeOrder`，和规范文档、`same_graph` 同一个解算器（`src/runtime/canonical.cpp`）。
2. **②比①贵约 10–14 倍，不是因为图更大，而是因为每调一次就重解一次**：①只调一次 `nodes()`（根图，
   一次解算覆盖全程序），②按 `graphs_of` 逐图调（train 5 次、mlp 24 次、transformer 79 次），
   每次调用各自从头解算。所以②虽然图规模差 10 倍，三个数都落在 ~17 ms——第一（根图）那次主导。
   ③=A pass 逐图读 `nodes` 时付的就是②这一档：`edits::drop_print`（真改写 + 真 commit）在 train 上
   17.63 [17.07, 18.17] ms，与②几乎相同，说明那条改写的时间几乎全花在"逐图读规范顺序"上。
3. **③是 `open(p)` + `commit(t)` 且中间没有编辑**：它包含 `open` 的开销，**不是纯 commit**。
   上界由 `std::cxx::inline` 给出：train 上 inline 改写了 63 个节点并 commit（文档 29945 → 45289 字节），
   整条 pass 只用 **0.85 [0.78, 0.96] ms**，所以这张图上一次**真 commit** ≤0.85 ms。
   把 `open` 与 `commit` 分开需要计时原语放进 `graph::commit`（或一个 `timed` 探针），本项没有做，
   也不拿估计顶替——两个数就是这两个窗口，写明白各自包住了什么。
4. **同进程内重复 15 次会变慢**：`--timeit canonical_walk::walk/15` 在 train 上是
   39.84 [16.30, 41.91] ms（第一轮 16.30，之后 ~40），比全新进程的 16.39 ms 高 2.4 倍。
   所以本页以**全新进程**为主读数；同进程复跑那组只作为"重复测量会漂"的证据，不当绝对值用。

## 3. 一条流水线的每个 pass（`std::cxx::inline` → `std::cxx::saturate` → `std::jgir`）

| 图 | `std::cxx::inline` | `std::cxx::saturate` | 流水线总（含编译） |
| --- | --- | --- | --- |
| train.cml | **0.85 [0.78, 0.96]**，文档 29945→45289（改） | **111.2 [109.4, 115.8]**，文档不变（29945，同 sha） | 137.6 |
| mlp-mnist | **拒绝**：`RuntimeDiag::PassEffectOrder`（`fs:read_bytes` 与 `:struct/copy_arr` 在 `main` 同一区域无先后，commit 拒收） | **134.1 [124.7, 346.7]**，CPU 133.9，文档 161076→162343（改） | 46.2（止于 inline） |
| char-transformer | **失败**（exit 1，未改动图；原因见 NOTES §2.4） | **138.7 [125.6, 766.2]**，CPU 130.3，文档 273255→273485（改） | 114.9（止于 inline） |

- **"每个 pass 的 commit 开销"只有这一档能报**：阶段计时把 scope 放在**整个 pass** 外面
  （`include/camel/utils/phase_timer.h`："scopes are only placed around whole phases (a module, a pass),
  never inside a loop over nodes"），commit 在 pass 内部，没有更细的 scope。所以上表是**每个 pass 的总时间**，
  其中包含该 pass 自己的 commit；单独的 commit 窗口见 §2 ③ 与 §2 第 3 条的上下界。
- `saturate` 在 train 上 ~111 ms 却没改文档（同 sha），在两张更大的图上改了文档（字节 +1267/+230）；
  它是否算"真优化"要看规则集，本项只报时间与文档是否变化。
- 流水线总时间取自 `--time-phases` 的 `total_ns`（含编译、加载、DOT 前的全部阶段），不是三行相加。

## 4. 量不到的、以及不拿来顶替的

- **两个更大的例子跑不起来**（与时间无关，但它们只是"图源"）：`c2-mlp-mnist` 的 `main` 要
  `--arg data=...` 且缺 `mnist/*.npy`；`c3-char-transformer` 的 `main` 要 `corpus`。它们的**编译与图**都在
  （jgir / 我们的探针都跑），但没有它们的运行时间读数——本项也不需要运行时间。
- **`std::cxx::inline` 在 mlp-mnist 上被 commit 拒绝**（上面引了原文），在 transformer 上 exit 1；
  这不是本项的时间读数，是第 2 项（真实 pass 过 commit）的材料，这里只记"没拿到时间"。
- **高负载**：本组读数在 1m load 23.7–29.5（32 核）下取得，wall 与 CPU 中位数几乎相等，故判为可比；
  但 `saturate` 的 max 列（280/346/766 ms）是负载尖峰，**不与静读可比**，不要引用 max 当典型值。
- 没有用任何旧数据：本页每个数都是这次构建、这次时段量到的。

## 5. 复现（探针源码就在本目录）

```powershell
# 探针（三个 .cml，纯 Camel，不改任何源码）：
#   internal/reports/eval-round10/probes/canonical_once.cml   一次解算
#   internal/reports/eval-round10/probes/canonical_walk.cml   逐图解算
#   internal/reports/eval-round10/probes/commit_only.cml      open + commit，无编辑
camel --input examples/case-study/train.cml canonical_once::run canonical_walk::walk commit_only::touch std::jgir canonical=true --time-phases=ph.json
```

逐条（宿主上，`CAMEL_PACKAGES` 指向 `probes` 目录）：

```bash
camel examples/case-study/train.cml canonical_once::run std::jgir canonical=true --time-phases=ph.json
camel examples/case-study/train.cml canonical_walk::walk std::jgir canonical=true --time-phases=ph2.json
camel examples/case-study/train.cml commit_only::touch  std::jgir canonical=true --time-phases=ph3.json
camel examples/case-study/train.cml std::cxx::inline std::cxx::saturate std::jgir canonical=true --time-phases=ph4.json
```

每个 `ph*.json` 里读 `phases[]` 中 `name == "pass"` 的那一条的 `wall_ns`/`cpu_ns`；规模读
`std::jgir canonical=true` 的输出。取数与统计脚本、原始输出见 `NOTES.md` §2。