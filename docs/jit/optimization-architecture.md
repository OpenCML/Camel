# JIT 基础优化与架构改进方案

本文档给出**系统性方案**，使「prologue 后冗余 mov rcx,rdi / mov rdx,rsi」这类基础优化能够被统一处理，并便于后续增加更多类似优化，而不依赖到处打补丁。

---

## 1. 问题与优化类型

### 1.1 当前流水线

```
字节码 + RegAlloc
    → 第一遍：按 opcode 计算每条字节码对应机器码长度，得到 pcToOffset
    → 第二遍：Encoder 直接 emit 到 vector<uint8_t>，无中间表示
    → load：拷贝到可执行页
```

**痛点**：优化只能塞进两处之一——

- **发射时**：在 backend/encoder 里加分支（如「若是第一条且为 TAIL 则少发两条 mov」），导致状态和特例散落各处。
- **发射后**：对已生成的字节流做 peephole（匹配 48 89 f9 48 89 f2 并删除），脆弱且难以扩展。

### 1.2 可归纳的「基础优化」类型

| 类型 | 示例 | 所需信息 |
|------|------|----------|
| **ABI 冗余** | Win64 下 prologue 后立即 call 时，rcx/rdx 仍为 slots/ctx，无需再 mov rcx,rdi / mov rdx,rsi | 是否「刚做完 prologue 或刚 call 返回且未改 rcx/rdx」 |
| **死代码** | mov rax, rbx 后紧跟 mov rax, [rdi+8]，前者可删 | 每条指令的 def/use 与下一条的 use |
| **冗余 mov** | mov rA, rA；mov rA, rB 后 mov rB, rA（可合并/化简） | 相邻指令的寄存器传递 |
| **常量/立即数** | 连续两次 mov r8d, imm 可只保留第二次 | 指令序列与立即数 |
| **调用后寄存器** | trampoline 返回后 rdi/rsi 是否仍为 slots/ctx（由 ABI 保证），可标记「rcx/rdx 已与 rdi/rsi 同值」 | 调用约定与是否被中间指令覆盖 |

要系统化处理这些，需要：**要么在发射时维护更多状态，要么引入一层可分析与改写的表示**。

---

## 2. 方案总览：三层改进

| 层级 | 内容 | 改动范围 | 适用优化 |
|------|------|----------|----------|
| **L1 发射时状态** | 在 backend/encoder 维护「Win64 下 rcx/rdx 是否已等于 slots/ctx」 | 小，仅 x64_backend + encoder | ABI 冗余（prologue 后首条 TAIL/FUNC/OPER 省 2 条 mov） |
| **L2 发射缓冲（MIR 或 macro-ops）** | 第二遍先输出到「机器级指令缓冲」，再做一次优化 pass，最后再 encode 成字节 | 中，新增 buffer + 优化 pass + 编码 pass | ABI 冗余、死 mov、简单 peephole、常量折叠等 |
| **L3 完整 MIR（可选远期）** | 显式基本块、def-use、可做更激进的寄存器分配与调度 | 大 | 全局寄存器分配、指令调度、更多窥孔与死代码删除 |

建议：**先做 L1 快速见效，再引入 L2 作为统一落地层**；L3 仅作远期选项，不在当前范围。

---

## 3. L1：发射时状态（最小改动）

### 3.1 思路

- 在 **Win64** 下，在 backend 或 encoder 中维护一个布尔（或枚举）：
  - `win64SlotsCtxInRcxRdx_`：当前 rcx 是否等于 slots、rdx 是否等于 ctx（即是否与 rdi/rsi 一致）。
- **Prologue 后**置为 `true`；**任意会改写 rcx/rdx 的指令**（如 mov rcx, xxx、mov rdx, xxx、JOIN 里用的 rcx 等）后置为 `false`；**trampoline 返回后**根据 ABI 可知 rdi/rsi 仍为 slots/ctx，但 rcx/rdx 被 callee 破坏，故置为 `false`。
- 在 `callTrampolineWin64` / `callTrampolineOperWin64` 中：若 `win64SlotsCtxInRcxRdx_ == true`，则**不发射**「mov rcx, rdi」「mov rdx, rsi」两条，然后置为 `false`（因为 call 会破坏 rcx/rdx）。

### 3.2 放置位置

- **选项 A**：状态放在 **Encoder** 里，encoder 提供 `setWin64ArgRegsValid(bool)` / `needPrepareWin64Args()`，由 backend 在每条字节码发射前后设置/查询。Encoder 的 `callTrampolineWin64` 内部根据 `needPrepareWin64Args()` 决定是否发两条 mov。
- **选项 B**：状态放在 **X64Backend** 里，backend 在第二遍循环中维护该 bool，在进入 FUNC/TAIL/OPER 的 case 时若为 true 则调用 encoder 的「仅设 r8d/r9 + mov rax + call」版本（或给 `callTrampolineWin64` 传一个 `skipRcxRdxSetup` 参数）。

推荐 **选项 A**：encoder 封装「何时需要准备 rcx/rdx」的逻辑，backend 只负责在合适时机调用 `setWin64ArgRegsValid`（prologue 后 true；任何可能改写 rcx/rdx 的指令后、以及每次 call 后 false）。

### 3.3 第一遍长度计算

- 若采用 L1，则「首条为 TAIL/FUNC 且 Win64」时，call 序列少 6 字节，**第一遍**计算 `pcToOffset` 时需对应减少该 6 字节（例如在 TAIL/FUNC 的 offset 累加处根据「是否为第一条」和「是否 Win64」减去 6）。否则第二遍生成的代码变短，与第一遍假设不一致，可能导致相对跳转等出错。

### 3.4 小结

- **优点**：改动小，只碰 x64_backend 与 x64_encoder，立即消除「prologue 后首条 call 的冗余 mov」。
- **缺点**：仅解决这一类优化；其它基础优化（死 mov、peephole）仍需在别处写逻辑或等 L2。

---

## 4. L2：发射缓冲 + 优化 pass（系统化基础优化）

### 4.1 目标

- 让「基础优化」有统一落脚点：**不直接 emit 到最终字节流，先 emit 到一层「机器级指令」缓冲，对该缓冲做一次（或多次）优化，再一次性 encode 成字节**。
- 这样「删除冗余 mov」「ABI 下省略 rcx/rdx 准备」等都变成对缓冲的改写，而不是在 encoder 里加分支。

### 4.2 机器级表示（MIR）的形状

不必一步到位做成「完整 MIR」，只需满足：**可表达当前 encoder 能发出的所有指令，且每条可被单独删除或替换**。

建议用 **线性指令列表 + 每条一条记录**，例如：

```text
enum class MIRKind {
    MovRegReg,      // 如 mov rdi, rcx
    MovRegImm32,
    MovRegImm64,
    MovRegFromFrame,
    MovFrameFromReg,
    MovRegFromMem,
    ...
    CallReg,        // call rax
    Ret,
    JmpRel32,
    ...
};

struct MIR {
    MIRKind kind;
    uint8_t r0, r1; // 寄存器编号（若有）
    int32_t disp;
    uint64_t imm64;
    // 仅需当前 encoder 用到的字段即可
};

std::vector<MIR> buffer;
```

- **第二遍**：backend 不再调用 `enc.emitXXX(...)` 直接写 `code`，而是调用 `buffer.push_back(MIR{...})`（或封装成 `emitMovRegReg(rdi, rcx)` 等），在 buffer 里追加 MIR。
- **优化 pass**：遍历 `buffer`，例如：
  - **Win64 ABI**：若发现序列「MovRegReg(rdi, rcx); MovRegReg(rsi, rdx); MovRegReg(rcx, rdi); MovRegReg(rdx, rsi); … CallReg(rax)」，且前两条是 prologue、后两条仅为 call 准备参数，则删除中间两条 MovRegReg(rcx,rdi) 与 MovRegReg(rdx,rsi)。
  - **死 mov**：若 MovRegReg(rA, rB) 的下一条立刻覆盖 rA，且中间无 use rA，则可删该 mov。
  - 其它 peephole 同理，在 buffer 上做模式匹配与删除/替换。
- **编码 pass**：遍历优化后的 buffer，对每条 MIR 调用现有 Encoder 的编码逻辑（或拆成「Encoder 从 MIR 编码」），输出到 `vector<uint8_t>`。

### 4.3 与现有 Encoder 的关系

- **方式 1**：Encoder 增加「从 MIR 编码」接口，例如 `encode(const std::vector<MIR>& buf, std::vector<uint8_t>& out)`，内部根据 `MIRKind` 调用现有的 `emitBytes(...)` 逻辑，不再由 backend 按字节码逐条调用 `enc.callTrampolineWin64` 等。
- **方式 2**：backend 第二遍改为生成 `vector<MIR>`，再写一个 `MIREncoder` 或扩展现有 Encoder，使其接受 MIR 流并输出字节。原有 `callTrampolineWin64` 等可拆成「生成若干条 MIR」的辅助函数，供 backend 填入 buffer。

这样，**「是否发 mov rcx,rdi / mov rdx,rsi」不再由 encoder 决定，而由「先生成 MIR，再由优化 pass 删除冗余 MIR」决定**，逻辑集中、易扩展。

### 4.4 第一遍长度计算

- 第一遍若仍按「字节码 → 长度」计算，有两种方式：
  - **保守**：第一遍仍按「最大可能长度」算（含两条 mov），优化只减少实际字节数；**第二遍**先按当前方式算 pcToOffset（按未优化长度），再在**优化后**对 buffer 做一次「按 MIR 序列重新计算偏移」，得到真实 offset，再编码。即：第一遍用「未优化尺寸」保证上界，第二遍先产 MIR → 优化 → 根据 MIR 序列重算所有偏移（含跳转目标）→ 再编码。或：
  - **两遍都基于 MIR**：第一遍就生成 MIR（不优化），由 MIR 长度算 pcToOffset，第二遍再生成 MIR（可与第一遍共用逻辑）、优化、编码。这样长度与最终代码一致，但第一遍也要走「字节码→MIR」的路径。

推荐先采用「第一遍仍按当前方式按字节码估长度（保守），第二遍生成 MIR → 优化 → 再算一次偏移 → 编码」，避免第一遍大改；若后续发现跳转或对齐有问题，再考虑第一遍也走 MIR。

### 4.5 小结

- **优点**：所有「基础优化」都在 MIR 上做，易加规则、易测；encoder 只负责 MIR→bytes，职责清晰。
- **缺点**：需要引入 MIR 结构和一次（或多次）优化与编码 pass，改动量中等。

---

## 5. L3：完整 MIR（远期）

- 显式基本块、前驱后继、def-use 链、可做图上的寄存器分配与指令调度。当前 JIT 仍是「线性字节码 → 线性机器码」，若未来要做更激进的优化（如跨基本块寄存器分配、更复杂的调度），再考虑引入 L3。**当前不实现，仅作路线图**。

---

## 6. 实施建议

1. **短期**：实现 **L1**（encoder 内 `win64ArgRegsValid` + `callTrampolineWin64` 条件省略两条 mov），并在第一遍对「首条 TAIL/FUNC 且 Win64」的图少算 6 字节。验证 fib 等用例无回归。
2. **中期**：设计并引入 **L2**：  
   - 定义 `MIR` 与 `MIRKind`（或等价命名），覆盖当前所有发射指令；  
   - 将第二遍改为「字节码 → MIR buffer」；  
   - 实现 Win64 冗余 mov 删除（及 1～2 个简单 peephole）；  
   - 实现「MIR → 字节」编码 pass，并保证第一遍长度与编码后长度一致（或按 4.4 保守处理偏移）。  
3. **长期**：视需要再考虑 L3（完整 MIR、图上的优化）。

---

## 7. 相关文件与索引

- **MIR 层（与 backend 并列的中端）**：`jit/mir/` — `mir.h` / `mir.cpp`（MIR 定义、可读 `mirToString`/`mirPrint`、`mirSizeBytes`）、`mir_builder.h`（发射到 buffer）、`mir_optimize.h` / `mir_optimize.cpp`（Win64 冗余 mov 删除）、`mir_encode.h` / `mir_encode.cpp`（MIR→字节，当前实现依赖 x64 Encoder）。pcToOffset 由优化后的 MIR 长度统一计算。
- **x64 后端**：`jit/backend/x64/` — `x64_backend.cpp`（构建 MIR → 优化 → 算 offset → 编码）、`x64_encoder.h`（机器码编码与 prologue/call 序列）。
- **Debug 打印 MIR**：在 `optimizeMirBuffer` 之后、`encodeMirBuffer` 之前调用 `x64::mirPrint(mirBuf, std::cerr, &pcToOffset)` 即可在控制台看到带偏移与 pc 标注的 MIR 序列；或使用 `std::asm` 时自动输出 `; --- MIR ---` 段。
- 架构总览：`docs/technical/07_jit_architecture.md`。
- 字节码→汇编注解：`docs/jit/bytecode-to-asm.md`。
