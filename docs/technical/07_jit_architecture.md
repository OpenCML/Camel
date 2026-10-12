# FastVM JIT 架构文档

> **适用范围（2026-10 更正）**：JIT 目前只编译"自足"的图——字节码里只有算术、比较、`BRCH`/`JOIN`/`JUMP`/`RETN`，以及目标是**本图自身**的 `FUNC`/`TAIL`。含 `OPER`、`CAST`、`COPY`、`FILL`、`CALL`、`ACCS` 或调用其他图的图在入口处（`describeFramelessSelfFuncFailure`）就被拒绝，留给解释器；`-v` 日志会给出原因。因此编译出的代码**不调用任何 C++**：自调用是原生调用，自尾调用是跳转。本文中关于跨图调用、trampoline 的内容描述的是保留但当前不可达的代码，已逐处标注。典型来源是 `std::inline` 之后只剩自递归的小函数。

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

rdi 在所有实参写完之后才切换，所以实参的来源始终按调用方的帧寻址；r11 不可分配，不会与装着实参的寄存器冲突。帧大于 `kMaxNativeFrameBytes`（2 KB）的图含自调用时不编译：256 层这样的帧在较小的机器栈上放不下。

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

## 8. Trampoline 系统（当前不可达）

`trampoline.cpp` 中的 `trampolineFunc`、`trampolineTail`、`trampolineOper`、`trampolineCast`、`trampolineBytecode`、`directSelfFuncInvoke` 是为"JIT 代码调用 C++ 运行时"准备的桥接函数，`x64_backend.cpp` 中也保留着 `OPER`/`CAST`/`COPY`/`FILL`/`CALL`/`ACCS` 的下降代码。**入口处的过滤使它们全部不可达**：含这些字节码的图不编译。

重新启用之前必须先解决两件事：

1. 这些函数通过 `slots[0]` 的 `Frame*` 读写数据，而原生自调用的帧在机器栈上（见 6.2）。
2. 它们会抛 C++ 异常（算子故障），而 JIT 代码没有展开信息；异常必须在 trampoline 边界上被接住，转成第 16 节的故障记录后由 JIT 代码走故障出口。

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
| FUNC | NativeJitFuncCall | 只可能是自调用，详见第 5 节 |
| TAIL | 写形参槽 + `JmpRel32(entry)` | 只可能是自尾调用，详见 5.3 |
| OPER / CAST / COPY / FILL / CALL / ACCS | — | 含这些字节码的图在入口处被拒绝，不编译（下降代码保留但不可达，见第 8 节） |

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

**不支持的 opcode**（OPER, CAST, COPY, ACCS, FILL, CALL, SCHD，以及目标不是本图的 FUNC/TAIL）使该图在入口处被拒绝，不进入 JIT 缓存。

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
| 跨图函数调用 | ❌ 不编译 | 入口处拒绝调用其他图的图 |
| 包含 OPER 调用的函数 | ❌ 不编译 | 入口处拒绝含 OPER 的图 |

---

## 12. 关键约束与边界条件

### 12.1 编译期约束

| 约束 | 位置 | 说明 |
|------|------|------|
| BRCH 仅支持 `withCnt()==0` | `x64_backend.cpp` BRCH case | 带 with 参数的 BRCH 无法编译，整个图退回解释器 |
| 调用的参数个数不限 | `NativeJitCallParams::args` | 不超过 4 个走寄存器，更多的经 r11 / 机器栈逐个移动 |
| 自调用的帧不超过 2 KB | `kMaxNativeFrameBytes` | 更大的帧配合 256 层递归会占用过多机器栈，该图不编译 |
| 不支持的 opcode 导致整图不编译 | `describeFramelessSelfFuncFailure` | OPER, CAST, COPY, ACCS, FILL, CALL，以及目标不是本图的 FUNC/TAIL |
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
| 递归深度上限 | 原生自调用 | 与解释器相同的上限（默认 256，`--max-recursion-depth`），由 r15 计数；另外每次调用检查机器栈余量。两者超限都给出 `MaxRecursionDepthExceeded`（第 16 节） |
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

输出格式：`[offset]  instruction  ; annotation`。C++ wrapper 从 offset 0 开始，JIT body 从 `jitEntryOffset`（包装器之后）开始。

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
| 整数除数为零（IDIV / LDIV） | 除法之前 | `RuntimeDiag::DivisionByZero`，位置是该除法 |
| 浮点除数等于零（FDIV / DDIV，±0.0） | 除法之前 | 同上（解释器对浮点除零同样报故障） |
| 原生自调用超过递归深度上限 | 调用之前 | `RuntimeDiag::MaxRecursionDepthExceeded`，位置是该调用，图名与上限同解释器 |
| 原生自调用时机器栈将满 | 调用之前 | 同一个 `MaxRecursionDepthExceeded`，消息与解释器的嵌套调用相同（`N open calls: the native stack of the thread, K KiB, is full`） |

最小整数除以 -1 不是故障：结果回绕（各后端一致）。整数取模是 `OPER`，含它的图不编译，由解释器检查。

### 16.4 递归深度的计数

解释器的上限是 `FastVMSchedPass::maxRecursionDepth_`（256），按 `push` 的次数计。为了让同一个递归无论由谁执行都在同一次调用上超限：

- 解释器从字节码 `FUNC` 进入 JIT 代码时照常 `push` / `pop`（`gotovm.cpp` / `casevm.cpp`）。
- `invokeOwnedJitFrame` 进入前把 `maxRecursionDepth_ - stackDepth_` 写入 `JitContext::depthBudget`，包装器装入 r15。
- 每次原生自调用 `sub r15, 1`，借位即故障；返回后 `inc r15`。尾调用不计数。

**机器栈的余量。** 上限可以设到一百万，而原生自调用每层都占机器栈（`depth` 每层 112 字节，8 MiB 的栈大约放七万层），所以只计数不够。`invokeOwnedJitFrame` 在写入 `depthBudget` 的同时把 `execute::nativeStackFloor()` 写入 `JitContext::stackLimit`：当前线程栈的下界加保留量（栈的十六分之一，至少 128 KiB，与解释器 `nativeStackNearlyExhausted` 用的是同一个保留量；栈界读不到或 `CAMEL_NATIVE_STACK_GUARD=0` 时为 0，任何栈指针都不低于它）。栈界按线程读取（`thread_local`），所以由哪个线程进入原生代码就用哪个线程的栈。每次原生自调用在计数之后执行 `cmp rsp, [ctx + stackLimit]; jb .fault_stack`。故障桩除了记录种类与 pc，还把 r15 存回 `depthBudget`，`raiseJitFault` 据此算出"已有多少次调用未返回"。故障出口本身不使用栈；保留量留给之后 C++ 一侧构造诊断。对不递归或浅递归的路径，代价是每次原生调用多一条比较和一条不跳转的条件跳转（fib(35)：改动前后在测量噪声之内）。

### 16.5 扩展时的注意事项

这条出口依赖"JIT 代码不调用 C++"。一旦放开 `OPER` 等需要 trampoline 的字节码：

- trampoline 必须接住 C++ 异常，把它存进 `JitContext`（例如 `std::exception_ptr`）并正常返回；JIT 代码在调用之后检查并走故障出口；`invokeOwnedJitFrame` 重新抛出。
- 如果 trampoline 会再次进入 JIT 代码或解释器，中间的 C++ 帧属于内层的包装器激活，仍然成立；但要保证 trampoline 持有的资源在返回前释放。
- 更彻底的做法是为 JIT 代码注册展开信息（Linux 上 `__register_frame` 的 DWARF CFI，Windows 上 `RtlAddFunctionTable`），那样 C++ 异常可以直接穿过，也能在调试器和崩溃转储里看到 JIT 帧。
