# GPU G4–G5：要解决什么、依赖 G1–G3 的哪一部分、每步拿什么量验收

状态：计划，不含实现。G1–G3 侧能守住的部分已收口：放置与段、驻留与传输表、一个作用域的编译产物跨调用复用都已由用例守住（复用的是编译好的源；一个作用域今天算出 2 次编译，绝对次数仍是 G3-1 的剩余项）。空着的三件是设备块的身份（`mm::Block::device_` 今天没有写入者）、异步跨算子边界、按形状签名的缓存与落盘，以及自适应池。

## 起点：G4/G5 能直接用的三样事实

- **段**：一个控制区域里的同放置节点成段，段的调用写在原位置（`modules/tiles/src/region_outline.cpp#outlineRegions`），分支的臂与循环体各成一段；`BRCH`/`JOIN`/`LOOP` 仍在主机上。
- **传输表**：驻留结论是唯一的传输表（`modules/tiles/src/residency.cpp#planResidency`），只有块的类别改变的地方才插 `tiles:upload`/`tiles:download`。
- **单元**：一个作用域的编译产物记在图的目标事实 `tiles.compiled_scope` 上（`modules/tiles/src/compiled_scope.cpp#compileTypes`），第二次调用命中。

## G4：支持面

**要解决什么**：`#gpu` 里"每个算子都在设备上"这件事只在 `tiles` 能降级的算子集内成立（`modules/tiles/src/graph_regions.cpp#deviceCapable` 读的 `operations()` 表，今天 20 个名字：`neg`/`abs`/`exp`/`log`/`sqrt`/`rsqrt`/`relu`、`add`/`subtract`/`multiply`/`divide`/`maximum`/`minimum`、`matmul`、`sum_axis`/`mean_axis`/`max_axis`/`sum_to`、`sum`/`mean`）。C2 与 `transformer_step` 需要的 `permute`/`transpose`/`reshape`、`softmax`、`gelu`、`layer_norm`、`cross_entropy`、`cast` 都不在表里，作用域里遇到就是编译期拒绝；`bf16`/`f16` 与 Tensor Core、批量矩阵乘、`take`/散射加、设备上的分支与循环同样没有。G4 就是把这张表补到训练步不缺算子。

**依赖 G1–G3 的哪一部分**（逐条，缺一条这一步就只是"能编"）：

| G4 的一步 | 依赖 | 为什么在这一条上 |
|---|---|---|
| 视图与形状算子（`permute`/`transpose`/`reshape` 作为下标映射） | G1 段 + G2-2 传输表 | 视图链要在**同一段内**完成；一旦落到段边界，就变成一次整块传输，视图的零拷贝就没有了 |
| 规范化与归约（`softmax`/`layer_norm`/`mean`/`cross_entropy`） | G1 段 + G3-1 单元 | 一段内的多归约才是一次 kernel；跨段就退回"每个归约一次提交" |
| 设备上的分支与循环 | G1 段（臂/循环体各自成段） | G4 换掉的是 `BRCH`/`LOOP` 留主机这一半，段的边界不动；今天的 `LoopCond` 每轮一次主机观察（`syncs` 数得出来），换成设备上判条件 |
| bf16/f16 与 Tensor Core、批量矩阵乘、`take`/散射加 | G2-1 设备块 + G3-2 形状签名 | 元素类型与秩必须从**值自己的类型**读（`device_` 没有写入者之前，bf16 的块不是一个能被看见的值）；`?` 的形状只能靠形状签名特化 |
| 优化器与参数树进作用域 | G1 放置传播（`modules/tiles/src/placement.cpp#calleeGraphOf`） | 无标签的 `sgd`/`adam` 跟着调用点进单元；放置不跟着走，优化器就静默落回主机 |

**每步的验收量**（都是可观测的数，不是"实现了"）：

1. 算子清点：把 C2 与 `transformer_step` 的算子按 `operations()` 表分组，输出**缺口行数**（表外算子的名字逐条）；缺口为 0 才允许记 G4 完成。今天表内 20 个名字、表外至少 8 个（三个视图名、`softmax`、`gelu`、`layer_norm`、`cross_entropy`、`cast`）。
2. 视图与形状：`gpu.tiles` 的 `index_maps`/`consistency` 八条读数逐位为 0；`std::profile json=` 的 `device_work.kernels` 对同一形状**不随视图个数增长**。
3. 规范化与归约：与 CPU 的差分读数保持在现有两条界内（softmax `1e-4`、不规整 matmul `5e-3`）；`hints` 为空。
4. 设备上的分支与循环：一个 `#gpu` 循环的 `device_work.syncs` 从"每轮一次"降到"每段一次"，`toHost` 每轮是常数。
5. 低精度：同一 GEMM 在 f32 与 bf16 下 `kernels` 与 `compiles` 相同；C2 记录步相对误差 `1e-2`，`#strict` 下退回逐位。

## G5：用上已授予的许可、融合、优化器

**要解决什么**：设计原则 7.3 默认授予 `reassociate` 与 `contract`，Tiles 今天用不上（求和串行、f64 累加）；逐元素链的融合已经在了，规范化+广播乘、注意力形状链、优化器多张量融合还没有；每步的分配数不是 0。

**依赖**：G2-1 的原地写规则（`refCount() == 1` ∧ 同类型同形状 ∧ 算子允许原地 → 写同一块）是"优化器每步 0 分配"的前提；G3-3 的池上界决定峰值；G3-2 的缓存键让融合后的计划不必每次重编；G1 的放置传播把 `sgd`/`adam` 带进单元。

**每步的验收量**：

1. 并行归约与 FMA：一条归约的 `allocated_bytes` 与 `kernels` 两个数；差分从逐位改为 ulp 上界，四个调度器各一行读数（`arithmetic_bits`/`math_ulp` 已有的形态）。
2. 逐元素链扩到规范化+广播乘：`stages[0].members` 的长度与 `kernels`（链长 5 → 1 是今天已有的读数，规范化链用它作对照）。
3. 优化器多张量融合：一个训练步的 `kernels` 从"每叶子常数个"降到整棵树常数；`allocations` 每步增量 0。
4. 池上界自适应：`PoolStats` 的 `allocations`、`peakLiveBytes`、`peakIdleBytes` 三项按步读；稳态 `allocations` 增量 0 且 `peakLiveBytes` 不随步数增长。这三项今天只在 native 检查里可读，要进 `std::profile` 才能从程序级读到。

## 两段闸门（现在就固定，别等实现完再定）

- 窄门禁：`feat/modules/tiles` 与 `gpu` 层逐次记 `total/pass/fail/skip`；`skip` 只因主机没有空闲卡，不因"这台机器没有设备"。
- 程序级：C2 与 `transformer_step` 的容差表不动（初值 `1e-5`、梯度 `1e-4`、前向 `rtol=1e-4/atol=1e-5`）；出现超界先用 `grad_referee.py` 分诊，不放宽容差。

## 不在 G4/G5

多卡数据并行（G7）；CPU 路径提速与编译启动提速（G6）。这两项与 G4/G5 共用段与传输表，但改的是另一批文件。