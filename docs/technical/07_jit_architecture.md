# FastVM JIT 架构文档

> **适用范围（2026-10-21）**：JIT 把图编译成两种代码之一。**无帧代码**（frameless）是"自足"的图——字节码里只有算术、比较、`BRCH`/`JOIN`/`JUMP`/`RETN`，以及目标是**本图自身**的 `FUNC`/`TAIL`；它不调用任何 C++，自调用是原生调用，自尾调用是跳转。第 1 至 16 节描述的是它。**有帧代码**（framed）是其余所有的图（含 `OPER`、`CAST`、`COPY`、`FILL`、`ACCS`、`CALL`、`SCHD`、`match`、循环的 `LBEG`/`LTST`/`LEND`，或调用其他图）：算子等经 C++ 辅助函数执行，循环的三个事件也在辅助函数里跑，调用 Camel 函数的字节码交回驱动循环执行，见第 17 节。哪一种由 `collectRuntimeConstructs` 一处决定，`-vv` 日志写明（`compiled & cached graph 'f' as framed code`）。最早只在 Linux x86-64 上构建和运行过；有帧代码新增编码的 Win64 分支（`CallHelper`、`ExitToDriver`、包装器的续点分支）此后在 Windows x64（clang++）上构建并运行过（H43a，含循环的图），第 8 节的汇编包装器仍没有被用到。

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
                 │  VReg 分配     │   linearScanVReg → VRegAllocation；失败则关掉跨字节码缓存重新下降一次
                 └───────┬───────┘
                         │
                         ▼
                 ┌───────────────┐
                 │  C++ 包装器    │   生成入口包装器（Win64 / SysV 各一份，原始字节）
                 └───────┬───────┘
                         │
                         ▼
                 ┌───────────────┐
                 │  MIR → x64    │   encodeMirBuffer 编码为 x64 机器码
                 └───────┬───────┘
                         │  + 故障桩与公共故障出口（见第 16 节）
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
- **只读 slot 值缓存**：在 MIR 构建前先扫描所有 `bc.result`，识别“从未被写入”的动态槽位；对这类槽位的首次 `VLoadFromFrame` 结果缓存在 `slotCache` 中，后续再次读取同槽位时生成 `VCopy` 而不是重复 load。缓存只在一个基本块内有效（在每个跳转目标和每条 BRCH/JUMP/RETN/TAIL 之后清空），调用之后也清空。第二次下降（见阶段 3）不使用这个缓存。
- **分支**（BRCH）：支持两种路径。
  - 普通路径：`VLoad + VTest + VCmovnz + JzRel32 + JmpRel32`
  - 融合路径：若 BRCH 的条件恰好来自紧邻比较结果，则比较阶段只发射 `VCmpRegImm` 设置 flags，BRCH 直接发射对应的 `JccRel32`，跳过布尔物化与 branch-index 物化
- **函数调用**（FUNC）：只可能是自调用，生成 `NativeJitFuncCall`（第 5 节）。参数预加载和返回值写回显式体现在 MIR 中，便于后续 peephole 和 dead-store pass 继续优化。
- **尾调用**（TAIL）：只可能是自尾调用，改写形参槽后跳回入口（5.3）。
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

`linearScanVReg(mirBuf, &vregAlloc, &reason)` 对 MIR 中所有 `V*` 指令做线性扫描分配：

- **可分配寄存器**：6 个（rax, rcx, rdx, r8, r9, r10）。rbx 保留（`&FramePool::top_`），**r11 保留给编码器当临时寄存器**（idiv 的除数、浮点比较的第二个标志、调用时搭建帧）。
- **虚拟寄存器编号是 16 位**（`Mir::r0/r1`）。
- **编码器没有溢出代码**，所以每个被使用的值在整个活跃区间内都必须有寄存器。分配在下列情况**失败并给出原因**，而不是留下没有寄存器的值：同时活跃的值多于寄存器数；某个值要跨过一次调用（调用会破坏全部可分配寄存器）；值在定义之前被使用。
- **失败后的第二次下降**：`X64Backend::compileBytecode` 先带着"值跨字节码留在寄存器里"的优化（只读 slot 缓存、store→load 转发）下降一次；分配失败就关掉这两项再下降一次，此时每个值的寿命都在单条字节码的下降之内（最多 4 个临时值），必然放得下。`-vv` 日志里的 `is lowered again without values kept in registers` 就是这条路径。两次都失败才把图留给解释器。
- 只有定义、从未被读的值没有寄存器，编码器跳过它的定义（除法除外：除法即使结果没人读也保留检查）。

#### 阶段 4：入口包装器生成

在 MIR 编码之前，先在代码缓冲区中写入入口包装器（详见第 3 节）。

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

```
┌──────────────────────────────────────────────┐
│  入口包装器                                    │  ← offset 0（C++ 调用者入口）
│  SysV:  push rbx, r12, r13, r14, r15          │
│         mov r13, rsi        ; JitContext*     │
│         mov r15, [rsi+24]   ; 递归深度余量     │
│  Win64: push rdi, rsi, rbx, r12, r14, r15     │
│         mov rdi, rcx; mov rsi, rdx            │
│         mov r15, [rsi+24]                     │
│  共同:  mov rbx, imm64  ; &FramePool::top_    │
│         mov r12, imm64  ; 当前 Graph*          │
│         mov r14, rsp    ; 故障出口要恢复的栈顶  │
│         call body                             │
│  epilogue:              ← 故障出口跳到这里      │
│         pop ...; ret                          │
├──────────────────────────────────────────────┤
│  JIT Body（MIR 编码后的机器码）                 │  ← jitEntryOffset（自调用入口）
│  无 prologue；返回是裸 ret                     │
├──────────────────────────────────────────────┤
│  故障桩（每个故障点一段）+ 公共故障出口          │
└──────────────────────────────────────────────┘
```

### 3.2 入口选择

| 调用来源 | 入口 | 说明 |
|----------|------|------|
| C++ / 解释器（`invokeOwnedJitFrame`） | `offset 0` | 包装器做 ABI 适配并装载 rbx/r12/r13/r14/r15 |
| 本图自调用（`FUNC`） | `jitEntryOffset` | `call rel32` 直接进 body，被调方的帧在机器栈上 |
| 本图自尾调用（`TAIL`） | `jitEntryOffset` | 改写参数槽后 `jmp rel32` 回到 body 入口，不产生新帧 |

跨图调用不会出现在被编译的图里（见文首的适用范围）。

### 3.3 栈对齐

包装器 push 的个数保持为：SysV 奇数（5）、Win64 偶数（6），与此前的 3 / 4 同奇偶，所以 body 入口处 RSP 的对齐与以前一致。自调用 `push rdi; sub rsp, N`（N 为 16 的倍数）`; call` 使被调方 body 入口的对齐与调用方相同。

---

## 4. JIT 内部调用约定

| 寄存器 | 用途 |
|--------|------|
| **rdi** | 当前帧 slot 基址，所有 frame 访问通过 `[rdi + disp]`；自调用前后 push/pop |
| **rsi** | `JitContext*`。SysV 下 rsi 是调用者保存的，稳定副本在 **r13**；编码器访问 JitContext 时 SysV 用 r13、Win64 用 rsi |
| **rbx** | `&FramePool::top_`（保留；当前可达路径不使用帧池） |
| **r12** | 当前 `Graph*`（同上） |
| **r14** | 包装器保存的 RSP，故障出口用它一步退回包装器（第 16 节） |
| **r15** | 递归深度余量：每次原生自调用减 1，返回加 1，减到负数即故障（第 16 节） |
| **r11** | 编码器的临时寄存器，不参与分配 |
| **rax, rcx, rdx, r8, r9, r10** | 可分配给虚拟寄存器；rax 同时是返回值 |
| **xmm0, xmm1** | 浮点运算的临时寄存器；浮点值以位模式放在通用寄存器里 |

---

## 5. NativeJitFuncCall：原生自调用

`NativeJitFuncCall` 编码"本图调用本图"。被调方的帧直接分配在机器栈上，不经过帧池，也不经过任何 C++。

### 5.1 NativeJitCallParams

```cpp
struct NativeJitCallParams {
    struct Arg {
        int32_t dstDisp;   // 被调方帧里形参槽的偏移
        VRegId vreg;       // 调用前已装入寄存器的实参；否则在调用内部拷贝，来源是：
        int32_t srcDisp;   //   调用方帧里的槽，或
        bool isImm;        //   静态区常量
        uint64_t imm;
    };
    std::vector<Arg> args;      // 任意个数
    int32_t resultDisp;
    VRegId resultVReg;          // 返回值所在的虚拟寄存器
    uint32_t calleeSlotBytes;   // 被调方帧大小（16 字节对齐）
    uint32_t pc;                // 该调用的字节码 pc（递归过深时报告的位置）
};
```

实参不超过 4 个时全部预先装入寄存器（可见的 `VLoadFromFrame`，便于 peephole 融合前面的 store）；超过 4 个时逐个经 r11 从调用方的帧拷到被调方的帧。参数块由 `compileBytecodeWith` 持有，编码结束后释放。

### 5.2 生成的序列

```asm
sub r15, 1                   ; 递归深度余量
jb  .fault_depth             ; 用尽 → 故障桩（第 16 节）
cmp rsp, [ctx + stackLimit]  ; 机器栈的余量（16.4；ctx 在 SysV 下是 r13，Win64 下是 rsi）
jb  .fault_stack             ; 不足 → 故障桩
push rdi                     ; 保存调用方 slot 基址
sub rsp, N                   ; 被调方的帧
mov [rsp + dst_i], reg_i     ; 已在寄存器里的实参
mov r11, [rdi + src_j]       ; 其余实参逐个经 r11（常量用 mov r11, imm64）
mov [rsp + dst_j], r11
mov rdi, rsp                 ; 切到被调方的帧
mov rsi, r13                 ; 仅 SysV
call body
add rsp, N
pop rdi
inc r15
mov result_reg, rax
```

rdi 在所有实参写完之后才切换，所以实参的来源始终按调用方的帧寻址；r11 不可分配，不会与装着实参的寄存器冲突。帧大于 `kMaxNativeFrameBytes`（2 KB）的图含自调用时不编译成无帧代码（而是有帧代码）：上限那么多层这样的帧在较小的机器栈上放不下。

### 5.3 自尾调用（TAIL）

两种 ABI 相同：把实参写进本帧的形参槽，然后 `jmp` 到 body 入口。

- 实参就是同一个形参（`f(a, b + 1)` 里的 `a`）时跳过。
- 需要移动的实参不超过 4 个：先全部装入寄存器，再逐个写回（实参可以是另一个形参，如 `f(b, a)`，所以必须先读完再写）。
- 超过 4 个：`push [rdi+src]` 逐个压栈，再按相反顺序 `pop [rdi+dst]`。
- 回跳的目标在跳转之前，`eliminateDeadFrameStores` 的单趟逆向扫描在这样的跳转处把所有槽视为活跃，否则对形参槽的写会被当成死写删掉。

尾调用不消耗递归深度余量，也不增长机器栈；无论当前帧来自帧池还是位于机器栈上都成立。

### 5.4 已删除与不可达的路径

基于帧池的调用编码（跨图、慢路径 `directSelfFuncInvoke`）已从编码器中删除：入口处的过滤使它无法到达，而它在 SysV 下按 Win64 的寄存器传参。`trampolineFunc` / `trampolineTail` 仍在 `trampoline.cpp` 中，但编译出的代码不再引用它们。

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
- 原生自调用的帧在机器栈上，**它的 slot[0] 不写**（只有开启 debug trace 时才把调用方的 slot[0] 抄过去，即最外层那个帧池帧的 `Frame*`）。
- 所以 **slot[0] 不能用来读当前激活的数据**：`trampolineFunc/Tail/Oper` 都通过 `slots[0]` 的 `Frame*` 读操作数，在栈上帧里读不到当前激活的值。这是此前 SysV 下自尾调用算错的原因之一，也是 trampoline 目前不可达、不应在未重写前重新启用的原因。

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

## 7. FUNC 字节码直调布局与 JIT 状态

### 7.1 打包格式

FUNC/TAIL 字节码现在把“调用方源槽布局”和“被调方目标槽布局”都编码在同一条指令中：

- `nargs()`：caller 源参数槽
- `wargs()`：callee 目标参数槽（按目标图 `normPorts + withPorts` 的顺序预编译）

这样解释器、computed goto、trampoline、NativeJitFuncCall 都直接消费字节码，不再回到 `GCGraph` 反推端口布局。

extra 字段布局如下：

| 字段 | 位域 | 用途 |
|------|------|------|
| `extra[0].graph` | 完整 64 位 | 目标 runtime `GCGraph*` |
| `extra2` | 高 16 位 = targetPc，低 48 位 = direct-call 热度计数 | 解释器入口 PC + JIT 触发计数 |
| `extra3` | 完整 64 位 | JIT 入口指针；0 表示当前仍走解释器 |

### 7.2 JIT 编译触发

1. 解释器/gotovm 在遇到 FUNC 字节码时调用 `incFuncExtraCount`，递增 `extra2` 中的计数。
2. `invokeCallOrJit` 检查 `shouldJit(count)` 决定是否编译。
3. 编译成功后，`compileAndCacheGraph` 遍历所有字节码，将目标图匹配的 FUNC/TAIL 的 `extra3` 设为 JIT 入口指针。

### 7.3 Graph 上的 JIT 信息存储

通过 `Graph::getExtra<T, Index>()` O(1) 访问机制：

| 索引 | 类型 | 用途 |
|------|------|------|
| 0 | `FrameMeta*` | 帧布局元信息 |
| 1 | `JitGraphInfo*` | JIT 入口函数指针 |

---

## 8. 旧的 Trampoline（不可达）

`trampoline.cpp` 中的 `trampolineFunc`、`trampolineTail`、`trampolineOper`、`trampolineCast`、`trampolineBytecode`、`directSelfFuncInvoke` 是早先为"JIT 代码调用 C++ 运行时"准备的桥接函数。编译出的代码不引用它们：它们会抛 C++ 异常，并且在 C++ 里嵌套地调用被调函数。有帧代码用的是同一文件里的 `jitHelperOper`、`jitHelperBytecode`、`jitHelperBranchArm`（不抛异常、不调用 Camel 函数），调用则交给驱动循环，见第 17 节。

---

## 9. 寄存器分配详解

| 分配索引 | x64 寄存器 | 说明 |
|---------|-----------|------|
| 0 | rax | 可分配 / 返回值 |
| 1 | rcx | 可分配 |
| 2 | rdx | 可分配 |
| 3 | **rbx** | **保留**：`&FramePool::top_` |
| 4 | r8 | 可分配 |
| 5 | r9 | 可分配 |
| 6 | r10 | 可分配 |
| 7 | **r11** | **保留**：编码器的临时寄存器 |

活跃区间是线性 MIR 上的 `[首次定义, 最后使用]`。寄存器在其值最后一次使用的那条指令上就空闲，所以**同一条指令的目的寄存器可以与某个操作数相同，两个操作数也可以是同一个寄存器**；编码器的每种二元下降都按这个前提写（第 10 节）。没有溢出：分配不下时失败，见 2.3 阶段 3。

---

## 10. 支持的 Opcode 与编码方式

所有算术/比较/加载存储均通过 **MIR V\* 虚拟寄存器指令** 生成，再经寄存器分配和编码输出 x64 机器码。

### 10.1 整数运算（64 位）

| 字节码 | MIR 序列 | x64 编码 |
|--------|---------|---------|
| LADD | `VLoad/VLoadImm` → `VAdd` or `VAddImm` → `VStore` | 优先 `add dst, src` / `add reg, imm32` |
| LSUB | `VLoad/VLoadImm` → `VSub` or `VSubImm` → `VStore` | 优先 `sub dst, src` / `sub reg, imm32` |
| LMUL | VLoad × 2 → VMul → VStore | `imul dst, src`（按下面的规则选操作数） |
| LDIV | VLoad × 2 → VIdiv → VStore | `mov r11, rr; test r11, r11; jz .fault; cmp r11, -1; jne .div; mov dst, lr; neg dst; jmp .done; .div: push rax/rdx; mov rax, lr; cqo; idiv r11; mov dst, rax; pop rdx/rax; .done:` |
| LMOD | 无（`jitHelperBytecode` 执行整个字节码） | 调用 helper，返回值写回结果槽；除数为零走故障出口（`DivisionByZero`） |
| LLE..LNE | `VLoad/VLoadImm` → `VCmpSet*` or `VCmpSet*Imm` → `VStore` | 普通路径：`cmp left, right; setcc dst8; movzx dst, dst8` |

**除法**：除数是 0 和 -1 以外的常量时不做检查；否则检查除数为零（走第 16 节的故障出口，诊断为 `DivisionByZero`），并把 `x / -1` 算成取负（最小整数除以 -1 的商放不下，`idiv` 会触发硬件异常；所有后端对它的结果都是回绕）。idiv 独占 rax 和 rdx，其中别的值在除法前后保存恢复。

**reg-to-reg 编码规则**：

- 若 `dst == left`：直接对目标寄存器原地运算
- 若 `dst == right` 且操作满足交换律（add、imul）：交换操作数，仍可原地运算
- 若 `dst == right` 且不满足交换律（sub）：`neg dst; add dst, left`
- 若 `dst != left/right`：`mov dst, left; op dst, right`
- 任何下降都不经过 rax 中转（rax 本身是可分配的，中转会破坏住在其中的值）
- 对 `cmp`：不需要结果目标寄存器，直接 `cmp left, right` 或 `cmp left, imm32`

### 10.2 浮点运算（64 位双精度）

| 字节码 | MIR 序列 | x64 编码 |
|--------|---------|---------|
| DADD | VXmmLoad × 2 → VXmmAdd → VXmmStore | `movq xmm0, lr; movq xmm1, rr; addsd xmm0, xmm1; movq dr, xmm0` |
| DDIV | 同上，VXmmDiv | 先检查除数是否等于零（`mov r11, rr; add r11, r11; jz .fault`，±0.0 都算），与解释器一致地报 `DivisionByZero` |
| DLT..DGE | VXmmLoad × 2 → VXmmCmpSet* → VStore | `ucomisd` + `seta`/`setae`（小于类比较交换操作数），任一操作数为 NaN 时结果为假 |
| DEQ / DNE | 同上 | `ucomisd; setnp r11b; sete dst8; and`（DNE：`setp; setne; or`），NaN 时 == 为假、!= 为真 |

### 10.3 32 位整数与 32 位浮点

类似 64 位版本，使用 32 位操作指令（`add eax, ecx` 等）或单精度 SSE（`addss`、`ucomiss`）。

### 10.4 控制流

| 字节码 | MIR | 说明 |
|--------|-----|------|
| BRCH | 普通路径：`VLoad + VTest + VCmovnz + JzRel32 + JmpRel32`；融合路径：`VCmpRegImm + JccRel32` | 仅 `withCnt()==0` 的 BRCH 支持 JIT；fused 路径下 branch-index 可完全省略 |
| JOIN | 简化路径：`VLoad/VCopy + VStore`；普通路径：`VCopy + VTest + VCmovnz + VStore` | 对“仅 else-path 可达”的 JOIN，可直接选择 `w1`；若下一条是 `RETN`，可直接发射 `VRet` |
| JUMP | `JmpRel32` 或直达 `VRet` | 当目标 `JOIN` 后紧跟 `RETN` 且读取该 `JOIN.result` 时，可直接返回 |
| RETN | `VLoad → VRet` 或被前驱折叠掉 | `mov rax, vr; ret`；部分模式下由前驱 `JUMP/JOIN` 直接发射 `VRet` |
| FUNC | 无帧：NativeJitFuncCall；有帧：`ExitToDriver(Call)` | 无帧代码里只可能是自调用，详见第 5 节；有帧代码见 17.3 |
| TAIL | 写形参槽 + `JmpRel32(entry)`；有帧代码里目标是别的图时 `ExitToDriver(Tail)` | 详见 5.3 与 17.3 |
| OPER / CAST / COPY / FILL / ACCS | `CallHelper` | 只出现在有帧代码里，见 17.2 |
| LBEG / LTST / LEND | `CallHelper`，LTST/LEND 之后 `VMovFromRax + VTest + JneRel32` | 循环的三个事件，只出现在有帧代码里：辅助函数在帧上跑 `loopBegin`/`loopTest`/`loopEnd`，返回 1 时跳向 `getJumpTargetPc`（LTST 跳向 LEND、LEND 跳回一轮的开头），见 17.11 |
| CALL / SCHD | `ExitToDriver(Interp)` | 只出现在有帧代码里，见 17.3 |
| BRCH（match）/ JOIN（多于两臂） | `CallHelper` + 比较链 | 只出现在有帧代码里，见 17.2 |

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

**BRCH 与 JOIN 的配合**：融合路径不写分支索引。只有当第一臂为空（它在跳转表里的那一项就是到 JOIN 的 JUMP，被下降成"取 w0 后离开"）时，JOIN 才直接取 w1；否则在两臂的入口（BRCH 之后的两项跳转表）分别写入索引 0 / 1，JOIN 按索引选择。臂的布局顺序是任意的（哪一臂落入 JOIN、哪一臂跳转由字节码编译器决定），不能假设"落入 JOIN 的一定是 else 臂"。

**没有不支持的 opcode。** 含 OPER、CAST、COPY、ACCS、FILL、CALL、SCHD、match 或目标不是本图的 FUNC/TAIL 的图编译成有帧代码（第 17 节）。还会被拒绝的只有没有帧布局、字节码为空或 `BRCH` 之后跳转表不完整的图（`collectBlockers`），以及两次下降寄存器都分配不下的图。

---

## 11. 性能特征

### 11.1 fib(30) 基准测试（std::opt::inline std::jit）

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
| 跨图函数调用 | ❌ 有帧代码 | 调用交给驱动循环（17.3） |
| 包含 OPER 调用的函数 | ❌ 有帧代码 | 算子经辅助函数执行（17.2） |
| 包含循环的图 | ❌ 有帧代码 | 三个循环事件经辅助函数执行，轮次边界是安全点（17.9） |

---

## 12. 关键约束与边界条件

### 12.1 编译期约束

| 约束 | 位置 | 说明 |
|------|------|------|
| 无帧代码的 BRCH 只有 `withCnt()==0` | `collectRuntimeConstructs` | 带 case 值的 BRCH（match）使图成为有帧代码 |
| 调用的参数个数不限 | `NativeJitCallParams::args` | 不超过 4 个走寄存器，更多的经 r11 / 机器栈逐个移动 |
| 无帧代码自调用的帧不超过 2 KB | `kMaxNativeFrameBytes` | 更大的帧配合上限那么深的递归会占用过多机器栈，该图编译成有帧代码 |
| 需要运行时的构造使图成为有帧代码 | `collectRuntimeConstructs` | OPER, CAST, COPY, ACCS, FILL, CALL, SCHD, match, LBEG/LTST/LEND，以及目标不是本图的 FUNC/TAIL |
| 寄存器分配失败时重新下降 | `X64Backend::compileBytecode` | 见 2.3 阶段 3 |
| peephole 不跨控制流做传播 | `mir_optimize.cpp` | jump target、jump/call/native-call/push-pop rdi 都视为 barrier |
| 只读 slot cache 只缓存“从不写入”的槽位 | `x64_backend.cpp` | 避免在无 CFG/别名分析下把可变 slot 的旧值错误传播过跳转或写回 |
| dead-store pass 只删除 frame 写回，不删除任意计算 | `mir_optimize.cpp` | 当前仅基于 slot 活跃性，尚未扩展为通用 vreg DCE |
| 当前 MIR 仍为线性结构 | 整个 JIT 中端 | 无 CFG/SSA，故优化以局部模式匹配和保守数据流分析为主 |

### 12.2 运行时约束

| 约束 | 影响 | 说明 |
|------|------|------|
| Frame 池图匹配失败 → 慢路径 | Frame-based 调用 | 池顶 Frame 的 graph_ 与目标不匹配时走 C++ 分配 |
| rbx 必须始终保持有效 | 所有 Frame-based 操作 | 任何使用 rbx 作为 scratch 的代码必须 push/pop 保护 |
| 递归深度上限 | 原生自调用 | 与解释器相同的上限（默认 1000，`--max-recursion-depth`），由 r15 计数；另外每次调用检查机器栈余量。两者超限都给出 `MaxRecursionDepthExceeded`（第 16 节） |
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
camel fib.cml std::opt::inline std::mir    # 输出优化后的 MIR（带 VReg）
camel fib.cml std::opt::inline std::rmir   # 输出原始 MIR（优化前）
```

### 13.2 ASM Dump

```bash
camel fib.cml std::opt::inline std::asm    # 输出 x64 汇编（含偏移和指令注释）
```

输出格式：`[offset]  instruction  ; annotation`。C++ wrapper 从 offset 0 开始，JIT body 从 `jitEntryOffset`（包装器之后）开始。

### 13.3 Debug Trace

编译时设置 `enableDebugTrace = true`，在每条字节码执行前插入 `DebugTrace` MIR 指令，运行时调用 `jitDebugTraceWrapper` 打印帧状态。**仅限 debug 模式编译**，不建议在大规模调用（如 fib(30)）时开启。

---

## 14. 分层策略与调用路径总览

### 14.1 TierPolicy

- `Disabled` → 不编译，始终解释执行。
- `OnDemand` → `callCount > hotThreshold` 时触发编译。**含循环区域的图（`ExecPlan::loops` 非空）在阈值 0 与 1（默认）下第一次调用就编译**：一个只进入一次的函数（程序的入口函数尤其如此）也可能跑一千万轮，调用次数看不见轮次（`TierPolicy::shouldJit` 的 `hasLoop`）。显式把阈值设到 1 以上时该捷径不生效，`hot_threshold=1000000000` 因此仍是"不编译任何图"的度量方式。
- `Always` → 全图预编译。

### 14.2 调用路径

```
解释器 FUNC
  ├─ 目标已编译 → invokeOwnedJitFrame：无帧代码直接进入；有帧代码进入驱动循环 runFramedJit
  ├─ shouldJit(count) → compile → 同上
  └─ 否则 → 解释执行

解释器 TAIL，目标已编译
  ├─ 当前激活是这次 call() 的根 → 把准备好的帧和入口交还调用者（CallResult::tailFn），由它运行
  └─ 否则 → invokeOwnedJitFrame，结果当作被替换的激活的返回值

有帧代码（第 17 节）
  ├─ OPER / CAST / COPY / FILL / ACCS / match → call 辅助函数 → 检查故障
  ├─ FUNC / 跨图 TAIL / CALL / SCHD → 记下 kind、pc、续点 → ret 回包装器 → 驱动循环执行 → 从续点再进入
  └─ 自尾调用 → 查回收器标志（有请求则交给驱动循环）→ 改写形参槽 → jmp body 入口

JIT body 内 FUNC（自调用）
  └─ sub r15,1 → 栈上分配帧 → call body → inc r15        [无 Frame，无 C++]

JIT body 内 TAIL（自尾调用）
  └─ 改写形参槽 → jmp body 入口

JIT body 内故障点（除零、递归过深）
  └─ 故障桩记录 kind/pc → mov rsp, r14 → 包装器 epilogue → C++ 抛出带位置的诊断
```

---

## 15. 参考资料

- [04_compilation_pipeline.md](./04_compilation_pipeline.md) — 前端编译流程
- [05_graph_ir.md](./05_graph_ir.md) — GIR 图中间表示
- [06_runtime_and_execution.md](./06_runtime_and_execution.md) — 运行时与执行模型
- [08_fastvm_bytecode_semantics.md](./08_fastvm_bytecode_semantics.md) — 字节码语义定义

---

## 16. 故障出口与递归深度

### 16.1 为什么不能抛异常

JIT 代码没有展开信息，C++ 异常穿不过它的栈帧。但编译出的代码不调用任何 C++（见文首），所以从入口包装器到 body 中任意一点之间，机器栈上**只有本函数自己的帧**：返回地址、保存的 rdi、栈上分配的 slot 帧。这些帧没有需要释放的资源，可以一次性丢弃。

### 16.2 机制

1. 包装器在 `call body` 之前执行 `mov r14, rsp`。r14 是被调用者保存的寄存器，body 从不使用它。
2. 故障点是一条向前的条件跳转，目标是 body 之后的一段**故障桩**。桩把故障种类和该字节码的 pc 写进 `JitContext`（`faultKind`、`faultPc`），然后跳到**公共故障出口**。
3. 公共出口执行 `mov rsp, r14; jmp epilogue`：栈顶回到包装器调用 body 之后的状态，包装器像正常返回一样恢复寄存器并 `ret` 到 C++ 调用者。
4. 唯一的 C++ 调用者是 `FastVMSchedPass::invokeOwnedJitFrame`。它在返回后检查 `faultKind`，非零时由 `raiseJitFault` 用记录的 pc 构造执行位置（`makePcExecutionSite`），释放入口帧，抛出与解释器相同的诊断（同样的名字、同样的源码位置）。之后的传播是普通的 C++ 异常。

每个包装器激活有自己的 r14，所以解释器与 JIT 代码交替嵌套时也成立。

### 16.3 覆盖的故障

| 故障 | 检查位置 | 诊断 |
|------|---------|------|
| 整数除数为零（IDIV / LDIV / IMOD / LMOD；后两者在 helper 里检查） | 除法之前 | `RuntimeDiag::DivisionByZero`，位置是该除法 |
| 浮点除数等于零（FDIV / DDIV，±0.0） | 除法之前 | 同上（解释器对浮点除零同样报故障） |
| 原生自调用超过递归深度上限 | 调用之前 | `RuntimeDiag::MaxRecursionDepthExceeded`，位置是该调用，图名与上限同解释器 |
| 原生自调用时机器栈将满 | 调用之前 | 同一个 `MaxRecursionDepthExceeded`，消息与解释器的嵌套调用相同（`N open calls: the native stack of the thread, K KiB, is full`） |

最小整数除以 -1 不是故障：结果回绕（各后端一致）。整数取模是 `OPER`，含它的图不编译，由解释器检查。

### 16.4 递归深度的计数

解释器的上限是 `FastVMSchedPass::maxRecursionDepth_`（默认 1000），按 `push` 的次数计。为了让同一个递归无论由谁执行都在同一次调用上超限：

- 解释器从字节码 `FUNC` 进入 JIT 代码时照常 `push` / `pop`（`gotovm.cpp` / `casevm.cpp`）。
- `invokeOwnedJitFrame` 进入前把 `maxRecursionDepth_ - stackDepth_` 写入 `JitContext::depthBudget`，包装器装入 r15。
- 每次原生自调用 `sub r15, 1`，借位即故障；返回后 `inc r15`。尾调用不计数。

**机器栈的余量。** 上限可以设到一百万，而原生自调用每层都占机器栈（`depth` 每层 112 字节，8 MiB 的栈大约放七万层），所以只计数不够。`invokeOwnedJitFrame` 在写入 `depthBudget` 的同时把 `execute::nativeStackFloor()` 写入 `JitContext::stackLimit`：当前线程栈的下界加保留量（栈的十六分之一，至少 128 KiB，与解释器 `nativeStackNearlyExhausted` 用的是同一个保留量；栈界读不到或 `--native-stack-guard off` 时为 0，任何栈指针都不低于它）。栈界按线程读取（`thread_local`），所以由哪个线程进入原生代码就用哪个线程的栈。每次原生自调用在计数之后执行 `cmp rsp, [ctx + stackLimit]; jb .fault_stack`。故障桩除了记录种类与 pc，还把 r15 存回 `depthBudget`，`raiseJitFault` 据此算出"已有多少次调用未返回"。故障出口本身不使用栈；保留量留给之后 C++ 一侧构造诊断。对不递归或浅递归的路径，代价是每次原生调用多一条比较和一条不跳转的条件跳转（fib(35)：改动前后在测量噪声之内）。

### 16.5 有帧代码里的故障出口

有帧代码调用 C++ 辅助函数，所以多一种故障：辅助函数接住的 C++ 异常（`JitFaultKind::PendingException`）。辅助函数把异常存进 `JitContext::pending` 后正常返回，代码在每次辅助函数调用之后检查 `faultKind`，非零就走同一个公共出口；驱动循环重新抛出。见 17.2。

为 JIT 代码注册展开信息（Linux 上 `__register_frame` 的 DWARF CFI，Windows 上 `RtlAddFunctionTable`）可以让 C++ 异常直接穿过，也能在调试器和崩溃转储里看到 JIT 帧；没有做。

---

## 17. 有帧代码（framed code）

### 17.1 模型

无帧代码只能表达不需要运行时的图。其余的图编译成有帧代码，它与解释器共用同一套状态：

- **每个激活都有帧池里的一个 `Frame`**，`slot[0]` 是它的 `Frame*`（辅助函数由此找到帧）。没有机器栈上的帧。
- **两条字节码之间，激活算出的一切都在帧里**：没有值留在寄存器或机器栈上。这由寄存器分配保证（辅助函数调用和离开点都算作调用，值不能跨过它们，见 2.3 阶段 3），死写消除在这些位置把所有槽视为活跃（`eliminateDeadFrameStores`）。
- 因此它可以调用读写帧的 C++ 辅助函数，也可以在任意一条字节码处停下、之后从这条字节码后面再进入。
- 对象常量不烧进代码：有帧代码从静态区读它（`VLoadFromMemAt`），回收器在静态区里维护这个引用。数值常量仍是立即数。（对象会移动时无帧代码也这样读，见 17.4。）

由此得到一条不变式：**Camel 函数被有帧代码"调用"而运行时，以及回收器运行时，机器栈上没有编译出的代码**（例外见 17.4 与 17.9：循环轮次的安全点在辅助函数里，此时机器栈上的有帧代码不持有值）。有帧代码在安全点上的"栈映射"是空的。

### 17.2 辅助函数

`MirOp::CallHelper` 调用 `slot_t helper(slot_t *slots, void *ctx, size_t pc)`（`trampoline.cpp`），做解释器对同一条字节码做的事：

| 字节码 | 辅助函数 | 说明 |
|---|---|---|
| `OPER` | `jitHelperOper` | 用 `FrameArgsView` 调用算子，结果写进结果槽 |
| `CAST` / `COPY` / `ACCS` / `FILL`，多于两臂的 `JOIN` | `jitHelperBytecode` | 在帧上执行这条字节码 |
| 带 case 值的 `BRCH`（match） | `jitHelperBranchArm` | `selectBranchArm` 选出臂，下标写进结果槽并返回；代码用比较链跳到 `BRCH` 后面跳转表的那一项 |
| `LBEG` / `LTST` / `LEND` | `jitHelperLoopBegin` / `jitHelperLoopTest` / `jitHelperLoopEnd` | 在帧上跑循环的三个事件（`loop_frame.h`），返回 1 时跳向 `getJumpTargetPc`，见 17.9 |

SysV 下的序列是 `push rdi; mov rsi, r13; mov edx, pc; mov rax, helper; call rax; pop rdi; cmp dword [r13+16], 0; jne .fault_exit`。body 入口处 RSP 模 16 余 8，`push rdi` 之后调用点对齐。

**辅助函数不抛异常。** 算子的故障（`RuntimeFault`）先按这条字节码定位（与解释器相同的 `makePcExecutionSite`），然后任何异常都存进 `JitContext::pending`，`faultKind` 置为 `PendingException`，正常返回；代码走第 16 节的公共出口，驱动循环释放帧后重新抛出。**辅助函数不调用 Camel 函数，也不到达回收器的安全点**（分配不是安全点）；唯一的例外是 `jitHelperLoopEnd`，它在轮次边界上轮询 FastVM 的安全点（17.9）。

### 17.3 离开与再进入：驱动循环

调用 Camel 函数的字节码不在编译出的代码里执行。`MirOp::ExitToDriver` 把续点地址（`lea r11, [rip+…]`）、离开的种类和字节码的 pc 写进 `JitContext`（`resumeAddr`、`exitKind`、`exitPc`），然后 `ret` 回包装器；有帧代码的 body 在字节码边界上不占机器栈，所以随处可以 `ret`。有帧代码的包装器在 `call body` 之前多一步：`resumeAddr` 非零就 `call` 那个地址。

驱动循环是 `FastVMSchedPass::runFramedJit`（`invokeOwnedJitFrame` 对有帧代码转到它）。每种离开它都照解释器的做法处理：

| `JitExitKind` | 字节码 | 驱动循环做什么 |
|---|---|---|
| `Call` | `FUNC`（目标是本图或别的图） | 安全点轮询、计热度（`materializeCallTarget`）、`push(pc, frame)`（递归上限在这里检查）、取新帧、拷实参。被调图有有帧代码：它成为当前激活，循环继续，**不在机器栈上嵌套**；是无帧代码：直接运行到返回；没有编译：`callBorrowed` 解释执行 |
| `Tail` | 目标是别的图的 `TAIL`，或回收器有请求时的自尾调用 | 安全点轮询、取实参、释放当前帧、取新帧，然后同上，但不 `push` |
| `Interp` | `CALL`、`SCHD` | 在当前帧上执行这一条字节码（经函数值的调用、高阶算子、`attempt`），与解释器一样在机器栈上嵌套并计数 |
| `Return` | `RETN` | 释放帧；回到驱动循环的入口深度就返回，否则 `pop()`、把结果写进调用方的结果槽、从调用方的续点再进入 |

有帧图之间的调用因此只用解释器的调用栈（`pcStack_` / `frameStack_`，外加续点的 `resumeStack_`），**递归上限、能到达的深度都与 `std::fvm` 相同**：上限一百万时 `depth(900000)` 在 `std::jit` 下算得完，与此前相同。故障在驱动循环里的处理也与解释器相同：释放它持有的帧、把调用栈退回入口深度、给还没有位置的故障补上当前字节码的位置。

**自尾调用**仍是跳回入口（5.3）。这是一个没有调用的循环，所以跳之前轮询回收器的标志字节（`MirOp::SafepointPoll`：`cmp byte [flag], 0`）；标志置位时以 `Tail` 离开，由驱动循环在机器栈上没有编译代码的状态下到达安全点并完成这次尾调用。

**解释器把对已编译图的尾调用交给编译出的代码。** 此前解释器遇到目标已编译的 `TAIL` 仍然解释执行目标，一个只从外面进入一次、之后自尾调用的循环即使被编译也永远不会以原生代码运行。现在：当前激活是这次 `callBorrowed` 的根时，解释器把准备好的帧和入口交还给调用者（`CallResult::tailFn`），由调用者（`call()` 或驱动循环）运行，解释器自己已经从机器栈上退出；否则就地 `invokeOwnedJitFrame`，结果当作被替换的激活的返回值。编译图与未编译图互相尾调用的循环因此不增长机器栈。

### 17.4 与回收器的关系

两种代码各自靠不同的理由在没有栈映射的情况下与回收器相容，标记-清除与分代（会移动对象）两种回收器都适用（规则的总表在 `36_gc_handle_rules.md` 第 4 节）：

- **无帧代码**把帧放在机器栈上、值放在寄存器里，回收器看不见也不会更新它们；它在进入到返回之间不调用任何东西、不轮询，所以那期间不会回收。分配器拒绝在它之下到达安全点：`mm::detail::nativeCodeDepth` 只在 `invokeOwnedJitFrame` 进入无帧代码的那次调用前后计数；有帧代码走 `runFramedJit`，不在其中。
- **有帧代码**的安全点与解释器的在同样的位置（`FUNC` / `TAIL` / `CALL`、高阶算子的每个元素、自尾调用的轮询、循环的每一轮（17.9）），都在驱动循环、解释器或辅助函数里，此时它已经 `ret` 或正处在 C++ 调用里。帧池的帧是已注册的根，回收器按帧布局扫描并就地更新；在辅助函数调用、离开点与轮询处没有值活在寄存器或机器栈上（寄存器分配拒绝跨过它们的值，之后代码重新读帧），所以这些位置上它的"栈映射"是空的。
- **由此得出的结论（原 `DESIGN-QUESTION(jit-safepoint-under-framed-code)`）**：自己会到达安全点的算子（调用 Camel 函数的 C++ 算子；标准库的高阶算子是 `SCHD`，不在此列）经 `jitHelperOper` 调用时，机器栈上有它上面的有帧代码。这是允许的：那段代码不持有回收器需要看见或更新的东西。"编译出的代码之下不得有安全点"是关于无帧代码的规则。
- **对象常量。** 回收器移动对象时（`--gc generational`），两种代码都不含对象的地址：对象常量在每次使用时从图的静态区读取，静态区是回收器就地更新的根（`36_gc_handle_rules.md` 第 5 节）。标记-清除下无帧代码仍把它当作立即数。唯一的例外是自调用或自尾调用在多于 4 个（经寄存器传递的上限）实参里传一个对象常量：那些实参作为立即数拷贝，这样的图在分代下编译成有帧代码（`collectRuntimeConstructs` 的 `object-constant-arg`）；在 17.5 的程序里没有出现。此前分代模式下含对象常量的图一律留给解释器，`test/cases/perf/gc/jit_const.cml`（返回字符串常量的双递归）因此慢 21 倍（`37_generational_gc_report.md` 6.1）；现在 `jit_const(36)` 在 `std::opt::inline std::jit` 下标记-清除 102 毫秒、分代 101 毫秒，同一程序分代下的 `std::opt::inline std::fvm` 是 2742 毫秒（各 5 轮中位数，2026-10-14）。
- 验证：`test/plans/feat/linear/jit_framed.plan.toml` 的 `gc_stress` 用例每第 7 个（循环用例每个）安全点回收并校验堆；`*.moving` 用例在安全点投毒（`CAMEL_GC_POISON=1`：每个安全点移动全部年轻对象并毒化原处）下、且每张图第一次调用就编译时运行；完整测试集在两种回收器下各跑一遍，随机程序也在两种回收器与投毒下跑过（17.7）。

### 17.5 覆盖面统计

`std::fvm(stats="<文件>")`（或 `std::jit(stats=...)`）让 `std::fvm` / `std::jit` 在结束时写出每张图的字节码数、解释器执行了其中多少条、JIT 的结论与原因（`fastvm_stats.cpp`）。`test/tools/jitcov/jitcov.py` 用它对一批程序在三条流水线下各跑 `std::fvm` 与 `std::jit`，汇总成表：

```
python3 test/tools/jitcov/jitcov.py --timeout 60 --out DIR      # 仓库里全部 .cml
python3 test/tools/jitcov/jitcov.py --progen 1-300              # 随机程序
```

**测量（2026-10-13，Linux x86-64，release）。** 程序是仓库里全部 `.cml`：`test/cases`、`examples`、`docs/tutorial/examples`、`benchmarks`，共 1015 个，其中 306 个在这种跑法下没有记录（不是完整程序、需要输入或模块不可用）。"改前"是只有无帧代码时（提交 `feat(jit): coverage statistics …`，当时的主干，893 + 85 个程序，每次运行限时 20 秒），"改后"是本节的实现（每次运行限时 60 秒）。"已编译"指编译并进入过；其余的图能编译但没有热到被编译（一张图在同一个调用点被调用第二次时才编译）。

| 程序 | 流水线 | 图数（改前 → 改后） | 已编译：改前 | 已编译：改后（无帧 + 有帧） | 被拒绝：改前 → 改后 | 不经解释器执行的字节码：改前 → 改后 |
|---|---|---|---|---|---|---|
| test/cases | `std::jit` | 5881 → 6162 | 120（2.0%） | 956（15.5%；130 + 826） | 4919 → 0 | 0.0% → 98.0% |
| test/cases | `std::opt::inline std::jit` | 5889 → 6162 | 69（1.2%） | 404（6.6%；71 + 333） | 4759 → 0 | 47.5% → 97.7% |
| test/cases | `std::opt std::jit` | 6527 → 6826 | 59（0.9%） | 342（5.0%；61 + 281） | 5252 → 0 | 37.5% → 97.7% |
| examples | `std::jit` | 623 → 637 | 17（2.7%） | 126（19.8%；17 + 109） | 581 → 0 | 0.0% → 99.9% |
| examples | `std::opt::inline std::jit` | 623 → 637 | 2（0.3%） | 17（2.7%；2 + 15） | 575 → 0 | 98.6% → 99.9% |
| examples | `std::opt std::jit` | 639 → 653 | 2（0.3%） | 18（2.8%；2 + 16） | 589 → 0 | 98.6% → 99.9% |
| tutorial | `std::jit` | 169 → 171 | 2（1.2%） | 18（10.5%；2 + 16） | 132 → 0 | 0.0% → 99.9% |
| tutorial | `std::opt::inline std::jit` | 169 → 171 | 5（3.0%） | 6（3.5%；5 + 1） | 126 → 0 | 21.3% → 99.9% |
| tutorial | `std::opt std::jit` | 173 → 175 | 3（1.7%） | 4（2.3%；3 + 1） | 127 → 0 | 21.3% → 99.9% |
| benchmarks | `std::jit` | 342 → 342 | 0 | 38（11.1%；0 + 38） | 334 → 0 | 0.0% → 54.6% |
| benchmarks | `std::opt::inline std::jit` | 342 → 342 | 0 | 37（10.8%；0 + 37） | 334 → 0 | 0.0% → 54.1% |
| benchmarks | `std::opt std::jit` | 443 → 443 | 0 | 33（7.4%；0 + 33） | 435 → 0 | 0.0% → 39.6% |

"不经解释器执行的字节码"是 `1 - 解释器在 std::jit 下执行的条数 / 在 std::fvm 下执行的条数`；驱动循环替有帧代码执行的调用不计入解释器。`benchmarks` 下能出记录的程序只执行几千条字节码，大部分图只进入一次。

**两种回收器（2026-10-14，主干 v0.2.9 之上）。** 同样的跑法，此时 `test/cases` 有 1010 个程序（325 至 327 个没有记录），`benchmarks` 有 43 个（16 个没有记录）。两种回收器下都没有被拒绝的图，也没有因为回收器而少编译的图：

| 程序 | 流水线 | 已编译：标记-清除（无帧 + 有帧） | 已编译：分代（无帧 + 有帧） | 不经解释器执行的字节码（两者相同） |
|---|---|---|---|---|
| test/cases | `std::jit` | 992（136 + 856） | 1000（136 + 864） | 96.5% |
| test/cases | `std::opt::inline std::jit` | 420（77 + 343） | 421（77 + 344） | 96.1% |
| test/cases | `std::opt std::jit` | 348（65 + 283） | 348（65 + 283） | 96.0% |
| examples | `std::jit` | 126（17 + 109） | 126（17 + 109） | 99.9% |
| examples | `std::opt::inline std::jit` | 17（2 + 15） | 17（2 + 15） | 99.9% |
| tutorial | `std::jit` | 18（2 + 16） | 18（2 + 16） | 99.9% |
| benchmarks | `std::jit` | 67（0 + 67） | 67（0 + 67） | 98.5% |
| benchmarks | `std::opt::inline std::jit` | 44（2 + 42） | 44（2 + 42） | 98.1% |

分代一列多出的几张图来自多出记录的两个程序。需要运行时的构造（标记-清除，`std::jit`，5520 张图）按图数是 `OPER` 45.0%、没有静态区 25.8%、`FILL` 24.6%、`CALL` 20.5%、跨图 `FUNC` 20.4%，按执行量是跨图 `TAIL` 70.4%、没有静态区 14.1%、跨图 `FUNC` 13.1%、`OPER` 11.4%，与 2026-10-13 的分布相同。

**改前被拒绝的原因**（test/cases，`std::jit`，4919 张被拒绝的图；一张图含几种就各计一次；"权重"是这些图在 `std::fvm` 下执行的字节码占全部的比例，"唯一"是只含这一种的图）：

| 构造 | 含它的图 | 权重 | 唯一原因的图 | 权重 |
|---|---|---|---|---|
| `OPER` | 2306（46.9%） | 8.5% | 414 | 0.8% |
| `FILL` | 1328（27.0%） | 0.1% | 147 | 0.0% |
| 没有静态区（图里没有常量） | 1172（23.8%） | 15.4% | 1172 | 15.4% |
| `CALL` | 1050（21.3%） | 0.0% | 609 | 0.0% |
| 目标是别的图的 `FUNC` | 1041（21.2%） | 14.2% | 99 | 13.8% |
| `ACCS` | 766（15.6%） | 0.1% | 0 | 0.0% |
| 目标是别的图的 `TAIL` | 581（11.8%） | 69.6% | 304 | 62.2% |
| `SCHD` | 203（4.1%） | 0.0% | 14 | 0.0% |
| `CAST` | 121（2.5%） | 0.0% | 33 | 0.0% |
| `COPY` | 86（1.7%） | 0.0% | 3 | 0.0% |
| match 的 `BRCH` / `JOIN` | 34 / 9 | 0.0% | 0 | 0.0% |

按图数最多的是算子（出现最多的是 `:io/println`、张量算子、`:str/format`、`:op/idx_str`、`:var/get`），按执行量最多的是跨图的尾调用与调用：不经 `std::opt::inline` 时分支的每个臂是一张单独的图，循环的每一轮都要跨图。`std::opt::inline std::jit` 下按权重排第一的是 `OPER`（19.2%）。改后这些构造不再拒绝图，同一张表在工具的输出里变成"哪些构造使图成为有帧代码"，分布与上表相同（改后 `std::jit` 下 5172 张图需要运行时，被拒绝的 0 张）。

### 17.6 性能

`test/tools/jitcov/workloads.cml` 有九个负载，`test/tools/jitcov/bench.py` 交替运行各个变体并报告最小值与中位数。下表是 2026-10-14 在 Linux x86-64 上（主干 v0.2.9 之上）各 7 轮的中位数（毫秒，含进程启动与编译；`/proc/loadavg` 的一分钟值在 9.3 到 12.1 之间，机器上有别的任务，数值只说明量级）。"不编译"是 `std::jit` 加参数 `hot_threshold=1000000000`：同一个解释器、同一套热度记账，但没有图被编译。

| 负载 | `std::fvm` | `std::jit` | `std::jit`，不编译 | `std::opt::inline std::fvm` | `std::opt::inline std::jit` | 分代：`std::opt::inline std::fvm` | 分代：`std::opt::inline std::jit` |
|---|---|---|---|---|---|---|---|
| `fib`（N=30） | 152.0 | 176.9 | 183.4 | 136.9 | 30.5 | 133.5 | 32.5 |
| `collatz`（60000） | 729.9 | 700.9 | 866.7 | 454.6 | 169.5 | 468.0 | 174.8 |
| `strings`（600000） | 171.2 | 166.4 | 187.7 | 162.9 | 145.1 | 120.0 | 103.7 |
| `arrays`（300000） | 97.4 | 104.0 | 115.8 | 108.9 | 96.3 | 95.3 | 77.9 |
| `mutual`（4000000） | 345.4 | 298.6 | 387.7 | 234.4 | 190.2 | 246.5 | 202.8 |
| `structs`（1000000） | 259.7 | 258.1 | 263.5 | 227.7 | 187.8 | 183.9 | 135.5 |
| `calls`（2000000） | 341.1 | 450.4 | 441.1 | 233.8 | 79.8 | 192.0 | 84.6 |
| `hof`（600000） | 128.4 | 128.8 | 138.3 | 133.8 | 137.6 | 118.8 | 114.3 |
| `float`（3000000） | 339.0 | 321.3 | 436.7 | 247.2 | 142.7 | 263.3 | 130.6 |

`std::opt::inline` 之后编译出的代码在两种回收器下都不比 `std::fvm` 慢（`hof` 在噪声之内），`fib`、`collatz`、`calls`、`float` 快 1.7 到 4.5 倍。不经 `std::opt::inline` 时七个负载与 `std::fvm` 相差在一成之内或更快，**`fib` 与 `calls` 慢约 16% 与 32%**。

**原因不在编译出的代码里。** "不编译"一列与 `std::jit` 一列在这两个负载上几乎相同（183 对 177、441 对 450），都比 `std::fvm` 慢：慢在 `std::jit` 下的解释器自己的调用路径（每次调用的热度计数与调用目标的查询 `materializeCallTarget`、`invokeCallOrJit` 等），只有无帧代码时就已如此（改动前各一次的测量：`fib(27)` 83 对 61 毫秒，`calls` 161 对 132 毫秒）。在其余七个负载上编译出的代码把这份开销挣了回来；这两个负载几乎只做跨图调用，每次调用在有帧代码里是一次离开与再进入，挣回的很少。有帧代码每次离开再进入要过两次包装器；把有帧图之间的调用做成"辅助函数完成压栈与取帧、代码直接跳到被调方 body"可以去掉这两次包装器；解释器一侧的记账可以在目标编译失败或已编译后不再进行。两者都没有做。

### 17.7 验证

- `test/plans/feat/linear/jit_framed.plan.toml`：数值、故障（算子的故障、编译代码自己检查的除零、递归超限，各有隔离与不隔离两种）、三十万轮的循环、每个安全点都回收，在四个后端与 `std::opt::inline std::jit` 上一致，并断言函数确实被编译成了预期的那种代码。
- 随机程序（`test/tools/progen`，参考求值器独立于各后端）：`progen.py diff --matrix full --backends jit`（25 种重写序列各接 `std::jit`，加不带遍的运行，每个程序 26 格）。种子 20001–20300：7800 格，7790 格与参考一致，10 格是合法的拒绝（`MacroEvaluationFailed`），没有坏格。加 `CAMEL_JIT_HOT_THRESHOLD=0`、种子 21001–21400：10400 格，10369 格一致，5 格合法拒绝，26 格是同一个程序（种子 21364）在全部 26 格上的 `IndexOutOfRange`，包括不带遍的节点虚拟机，所以与 JIT 无关：程序里是 `false && xs[2].0`，参考求值器对 `&&` 短路，而实现先求了右边。
- `test/tools/jitcov/deep_recursion.sh`：上限 100000 与 1000000、递归 50000 与 900000 层、机器栈 256 KiB 到 8 MiB、`std::nvm` / `std::fvm` / `std::jit` / `std::prl` 及其与 `std::opt::inline`、`std::opt` 的组合、直接递归、经函数值、在 `map` 元素里、在 `attempt` 里，共 256 次运行：96 次算完，160 次给出带位置的 `MaxRecursionDepthExceeded`，没有一次以信号结束。
- 2026-10-14（主干 v0.2.9 之上）：完整默认测试集在标记-清除下 4418 个中 4413 个通过，5 个因机器负载超时（都不经 JIT：`unordered_foreach_smoke.nvm`、`export_tree_recursion`、三个 `gc.collection_trees_tiny_*_young_nvm`），单独重跑全部通过；`CAMEL_GC=generational` 下 4418 个全部通过。默认测试集本身含安全点投毒的扫测（`mm.gc_poison_sweep`）。随机程序（生成器 VERSION 3，全矩阵 101 格）：种子 30001–30150 标记-清除 15150 格、30151–30300 分代 15150 格，全部与参考一致；种子 30301–30400 在投毒且第一次调用即编译下跑 `std::jit` 的 26 格，2600 格全部一致。`deep_recursion.sh` 在两种回收器下各 256 次，没有一次以信号结束。
- `std::jit(hot_threshold=0)` 让图在第一次调用时就编译（默认是同一调用点的第二次），用于让短程序也跑在编译出的代码上。
- 调试器下 `std::jit` 按 `std::fvm` 运行（`35_debugger.md`）。带观察者钩子的解释循环（`inspectvm.cpp`）不把尾调用交给编译出的代码，驱动循环在有观察者时经 `call()` 运行解释的被调方，所以钩子不会被绕过。

### 17.8 没有做的

- Win64：`CallHelper`、`ExitToDriver`、包装器的续点分支按 Win64 的约定写了；H43a 在 Windows x64（clang++）上构建并运行了它们（含循环的图），第 8 节的汇编包装器仍没有被用到。
- 有帧图之间的原生调用与解释器一侧热度记账的精简（见 17.6）；算子的原生下降（整数取模、数组长度与下标等仍经辅助函数）。
- `CALL` 与 `SCHD` 由驱动循环按解释器的方式嵌套执行，深度受机器栈限制，与 `std::fvm` 相同。
- 为 JIT 代码注册展开信息（16.5）。
- 循环事件的原生下降（17.9）：`LBEG`/`LTST`/`LEND` 仍是每轮两次辅助函数调用，代价在 H43 的第 7 项里再降。

### 17.9 循环：`LBEG` / `LTST` / `LEND`

循环是图里的区域（`39_loops.md` 第 4 节）：执行计划把条件、循环体和 `LOOP` 节点排成一段，在三个位置发出 `LBEG`（一轮开始）、`LTST`（条件跑完：停止，或绑定循环体的端口）和 `LEND`（循环体跑完：取下一轮的状态，并跳回一轮的开头）。编译出的代码不把这些规则重写一遍——状态的元组与解包、端口的绑定、每轮收集的追加、H29 的元素物化都只在 `camel/execute/loop_frame.h` 里有一份——而是经辅助函数在帧上执行：

| 字节码 | 辅助函数 | 之后 |
|---|---|---|
| `LBEG` | `jitHelperLoopBegin` → `loopBegin` | 无跳转 |
| `LTST` | `jitHelperLoopTest` → `loopTest` | 返回 1：`JneRel32(getJumpTargetPc)`，跳到它的 `LEND`；否则落到循环体的第一条 |
| `LEND` | `jitHelperLoopEnd` → `loopEnd` | 返回 1（还有一轮）：`JneRel32(getJumpTargetPc)`，跳回一轮的开头；否则落到循环之后 |

与解释器（`gotovm.cpp`、`casevm.cpp`）里这三条字节码逐句相同。含循环的图因此是有帧代码（`collectRuntimeConstructs` 的 `LOOP`）。两个跳转都是一轮的边界：目标是 `blockStarts` 的一项（`slotCache` 在这里清空），事件之后也清空缓存，寄存器分配还不允许值跨过 `CallHelper`，所以每一轮都从帧重新读写状态。

`LEND` 在轮次边界上轮询 `fastVmCallBoundarySafepoint()`（`loopRoundSafepoint`），位置与 FastVM 相同：解释的轮次与编译的轮次到达同样多的安全点（在 `CAMEL_GC_STRESS_SAFEPOINT=1` 下两者给出相同的回收次数与移动对象数），会收集的循环的 `MemoryExhausted` 也仍报在循环的位置。17.4 的"机器栈上没有编译出的代码"在这一处是有意的例外：轮询发生在辅助函数里，`LEND` 已经进入 C++，寄存器里没有值（辅助函数调用是分配的屏障）。

数字（2026-10-21，Linux x86-64，一千万轮两状态的 `while`，整进程 CPU 时间、批内交替五次取中位数）：`std::nvm` 1.611 秒、`std::fvm` 0.457 秒、`std::jit` 0.363 秒（含循环的入口函数在第一次调用就编译）、手写尾递归解释 0.309 秒、同一尾递归编译 0.084 秒。编译后的循环比解释器快 20%（36.3 对 45.7 ns/轮），但仍是编译后的尾递归（8.4 ns/轮）的 4.3 倍：每轮两次辅助函数调用（`LTST`、`LEND`）是剩下的固定开销，目标 0.155 秒仍未达到。详见 `handover note ⟨71f8e6b⟩ (H43-jit-loops)`。测试在 `test/plans/feat/linear/loops.plan.toml`（`loops.compiled*`）与 `jit_framed.plan.toml`（`framed.loops.regions.*`，`-vv` 日志断言图被编译成有帧代码）。
