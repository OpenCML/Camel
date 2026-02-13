# JIT fib(4) 错误排查笔记

## 根因总结（已修复）

### 现象与条件

- **现象**：fib(4) 得到 5，正确应为 3。
- **条件**：仅在 RegAlloc 报告「5 slots in reg, 4 spilled」时出现，即**存在 spill 时必现**。

### 真实根因：cmovnz/cmove 的 ModRM 操作数顺序写反

- **JOIN 语义**：`result = (idx != 0) ? w1 : w0`。JIT 实现为：rax=w0，rcx=w1，从 frame 加载 idx 并 test，然后 **cmovnz result_reg, w1_reg**（idx≠0 时把 w1 写入结果），最后存 result。因此应发射 **cmovnz rax, rcx**（idx≠0 时 rax←rcx，再 `mov [rdi+32], rax`）。
- **编码器错误**：`x64_encoder.h` 中 `cmovnzRegFromReg(dst, src)` 与 `cmoveRegFromReg(dst, src)` 的 ModRM 字节使用了 `(src<<3)|dst`。x64 的 `CMOVcc r64, r/m64` 规定 **ModRM.reg = 目标，ModRM.r/m = 源**，因此正确应为 `(dst<<3)|src`。原公式等价于 reg=src、r/m=dst，即编出了 **cmovnz rcx, rax**（更新的是 rcx，不是 rax）。
- **运行时效果**：idx=1 时 ZF=0，执行的是「rcx := rax」（把 w0 拷进 rcx），rax 仍为 w0；随后 `mov [rdi+32], rax` 存的仍是 w0，故得到 2 而非 1。整个 fib(2) 的 JOIN 因此错误返回 2，进而导致 fib(4)=5。
- **修复**：将两处 ModRM 改为 `(dst<<3)|src`，使发射的指令为 **cmov cc dst, src**，JOIN 正确实现「idx≠0 时 result←w1」。

### 排查过程中被排除的方向

- **trace 恢复 rdi**：log 中 [JOIN-dbg] 显示 trace 内 rdi 与 [rdi+24] 均正确，曾怀疑 trace 返回后 rdi 被破坏；改为「call 返回后先恢复 rdi」未解决问题，且 binary 中恢复顺序正确，故排除。
- **VTest 未从 frame 读 idx**：已通过「m.disp!=0 时一律用 rbx+[rdi+disp]」保证 idx 从 frame 加载；binary 中也为 push rbx; mov rbx,[rdi+24]; test rbx; pop rbx，故排除。
- **trampoline 未保存 rdi**：若未保存，返回后 rdi 应为 callee frame，与 [JOIN-dbg] 中 rdi 与 frame 一致矛盾；且修复 cmov 后问题消失，故可确认根因在 cmov 编码。

### 小结

| 项目 | 说明 |
|------|------|
| **根因** | `cmovnzRegFromReg` / `cmoveRegFromReg` 的 ModRM 使用了 (src<<3)\|dst，导致发射 **cmov cc src, dst** 而非 **cmov cc dst, src**。 |
| **表现** | JOIN 在 idx≠0 时本应 result=w1，实际 result 仍为 w0（rax 未被更新），故 fib(2) 得 2、fib(4) 得 5。 |
| **修复** | ModRM 改为 (dst<<3)\|src。 |
| **文件** | `src/builtin/passes/sched/linear/fastvm/jit/backend/x64/x64_encoder.h`。 |

---

## fib(4) 执行流摘要（从 Debug trace 提取）

### 语义约定

- **BRCH**：将分支索引（0=then, 1=else）写入 `bc.result`（如 slot 3）。
- **JOIN**：`result = (idx != 0) ? w1 : w0`，idx 来自 BRCH 写入的槽，w0/w1 来自两分支结果槽。
- fib：`fib(n) = n<=1 ? n : fib(n-1)+fib(n-2)`；then 分支结果即 n（在 slot 1），else 分支结果在 slot 9。

### 调用树与出错点

```
main → fib(4)
  → BRCH idx=1 (else)
  → fib(3) → BRCH idx=1 → fib(2)
      → BRCH idx=1
      → fib(1)=1, fib(0)=0
      → JOIN: idx=1, w1=1 → 应为 1，实际 2 ❌（首次 JIT JOIN）
  → fib(2) 错误返回 2，导致后续 fib(3)=3、fib(4)=5
```

唯一错误发生在 **fib(2) 的 JIT JOIN（pc=41）**：当时 frame 为 [3]=1（idx）、[9]=1（w1），应取 w1=1，实际得到 2（相当于取了 w0）。

---

## 汇编分析（std::inline std::asm 生成）

### fib 中 pc=41（JOIN）对应汇编

以 `fib<FN1iRi>` 为例（与 __root__ 中 fib 块一致）：

```asm
  debug_trace pc=41
  mov rax, [rdi+8]    ; rax = w0  ← slot 1 (disp 8)
  mov rcx, [rdi+72]   ; rcx = w1  ← slot 9 (disp 72)
  mov rdx, [rdi+24]   ; rdx = idx ← slot 3 (disp 24)
  test rdx, rdx
  cmovnz rax, rcx     ; idx!=0 → result=w1，否则 result=w0
  mov [rdi+32], rax   ; result → slot 4 (disp 32)
```

### 结论（无 spill 时）

- **槽位与语义一致**：w0 来自 slot 1（then 分支即参数 n），w1 来自 slot 9（else 分支 fib(n-1)+fib(n-2)），idx 来自 slot 3，结果写 slot 4。与 08_fastvm_bytecode_semantics 一致。
- **指令逻辑正确**：`test rdx,rdx` + `cmovnz rax,rcx` 实现 `result = idx ? w1 : w0`。
- 因此，**无 spill 时**（如 std::inline std::asm 全从 frame 加载）JOIN 的槽位与分支选择应是对的。

### 与 trace 的对应

- fib(2) 出错时：slot 1=2（n），slot 3=1（idx），slot 9=1（w1）。正确应为 result=1；得到 2 说明实际走了「取 w0」分支，即 **idx 在 test 时被当成 0**。
- 故在 **有 spill 的 JIT 路径** 上，问题集中在：**idx 对应的 vreg（v2）被 spill 时，在 JOIN 的 VTest 前没有从 frame 正确加载**，导致 test 作用在 0 或未初始化值上，ZF=1，cmovnz 不执行，结果错误地保留 w0（slot 1 = n = 2）。

---

## Binary 分析（JIT 实际生成的机器码）

从 `fib<FN1iRi>` 的 hex dump（offset 1333–1490，pc=41 段）可还原 JOIN 序列：

| 偏移 (hex) | 机器码 (片段) | 指令 |
|------------|----------------|------|
| 1333       | …              | debug_trace pc=41 |
| 1471       | 48 8b 47 08    | mov rax, [rdi+8]   → w0 (slot 1) |
| 1475       | 48 8b 4f 48    | mov rcx, [rdi+72]  → w1 (slot 9) |
| 1479       | 48 8b 57 18    | mov rdx, [rdi+24]  → idx (slot 3) |
| 1483       | 48 85 d2       | test rdx, rdx |
| 1486       | 48 0f 45 c8    | cmovnz rax, rcx |
| 1490       | 48 89 47 20    | mov [rdi+32], rax  → result (slot 4) |

- 该 binary 中 **JOIN 的三个操作数均从 frame 显式加载**（w0、w1、idx 都有对应的 `mov`），与无 spill 时的预期一致。
- 若此 binary 即「4 spilled」时运行的版本，则说明在 spill 配置下编码器仍对 JOIN 的 v0/v1/v2 都发了 frame load；此时若仍出现 result=2，需考虑 **slot 3 在 BRCH 与 JOIN 之间被覆盖** 或 **运行时代码路径与 dump 不一致**（例如 OnDemand 多次编译、dump 与执行非同一镜像）。
- 若「4 spilled」时实际执行的是**另一套代码**（例如 idx 被 spill 时不再发射 `mov rdx,[rdi+24]`，而改为 `push rbx; mov rbx,[rdi+24]; test rbx; pop rbx`），则 bug 更可能出在 **spill 路径**：要么未发射 idx 的 load+test（见下节），要么 disp/寄存器用错。

---

## 编码器逻辑与推测原因（mir_encode.cpp）

JOIN 的 MIR 顺序为：`VLoadFromFrame(v0), VLoadFromFrame(v1), VLoadFromFrame(v2), VCopy(v3,v0), VTest(v2), VCmovnz(v3,v1), VStoreToFrame(dr,v3)`。编码器对 spill 的处理如下。

### 1. 只维护一个「待加载」spill 槽（spilledLoadVReg / spilledLoadDisp）

- `VLoadFromFrame`：若 vreg 在 reg 则发射 load 并 **清空** `spilledLoadVReg`；若 vreg 被 spill 则**不发射**，只设 `spilledLoadVReg = 该 vreg`、`spilledLoadDisp = disp`。
- 因此若 v0 被 spill、v1 在 reg、v2 被 spill，顺序为：v0 设 spilledLoad=(v0,8)；v1 发射 load 并**清空** spilledLoad；v2 设 spilledLoad=(v2,24)。到 `VTest(v2)` 时仍为 (v2,24)，可走 spill 路径用 rbx 从 disp 24 加载并 test。
- **风险点**：若中间出现**其他** `VLoadFromFrame`（例如同一 basic block 内、或 MIR 顺序与预期不符），会再次清空或覆盖 spilledLoad，导致对 v2 的延迟加载从未发生，VTest 作用在未初始化寄存器上（多为 0）→ ZF=1 → 错误地保留 w0。

### 2. VCopy(v3, v0) 在源 v0 被 spill 时未物化

- 当前实现只处理：`dr>=0 && sr>=0`（正常 mov），或 `dr<0`（BRCH 结果 spill 的写回路径）。
- **未处理** `dr>=0 && sr<0`（目标在 reg、源被 spill）。此时**不发射任何指令**，v3 所在物理寄存器（如 rax）保持**上一段代码的残留值**。
- 若 v3 分配在 rax，且上一段是 pc=40 的 add（rax = w1 = 1），则残留为 1；若之后有 `debug_trace` 等会保存/恢复寄存器的调用，rax 可能被恢复成**错误值**（如 2）。于是：当 **idx 被错误地 test 成 0**（见上）时，cmovnz 不执行，result = 当前 rax；若该值被错误恢复为 2，则得到 result=2。

### 3. 推测原因归纳

| 假设 | 条件 | 后果 |
|------|------|------|
| **A. idx 未从 frame 加载** | v2 被 spill，且 VTest 前 spilledLoad 被清空或未走 spill 路径 | test 作用在未初始化/0 的寄存器 → ZF=1 → 取 w0；若此时 w0 也未正确加载到 v3（见 B），v3 为残留 2 → result=2。 |
| **B. w0 未物化到 v3** | v0 被 spill，VCopy(v3,v0) 不发射 load | v3（如 rax）保持旧值；若 idx 被错误当成 0，result = 该旧值；若 debug_trace 等把 rax 恢复成 2，则 result=2。 |

**结论**：result=2 需要同时满足「test 认为 idx=0」和「v3 中用于 w0 的值恰好为 2」。因此最可能的是 **假设 A**（idx 在 spill 时未正确加载并 test），在部分运行中叠加 **假设 B**（v0 未物化导致 v3 为残留值）或 **debug_trace 对 rax 的错误恢复**。

### 4. 建议修改方向

- **VTest 前保证 idx 被加载**：若 v2 被 spill，在 VTest(v2) 时若 `m.r0 == spilledLoadVReg` 必须从 `spilledLoadDisp` 发射 load+test（当前 rbx 路径）；并确认 JOIN 块内、VLoadFromFrame(v2) 与 VTest(v2) 之间**没有**会清空 spilledLoadVReg 的 VLoadFromFrame。
- **VCopy 在源 spill 时物化**：当 `sr<0`（源 vreg 被 spill）且 `dr>=0`（目标在 reg）时，应先从 frame 加载源到临时或目标 reg（例如用 spilledLoadDisp 若上一拍正是该 vreg，或为该 vreg 单独记 disp），再写入 v3，避免 v3 使用未定义的残留值。
- **核对 binary 与运行一致**：确认「4 spilled」时实际执行的 JOIN 段是否与上述 hex 一致；若不一致，以实际执行序列为准再对 disp/寄存器与 spill 路径做一次核对。

---

## 执行与 dump 的一致性检查

以下说明**实际执行的 JIT 代码**与 **bindump/asm 输出的代码**在何种条件下一致，以及如何验证。

### 代码路径

| 场景 | bytecodes 来源 | 编译入口 | 结果 |
|------|-----------------|----------|------|
| **执行** | `FastVMSchedPass::precompile(graph)` 里 `compileAndLink(context_, graph, opts)` → `bytecodes_` | `compileAndCacheGraph` → `jitBackend_->compile(unit)`，`unit.bytecodes = bytecodes_`，`unit.entryPc` = 该图入口 | 生成 code → `load()` 拷到可执行页 → `jitCache_[graph]`，执行时直接调该指针 |
| **Dump** | `JitBinaryDumpPass::apply` 里 `compileAndLink(context_, graph.get(), opts)` → 局部 `bytecodes` | 新建 `backend`，对 offsetMap 中每个 `(g, entryPc)` 调 `backend->compile(unit)`，`unit.bytecodes = bcSpan`（同一 compileAndLink 的 bytecodes） | 生成 code → 不 load，直接用于 hex/asm 输出 |

- 两边用的都是 **同一套** `compileAndLink` 选项（`enableTailCallDetection`、`enableInlineOperators`、`OptimizationStrategyCode::All`）。
- 若传入的 **graph 相同**（同一 pipeline 下同一棵图），则 **bytecodes 内容相同**；`unit.entryPc` 对同一图也相同。
- **寄存器分配**：`linearScanAllocate` 仅依赖 bytecodes 与 entryPc，迭代与排序均确定，无随机或全局状态 → **相同输入得到相同 slotToReg**。
- 因此：**同一 graph、同一 compileAndLink 选项下，执行时编译出的机器码与 bindump 编译出的机器码应逐字节一致**；你看到的 dump 即实际执行的那份。

### 如何验证

1. **运行时 dump 实际执行的 code**（已实现）：设置环境变量 `CAMEL_JIT_DUMP_EXECUTED=1` 后运行程序（如 `camel fib.cml`），首次 JIT 编译某图时会把**实际执行**的机器码写入 `jit_executed_<graphname>.bin`。再用 bindump 生成同图的 code（或从 bindump 的 hex 还原为 bin），对两者做二进制比较即可确认执行与 dump 是否一致。
2. **日志 hash**：在编译路径（如 `X64Backend::compileBytecode` 末尾）对 `code` 做一次 hash 并打日志；执行与 dump 各打一次，对比 hash 是否相同。

若验证结果一致，则「实际执行的应该和 dump 的一致」成立，可放心用 dump 中的 JOIN 序列分析 bug；若不一致，再检查 pipeline 是否导致 graph 或 opts 不同、或是否存在多次编译/多 backend 实例等。

---

## 根因与修复（2026-02-12）

### 根因

编码器只维护**一个**「待延迟加载」的 spill 槽（`spilledLoadVReg` / `spilledLoadDisp`）。JOIN 顺序为：

`VLoadFromFrame(v0), VLoadFromFrame(v1), VLoadFromFrame(v2), VCopy(v3,v0), VTest(v2), ...`

- 当 **v1 也被 spill** 时：处理 v1 会把 `spilledLoad` 从 (v0,8) 覆盖为 (v1,72)，并把 (v0,8) 存到 spilledCopy；处理 v2 再把 spilledLoad 改为 (v2,24)，spilledCopy 改为 (v1,72)。到 VTest(v2) 时 `spilledLoadVReg == v2` 仍成立，本应能发射 load+test。
- 真正出错的情况是：**v0、v1 在 reg，v2 被 spill** 时，若 VCopy(v3,v0) 或中间逻辑**清空了** `spilledLoadVReg`，或 RegAlloc 导致 v2 与 v3 共用同一物理寄存器、VCopy 先写 v3 覆盖了 v2 所在 reg，则到 VTest 时要么 `spilledLoadVReg != v2`，要么 v2 所在 reg 已被覆盖。
- 编码器中原逻辑：**仅当** `m.r0 == spilledLoadVReg` 时才从 `spilledLoadDisp` 发射 load+test；否则**不发射任何指令**，ZF 未定义，cmovnz 可能选错分支 → result=2。

因此根因是：**VTest 在 vreg 被 spill 时，只依赖“上一个 spill 是否恰好是该 vreg”；若不是（被覆盖或未设置），就不发射 load+test，导致 test 作用在错误/未初始化值上。**

### 修复

1. **VTest 支持 MIR 传入 disp**  
   - `mir_builder.h`：`emitVTest(VRegId vreg, int32_t frameDisp = 0)`，将 `frameDisp` 写入 `m.disp`。  
   - JOIN 在 backend 中调用 `build.emitVTest(v2, dIdx)`，保证 idx 的 frame 偏移在 MIR 中可用。

2. **编码器 VTest 的 spill 路径**（`mir_encode.cpp`）  
   - vreg 被 spill 时（`r < 0`）：  
     - 若 `m.r0 == spilledLoadVReg`，用 `spilledLoadDisp`；  
     - 否则若 `m.disp != 0`（JOIN 传入的 dIdx），用 `m.disp`。  
   - 只要二者之一有效就发射 `push rbx; mov rbx,[rdi+disp]; test rbx; pop rbx`，不再依赖“仅一个”spilledLoad 未被覆盖。

3. **VCopy 源 spill 时的物化**（此前已做）  
   - 用 `spilledCopyVReg` / `spilledCopyDisp` 在 VCopy(v3,v0) 时从 frame 加载 w0 到 v3，避免 v3 沿用残留值。

效果：**有 spill 时 JOIN 的 idx 总能从正确的 frame disp 加载并 test**，即使 spilledLoad 被后续 spill 覆盖，也可用 MIR 的 `m.disp`（dIdx）正确生成 load+test。

### 补充修复（仍错时）：JOIN 的 VTest 始终用 m.disp 路径

- **现象**：即使加上上述修复，fib(4) 仍得 5；binary 中 JOIN 为 `mov rax,[rdi+8]; mov rcx,[rdi+72]; mov rdx,[rdi+24]; test rdx,rdx; cmovnz...`，即 v2 在寄存器、未走 spill 路径。
- **原因**：VCopy(v3,v0) 若把 v3 与 v2 分到同一物理寄存器（如 rdx），会先执行 `mov rdx,rax`，在 VTest 前覆盖 idx；或 trace 恢复的 rdi 异常时，前面已发射的 `mov rdx,[rdi+24]` 读到错误 frame。
- **修复**：当 `m.disp != 0`（JOIN 传入的 dIdx）时，VTest **一律**从 frame 用 `m.disp` 做 load+test（push rbx; mov rbx,[rdi+disp]; test rbx; pop rbx），不再用 v2 的物理寄存器。这样 idx 在 test 前从当前 rdi 重新读一次，不依赖 v2 的 reg 是否被 VCopy 覆盖。

### 若仍错：trace 后 rdi 被破坏，从备份恢复

- **现象**：binary 已含 rbx 路径（push rbx; mov rbx,[rdi+24]; test rbx; pop rbx），但 JOIN 仍得 2，说明运行时 [rdi+24] 被读成 0，即 **rdi 在 debug_trace 返回后错误**（例如 callee 写 shadow/栈覆盖了 [rsp+64] 的 rdi 保存槽）。
- **修复**：在 `emitDebugTraceCall` 中把 rdi 再存一份到 [rsp+128]（分配区末尾），恢复时从 **备份** [rsp+128] 取 rdi，不再从 [rsp+64] 取，避免被 callee 覆盖。

### 根因：trampoline 调用未保存/恢复 rdi（真正方向）

- **现象**：即使 trace 用 [rsp+128] 备份恢复 rdi，fib(4) 仍得 5；trace 显示 pc=35/36 时 rdi 正确，说明问题不在 trace。
- **原因**：JIT 调用 trampoline（递归 fib）时，**没有在 call 前后 push/pop rdi**。trampoline 和 callee 会把 rdi 设为 callee 的 frame；返回后 rdi 仍是 callee 的 frame，**不是** caller 的。随后执行 JOIN 时 [rdi+24] 等读的是 **callee** 的 slot（多为 0 或未初始化），test 得 ZF=1，错误地取 w0，且写回 [rdi+32] 也是写到已释放的 callee frame。
- **修复**：在 `callTrampolineWin64` 与 `callTrampolineOperWin64` 中，在设置参数前 **push rdi**、在 callRax() 后 **pop rdi**，保证从 trampoline 返回后 rdi 仍是 caller 的 slot 基址，JOIN 等从正确 frame 读。

---

## 执行流解析（从终端 log 逐段）

以下按你提供的终端输出（约 1331–2482 行）梳理 **fib(4) 的 JIT 执行顺序**，并标出出错点。

### 1. 入口与第一次 fib(2)

- **FUNC fib(4)**：main 调 fib(4)，分配 frame 0xC765'6070，slot 5 写入 4，然后 **Calling JIT function of graph \<fib\> with args: 4**。
- **JIT 内部**：pc=26→27→30→31，然后 **trampolineFunc ENTER pc=31**（第一次递归 fib(3)）。
- trampoline 分配 callee frame 0xC765'61D0，从 caller 的 slot 5 拷 3 到 callee slot 1，**about to call JIT entry**。
- 再次进入 JIT，pc=26→27→30→31，又一次 **trampolineFunc ENTER pc=31**（fib(2)）。
- 又分配 0xC765'6248，slot 1=2，**about to call JIT entry**。
- JIT 跑 pc=26→27→29→43（基例 n<=1 直接返回），**trampolineFunc JIT->JIT return result=1**，释放 0xC765'6248。
- 回到 **caller 0xC765'61D0**，trace **pc=35**：rdi=0x175c76561f8，frame [1]=2,[2]=false,[3]=1,[5]=1,[6]=1,[7]=cdcd…（即 idx=1，w0 在 slot1=2，w1 在 slot9 尚未写）。
- 接着 **pc=36**：rdi 仍 0x175c76561f8，[6]=1,[7]=0（第二次递归 fib(0) 的 slot 7=0 已就绪）。
- **trampolineFunc ENTER pc=36**，复用 0xC765'6248，slot 1=0，**about to call JIT entry**。
- JIT 跑 pc=26→27→29→43，**return result=0**，释放 0xC765'6248。
- 回到 **caller 0xC765'61D0**，trace **pc=40**：rdi=0x175c76561f8，[6]=1,[7]=0,[8]=0。
- **pc=41**：rdi=0x175c76561f8，**[3]=1, [5]=1, [6]=1, [7]=0, [8]=0, [9]=1** → idx=1，w0=2（slot1），w1=1（slot9）。JOIN 应取 w1=1。
- **pc=43**：**[4]=2**，rax=2 → **错误**：实际取了 w0=2。

结论：**在 fib(2) 的 JOIN（pc=41→43）**，trace 显示 rdi 和 frame 都对（[3]=1），但 **运行时 result=2**，说明 **执行 JOIN 的 test 时 [rdi+24] 被当成 0**（ZF=1），或 rdi 在 trace 返回后到 JOIN 之间被改过。

### 2. 可能原因归纳

| 假设 | 说明 |
|------|------|
| **A. trace 恢复的 rdi 被覆盖** | 虽从 [rsp+128] 恢复，若 callee 写栈越界或 ABI 差异，仍可能破坏 [rsp+128]。曾尝试扩栈到 256、备份 [rsp+248]，会导致执行到 trace 时崩溃，已回退为 136 + [rsp+128]。 |
| **B. JOIN 的 test 未走 frame 路径** | 若 VTest 未用 m.disp（即未用 push rbx; mov rbx,[rdi+24]; test rbx; pop rbx），而用 v2 的物理寄存器，且该寄存器被前面 VCopy(v3,v0) 覆盖，则 test 到 0。已通过「m.disp!=0 一律用 rbx+disp」修复，需确认 binary 中 pc=41 后确为该序列。 |
| **C. Win64 call 前 rsp 未 16 字节对齐** | 若 trace 或 trampoline 的 call 前 rsp≡8(mod 16)，callee 可能用 movaps 等破坏栈上保存区。当前：JIT 入口 rsp≡8；trace 内 sub rsp,136 后 rsp≡0；trampoline push+sub 32 后 rsp≡0。理论上已对齐，可再核对。 |

### 3. 根因：cmovnz/cmove 的 ModRM 操作数顺序反了

- **x64_encoder.h** 中 `cmovnzRegFromReg(dst, src)` 与 `cmoveRegFromReg(dst, src)` 的 ModRM 使用了 `(src<<3)|dst`，即编码成 **cmov cc src, dst**（更新的是 src 寄存器），而语义应为 **cmov cc dst, src**（ZF 满足时 dst:=src）。
- JOIN 序列为：rax=w0，rcx=w1，test idx，**cmovnz result_reg, w1_reg**，存 result。正确应为 idx≠0 时把 w1 写入 result（即 result:=rcx），故应发射 **cmovnz rax, rcx**。此前发射成 **cmovnz rcx, rax**，导致 idx≠0 时把 w0 写进 rcx，最后存的仍是 rax（w0）→ 得到 2 而非 1。
- **修复**：ModRM 改为 `(dst<<3)|src`，使 reg=dst、r/m=src，即 cmov cc dst, src。

### Build 模式崩溃（trace 后无法正常退出）

- **现象**：Debug 构建下 fib(3) 正常结束并输出结果；Build（Release）下执行到最后一轮 trace pc=43 后崩溃、无法正常退出。
- **原因（分 ABI）**：
  - **Win64（含 Clang on Windows）**：调用 trampoline 时仅 `push rdi` 未预留 **32 字节 shadow space**。Windows x64 ABI 要求 caller 在 call 前 `sub rsp, 32`；callee（C++ trampoline）会使用 [rsp+0..31]，从而覆盖栈上保存的 rdi，返回后 `pop rdi` 恢复错误 → 后续 JIT 用错误 rdi 访问 frame 崩溃。
  - **SysV**：`callTrampolineSysV` / `callTrampolineOperSysV` 未在调用前后保存/恢复 rdi（rdi 为 caller-saved），trampoline 返回后 rdi 被覆盖，同样导致崩溃。
- **修复**：在 `x64_encoder.h` 中：**Win64** 在 `callTrampolineWin64` / `callTrampolineOperWin64` 内于 push rdi 后增加 `sub rsp, 32`、call 后 `add rsp, 32` 再 pop rdi；**SysV** 在 `callTrampolineSysV` / `callTrampolineOperSysV` 内增加 call 前 `pushRdi()`、call 后 `popRdi()`。

---

## 排查过程中曾考虑的假设（已排除，仅供参考）

| 假设 | 含义 |
|------|------|
| 执行 JOIN 时 rdi 已错 | trace 返回后到 test 之间 rdi 被改写，[rdi+24] 读到错误 frame → ZF=1 → 取 w0。 |
| [rdi+24] 被当成 0 | slot 3 被写坏或 test 用了被 VCopy 覆盖的 v2 的 reg → ZF=1 → 取 w0。 |
| trace 恢复的 rdi 被 callee 写坏 | [rsp+128] 在调用链中被覆盖，恢复到的不是 caller 的 rdi。 |

---

## 参考

- JOIN/BRCH 语义：[08_fastvm_bytecode_semantics.md](technical/08_fastvm_bytecode_semantics.md)
- JIT 架构与 ABI：[07_jit_architecture.md](technical/07_jit_architecture.md)
