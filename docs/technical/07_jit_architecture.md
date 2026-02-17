# FastVM JIT 架构文档

本文档描述 Camel FastVM 的 **JIT 模块实现**：将字节码在运行时编译为 x86-64 机器码，与解释器、trampoline 协同工作。JIT 通过 **slot[0] = Frame\*** 规范统一使用 Frame：动态区 slot[0] 恒存当前 Frame\*，slot[1..] 为数据槽；FUNC/TAIL 通过 trampoline 分派；OPER 通过 trampolineOper + FrameArgsView 调用算子。

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
├── mir/                      # MIR 层（与 backend 并列的中端）
│   ├── mir.h / mir.cpp       # 机器级 IR 定义、mirToString/mirPrint/mirSizeBytes
│   ├── mir_builder.h         # MirBuilder：字节码→MIR buffer
│   ├── mir_optimize.h/cpp    # 优化 pass（如 Win64 冗余 mov 删除）
│   └── mir_encode.h/cpp      # MIR→字节（当前为 x64，依赖 backend/x64 Encoder）
├── backend/
│   ├── backend.h             # CompilationUnit、CompiledCode、IJitBackend、JitEntryFn
│   ├── backend.cpp           # createBackend → X64Backend / FallbackBackend
│   ├── fallback.h            # FallbackBackend（compile/load 返回空）
│   └── x64/
│       ├── x64_backend.h/cpp # compileBytecode：构建 MIR → 优化 → 编码
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

### 2.1.1 x64 函数调用约定（ABI）详解

**重要**：汇编中的 `call`/`ret` 和 `jmp` **不会自动保存/恢复任何通用寄存器**，仅 `call` 会将返回地址压栈、`ret` 会弹栈并跳转。寄存器的保存责任由 **ABI** 约定划分：

| 类别 | SysV x64 寄存器 | Win64 寄存器 | 说明 |
|------|-----------------|--------------|------|
| **参数/返回值** | rdi, rsi, rdx, rcx, r8, r9（第 1–6 参）；rax（返回值） | rcx, rdx, r8, r9（第 1–4 参）；rax（返回值） | 调用前后均可被 callee 覆盖 |
| **Caller-saved** | 参数寄存器 + r10, r11 | rcx, rdx, r8, r9, r10, r11（易失） | **Caller 负责保存**：若调用后仍需使用，caller 须在 call 前压栈/保存，call 后恢复 |
| **Callee-saved** | rbx, rbp, r12, r13, r14, r15 | rbx, rbp, rdi, rsi, r12–r15 | **Callee 负责保存**：若 callee 使用这些寄存器，必须在返回前恢复 |

**JIT 专用寄存器约定**（内部统一为 SysV 风格）：

| 寄存器 | 用途 | 说明 |
|--------|------|------|
| **rdi** | slot base（`slot_t*`） | 所有 frame 访问 `[rdi + disp]` 的基址；**必须**在 trampoline 调用前后保存（见下） |
| **rsi** | ctx（`JitContext*`） | 传入 trampoline 的第二参数；**Caller-saved**，若 call 后仍用 ctx 需自行保存 |
| **rax** | 临时、返回值 | trampoline 返回结果在 rax；V* 算术也常用 |
| **rbx–r11** | 通用/regalloc | V* 分配及临时用 |

**为什么 `jmp`/`call` 不会自动保存上下文？**

- **`jmp`**：仅修改 PC，不压栈、不改任何寄存器。若跳转到另一段代码，原代码的寄存器状态**完全保留**；但若 `jmp` 到子例程且不 `ret` 回来，则不存在“返回后恢复”的需求。
- **`call`**：等价于 `push 返回地址; jmp 目标`。只压入**返回地址**，不保存任何 GPR。返回时 `ret` 只负责 `pop` 并跳回，**不恢复**任何寄存器。
- **责任划分**：ABI 规定哪些寄存器由 **caller** 保存、哪些由 **callee** 保存。谁使用、谁负责；汇编器/CPU 不会自动插入保存/恢复代码。

### 2.1.2 Trampoline 调用与 rdi 保存

trampoline（`trampolineFunc`/`trampolineTail`/`trampolineOper`）内部会调用 `fn(callee_slots, ctx)`，此时按 ABI 会设置 **rdi = callee_slots**。因此：

1. **从 trampoline 返回后**，rdi 已不再是 caller 的 slot base，而是 callee 的（且 callee 的 Frame 可能已被 release）。
2. **若 caller 在 call 后继续用 rdi 访问 frame**，会错误地访问已释放的 callee frame，导致逻辑错误（如 fib 中 `fib(n-1)` 返回后继续用 rdi 取 `n-2`）。

**修复**：在调用 trampoline 前 `push rdi`，返回后 `pop rdi`，保证 rdi 始终为 caller 的 slot base。MIR 已提供 `PushRdi`/`PopRdi`，`emitCallTrampoline*` 会在 FUNC/TAIL/OPER 的 call 两侧插入。

**注意**：rsi（ctx）同样是 caller-saved。当前 trampoline 实现通常不会破坏 rsi，但 **ABI 不保证**。若 JIT 在 trampoline 返回后仍需 ctx（如用于下一次调用），建议同样保存/恢复 rsi，或确认 trampoline 实现不会修改它。

### 2.2 slot[0] = Frame* 规范

- **slot[0]**：保留，恒存当前 Frame 指针（`reinterpret_cast<slot_t>(frame)`）。解释器、gotovm、casevm 在调用 JIT 入口前写入；trampoline 在 JIT→JIT 调用前写入。
- **slot[1..]**：正常数据槽，对应 Frame 动态区索引 1 起。
- **用途**：
  - trampolineOper 从 slots[0] 取 Frame，用 FrameArgsView 调用算子，无需传 Graph；
  - trampolineFunc/TrampolineTail 从 callerSlots[0] 取 caller Frame，用 `frame->get(operand)` 取参，支持 operand 为负（静态区索引）；
  - jitDebugTraceBody 从 rdi[0] 取 Frame，打印当前帧槽值。

### 2.3 slot 布局（FrameMeta 驱动）

- **编译期**：`unit.frameMeta` 来自 `graph->getExtra<FrameMeta,0>()`，用于：
  - `dynamicSlotCount = meta->runtimeDataType->size()`
  - slot 边界校验：`slotInRange(idx)`
- **运行时**：JIT 使用 `slotDisp(idx) = idx * 8`（基址 0），直接访问 `[rdi + idx*8]`。

### 2.4 FUNC 字节码 extra 打包

- **未 JIT**：`extra.raw` 低 48 位存 `Graph*`，高 16 位存调用计数；`fastop[1]` 存目标 pc。
- **已 JIT**：`extra.raw` 低 48 位存 `JitEntryFn`，`fastop[1]=0` 标识已替换。
- **接口**：`getFuncExtraPtr`、`getFuncExtraCount`、`setFuncExtraPacked`、`incFuncExtraCount`。

### 2.5 编译流水线（字节码 → 机器码）

1. **字节码 → MIR**：`X64Backend::compileBytecode` 按 opcode 调用 `MirBuilder`，生成线性 MIR buffer（控制流 + V* 指令，vreg 由 `nextVReg` 递增分配）。
2. **MIR 优化**：`optimizeMirBuffer`（如 Win64 下删除 prologue 后冗余 mov）。
3. **pcToOffset**：按 MIR 编码长度计算每条指令偏移，得到 pc → 机器码 offset。
4. **寄存器分配**：`linearScanVReg(mirBuf, …)` → `VRegAllocation`（vreg → preg）。
5. **编码**：`encodeMirBuffer(mirBuf, pcToOffset, code, asmOut, baseOffset, vregAlloc)` 输出 x64 字节流。

---

## 3. 支持的 Opcode 与约束

所有算术/比较/加载存储均通过 **MIR V* 虚拟寄存器** 生成，再经 **linearScanVReg** 分配物理寄存器（rax..r11），最后 **encodeMirBuffer** 生成机器码。

| 指令 | 说明 | 实现 |
|------|------|------|
| LADD/LSUB/LMUL/LDIV | 64 位加减乘除 | VLoadFromFrame/MemAt → VAdd/VSub/VMul/VIdiv → VStoreToFrame |
| LLE/LLT/LGT/LEQ/LNE/LGE | 64 位比较，结果 0/1 | → VCmpSetLE/VCmpSetL/…/VCmpSetNE/VCmpSetGE → VStoreToFrame |
| DADD/DSUB/DMUL/DDIV | 双精度加减乘除 | → VXmmAdd/VXmmSub/VXmmMul/VXmmDiv → VStoreToFrame |
| DLT/DGT/DEQ/DNE/DLE/DGE | 双精度比较 | → VXmmCmpSetB/A/E/NZ/BE/AE → VStoreToFrame |
| IADD/ISUB/IMUL/IDIV | 32 位整数加减乘除 | → VAdd32/VSub32/VMul32/VIdiv32 → VStoreToFrame |
| ILT/IGT/IEQ/INE/ILE/IGE | 32 位整数比较 | → VCmpSetL32/…/NE32 → VStoreToFrame |
| FADD/FSUB/FMUL/FDIV | 32 位浮点加减乘除 | VXmm32Load* → VXmm32Add/Sub/Mul/Div → VXmm32StoreToFrame |
| FLT/FGT/FEQ/FNE/FLE/FGE | 32 位浮点比较 | → VXmm32CmpSet* → VStoreToFrame |
| BRCH | 条件分支 | 仅支持简单 if-else（withCnt()==0）；VTest + VCmovnz + JzRel32/JmpRel32 |
| FUNC | 普通调用 | trampolineFunc(slots, ctx, pc) |
| TAIL | 尾调用 | trampolineTail(slots, ctx, pc) |
| JUMP | 相对跳转 | 目标 pc 由 pcToOffset 得到 |
| RETN | 返回 | VLoadFromFrame/MemAt → VRet（返回值 vreg→rax 后 ret） |
| OPER | 算子调用 | trampolineOper(slots, ctx, pc)；从 slots[0] 取 Frame，FrameArgsView 调用算子 |
| JOIN | 两路合并 | VCopy + VTest + VCmovnz，选分支值写 result 槽 |

遇到不支持 opcode 或约束不满足时，`compileBytecode` 返回 `false`，该图不进入 JIT 缓存。

---

## 4. Trampoline 实现

### 4.1 trampolineFunc / trampolineTail

**签名**：`extern "C" slot_t trampolineFunc(slot_t* callerSlots, void* ctx, size_t pc)`（Tail 同）

**逻辑**：

1. 从 `base[pc]` 取 `extraPtr`、`targetPc`、`argsCnt`。
2. 若 `targetPc != 0`：`incFuncExtraCount(extra)`，得到 `count`。
3. **目标已 JIT（targetPc==0）**：
   - `callerFrame = reinterpret_cast<Frame*>(callerSlots[0])`，用 `callerFrame->get(operands[i])` 取参（支持 operand 为负，即静态区索引）。
   - `acquireFrameForCall/Tail(g)` 得 `newFrame`，`newFrame->slotBase()[0] = newFrame`（slot[0] 规范）。
   - 调用 `fn(newFrame->slotBase(), ctx)`，返回结果。
4. **目标为解释器（targetPc!=0）**：
   - 同上取 caller Frame，拷贝参数到新 Frame，调用 `vm->call(targetPc, newFrame)`。
   - 释放 Frame，返回结果。

### 4.2 trampolineOper

**签名**：`extern "C" slot_t trampolineOper(slot_t* slots, void* ctx, size_t pc)`

**逻辑**：

1. `frame = reinterpret_cast<Frame*>(slots[0])`（slot[0] 规范）。
2. 从 `base[pc]` 取 `func`、`nargs`、`wargs`、`result`。
3. 构造 `FrameArgsView(withView)`、`FrameArgsView(normView)`，调用 `func(withView, normView, vm->context())`。
4. 将结果写入 `frame->set(result, ret)` 并返回。

**说明**：无需传 Graph，Frame 已包含 graph、staticArea、runtimeDataType 等，FrameArgsView 直接基于 Frame 实现 ArgsView。

---

## 5. 寄存器分配（regalloc）

- **可分配寄存器**：8 个（rax, rcx, rdx, rbx, r8–r11）；rax 兼作临时与返回值。
- **V* 虚拟寄存器**：字节码先转为 MIR，其中算术/比较/加载存储使用 **V* 指令**（VLoadFromFrame、VAdd、VCmpSetLE、VXmmSub、VAdd32、VXmm32Add 等），操作数为 vreg（v0, v1, …）。
- **linearScanVReg**：对 MIR 中所有 V* 指令做 **collectVRegDefUse**（def/use 收集），线性扫描得到 **vreg → preg**（0–7）；溢出时写回 frame slot。
- **编码**：**encodeMirBuffer** 根据 VRegAllocation 将每条 MIR 编码为 x64（如 VAdd → mov rax, left_preg; add rax, right_preg; mov dst_preg, rax）。
- **AllocationResult**：除 V* 的 vreg→preg 外，原有 slot 级 **linearScanAllocate** 仍用于与解释器一致的 slot 边界；JIT 路径以 V* + linearScanVReg 为主。

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

JIT OPER → trampolineOper(slots, ctx, pc)
    └─ FrameArgsView(frame from slots[0]) + func(withView, normView, context)
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
| **rdi 被 trampoline 覆盖** | FUNC/TAIL/OPER 的 trampoline 调用 | trampoline 内部调用 fn(callee_slots) 会设 rdi=callee_slots；返回后 caller 若继续用 rdi 访问 frame，会错误使用已释放的 callee frame。 | ✅ 已修复：call 前后插入 push rdi / pop rdi |
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
| trampolineOper + FrameArgsView（slot[0]=Frame*） | ✅ |
| OnDemand：incFuncExtraCount + shouldJit | ✅ |
| Always：全图预编译 + jitCache_ | ✅ |
| OPER JIT 支持 | ✅ |
| MIR V* + linearScanVReg + encodeMirBuffer | ✅（LADD/LSUB/LMUL/LDIV/LLE～LGE，DADD～DGE，IADD～IGE，FADD～FGE 等均已迁移） |
| ACCS/FILL/CALL/SCHD JIT | ❌ 未实现 |

---

## 9. 参考资料

- [06_runtime_and_execution.md](./06_runtime_and_execution.md)
- [bytecode.h](../../src/builtin/passes/sched/linear/fastvm/bytecode.h)
- [frame.h](../../src/core/context/frame.h)
