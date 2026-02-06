# FastVM JIT 模块架构（当前实现）

本文档描述 Camel FastVM 的 **JIT 模块当前实现**：将字节码在运行时编译为 x86-64 机器码，并与解释器、trampoline 协同工作。跨平台通过 `IJitBackend` 抽象，非 x64 使用 Fallback 后端（始终走解释器）。

---

## 1. 概述与字节码背景

- **目标**：对热点或全量图做 JIT 编译，减少解释分派与 slot 访问开销。
- **字节码**：约 50 种 opcode，定长（RETN, JUMP, LADD 等）与变长（FUNC, OPER, BRCH 等）；`slot_t = uint64_t`，Frame 的 `dynamicArea_[]` 按 `data_idx_t` 索引（正=动态区，负=静态区）。
- **当前实现**：无第三方 JIT 库，x64 使用**手写编码器**（`x64_encoder.h`）生成机器码；字节码→机器码的 lowering 与寄存器分配均在 **X64Backend** 与 **regalloc** 内完成。

---

## 2. 当前支持的指令与限制

### 2.1 已实现 JIT 的 opcode

| 指令 | 说明 | 约束 |
|------|------|------|
| **LADD** / **LSUB** | 64 位加减 | 操作数/结果可为 slot 或分配寄存器 |
| **LLE** | 64 位 ≤ 比较，结果 0/1 | 仅支持右操作数为常量 1（`fastop[1] == -1` 表示立即数 1） |
| **BRCH** | 条件分支 | 仅支持简单 if-else（`withCnt() == 0`） |
| **FUNC** | 普通调用 | 需 trampolineFunc，结果写回 Frame |
| **TAIL** | 尾调用 | 需 trampolineTail，不返回当前帧 |
| **JUMP** | 相对跳转 | 目标 pc 由第一遍扫描得到 |
| **RETN** | 返回 | 从 slot/寄存器取返回值到 rax 后 ret |

遇到上述以外的 opcode 或约束不满足时，`X64Backend::compileBytecode` 返回 `false`，该图不会进入 JIT 缓存，执行时走解释器。

### 2.2 未内联、需运行时支持的指令

- **OPER**：未在 JIT 中实现，若图内包含 OPER 则整图无法 JIT（当前实现会编译失败并回退解释器）。
- **ACCS / FILL / CALL**：同上，依赖运行时类型与闭包，当前未做 trampoline 内联。

---

## 3. 模块与目录结构（当前）

```
src/builtin/passes/sched/linear/fastvm/
├── bytecode.h/cpp
├── fastvm.h/cpp
├── gotovm.cpp
├── compile.h/cpp
│
├── jit/
│   ├── jit.h                    # 聚合 backend + jit_config
│   ├── jit_config.h             # ENABLE_JIT、JitPolicy、JitConfig
│   │
│   ├── backend/
│   │   ├── backend.h            # CompilationUnit、CompiledCode、RelocInfo、IJitBackend、createBackend
│   │   ├── backend.cpp         # createBackend → X64Backend / FallbackBackend
│   │   ├── fallback.h           # FallbackBackend（compile/load 返回空）
│   │   └── x64/
│   │       ├── x64_backend.h    # X64Backend
│   │       ├── x64_backend.cpp  # compile → compileBytecode；load/unload；两遍编码 + regalloc
│   │       └── x64_encoder.h    # 手写 x64 编码（Encoder 类，无独立 .cpp）
│   │
│   ├── regalloc/
│   │   ├── regalloc.h           # AllocationResult、linearScanAllocate
│   │   └── regalloc.cpp         # 线性扫描寄存器分配（def/use、LiveInterval、4 寄存器）
│   │
│   ├── runtime/
│   │   ├── trampoline.h         # JitContext、trampolineFunc / trampolineTail 声明（C linkage）
│   │   └── trampoline.cpp       # FUNC/TAIL 的 trampoline 实现，调用 FastVMSchedPass
│   │
│   └── tier/
│       ├── tier_policy.h         # TierPolicy（shouldJit、recordCall）
│       └── tier_policy.cpp      # 按 graph/pc 计数与阈值判断
└── (其余 fastvm 文件不变)
```

说明：**无独立 compiler/** 目录；字节码到机器码的转换在 **X64Backend::compileBytecode** 内完成，并依赖 **regalloc** 的 `linearScanAllocate`。

---

## 4. 核心数据结构与接口

### 4.1 JIT 配置（`jit_config.h`）

- **ENABLE_JIT**：在 `__x86_64__` / `_M_X64` / `__amd64__` 下为 1，否则为 0。
- **JitPolicy**：`Disabled`（仅解释）、`OnDemand`（按热点触发）、`Always`（全量 JIT）。
- **JitConfig**：`policy`、`hotThreshold`（默认 1000）、`maxCodeCacheSize`（默认 4MB）。

### 4.2 编译单元与产物（`backend/backend.h`）

- **CompilationUnit**：`graph`、`bytecodes`（span）、`entryPc`、**trampolineFunc**、**trampolineTail**（void*，由 FastVM 在构造时填入）。
- **RelocInfo**：`offset`、`kind`、`target`（当前实现中未在 X64 路径使用，预留）。
- **CompiledCode**：`code`（机器码）、`entryOffset`、`relocs`。
- **JitEntryFn**：`slot_t (*)(Frame *frame, void *ctx)`；ctx 为 **JitContext***。
- **IJitBackend**：`compile(unit)`、`registerTrampoline(name, addr)`、`load(compiled)`、`unload(fn)`。

### 4.3 寄存器分配（`regalloc/`）

- **可分配寄存器**：4 个，对应 x64 的 rax(0)、rcx(1)、rdx(2)、rbx(3)；rax 同时用作临时与返回值。
- **AllocationResult**：`slotToReg[i]` 为 slot i 的寄存器编号，或 **kSpilled**（-1）表示溢出到 Frame。
- **linearScanAllocate(bytecodes, entryPc, pcEnd)**：
  - 对 LADD/LSUB/LLE/BRCH/FUNC/TAIL/RETN 做 def/use 收集，得到每个 slot 的 firstDef/lastUse；
  - 构造 LiveInterval，按 start 排序，线性扫描分配寄存器，冲突时溢出到栈（当前实现中溢出 slot 仍通过 Frame 的 disp 访问）。

---

## 5. x86-64 后端实现要点

### 5.1 调用约定与入口

- **SysV**：`Frame*` → rdi，`void *ctx`（JitContext*）→ rsi；返回值 slot_t → rax。
- **Windows x64**：入口为 rcx=Frame、rdx=ctx；**Prologue** 将 rcx→rdi、rdx→rsi，再与 SysV 一致。
- **Frame 槽位**：`slotDisp(idx) = frameBaseOffset_ + idx * sizeof(slot_t)`；当前编码器使用 **disp8**（1 字节偏移），若槽位过多可能导致偏移超出 128，需后续扩展为 disp32。

### 5.2 编码器（`x64_encoder.h`）

- **Encoder** 向 `std::vector<uint8_t>` 追加字节；无独立 .cpp，全部内联。
- 提供：mov rax/reg ↔ Frame(disp8)、add/sub rax 与 reg/Frame、cmp rax 与 imm8 后 setle、test rax + jz rel32、jmp rel32、ret、mov rax imm64 + call rax；以及 **prologueWin64**、**callTrampolineWin64** / **callTrampolineSysV**（用于 FUNC/TAIL）。

### 5.3 编译流程（`x64_backend.cpp`）

1. **寄存器分配**：`linearScanAllocate(unit.bytecodes, entryPc, pcEnd)` → `AllocationResult alloc`。
2. **第一遍**：按 pc 顺序遍历字节码，根据 opcode 与约束计算每条指令的机器码长度，得到 **pcToOffset**；遇到不支持的指令或约束则返回 false。
3. **第二遍**：若 Windows，先 **prologueWin64**；再按 pc 顺序 **emit**：
   - LADD/LSUB：根据 alloc 取 slot 在 reg 或 Frame，用 rax 做运算，结果写回 reg 或 Frame。
   - LLE：取操作数到 rax，cmp+setle+movzx，结果写回。
   - BRCH：条件 slot 到 rax，test+jnz rel32 到 else 块入口。
   - FUNC：callTrampolineWin64/SysV(pc, trampolineFunc)，再将 rax 写回 result slot。
   - TAIL：callTrampoline + ret（不写回当前帧）。
   - JUMP：jmp rel32 到 pcToOffset[target]。
   - RETN：返回值 slot/reg → rax，ret。

### 5.4 加载与卸载

- **load**：按 4KB 对齐分配可执行内存（Windows：VirtualAlloc PAGE_EXECUTE_READWRITE；POSIX：mmap PROT_READ|PROT_WRITE|PROT_EXEC），将 `code` 拷贝进去，把首地址强转为 **JitEntryFn** 返回；并记录到 **allocatedPages_**。
- **unload**：在 allocatedPages_ 中找到对应页，VirtualFree/munmap，并从表中移除。

---

## 6. 运行时 Trampoline（`runtime/`）

### 6.1 JitContext

- **JitContext**：`FastVMSchedPass *vm`、`const void *base`（Bytecode* 基址）。由 FastVM 在 apply 时构造，作为 JIT 入口的第二个参数传入。

### 6.2 trampolineFunc（FUNC 指令）

- **签名**：`extern "C" slot_t trampolineFunc(Frame *frame, void *ctx, size_t pc)`。
- **逻辑**：从 ctx 取 vm、base；从 `base[pc].extra()` 取 targetGraph、从 fastop[1] 取 targetPc、normCnt；**acquireFrameForCall(targetGraph)**，按 operands 把当前 frame 的实参拷贝到新 frame；**invokeCallOrJit(targetPc, targetGraph, newFrame, ctx)**；**releaseFrameForCall(newFrame)**；返回结果。

### 6.3 trampolineTail（TAIL 指令）

- **签名**：`extern "C" slot_t trampolineTail(Frame *frame, void *ctx, size_t pc)`。
- **逻辑**：同上取 targetGraph、targetPc、argsCnt；**FrameView** 保存当前 frame 引用；**releaseFrameForTail(frame)**；**acquireFrameForTail(targetGraph)**；把原 frame 的实参拷到新 frame；**return invokeCallOrJit(...)**（不再返回当前 JIT 帧，故无需 release 当前帧）。

---

## 7. 与 FastVM 的集成

### 7.1 FastVMSchedPass 中的 JIT 成员（`fastvm.h`）

- **jitBackend_**：`std::unique_ptr<IJitBackend>`，在 apply 中若未初始化则 **createBackend()**。
- **jitCache_**：`std::unordered_map<Graph*, JitEntryFn>`，图→已加载的 JIT 入口。
- **jitCacheMutex_**：保护 jitCache_ 的并发写入（当前为多线程 compile、主线程 load 并写 cache）。
- **jitConfig_**：从 **FastVMConfig::enableJit** 推导（true → OnDemand，false → Disabled）。

**未使用**：当前实现中未保存 **TierPolicy** 实例；启用 JIT 时采用「对 offsetMap_ 中全部图做预编译」，而非在 call() 中按热点 recordCall/shouldJit 再编译。

### 7.2 apply() 中的 JIT 流程（`fastvm.cpp`）

1. **precompile** 后，若 `jitConfig_.policy != Disabled`：
   - 对 **offsetMap_** 中每个 (graph, entryPc)，若该 graph 尚不在 **jitCache_**：
     - 构造 **CompilationUnit**（graph、bytecodes、entryPc、**trampolineFunc**、**trampolineTail** 函数指针）；
     - 使用 **std::async** 在后台调用 **backend->compile(unit)**；
   - 主线程对每个 future **get()**，若 **compile** 返回非空，则 **load(compiled)**，在锁内写入 **jitCache_[graph]**。
2. 构造 **JitContext**（this, bytecodes_.data()）。
3. 若 **entryGraph** 在 **jitCache_** 中：**framePool_.acquire(entryGraph)**，调用 **jitCache_[entryGraph](frame, &jitCtx)**，release frame，return。
4. 否则：按原解释路径执行（push/call/pop/release）。

### 7.3 invokeCallOrJit

- 在 trampoline 中调用：若 **graph** 在 **jitCache_** 中则执行 **jitCache_[graph](frame, jitCtx)**，否则 **call(pc, frame)**（解释执行）。从而实现 JIT 与解释器互调。

---

## 8. 分层策略（tier/）与当前策略

- **TierPolicy** 提供 **shouldJit(graph, pc)**（按 policy 与 callCounts_ 达到 hotThreshold）和 **recordCall(graph, pc)**。
- **当前 FastVM 行为**：启用 JIT 时在 **apply()** 内对所有图做**异步并行编译 + 主线程 load**，不依赖 call() 中的 recordCall/shouldJit。因此 TierPolicy 目前未被 FastVM 使用，可留作日后「按热点延迟编译」时接入。

---

## 9. 跨平台与 Fallback

- **createBackend()**：在 JIT_TARGET_X64 时返回 **X64Backend**，否则返回 **FallbackBackend**。
- **FallbackBackend**：**compile** 返回 nullptr，**load** 返回 nullptr，**registerTrampoline** / **unload** 空实现；上层若未得到 JIT 入口则走解释器。

---

## 10. 实现状态与后续可做

| 项目 | 状态 |
|------|------|
| 目录与接口（backend、regalloc、runtime、tier） | ✅ 已实现 |
| JitConfig、CompilationUnit、JitEntryFn、trampoline 签名 | ✅ 已实现 |
| X64Backend + 手写 Encoder（LADD/LSUB/LLE/BRCH/JUMP/RETN/FUNC/TAIL） | ✅ 已实现 |
| 线性扫描寄存器分配（4 寄存器，溢出到 Frame） | ✅ 已实现 |
| FUNC/TAIL trampoline 与 invokeCallOrJit | ✅ 已实现 |
| FastVM apply 中全图预编译与 jitCache_ | ✅ 已实现 |
| OPER/ACCS/FILL/CALL 的 JIT 或 trampoline | ❌ 未实现 |
| 热点计数 + TierPolicy 接入（OnDemand 延迟编译） | ❌ 未接入 |
| disp8 → disp32、更多 opcode、窥孔优化 | 可选 |

---

## 11. 风险与约束

- **ABI**：JIT 代码与宿主 ABI 绑定（SysV/Windows x64），不可跨进程/跨机器复用。
- **GC**：若将来引入精确 GC，需在 JIT 代码中插入安全点。
- **调试**：需维护 pc↔机器码映射或符号信息，便于调试器与 Profiler。
- **Slot 偏移**：当前使用 disp8，Frame 槽位过多时需改为 disp32。

---

## 12. 参考资料

- [AsmJit](https://asmjit.com/)（当前未用，可作扩展参考）
- [LuaJIT 2.0 SSA-IR](https://wiki.luajit.org/SSA-IR-2.0)
- [V8 Ignition → TurboFan](https://v8.dev/blog/ignition-interpreter)
