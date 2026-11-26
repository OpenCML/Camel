# CC 稿的需求在 integration 上的现状（2026-11-25）

对照两份清单：CC 稿 `ISSUES.md` 第二节（归档仓库 `CC2027/paper/ISSUES.md`），以及路线图 §0、§2 与 11-18 车道图的 J、K、L、M。核对在 `integration`（2dcda93c7）上做，用的是 `git show`/`git grep`。

## CC 稿第二节

| 条 | 内容 | 现状 | 缺什么 |
|---|---|---|---|
| 1 | `edit`/`view` 块 | 部分 | `graph.cml` 有 `order`/`inline`/`outline`，但仍是 `open`…`commit` 的写法；没有块语法、句柄作用域与 `add_param`，草稿不记编辑种类 |
| 2 | 提交按编辑种类选检查（R3） | 机制已在 | `resources.h` 的分档；还没有 E8 的读数 |
| 3 | 图版本回收、原生持有者 pin/unpin | 部分 | 有 `gc_root_window` 计划；路线图仍把它列为未关闭 |
| 4 | 学到的值绑定后再变换 | 部分 | 只有 `--arg` 给的参数成为常量 |
| 5 | `int8` 量化规则集 | 未开始 | 整个规则集 |
| 6 | 求导 `vjp = transpose(split(jvp(f)))` | 部分 | 旧引擎还在，路线五在做 |
| 7 | ONNX 导出按数据收集的循环 | 未开始 | `exporter.cpp` 遇收集值的循环直接拒绝 |
| 8 | TVM 出口 | 部分 | 只有 Python 胶水，要在定版上重跑 |
| 9 | 优化遍挂 `#impl`、规范比对 | 部分 | 有逐字节比对计划；C++ 遍没挂到 Camel 定义下 |
| 10 | `std::cxx::saturate` 与 `opt::saturate` | 部分 | 两者各自独立 |
| 13 | 运行时注入的环境 | 部分 | 只有 `--devices` |
| 16 | `std::default` 是 `std::fvm` | 已裁未改 | 测量中（D-1124-88） |

11、12、14、15 是稿件措辞或读数重跑，不是代码条目，没有核对。

## 实验

- 有现成计划：E3（`cxx_isolation`）、E9（`e9.plan.toml`）、E6（onnx 各计划）。
- 还没有工具：G1、E8、V1、X3、C1、L1。
- 依赖上面未开始的项：C4（`int8`）、E5 与 E6 的梯度模型（循环导出、求导合一）。

## 我方计划没覆盖到的

`int8` 量化、学到的值绑定、循环导出（只记在路线图"重构之后的新特性"）、TVM 出口、优化遍挂 `#impl`、几项实验工具。负责人 2026-11-25 定：瘦身不停，8 格里常驻 3 格做其中较难的几项，见 `internal/plans/2026-11-25-lane-queue.cn.md`。
