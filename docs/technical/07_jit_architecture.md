# FastVM JIT 架构文档

本文档描述 Camel FastVM 的 **JIT 编译 pipeline**：从字节码到 x86-64 机器码的完整流程，涵盖双入口函数布局、JIT 内部调用约定、Frame 池管理、frameless 栈分配优化、近期引入的 MIR 定向代码生成优化，以及与解释器/trampoline 的协同工作机制。

---

## 1. 总体架构

### 1.1 设计目标

- **热点加速**：对热点图或全量图做 JIT 编译，将字节码分派开销降至原生指令级别。
- **双入口函数**：每个 JIT 编译的函数包含一个 C++ ABI 包装器和一个精简 JIT 内部入口，JIT-to-JIT 调用可完全绕过 ABI 转换开销。
- **frameless 自递归优化**：对纯自递归函数（如 fib with inline），完全消除 Frame 池操作，改用原生栈分配。
- **定向代码生成优化**：优先针对热点模式做局部但高收益的改写，例如静态常量内联、立即数 MIR、比较-分支融合、局部 peephole。
- **分层策略**：支持 Disabled（仅解释）、OnDemand（热点触发）、Always（全量预编译）。

### 1.2 目录结构

```
src/passes/sched/fastvm/jit/
├── jit.h / jit_config.h      # 聚合入口、JitPolicy、JitConfig
├── mir/                       # MIR 层（中端 IR）
│   ├── mir.h / mir.cpp        # MIR 指令定义、NativeJitCallParams、mirToString 等
│   ├── mir_builder.h          # MirBuilder：字节码 → MIR buffer
│   ├── mir_optimize.h/cpp     # MIR 优化 pass
│   └── mir_encode.h/cpp       # MIR → x64 机器码编码
├── backend/
│   ├── backend.h              # CompilationUnit、CompiledCode、IJitBackend
│   ├── backend.cpp            # createBackend → X64Backend / FallbackBackend
│   └── x64/
│       ├── x64_backend.h/cpp  # 编译主流程：MIR 构建 → 优化 → 编码
│       └── x64_encoder.h      # x64 机器码编码原语
├── regalloc/
│   ├── regalloc.h             # AllocationResult、VRegAllocation、kReservedRegRbx
│   └── regalloc.cpp           # 线性扫描寄存器分配
├── runtime/
│   ├── trampoline.h           # JitContext、各 trampoline 声明
│   └── trampoline.cpp         # FUNC/TAIL/OPER trampoline 实现
└── tier/
    ├── tier_policy.h          # TierPolicy::shouldJit(callCount)
    └── tier_policy.cpp        # 分层决策逻辑
```

---

## 2. 编译 Pipeline 全流程

### 2.1 流程概览

```
                    CompilationUnit
                         │
                         ▼
                 ┌───────────────┐
                 │  字节码 → MIR  │   MirBuilder 逐条字节码生成 MIR 指令
                 └───────┬───────┘
                         │ MirBuffer（线性 MIR 指令序列）
                         ▼
                 ┌───────────────┐
                 │  MIR 优化      │   runMirOptimizationPasses（冗余 mov 删除等）
                 └───────┬───────┘
                         │
                         ▼
                 ┌───────────────┐
                 │  VReg 分配     │   linearScanVReg → VRegAllocation（vreg → preg）
                 └───────┬───────┘
                         │
                         ▼
                 ┌───────────────┐
                 │  C++ 包装器    │   生成 42 字节 Win64 ABI wrapper（原始字节）
                 └───────┬───────┘
                         │
                         ▼
                 ┌───────────────┐
                 │  MIR → x64    │   encodeMirBuffer 编码为 x64 机器码
                 └───────┬───────┘
                         │
                         ▼
                 ┌───────────────┐
                 │  跳转修补      │   JumpPatch + NativeJitFuncCall 内部 patch
                 └───────┬───────┘
                         │
                         ▼
                   CompiledCode { code[], jitEntryOffset }
```

### 2.2 CompilationUnit 结构

```cpp
struct CompilationUnit {
    GIR::Graph *graph;             // 当前编译的图（== 函数）
    FrameMeta *frameMeta;          // 包含 frameSize、runtimeDataType、staticArea
    std::span<const Bytecode> bytecodes;  // 字节码序列
    size_t entryPc;                // 入口 PC
    void *trampolineFunc;          // FUNC trampoline 地址
    void *trampolineTail;          // TAIL trampoline 地址
    void *trampolineOper;          // OPER trampoline 地址
    void *trampolineCast;          // CAST trampoline 地址
    void *poolTopAddr;             // &FramePool::top_（帧管理用）
    void *directSelfFuncInvokeAddr; // 同图调用慢路径回退函数
};
```

### 2.3 各阶段详解

#### 阶段 1：字节码 → MIR

`X64Backend::compileBytecode` 遍历字节码，调用 `MirBuilder` 生成线性 MIR：

- **算术/比较**（LADD, LSUB, LLE..LNE 等）：先尝试将静态区操作数在编译期折叠为立即数，优先生成 `VLoadImm32/VLoadImm64`、`VAddImm/VSubImm/VCmpSet*Imm`；无法立即数化时再退回普通 `VAdd/VSub/VCmpSet*`。
- **只读 slot 值缓存**：在 MIR 构建前先扫描所有 `bc.result`，识别“从未被写入”的动态槽位；对这类槽位的首次 `VLoadFromFrame` 结果缓存在 `slotCache` 中，后续再次读取同槽位时生成 `VCopy` 而不是重复 load。缓存跨普通跳转保留，仅在 `NativeJitFuncCall`/trampoline 调用后清空。
- **分支**（BRCH）：支持两种路径。
  - 普通路径：`VLoad + VTest + VCmovnz + JzRel32 + JmpRel32`
  - 融合路径：若 BRCH 的条件恰好来自紧邻比较结果，则比较阶段只发射 `VCmpRegImm` 设置 flags，BRCH 直接发射对应的 `JccRel32`，跳过布尔物化与 branch-index 物化
- **函数调用**（FUNC）：根据 `isSameGraph` 生成 `NativeJitFuncCall`（含 frameless 或 Frame-based 路径参数）或退回 trampoline 调用。对 frameless 调用，参数预加载和返回值写回都会显式体现在 MIR 中，便于后续 peephole 和 dead-store pass 继续优化。
- **返回**（RETN）：`VLoadFromFrame → VRet`（将值置于 rax 后 `ret`）。

#### 阶段 2：MIR 优化

`runMirOptimizationPasses(mirBuf)` 执行以下优化：

- **Win64 冗余 mov 删除**：在 prologue 被移到 wrapper 后，检测并删除 `mov rdi,rcx; mov rsi,rdx; mov rcx,rdi; mov rdx,rsi` 这类冗余来回复制。
- **局部 peephole**：在不构建 CFG 的前提下做线性窗口优化。
  - `VStoreToFrame(d, vA)` 后遇到安全的 `VLoadFromFrame(vB, d)`，替换为 `VCopy(vB, vA)`
  - 连续两次写同一 frame slot，若中间无读取，则删除前一次死 store
  - 在 jump target、call、native call、显式跳转等 barrier 处清空跟踪状态，避免跨控制流误优化
- **反向 dead frame store 消除**：在 peephole 之后做一次反向活跃性扫描，跟踪 `liveSlots`，删除在后续路径上不再被读取的 `VStoreToFrame` / `VXmmStoreToFrame`。该 pass 会合并 jump target 处的活跃集，并把 `NativeJitFuncCall` 的隐式 frame 读写纳入分析。
- **当前边界**：仍是线性 MIR，无 CFG/SSA，因此优化仍以局部模式匹配和保守的数据流跟踪为主，不做通用跨基本块 CSE、全局常量传播或完整 SSA DCE。

#### 阶段 3：虚拟寄存器分配

`linearScanVReg(mirBuf, &vregAlloc)` 对 MIR 中所有 `V*` 指令进行线性扫描分配：

- **可分配寄存器**：8 个物理寄存器（rax=0, rcx=1, rdx=2, rbx=3, r8=4, r9=5, r10=6, r11=7）。
- **保留寄存器**：rbx（index 3）被显式保留，不参与分配，用于缓存 `&FramePool::top_`。
- **溢出处理**：分配不到寄存器的 VReg 标记为 `kSpilled`，编码时通过 frame slot 中转或使用 `push rbx / pop rbx` 的 scratch 机制。
- **跨调用存活**：`spansCall == true` 的 VReg 不分配给易失寄存器（当前所有 allocatable 寄存器均为易失的，故跨调用 VReg 必然溢出）。

#### 阶段 4：C++ ABI 包装器生成

在 MIR 编码之前，先在代码缓冲区中写入 42 字节的原始 C++ ABI 包装器（详见第 3 节）。

#### 阶段 5：MIR → x64 编码

`encodeMirBuffer(mirBuf, code, ...)` 将每条 MIR 指令编码为 x64 机器码：

- 根据 `VRegAllocation` 将 VReg 映射到物理寄存器。
- 处理 VReg 溢出：检测 `SpillState` 链（VCopy/VTest/VCmovnz 的溢出路径）。
- 对 `VAdd/VSub/VCmpSet*` 优先走 reg-reg / reg-imm 直接编码，尽量避免 `rax` 中转。
- 对比较-分支融合后的 `VCmpRegImm + JccRel32` 直接按 flags 编码，不再插入 `setcc/movzx/test/cmov`。
- 编码 `NativeJitFuncCall`：根据 `frameless` 和 `isSameGraph` 标志选择不同路径（详见第 5 节）。
- 记录跳转修补信息（`JumpPatch`），最后统一 patch rel32。

### 2.4 近期引入的定向优化

这一轮优化并未引入 CFG，而是围绕 fib 这类热点递归路径做了 8 类局部改进：

1. **编译期静态常量内联**
   - `loadOperandToVReg()` 在静态操作数路径下直接读取 `staticArea` 的 `slot_t`
   - 生成 `VLoadImm32/VLoadImm64`，避免 `mov reg, imm64(addr); mov reg, [reg]`

2. **立即数 MIR 指令**
   - 新增 `VAddImm`、`VSubImm`、`VCmpSetL/LE/G/GE/E/NEImm`
   - 对小整数常量优先生成 `add/sub/cmp reg, imm32`

3. **比较-分支融合**
   - 新增 `VCmpRegImm`
   - 当比较结果仅被后继 `BRCH` 消费时，不再物化布尔值，而是直接发射 `cmp + jcc`

4. **reg-to-reg 直接编码**
   - 新增 `addRegReg`、`subRegReg`、`cmpRegReg`
   - `VAdd/VSub/VCmpSet*` 不再固定走 `mov rax, left; op rax, right; mov dst, rax`

5. **局部 peephole**
   - 消除安全范围内的 store-then-reload
   - 消除连续写同槽位的死 store

6. **BRCH/JOIN 定向简化**
   - fused compare-branch 路径下不再物化 branch index
   - JOIN 在“只可能从 else-path 到达”的情况下直接取 `w1`，跳过 `test/cmov` 合并链

7. **只读 slot 缓存与 call result 可见化**
   - 对只读 frame slot 的重复读取改成 `VCopy`
   - `NativeJitFuncCall` 的 frameless 参数预加载与 result store 显式体现在 MIR 中，使 peephole 可以把 call 后 reload 融合掉

8. **返回路径定向清理**
   - JUMP / JOIN 在紧邻 `RETN` 时可直接发射 `VRet`
   - 反向 dead-store pass 可删除 `VRet` 前对 frame 的尾部写回

这些优化的共同特点是：不改变整体 pipeline 结构，但显著改善热点路径的 uop 数、寄存器依赖链与内存往返。

---

## 3. 双入口函数布局（Dual-Entry Layout）

### 3.1 内存布局

每个 JIT 编译的函数在内存中的布局：

```
┌─────────────────────────────────────────┐
│  C++ ABI Wrapper (42 bytes)             │  ← offset 0（C++ 调用者入口）
│  ┌─ push rdi; push rsi; push rbx       │
│  │  push r12                            │
│  │  mov rdi, rcx    ; Win64 → JIT conv  │
│  │  mov rsi, rdx                        │
│  │  mov rbx, imm64  ; &FramePool::top_  │
│  │  mov r12, imm64  ; current Graph*    │
│  │  call body       ; rel32             │
│  │  pop r12; pop rbx; pop rsi; pop rdi  │
│  └─ ret                                 │
├─────────────────────────────────────────┤
│  JIT Body (MIR 编码后的机器码)            │  ← jitEntryOffset（JIT-to-JIT 入口）
│  ┌─ (无 prologue，直接执行)              │
│  │  ... 函数逻辑 ...                     │
│  │  ... NativeJitFuncCall 序列 ...       │
│  └─ ret (bare: 仅 0xC3)                 │
└─────────────────────────────────────────┘
```

### 3.2 入口选择

| 调用来源 | 入口偏移 | 说明 |
|----------|---------|------|
| C++ / 解释器 | `offset 0`（包装器入口） | 走完整 Win64 ABI 转换：rcx/rdx → rdi/rsi，设置 rbx/r12 |
| JIT 同图自调用 (frameless) | `jitEntryOffset`（body 入口） | 直接 `call rel32`，无 ABI 转换，rdi/rsi/rbx/r12 已就绪 |
| JIT 同图调用 (Frame-based) | `jitEntryOffset`（body 入口） | 通过 NativeJitFuncCall 内部 `call rel32` 进入 |
| JIT 跨图调用 | `offset 0`（包装器入口） | 走完整 Win64 ABI：`mov rcx,rdi; mov rdx,rsi; call rax` |

### 3.3 栈对齐约束

- Win64 ABI 要求 `CALL` 指令执行前 RSP ≡ 0 (mod 16)。
- 包装器的 4 次 push（32 字节）使 body 入口时 RSP ≡ 0 (mod 16)。
- 从 body 内执行 frameless 自调用：`push rdi` → RSP ≡ 8；`sub rsp, N`（N ≡ 0 mod 16）→ RSP ≡ 8；`call` → 被调方 body 入口 RSP ≡ 0。对齐保持一致。

---

## 4. JIT 内部调用约定

### 4.1 寄存器用途

| 寄存器 | 用途 | 生命周期 |
|--------|------|---------|
| **rdi** | 当前帧 slot 基址（`slot_t*`），所有 frame 访问通过 `[rdi + disp]` | JIT body 全程有效；NativeJitFuncCall 前后通过 push/pop 保存恢复 |
| **rsi** | `JitContext*`（含 VM 指针和字节码基址） | JIT body 全程有效，传递给跨图 C++ 调用 |
| **rbx** | `&FramePool::top_`（Frame 池顶地址的指针） | 包装器初始化，body 全程有效。**从寄存器分配池中保留**，不可被 VReg 占用 |
| **r12** | 当前 `GIR::Graph*` 地址 | 包装器初始化，用于 Frame 获取时的图比较（`cmp r11, r12`），避免 10 字节 `mov rax, imm64` |
| **rax** | 临时 / 返回值 | 调用前后不保证，用于算术中转和函数返回值 |
| **rcx, rdx** | 临时 / Win64 ABI 参数 | VReg 可分配，跨图调用时用于 ABI 参数传递 |
| **r8–r11** | 临时 / VReg 可分配 | r10/r11 也用于 NativeJitFuncCall 的 Frame 管理 |

### 4.2 编码器中的 rbx scratch 机制

由于 rbx 被保留缓存 `&FramePool::top_`，但部分编码器函数（`*FromMemAt` 系列）需要使用 rbx 作为临时寄存器加载 64 位地址，引入了安全的 scratch 对：

```cpp
void beginScratchAddr(uint64_t addr) {
    push rbx           // 保存 &pool.top_
    mov rbx, imm64     // 临时加载目标地址
}
void endScratch() {
    pop rbx            // 恢复 &pool.top_
}
```

`movRaxFromMemAt` 和 `movRegFromMemAt` 经过优化，直接将地址加载到目标寄存器自身再解引用，避免使用 rbx scratch。

---

## 5. NativeJitFuncCall：函数调用编码

`NativeJitFuncCall` 是 JIT 中最关键的"mega opcode"，负责同图/跨图函数调用的完整编码。它根据 `NativeJitCallParams` 中的标志选择不同路径。

### 5.1 NativeJitCallParams 结构

```cpp
struct NativeJitCallParams {
    uint64_t poolTopAddr;       // &FramePool::top_
    uint64_t targetGraphAddr;   // 目标 Graph*
    uint64_t slowPathFnAddr;    // 慢路径回退函数地址
    uint64_t slowPathBcAddr;    // 慢路径用字节码地址
    uint32_t slowPathPc;        // 慢路径用 PC
    int32_t  resultDisp;        // 结果存储的 frame 偏移
    uint8_t  argsCnt;           // 参数个数
    int32_t  argSrcDisps[8];    // 各参数在 caller frame 中的偏移
    uint8_t  argVRegs[8];       // frameless 参数预加载得到的 vreg；0xFF 表示仍在 call 内部加载
    uint8_t  resultVReg;        // frameless 返回值承载 vreg；0xFF 表示仍在 call 内部写回 frame
    bool     isSameGraph;       // 是否同图调用
    uint64_t extra2Addr;        // bc->extra2() 地址（跨图用）
    uint64_t fastop1Addr;       // bc->fastop[1] 地址（跨图 JIT 状态检查）
    bool     frameless;         // 是否走 frameless 栈分配
    uint32_t calleeSlotBytes;   // frameless 时栈分配大小（16 字节对齐）
};
```

**设置逻辑**（`x64_backend.cpp` 中 `OpCode::FUNC` 处理）：

- `isSameGraph = (getFuncExtraGraph(&bc) == unit.graph)` — 目标图与当前编译图是否相同。
- `frameless = isSameGraph` — 所有同图调用默认启用 frameless。
- `calleeSlotBytes = round_up_16(graph->runtimeDataType()->size() * 8)` — 从目标图的运行时数据类型计算帧大小。

### 5.2 路径 A：Frameless 栈分配（同图自递归）

**适用条件**：`frameless == true`（当前等价于 `isSameGraph == true`）。

**核心思想**：对于纯自递归函数（如 fib with inline），所有内部调用都是同图自调用，不需要通过 Frame 池分配/释放帧。直接在原生栈上分配被调方的 slot 空间，彻底消除 Frame 管理开销。

**生成的 x64 序列**（以 1 个参数为例）：

```asm
; 参数预加载已在 MIR 中显式出现，可被 peephole / slot cache 继续优化
mov rax, [rdi + argDisp]    ; (1) 从 caller frame 加载参数（或来自只读 slot cache）
push rdi                     ; (2) 保存 caller frame 基址
sub rsp, 80                  ; (3) 在栈上分配 callee 帧空间（80 = 10 slots × 8，16 对齐）
mov rdi, rsp                 ; (4) callee slot 基址 = 栈顶
mov [rdi + 8], rax           ; (5) 写入参数到 callee slot[1]
call body_start              ; (6) 直接 call rel32 到同函数 body 入口
add rsp, 80                  ; (7) 释放 callee 帧空间
pop rdi                      ; (8) 恢复 caller frame 基址
; 若 resultVReg != 0xFF，rax 结果先映射到对应物理寄存器，再由显式 VStoreToFrame 写回
mov [rdi + resultDisp], rax  ; (9) 存储返回值到 caller frame（或被后续优化删掉）
```

**关键约束**：

1. **参数加载必须在 rdi 切换之前**：`movRegFromFrame` 依赖 rdi 指向 caller frame，所以先加载参数到临时寄存器（rax, rcx, rdx, r8, r9, r10, r11），再切换 rdi。最多支持 7 个参数。
2. **栈分配大小必须 16 字节对齐**：`calleeSlotBytes = (slotCount * 8 + 15) & ~15`。确保 `push rdi; sub rsp, N; call` 后被调方 body 入口的 RSP 对齐与顶层一致。
3. **slot[0] 未写入**：frameless 路径不写 Frame* 到 slot[0]，因为整条调用链上没有人会读取它（无 Frame 释放操作）。这在纯自递归场景下完全安全。
4. **无慢路径**：自调用的目标就是当前正在编译的函数 body，`call rel32` 直接跳转到 body start，不存在"目标未编译"的情况。
5. **与 Frame-based 路径可混用**：同一函数内，frameless 自调用和 Frame-based 跨图调用可以共存，互不干扰。
6. **call 是只读 slot cache 的失效点**：所有 allocatable GPR 都可能被 clobber，因此 `slotCache` 仅在 call/trampoline 之后清空，而不会因普通 `JUMP/JOIN` 清空。

### 5.3 路径 B：Frame-based 调用（跨图或同图 fallback）

**适用条件**：`frameless == false`（即跨图调用）。

分为**快速路径**（Frame 池成功获取）和**慢路径**（Frame 池失败，走 trampoline C++ 回退）。

#### 快速路径序列

```asm
; ═══ 保存 caller ═══
push rdi                         ; 保存 caller slot 基址

; ═══ 跨图：检查目标是否已 JIT 编译 ═══
; （仅 !isSameGraph 时生成）
movzx eax, byte [fastop1Addr]   ; 加载 bc->fastop[1]
test rax, rax
jne slow_common                  ; 非零 = 目标未编译，跳慢路径
mov rax, [extra2Addr]            ; 加载 JIT 入口指针
shl rax, 16 ; shr rax, 16       ; 提取低 48 位（去掉 targetPc）
push rax                         ; 保存 fn 到栈上

; ═══ Frame 获取检查 ═══
mov r10, [rbx]                   ; r10 = pool top Frame*
mov r11, [r10 + 8]               ; r11 = frame->graph_
cmp r11, r12                     ; 同图：cmp r11, r12（3 字节）
;   或
; mov rax, imm64(targetGraphAddr); cmp r11, rax（跨图：13 字节）
jne slow                         ; 图不匹配 → 慢路径

; ═══ 获取 Frame ═══
mov r11, [r10 + 16]              ; r11 = frame->next_
mov [rbx], r11                   ; 更新池顶 = next
lea r11, [r10 + 40]              ; r11 = &frame->dynamicArea_（slot 基址）
mov [r11], r10                   ; slot[0] = Frame*

; ═══ 拷贝参数 ═══
mov rax, [rdi + argSrcDisp_i]    ; 从 caller 加载第 i 个参数
mov [r11 + (i+1)*8], rax         ; 写入 callee slot[i+1]

; ═══ 切换帧并调用 ═══
mov rdi, r11                     ; rdi = callee slot 基址
; 同图：call rel32(body)
; 跨图：pop rax; mov rcx, rdi; mov rdx, rsi; sub rsp, 32; call rax; add rsp, 32

; ═══ 释放 Frame ═══
mov r10, [rdi]                   ; r10 = callee slot[0] = Frame*
pop rdi                          ; 恢复 caller slot 基址
mov r11, [rbx]                   ; r11 = 当前池顶
mov [r10 + 16], r11              ; frame->next_ = 池顶
mov [rbx], r10                   ; 池顶 = frame（归还）
mov [rdi + resultDisp], rax      ; 存储结果
jmp done

; ═══ 慢路径 ═══
slow:
    ; 调用 directSelfFuncInvoke 或 trampolineFunc（C++ fallback）
    mov rcx, rdi; mov rdx, rsi; mov r8, ...
    mov rax, slowPathFnAddr
    call rax
    pop rdi
    mov [rdi + resultDisp], rax
done:
```

#### Frame 内存布局

```
Frame 对象:
  Offset  0: [Object vtable/header]
  Offset  8: graph_     (GIR::Graph*)    ← 用于图匹配检查
  Offset 16: next_      (Frame*)         ← 池链表指针
  Offset 24: staticArea_ (Tuple*)
  Offset 32: dynamicAreaType_ (TupleType*)
  Offset 40: dynamicArea_[0] = slot[0]   ← Frame* 自引用
  Offset 48: dynamicArea_[1] = slot[1]   ← 第一个参数/数据
  ...
```

**关键偏移常量**：`graph_` 在 +8，`next_` 在 +16，`dynamicArea_` 在 +40。

### 5.4 Frame 池操作流程

FramePool 是一个基于连续内存区域的 LIFO 栈式分配器：

```
             ┌─────────┐
  pool.top_──►│ Frame A │──next_──► Frame B ──next_──► Frame C ──► ...
             └─────────┘
```

**获取（acquire）**：
1. `r10 = *(&pool.top_)`（通过 rbx 间接访问）
2. 检查 `r10->graph_ == targetGraph`（快速路径通过 r12 缓存当前图）
3. 若匹配：`pool.top_ = r10->next_`，返回 r10
4. 若不匹配：跳转慢路径（C++ 分配新 Frame）

**释放（release）**：
1. `r10 = callee slot[0]`（Frame* 从 slot[0] 取回）
2. `r10->next_ = pool.top_`
3. `pool.top_ = r10`

**store forwarding 依赖链**：释放写入 `[rbx]`，下一次获取从 `[rbx]` 读取。CPU store-to-load forwarding 约 4-5 cycle。随后 `[r10+8]` 和 `[r10+16]` 的读取依赖 r10，形成 2 级依赖链，总计约 10 cycle/次。这是 frameless 优化能大幅提升性能的根本原因。

---

## 6. Slot 模型与数据访问

### 6.1 基本概念

- **slot_t**：`uint64_t`，一个槽位可存整型、浮点位模式、指针或装箱值。
- **data_idx_t**：正数 = 动态区索引，负数 = 静态区索引（绝对值），0 = 无效。
- **编译期偏移**：`slotDisp(idx) = idx * 8`（`frameBaseOffset_ = 0`，直接字节偏移）。
- **运行时访问**：`[rdi + slotDisp(idx)]`。

### 6.2 slot[0] = Frame* 规范

在 Frame-based 路径中，`slot[0]` 恒存当前 Frame 指针：

- 解释器/gotovm 在调用 JIT 入口前写入。
- NativeJitFuncCall 的 Frame 获取路径在 `lea r11, [r10+40]; mov [r11], r10` 中写入。
- `trampolineOper` 从 `slots[0]` 取 Frame，用 `FrameArgsView` 调用算子。
- `trampolineFunc/Tail` 从 `callerSlots[0]` 取 caller Frame，支持负索引操作数（静态区）。

在 **frameless** 路径中，slot[0] 不被写入，且整条调用链上无人读取。

### 6.3 静态区访问

静态区数据（`data_idx < 0`）存储在 `FrameMeta::staticArea` 中。当前实现优先走**编译期内联**：

- JIT 构建 MIR 时直接读取 `staticArea[idx]`
- 若值可直接视为数值常量，则生成 `VLoadImm32/VLoadImm64`
- 若该静态值进一步参与整数算术/比较，后续还能继续折叠为 `VAddImm/VSubImm/VCmpSet*Imm`

这使得热点路径中的：

```asm
mov rcx, imm64(addr)
mov rcx, [rcx]
cmp rax, rcx
```

可直接降为：

```asm
cmp rax, 1
```

**边界说明**：

- 当前这套内联主要服务于数值型热点路径，特别是 inline 后的 fib。
- 若后续要把该策略推广到更复杂的 boxed/reference 语义，仍需要更细的类型与别名约束，避免把“稳定地址上的对象引用”误当成纯数值立即数。

---

## 7. FUNC 字节码 Extra 打包与 JIT 状态标记

### 7.1 打包格式

FUNC/TAIL 字节码有两个 extra 字段：

| 字段 | 位域 | 用途 |
|------|------|------|
| `extra[0].graph` | 完整 64 位 | 目标 `GIR::Graph*` |
| `extra2` (未 JIT) | 完整 64 位 | 调用计数 |
| `extra2` (已 JIT) | 低 48 位 = JitEntryFn, 高 16 位 = targetPc | 打包后的 JIT 入口指针 |
| `fastop[1]` | 1 字节 | 0 = 已 JIT 编译，非 0 = 目标 PC |

### 7.2 JIT 编译触发

1. 解释器/gotovm 在遇到 FUNC 字节码时调用 `incFuncExtraCount`，递增 `extra2` 中的计数。
2. `invokeCallOrJit` 检查 `shouldJit(count)` 决定是否编译。
3. 编译成功后，`compileAndCacheGraph` 遍历所有字节码，将目标图匹配的 FUNC/TAIL 的 `extra2` 设为打包后的 fn 指针，`fastop[1] = 0`。

### 7.3 Graph 上的 JIT 信息存储

通过 `Graph::getExtra<T, Index>()` O(1) 访问机制：

| 索引 | 类型 | 用途 |
|------|------|------|
| 0 | `FrameMeta*` | 帧布局元信息 |
| 1 | `JitGraphInfo*` | JIT 入口函数指针 |

---

## 8. Trampoline 系统

Trampoline 是 JIT 代码与 C++ 运行时的桥接层，用于处理 JIT 无法直接完成的操作。

### 8.1 trampolineFunc / trampolineTail

**签名**：`extern "C" slot_t trampolineFunc(slot_t* callerSlots, void* ctx, size_t pc)`

**执行流程**：
1. 从 `base[pc]` 取字节码元信息。
2. 检查 `targetPc`：
   - `targetPc == 0`（目标已 JIT）：从 extra2 提取 JitEntryFn，获取 Frame，调用 `fn(calleeSlots, ctx)`。
   - `targetPc != 0`（目标未 JIT）：获取 Frame，调用 `vm->call(targetPc, newFrame)` 走解释执行。
3. 释放 Frame，返回结果。

### 8.2 directSelfFuncInvoke

NativeJitFuncCall 同图调用的慢路径回退函数。当 Frame 池获取失败（图不匹配）时调用：

```cpp
slot_t directSelfFuncInvoke(slot_t* callerSlots, void* ctx, const Bytecode* bc) {
    // 从字节码获取目标图、参数信息
    // 通过 C++ 运行时获取 Frame
    // 调用 JIT 编译后的函数入口
    // 释放 Frame，返回结果
}
```

### 8.3 trampolineOper / trampolineCast

**签名**：`extern "C" slot_t trampolineOper(slot_t* slots, void* ctx, size_t pc)`

从 `slots[0]` 取 Frame*（slot[0] 规范），构造 `FrameArgsView` 调用算子或类型转换函数。

### 8.4 JIT 代码中的 rdi 保存

所有 trampoline 调用前后必须 `push rdi / pop rdi` 保存恢复 caller 的 slot 基址，因为 trampoline 内部会将 rdi 设为 callee 的 slot base。

---

## 9. 寄存器分配详解

### 9.1 物理寄存器映射

| 分配索引 | x64 寄存器 | 说明 |
|---------|-----------|------|
| 0 | rax | 临时 / 返回值 / 算术中转 |
| 1 | rcx | 通用 / Win64 第 1 参数 |
| 2 | rdx | 通用 / Win64 第 2 参数 |
| 3 | **rbx** | **保留**：缓存 `&FramePool::top_`，不参与分配 |
| 4 | r8 | 通用 / Win64 第 3 参数 |
| 5 | r9 | 通用 / Win64 第 4 参数 |
| 6 | r10 | 通用 / Frame 管理临时 |
| 7 | r11 | 通用 / Frame 管理临时 |

### 9.2 专用寄存器（不参与分配）

| 寄存器 | 用途 | 初始化位置 |
|--------|------|-----------|
| rdi | slot base（`slot_t*`） | C++ wrapper: `mov rdi, rcx` |
| rsi | JitContext* | C++ wrapper: `mov rsi, rdx` |
| rbx | `&FramePool::top_` | C++ wrapper: `mov rbx, imm64` |
| r12 | 当前 `Graph*` | C++ wrapper: `mov r12, imm64` |

### 9.3 溢出（Spill）处理

当 VReg 无法分配物理寄存器时（`pregForVReg(v) == kSpilled`），编码器使用以下策略：

- **VLoadFromFrame → VStoreToFrame**：直接生成 `mov [rdi+disp], rax` 等，通过 frame slot 中转。
- **VCopy + VTest + VCmovnz 链（BRCH/JOIN 路径）**：使用 `push rbx; mov rbx, reg; cmovnz rbx, reg; mov [rdi+disp], rbx; pop rbx` 的 scratch 序列。
- **SpillState 追踪**：编码器维护 `SpillState` 结构，跟踪延迟加载（`deferredLoad`）和溢出目标（`spilledDest`），确保 VCopy → VTest → VCmovnz → VStoreToFrame 链的正确物化。

---

## 10. 支持的 Opcode 与编码方式

所有算术/比较/加载存储均通过 **MIR V\* 虚拟寄存器指令** 生成，再经寄存器分配和编码输出 x64 机器码。

### 10.1 整数运算（64 位）

| 字节码 | MIR 序列 | x64 编码 |
|--------|---------|---------|
| LADD | `VLoad/VLoadImm` → `VAdd` or `VAddImm` → `VStore` | 优先 `add dst, src` / `add reg, imm32` |
| LSUB | `VLoad/VLoadImm` → `VSub` or `VSubImm` → `VStore` | 优先 `sub dst, src` / `sub reg, imm32` |
| LMUL | VLoad × 2 → VMul → VStore | `mov rax, lr; imul rax, rr` |
| LDIV | VLoad × 2 → VIdiv → VStore | `mov rax, lr; cqo; idiv rr` |
| LLE..LNE | `VLoad/VLoadImm` → `VCmpSet*` or `VCmpSet*Imm` → `VStore` | 普通路径：`cmp left, right; setcc al; movzx rax, al` |

**reg-to-reg 编码规则**：

- 若 `dst == left`：直接对目标寄存器原地运算
- 若 `dst == right` 且操作满足交换律（如 add）：交换操作数，仍可原地运算
- 若 `dst != left/right`：`mov dst, left; op dst, right`
- 对 `cmp`：不需要结果目标寄存器，直接 `cmp left, right` 或 `cmp left, imm32`

### 10.2 浮点运算（64 位双精度）

| 字节码 | MIR 序列 | x64 编码 |
|--------|---------|---------|
| DADD | VXmmLoad × 2 → VXmmAdd → VXmmStore | `movq xmm0, lr; movq xmm1, rr; addsd xmm0, xmm1; movq dr, xmm0` |
| DLT..DGE | VXmmLoad × 2 → VXmmCmpSet* → VStore | `ucomisd xmm0, xmm1; setb al; movzx rax, al` |

### 10.3 32 位整数与 32 位浮点

类似 64 位版本，使用 32 位操作指令（`add eax, ecx` 等）或单精度 SSE（`addss`、`ucomiss`）。

### 10.4 控制流

| 字节码 | MIR | 说明 |
|--------|-----|------|
| BRCH | 普通路径：`VLoad + VTest + VCmovnz + JzRel32 + JmpRel32`；融合路径：`VCmpRegImm + JccRel32` | 仅 `withCnt()==0` 的 BRCH 支持 JIT；fused 路径下 branch-index 可完全省略 |
| JOIN | 简化路径：`VLoad/VCopy + VStore`；普通路径：`VCopy + VTest + VCmovnz + VStore` | 对“仅 else-path 可达”的 JOIN，可直接选择 `w1`；若下一条是 `RETN`，可直接发射 `VRet` |
| JUMP | `JmpRel32` 或直达 `VRet` | 当目标 `JOIN` 后紧跟 `RETN` 且读取该 `JOIN.result` 时，可直接返回 |
| RETN | `VLoad → VRet` 或被前驱折叠掉 | `mov rax, vr; ret`；部分模式下由前驱 `JUMP/JOIN` 直接发射 `VRet` |
| FUNC | NativeJitFuncCall | 详见第 5 节 |
| TAIL | 跨图时走 trampoline + jmp | 释放当前帧后跳转到目标 |
| OPER | trampolineOper 调用 | push rdi; Win64 ABI call; pop rdi |
| CAST | trampolineCast 调用 | 同 OPER |

**比较-分支融合示例**：

原始逻辑目标是：

```c
cond = (n <= 1);
if (cond) ...
```

当前热点路径会尽量压缩为：

```asm
mov rax, [rdi+8]
cmp rax, 1
jg  .recurse
```

而不是先 `setcc`、再 store、再 reload、再 `test/cmov/jz`。

**不支持的 opcode**（ACCS, FILL, CALL, SCHD 等）会导致 `compileBytecode` 返回 `false`，该图不进入 JIT 缓存。

---

## 11. 性能特征

### 11.1 fib(30) 基准测试（std::inline std::jit）

| 优化阶段 | 耗时 | 每次调用开销 | 关键改进 |
|---------|------|-------------|---------|
| 纯解释器 (std::fvm) | ~50ms | ~18.5 cycle | — |
| JIT (trampoline) | ~34ms | ~12.6 cycle | 消除字节码分派 |
| JIT + 双入口 + rbx/r12 缓存 | ~7ms | ~2.6 cycle | 消除 Win64 ABI 转换、图比较优化 |
| JIT + frameless 栈分配 | **~4.4ms** | **~1.6 cycle** | **彻底消除 Frame 池操作** |
| JIT + F-J / K-N 定向代码生成优化 | **~2.9-3.0ms** | 更低 | 常量内联、立即数编码、比较-分支融合、JOIN/JUMP 直接返回 |
| JIT + O/Q/P Frame Store 优化 | **~2.6ms** | 进一步下降 | 只读 slot 缓存、call result 可见化、dead frame store 消除 |

### 11.2 F-J / K-N / O-Q-P 定向优化的性能画像

对 fib 这类“纯数值 + 自递归 + inline 后无外部函数/算子依赖”的函数，近期收益主要来自：

- **减少控制流绕行**：`cmp + jcc` 取代布尔物化
- **减少常量内存访问**：静态区常量直接变成 immediates
- **减少寄存器与内存往返**：`cmp/add/sub` 尽量直接作用于目标寄存器
- **减少局部冗余 load/store**：peephole 把短距离 reload 改成 `VCopy`
- **减少只读 slot 重复加载**：如 fib 中 `[rdi+8]` 的跨块重复读取被只读 slot cache 压缩
- **减少 call 边界后的伪内存往返**：`NativeJitFuncCall` 的结果写回从“编码器内部黑盒 store”变成可继续被 peephole / dead-store 观察的显式 MIR
- **减少尾部无意义写回**：反向活跃性扫描删除 `VRet` 前对 frame 的尾部 store

### 11.3 Frameless 优化的适用范围

| 场景 | frameless | 原因 |
|------|-----------|------|
| 自递归（如 fib with inline） | ✅ | 所有调用同图自调用，无外部依赖 |
| 同图内函数互调 | ✅ | `isSameGraph == true` |
| 跨图函数调用 | ❌ | 需要 Frame 作为 trampoline 桥接 |
| 包含 OPER 调用的函数 | 混合 | FUNC 同图调用走 frameless，OPER 走 trampoline |

---

## 12. 关键约束与边界条件

### 12.1 编译期约束

| 约束 | 位置 | 说明 |
|------|------|------|
| BRCH 仅支持 `withCnt()==0` | `x64_backend.cpp` BRCH case | 带 with 参数的 BRCH 无法编译，整个图退回解释器 |
| 最大参数数 8 | `NativeJitCallParams::argSrcDisps[8]` | FUNC 字节码参数超过 8 个时，NativeJitFuncCall 无法处理 |
| Frameless 最多 7 个参数 | frameless 编码路径 | 使用 7 个临时寄存器（rax,rcx,rdx,r8-r11）暂存参数 |
| 不支持的 opcode 导致 bail out | `compileBytecode` default case | ACCS, FILL, CALL, SCHD 等不支持 |
| peephole 不跨控制流做传播 | `mir_optimize.cpp` | jump target、jump/call/native-call/push-pop rdi 都视为 barrier |
| 只读 slot cache 只缓存“从不写入”的槽位 | `x64_backend.cpp` | 避免在无 CFG/别名分析下把可变 slot 的旧值错误传播过跳转或写回 |
| dead-store pass 只删除 frame 写回，不删除任意计算 | `mir_optimize.cpp` | 当前仅基于 slot 活跃性，尚未扩展为通用 vreg DCE |
| 当前 MIR 仍为线性结构 | 整个 JIT 中端 | 无 CFG/SSA，故优化以局部模式匹配和保守数据流分析为主 |

### 12.2 运行时约束

| 约束 | 影响 | 说明 |
|------|------|------|
| Frame 池图匹配失败 → 慢路径 | Frame-based 调用 | 池顶 Frame 的 graph_ 与目标不匹配时走 C++ 分配 |
| rbx 必须始终保持有效 | 所有 Frame-based 操作 | 任何使用 rbx 作为 scratch 的代码必须 push/pop 保护 |
| 栈溢出风险 | frameless 深度递归 | fib(30) 约 30 层 × 80 字节 ≈ 2.4KB（安全），极深递归需注意 |
| `fastop[1] == 0` 语义 | 跨图 JIT 状态检查 | 利用"第一条字节码的 targetPC 不会是 0"的不变量作为 JIT 编译标记 |
| 静态常量内联以数值热点为主 | 编译期常量折叠 | 更复杂的引用/对象语义仍需后续更强类型约束 |

### 12.3 对齐约束

| 场景 | 要求 | 当前状态 |
|------|------|---------|
| C++ ABI 调用前 | RSP ≡ 0 (mod 16) | 包装器 4 push → body RSP ≡ 0；跨图 `push rdi; sub rsp, 32; call` → RSP ≡ 8 - 40 ≡ 0 ✓（仅当 body RSP ≡ 8 时正确） |
| Frameless 自调用 | callee body RSP 对齐一致 | `push rdi; sub rsp, N(≡0 mod 16); call` → callee RSP ≡ body RSP ✓ |
| Shadow space | 跨图 C++ 调用需 32 字节 | `sub rsp, 32` / `add rsp, 32` 在跨图快速路径中生成 |

---

## 13. 调试与诊断

### 13.1 MIR Dump

```bash
camel fib.cml std::inline std::mir    # 输出优化后的 MIR（带 VReg）
camel fib.cml std::inline std::rmir   # 输出原始 MIR（优化前）
```

### 13.2 ASM Dump

```bash
camel fib.cml std::inline std::asm    # 输出 x64 汇编（含偏移和指令注释）
```

输出格式：`[offset]  instruction  ; annotation`。C++ wrapper 从 offset 0 开始，JIT body 从 `jitEntryOffset`（通常 42）开始。

### 13.3 Debug Trace

编译时设置 `enableDebugTrace = true`，在每条字节码执行前插入 `DebugTrace` MIR 指令，运行时调用 `jitDebugTraceWrapper` 打印帧状态。**仅限 debug 模式编译**，不建议在大规模调用（如 fib(30)）时开启。

---

## 14. 分层策略与调用路径总览

### 14.1 TierPolicy

- `Disabled` → 不编译，始终解释执行。
- `OnDemand` → `callCount >= hotThreshold` 时触发编译。
- `Always` → 全图预编译。

### 14.2 调用路径

```
解释器 FUNC/TAIL
  ├─ jitCache 命中 → fn(frame->slotBase(), ctx)     [C++ wrapper 入口]
  ├─ shouldJit(count) → compile → fn(...)
  └─ 否则 → call(pc, frame) [解释执行]

JIT body 内 NativeJitFuncCall
  ├─ frameless (同图) → sub rsp, N; call body        [无 Frame，栈分配]
  └─ Frame-based (跨图)
       ├─ Frame acquire 成功 → 同图 call rel32 / 跨图 call through ABI
       └─ Frame acquire 失败 → directSelfFuncInvoke / trampolineFunc [C++ fallback]

JIT body 内 OPER/CAST → trampolineOper/Cast(slots, ctx, pc)  [C++ 调用]

JIT body 内 TAIL
  ├─ 跨图已 JIT → prepareDirectJitTailCall + jmp     [尾调用优化]
  └─ 否则 → trampolineTail(slots, ctx, pc)
```

---

## 15. 参考资料

- [04_compilation_pipeline.md](./04_compilation_pipeline.md) — 前端编译流程
- [05_graph_ir.md](./05_graph_ir.md) — GIR 图中间表示
- [06_runtime_and_execution.md](./06_runtime_and_execution.md) — 运行时与执行模型
- [08_fastvm_bytecode_semantics.md](./08_fastvm_bytecode_semantics.md) — 字节码语义定义
