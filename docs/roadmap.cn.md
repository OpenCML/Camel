# 工作路线与待办

本文件只记录当前顺序、依赖和尚未完成的工作。完成的条目直接删除，历史见提交记录。语义以 [`docs/design-principles.cn.md`](design-principles.cn.md) 为准，最新裁定以 `fsh-dev` 的 `docs/handover/reviews/` 为准。

## 1. 当前基线

- 编译器、图 IR、泛型与类型包、规则语言、多后端执行、ONNX/C++ 导出、Python 绑定及编辑工具已经具备。
- 最近一轮完整快层没有非预期失败；已有 `known_fail` 仍是需要逐项消减的真实欠账。
- 开发分支已收束到本地 `develop`。已合内容保留归档标签；负责人专用引用、探针和仍被工作树使用的分支不按普通已合分支删除。

当前重点不是继续铺新功能，而是关闭内存安全、自动微分迁移和图语义三类结构性缺口。

## 2. 近期顺序

### 1. 关闭 GC 根持有窗口（最高优先级）

- 找全 pass 运行期创建、只被 C++ 持有的 `Program` 和图版本，明确每个持有者的 `pin/unpin`。
- 不用“所有 Program 永久 root”掩盖问题，避免图空间、字节码和 JIT 表持续增长。
- 两条 `*_large_bytecode` 在 poison 和移动压力下转绿；修好后加入不依赖 poison 的回归用例。
- 真实程序上的内存安全失败不标 `known_fail`。

入口：`docs/handover/questions/2026-11-06-who-roots-a-program-a-pass-holds-while-a-call-runs.cn.md`。

### 2. 完成库层自动微分迁移

- 收口反向 `has_aux` 和泛型 `with_grad` 组合，不保留前向实现与反向目标并存的中间态。
- S2 从直接 `vjp` 改为真正的 `grad`，权重放回第一个 `with` 参数；复杂 Transformer 继续暴露缺规则和函数值调用问题，不绕开。
- 删除 `GCNode::tangentArgs`，切向实参身份改由类型和端口内容推导。
- 库层与旧引擎在全部自动微分用例上对齐后，删除 `buildGradientGraph`、`GradResolver` 和 `pullback.cpp`，不留双实现兼容层。
- 后续再做完整 `vmap` 覆盖、组合变换和 `remat`。

依据：`docs/design/program-transforms/README.cn.md` 与 `docs/handover/batches-2026-10-31-72h.cn.md`。

### 3. 收尾 P0-2 图语义

- 封口检查改问结构推导谓词，在同一批改动中删除 `kGCNodeFlagSyncStatement` 及其写入点。
- 完成剩余控制边清理；分支、循环和调用的链位都从拥有节点到其 `JOIN`，`JOIN` 不是独立语句。
- 增加能区分删守卫前后行为的用例，不能拿本来就绿的页面当证明。

依据：`docs/design/effects/edges.cn.md`。

### 4. 形状类型与导出正确性

- `match type` 已能拆数组维度，`ShapeOf<T>`、`ElementOf`、`WithElement`、`RowsOf` 已在库层表达；不要重新加入同类 C++ 原语。
- 继续删除 `sumToInfer`、`shapeListType`、`shapeInfer` 中重复描述形状的代码，让声明和通用类型函数成为唯一来源。
- 让动态 batch 导出用例转绿。秩未知且需要前导轴归约时明确拒绝，不能猜 `Reshape` 后静默算错。
- `match type` 分支守卫仍是独立待裁问题，不把 `ShapeOf<string>` 的当前结果写成已批准语义。

### 5. 工程与性能欠账

- 迁移账本只保留“通过数不下降”，删除硬编码计数和按词分类的重复事实。
- 收敛拒绝诊断、测试选择、重复遍历和中英文文档不一致；真实程序上的编译器内部错误不挂 `known_fail`。
- `std::cml` 往返和标准库换入继续收尾，不另建账本或过程守卫。
- 下一项性能任务是字符串构造热路径 `:op/atos`、`:op/add_s`；`gc_soak` 预算不为此调整。

## 3. 后续批次

上述主线完成后，按依赖继续：

1. `vmap` 覆盖 JAX 对齐清单，`grad(vmap(f))`、`vmap(grad(f))`、`jvp(vjp(f))` 等组合及 `remat`。
2. H7 资源效应、H17 原生多维数组、H49/H50 GPU 收尾。
3. 通用语言线：内存管理、诊断、图优化、公开仓库重组和稳定化。

每项工作都保留当前后端的明确拒绝，不为追求用例通过而放宽语义或改变测试期望。

## 4. 科研与稳定化目标

- 核实并推进论文、软件著作权和专利材料；仓库中的年度进展草稿不能代替实际发表或申请证据。
- 稳定 v1.0 前安排语义冻结、内存安全、全量测试、文档与发布窗口，不把持续增加功能当作稳定化。
- 年度进展报告补齐批准号、实际经费、成果证明和最新、可复现的性能数据。

## 5. 本轮不再重复实施

- 库层 `grad = vjp(first_with_as_param(f))` 及旧 `with` 提升路径删除。
- 零数组冒充形状/值的载体删除，以及 `tree::arity` 统一为可返回“无答案”的单一名称。
- 数组维度拆解、库层 `ShapeOf`、`ElementOf`、`WithElement`、`RowsOf`。
- 可选择的删除保护、pass 边界检查和有序操作数匹配。
- 动态导出失败不再被 `let _` 静默丢弃；相关用例显式处理拒绝并锁定真实诊断。
