# FastVM JIT 架构文档

本文档描述 Camel FastVM 的 **JIT 模块实现**：将字节码在运行时编译为 x86-64 机器码，与解释器、trampoline 协同工作。JIT 不依赖 Frame 对象，直接操作 `slot_t*` 动态区；FUNC/TAIL 通过 trampoline 分派；OPER 通过 trampolineOper + SlotArgsView 调用算子。

---

## 1. 概述

### 1.1 设计目标

- **热点加速**：对热点图或全量图做 JIT 编译，减少解释分派与 slot 访问开销。
- **无 Frame 依赖**：JIT 函数接收 `slot_t*`（动态区基址），编译期用 FrameMeta 驱动 slot 布局，运行时不再依赖 Frame/Graph 对象。
- **分层策略**：支持 Disabled（仅解释）、OnDemand（热点触发）、Always（全量预编译）。

### 1.2 字节码背景

- **opcode**：约 50 种，定长（RETN, JUMP, LADD 等）与变长（FUNC, OPER, BRCH 等）。
- **slot_t**：`uint64_t`，一个槽位可存整型、指针或装箱值。
- **data_idx_t**：正数=动态区索引，负数=静态区索引相反数，0=无效。
- **Frame**：动态区 `dynamicArea_[]` 紧随对象头后；`slotBase()` 返回动态区基址。

### 1.3 目录结构

```
src/builtin/passes/sched/linear/fastvm/jit/
├── jit.h / jit_config.h      # 聚合入口、JitPolicy、JitConfig
├── backend/
│   ├── backend.h             # CompilationUnit、CompiledCode、IJitBackend、JitEntryFn
│   ├── backend.cpp           # createBackend → X64Backend / FallbackBackend
│   ├── fallback.h            # FallbackBackend（compile/load 返回空）
│   └── x64/
│       ├── x64_backend.h/cpp # compileBytecode、load/unload、slotDisp
│       └── x64_encoder.h     # Encoder：x64 机器码编码（无独立 .cpp）
├── regalloc/
│   ├── regalloc.h            # AllocationResult、linearScanAllocate
│   └── regalloc.cpp          # 线性扫描寄存器分配（8 寄存器，溢出到 slot）
├── runtime/
│   ├── trampoline.h          # JitContext、trampolineFunc/TrampolineTail/trampolineOper 声明
│   └── trampoline.cpp        # FUNC/TAIL/OPER trampoline 实现
└── tier/
    ├── tier_policy.h         # TierPolicy::shouldJit(callCount)
    └── tier_policy.cpp       # 按 callCount 与 hotThreshold 判断
```

---

## 2. 核心数据流

### 2.1 调用约定与入口

| 平台   | JitEntryFn 参数      | 入口寄存器 |
|--------|----------------------|------------|
| SysV   | `(slot_t* slots, void* ctx)` | rdi=slots, rsi=ctx |
| Win64  | 同上                 | rcx=slots, rdx=ctx；Prologue 复制到 rdi/rsi |

- **slots**：动态区基址，`slot[idx]` 位于 `slots[idx]`（8 字节对齐）。
- **ctx**：`JitContext*`，含 `vm`（FastVMSchedPass*）、`base`（Bytecode* 基址）。

### 2.2 slot 布局（FrameMeta 驱动）

- **编译期**：`unit.frameMeta` 来自 `graph->getExtra<FrameMeta,0>()`，用于：
  - `dynamicSlotCount = meta->runtimeDataType->size()`
  - slot 边界校验：`slotInRange(idx)`
- **运行时**：JIT 使用 `slotDisp(idx) = idx * 8`（基址 0），直接访问 `[rdi + idx*8]`。

### 2.3 FUNC 字节码 extra 打包

- **未 JIT**：`extra.raw` 低 48 位存 `Graph*`，高 16 位存调用计数；`fastop[1]` 存目标 pc。
- **已 JIT**：`extra.raw` 低 48 位存 `JitEntryFn`，`fastop[1]=0` 标识已替换。
- **接口**：`getFuncExtraPtr`、`getFuncExtraCount`、`setFuncExtraPacked`、`incFuncExtraCount`。

---

## 3. 支持的 Opcode 与约束

| 指令     | 说明                 | 约束 / 实现 |
|----------|----------------------|-------------|
| LADD/LSUB| 64 位加减            | 操作数/结果可为寄存器或 slot |
| LLE      | 64 位 ≤ 比较，结果 0/1 | 仅支持 rhs=常量 1 |
| BRCH     | 条件分支             | 仅支持简单 if-else（withCnt()==0） |
| FUNC     | 普通调用             | trampolineFunc(slots, ctx, pc) |
| TAIL     | 尾调用               | trampolineTail(slots, ctx, pc) |
| JUMP     | 相对跳转             | 目标 pc 由 pcToOffset 得到 |
| RETN     | 返回                 | 从 slot/寄存器取返回值到 rax 后 ret |
| OPER     | 算子调用             | trampolineOper(slots, ctx, pc, graph) |

遇到不支持 opcode 或约束不满足时，`compileBytecode` 返回 `false`，该图不进入 JIT 缓存。

---

## 4. Trampoline 实现

### 4.1 trampolineFunc / trampolineTail

**签名**：`extern "C" slot_t trampolineFunc(slot_t* callerSlots, void* ctx, size_t pc)`（Tail 同）

**逻辑**：

1. 从 `base[pc]` 取 `extraPtr`、`targetPc`、`argsCnt`。
2. 若 `targetPc != 0`：`incFuncExtraCount(extra)`，得到 `count`。
3. **目标已 JIT（targetPc==0）**：
   - `extraPtr` 为 `JitEntryFn`；`g = vm->jitFnToGraph(fn)`。
   - 用 `alloca` 分配 callee slot 区，`calleeSlots[i+1] = callerSlots[operands[i]]`。
   - 调用 `fn(calleeSlots, ctx)`，返回结果。
4. **目标为解释器（targetPc!=0）**：
   - `extraPtr` 为 `Graph*`；`acquireFrameForCall/Tail(targetGraph)`。
   - 拷贝参数到 Frame，调用 `invokeCallOrJit(targetPc, targetGraph, newFrame, ctx, count)`。
   - 释放 Frame，返回结果。

### 4.2 trampolineOper

**签名**：`extern "C" slot_t trampolineOper(slot_t* slots, void* ctx, size_t pc, GraphIR::Graph* graph)`

**逻辑**：

1. 从 `base[pc]` 取 `func`、`nargs`、`wargs`、`result`。
2. 用 `graph` 的 FrameMeta 构造 `SlotArgsView(withView)`、`SlotArgsView(normView)`。
3. 调用 `func(withView, normView, vm->context())`，将结果写入 `slots[result]` 并返回。

**SlotArgsView**：基于 `slot_t*` 实现 ArgsView，支持 `slot/setSlot/code/type`，无需分配 Frame。

---

## 5. 寄存器分配（regalloc）

- **可分配寄存器**：8 个（rax, rcx, rdx, rbx, r8-r11）；rax 兼作临时与返回值。
- **AllocationResult**：`slotToReg[i]` 为寄存器编号，或 `kSpilled`（-1）表示溢出到 slot。
- **linearScanAllocate**：对 LADD/LSUB/LLE/BRCH/FUNC/TAIL/OPER/RETN 等收集 def/use，构造 LiveInterval；跨越 call 的 interval 或 def 在 call 处的 slot 强制溢出（caller-saved 会被破坏）。

---

## 6. 分层策略与调用路径

### 6.1 TierPolicy

- **shouldJit(callCount)**：`Disabled`→false；`Always`→true；`OnDemand`→`callCount >= hotThreshold`。
- **callCount**：由解释器/gotovm 在调用 FUNC/TAIL 时通过 `incFuncExtraCount` 更新，并传入 `invokeCallOrJit`。

### 6.2 调用路径总览

```
解释器 FUNC/TAIL → invokeCallOrJit(pc, graph, frame, ctx, count)
    ├─ jitCache_[graph] 存在 → fn(frame->slotBase(), ctx)
    ├─ shouldJit(count) 且编译成功 → fn(frame->slotBase(), ctx)
    └─ 否则 → call(pc, frame)（解释执行）

JIT FUNC/TAIL → trampolineFunc/Tail(callerSlots, ctx, pc)
    ├─ targetPc==0 → fn(calleeSlots, ctx)（JIT→JIT）
    └─ targetPc!=0 → invokeCallOrJit(...)（JIT→解释器）

JIT OPER → trampolineOper(slots, ctx, pc, graph)
    └─ SlotArgsView + func(withView, normView, context)
```

### 6.3 compileAndCacheGraph 与 bytecode patch

1. 若 graph 已在 jitCache_，直接返回。
2. 加锁，双重检查，获取/安装 FrameMeta。
3. 构造 CompilationUnit，调用 `backend->compile(unit)`。
4. 若编译成功，`load(compiled)` 得到 fn，写入 `jitCache_[graph]`、`jitFnToGraph_[fn]`。
5. 遍历 bytecodes_，对所有 FUNC/TAIL 且 `fastop[1]!=0` 且 `getFuncExtraPtr(extra)==graph` 的字节码：
   - `setFuncExtraPacked(extra, fn, count)`
   - `fastop[1] = 0`

---

## 7. 潜在逻辑问题与注意事项

### 7.1 已识别风险与修复状态

| 问题 | 位置 | 说明 | 状态 |
|------|------|------|------|
| **jitFnToGraph 返回 null** | trampolineFunc/Tail | 当 targetPc==0 时，若 `vm->jitFnToGraph(fn)` 返回 nullptr 会崩溃。 | ✅ 已修复：增加 null 检查，返回 NullSlot 并打日志 |
| **closure 回调无 callCount** | evalMarkedOperator_* | map_arr/apply_arr 等调用 `invokeCallOrJit(..., 0)`，callCount 始终为 0，无法触发 OnDemand JIT。 | ⚠️ 未修复：需改动分层策略 |
| **disp8 偏移限制** | x64_encoder | slot 偏移 > 127 字节时 disp8 会截断。 | ✅ 已修复：自动选择 disp8/disp32，offset 计算同步 |
| **指针比较语义** | compileAndCacheGraph | `getFuncExtraPtr(extra) == (void*)graph` 依赖 x64 指针低 48 位唯一性。 | ✅ 已修复：比较时对 graph 做 `& kFuncExtraPtrMask` |

### 7.2 跨平台与 Fallback

- **createBackend()**：JIT_TARGET_X64 时返回 X64Backend，否则 FallbackBackend。
- **FallbackBackend**：compile/load 返回空，上层走解释器。

### 7.3 GC 与调试

- 若引入精确 GC，需在 JIT 代码中插入安全点。
- 需维护 pc↔机器码映射或符号信息，便于调试与 Profiler。

---

## 8. 参考实现状态

| 模块 | 状态 |
|------|------|
| JitEntryFn(slot_t*, void*) 与 slotBase | ✅ |
| FrameMeta 驱动 slot 布局与校验 | ✅ |
| trampolineFunc/Tail(slot_t*, void*, pc) | ✅ |
| JIT→JIT：alloca + 直连 fn(calleeSlots, ctx) | ✅ |
| JIT→解释器：Frame + invokeCallOrJit | ✅ |
| trampolineOper + SlotArgsView | ✅ |
| OnDemand：incFuncExtraCount + shouldJit | ✅ |
| Always：全图预编译 + jitCache_ | ✅ |
| OPER JIT 支持 | ✅ |
| ACCS/FILL/CALL/SCHD JIT | ❌ 未实现 |

---

## 9. 参考资料

- [06_runtime_and_execution.md](./06_runtime_and_execution.md)
- [bytecode.h](../../src/builtin/passes/sched/linear/fastvm/bytecode.h)
- [frame.h](../../src/core/context/frame.h)
