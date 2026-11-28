# Slow 层已知红 —— 一张人工台账

**为什么有这一页**：`internal/reports/*-reds.tsv` 是 **fast 层**红表。计划里写着
`tier = "slow"` 的用例，快层物理上选不到，因此**它们红了也不进表**。于是"把一条用例移进
slow 层"这个动作可以在台账上**静默消红**：2026-11-24 与 2026-11-27 各有两条用例是
**带着红**被移进 slow 的，移完就再没有台账看它们。

这一页不是新的量具，也不新增任何自动检查（见协调者铁律 17）。它只是把**已经知道的红**
记在案，并写明是哪一步把它们的可观测性拿掉的。**移层的人在移层的那一笔里往这里加一行。**

## 台账

| 用例 | 层 | 判定 | 证据（原文摘录） | 何时离开快层 |
|---|---|---|---|---|
| `export_loop_for` | slow | 真缺陷，长期 | `export_loop_node.cml:25:47: [Error]: An error value reached the exit of 'main' without being handled: Export…` —— 导出拒绝 `sweep` 的区间循环 `for i in 0 .. int(sum(x))`，用例期望 PASS | `a5cd47367c9`（11-24，「261 条超过 2 s 的用例移出快层」）；**11-24 四张快层红表逐名都有它** |
| `ort_core_slice` | slow | 真缺陷，长期 | `ort core slice: 5 failing` —— 一处 `sweep: export failed: has failed must be opened before the program ends…`，其余是形状声明名与 ONNX 自推不自洽：`start: Slice … -> declared ['output_d0'], inferred ['unk__1']` | 同上 `a5cd47367c9`；**11-24 四张快层红表逐名都有它** |
| `pass_export_equals_export_model` | slow | 真缺陷，**不是成本超支** | `13.82 s cpu of 75.00 s budget` —— **预算内**；红在内容：`test/cases/modules/onnx/pass_export.py:114` 的 `ort.InferenceSession(by_op)` 抛异常（per-op 模型 ORT 载不进去）；上一行 `export_train_step_bn.cml: pipeline exit 0, program executed: False` | `c3f87277a7d`（11-27，「tests: cost outranks the directory a case lives in」） |
| `resnetbn_train_step_export` | slow | 真缺陷，**不是 CPU 预算** | camel 真挂：`benchmarks/resnetbn/model.cml:255:7: [Error]: Out of memory in pass 'std::run': The graph space (runtime graphs) is exhausted while encoding graph …`。`255.80 s cpu of 199.00 s budget`（`onnx.plan.toml:791` `cpu_budget_ms = 199000`、`timeout_ms = 300000`）只是跑到该错误所花的时长。图形空间默认 256 MB（`docs/cli.md:562`，`--spaces graph_mb`） | 同上 `c3f87277a7d` |
| `ort_loop_loaded_weights` | slow | **XFAIL 标记陈旧**，不是新红 | `known failure changed its mind: expected "kind: ExportUnsupported, message: in 'arm4.1.pb': …" got "expected exit 0, got exit 1"`；用例自己 `ort_loop_loaded_weights.py:59 onnx.load(m.onnx)` 抛 `FileNotFoundError`（导出没写文件），已不是"以那句拒绝收场" | `known_fail = true`，本来就不进红表（runner 单列） |

## 读这一页要注意的三件事

1. **它们不在 73/74 表里，不是"表漏了"**：表是 fast 层的，这些用例 `tier = "slow"`，快层选不到
   （在快层完整日志里按名精确匹配，`export_loop_for`/`ort_core_slice` 0 次命中；`ort_core_slice`
   的 3 次命中全是 `ort_core_slice_grad`/`export_core_slice*` 这类别的用例）。
2. **两次移层的理由都成立**（都是成本型、快层预算 220 s），问题只在于**移层同时把可观测性拿掉了**。
   正确的补救不是把它们移回快层（那会让快层超预算），而是**移层时留一行**。
3. **里面没有一条是负载敏感**：`[load] 1m 23.72 > 8` 只影响计时可比性，不影响这五条的判定。

## 背后的活（不是一件事）

- `export_loop_for` + `ort_core_slice`：**同一个导出能力缺口** —— 区间循环（`for i in 0 .. int(sum(x))`）
  的导出被拒。两条用例只是说法不同（一条报 `main` 退出时错误未处理，一条报 `has failed must be opened`）。
- `pass_export_equals_export_model`：per-op 模型的 ORT 载入失败（`ort.InferenceSession(by_op)`）。
- `resnetbn_train_step_export`：`std::run` 编码阶段把 256 MB 图形空间打满 —— **要么抬 `graph_mb`，
  要么查 `std::run` 为什么编这么多图**，是语义/预算决定。
- `ort_loop_loaded_weights`：XFAIL 标记与现状不符，标记要重写（不是修行为）。

## 口径待定（决策者）

- **①** 给 slow 层单独一张红表（跑的层与表一一对应）：最彻底，但属"新量具进频繁流程"。
- **②** 规定"**带红移层必须在表里留一行**"：不需要新表、不需要新跑，移层那一步的人写。

本页按 **②** 的临时形态存在：人工台账。若决策者选 ①，本页并入 slow 层的正式红表。

日期：2026-11-28。来源：`lane-stdlib-reds` 只读普查（`-Plan test/plans/feat/modules/onnx.plan.toml` 全量、
`tip 09acc5fe89`、clean tree、`total 160 / pass 149 / fail 10 / known 1`）。