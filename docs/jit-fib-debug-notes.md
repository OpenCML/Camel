# JIT fib 错误与 Release 卡死 — 排查笔记

## 现象
- **Debug**：能进 JIT，但结果错误（如 fib(10)=-37）；trace 中多次出现传参为负数（-11, -13 等）和 `rax=0xcdcdcdcd`（未初始化）。
- **Release**：卡死后退出（无 debug_trace，可能是栈溢出或死循环）。

## 已排除
- **debug_trace**：改用 `jitDebugTraceWrapper` + thread_local 后，保存区不再被 stub 覆盖，问题依旧 → 说明不是 trace 覆盖导致。

## 可能原因（按优先级）

### 1. 调用前后 rdi（frame）被破坏 — ✅ 已修复
**根因**：trampoline 内部调用 `fn(callee_slots)` 时按 ABI 会设置 **rdi = callee_slots**；`call` 仅压返回地址，**不会**自动保存/恢复 rdi。返回后 caller 若继续用 rdi 访问 frame，会错误地使用已释放的 callee frame。

**修复**：在 trampoline 调用前 `push rdi`，返回后 `pop rdi`（MIR 指令 `PushRdi`/`PopRdi`）。详见 [07_jit_architecture.md](technical/07_jit_architecture.md) 2.1.1、2.1.2。

### 2. 0xcdcdcdcd：某槽未写就读（JOIN / 分支结果）
trace 里 pc=41 等处 `rax=0xcdcdcdcd` 说明某次用的是未初始化内存。JOIN 会按 `idxSlot` 选 w0 或 w1 槽；若某一分支没把返回值写到 JOIN 期望的槽，就会读到未初始化或错误槽。

**建议**：对 fib 打开字节码 dump，确认：
- 每个 LSUB 的 `result` 与后面 FUNC 的 `operands()[0]` 是否一致（n-1 写哪槽、fib(n-1) 读哪槽；n-2 写哪槽、fib(n-2) 读哪槽）。
- JOIN 的 `wargs()[0]`/`wargs()[1]` 与两分支的写槽是否一致。

### 3. BRCH 目标 t0/t1
当前 JIT 用 `t0 = pc + bc.opsize`、`t1 = pc + bc.opsize + 1`，与解释器 `pc += bc->opsize + jumpIdx` 一致。若某处 opsize 或布局与解释器不一致，会跳到错误块，导致“走错分支”或“读到未写槽”。

**建议**：对 fib 打印每条 BRCH 的 pc、opsize、t0、t1，并与字节码布局对照。

### 4. Release 卡死
- 错误递归（传错 n）→ 深度暴增 → 栈溢出 → 卡死/退出。
- 或条件/跳转错 → 同一路径无限循环。

**建议**：先在同一逻辑下用 Debug 确认“传参/槽位/分支”都正确，再关 trace 看 Release 是否仍卡死；若仍卡死，再查是否有“仅 Release 下”的优化或代码路径差异。

## 建议的下一步
1. 在 trampoline 中加“fib 首参 < 0 时打 log + 打 caller 前几槽”。
2. 对 fib 图做一次字节码 dump（含 result、operands、wargs），核对 LSUB result 与 FUNC operands、JOIN wargs 与各分支写槽一致。
3. 若方便：贴一段“从第一次错误传参（如 -11）往前”的 trace（含 pc、rax、rdi 和关键槽位），便于对到具体字节码与生成指令。

## 后续修复

### slot[0] = Frame* 规范
- 动态区 slot[0] 恒存当前 Frame\*，slot[1..] 为数据槽。
- trampolineFunc/TrampolineTail 用 `callerFrame->get(operand)` 取参，支持 operand 为负（静态区索引）。
- trampolineOper 从 slots[0] 取 Frame，用 FrameArgsView 调用算子，无需传 Graph。
- 详见 [07_jit_architecture.md](technical/07_jit_architecture.md) 第 2.2 节。

### rdi 跨 trampoline 调用保存
- rdi 为 caller-saved；trampoline 内部调用 fn(callee_slots) 会覆盖 rdi。
- FUNC/TAIL/OPER 的 trampoline 调用两侧插入 `push rdi` / `pop rdi`。
- 完整 ABI（caller/callee-saved、jmp/call 不自动保存等）见 [07_jit_architecture.md](technical/07_jit_architecture.md) 2.1.1。
