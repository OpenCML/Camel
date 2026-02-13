# Build 模式 fib 崩溃 — 最终综合修复方案

本文档汇总讨论结论，供回退近期修改后按此方案一次性重新施加修复。

---

## 1. 背景与现象

- **Debug 构建**：fib 等正常，有 trace，结果正确。
- **Build（Release/NDEBUG）**：执行到 JIT 相关路径时崩溃，Exception 0xc0000005（DEP），或 NEXT() 跳转到无意义代码（如 std::cerr 附近）。

---

## 2. 根因总结

### 2.1 JIT 内部 DEP（已单独修复，需保留）

- **原因**：JIT 内 `call jitDebugTraceWrapper` 时，Win64 下 callee 使用 shadow 写坏返回地址。
- **修复**：Build 下 `debugTraceFn = nullptr`，encoder 对 DebugTrace 块发射 16 字节 NOP 替代 lea+mov+call，不执行 call。  
- **位置**：`x64_backend.cpp` 传 nullptr；`x64_encoder.h` 中 `emitDebugTraceCall` 当 `fnAddr == nullptr` 时 16 字节 NOP。

### 2.2 gotovm 中「JIT 返回后」崩溃 / NEXT 跳错

- **原因 1（shadow 复用）**：Release 下编译器在「先 call fn，再 call 其他（release/set/NEXT 内逻辑）」路径上，仍按「未插入 call fn」复用 shadow，导致后续某次 call 的 callee 写坏 [rsp]，返回时 DEP。
- **原因 2（pc/bc 被提前破坏）**：在 Build 优化下，**在尚未调用 JIT 之前**，`pc`（以及 `bc`）可能只存在于寄存器中；`getFuncExtraGraph(bc)`、`framePool_.acquire()`、`currFrame->get()` 等调用会破坏这些寄存器。因此执行到 472–479 行附近时，`pc` 已经变成错误的大数，之后再「保存/恢复 pc、bc」已无意义。
- **结论**：需要 (1) 通过独立 TU 的 `jit_call_wrapper` 调用 JIT，避免编译器对「call fn 后」栈的错误假设；(2) 在**进入「要调 JIT」的分支后、在任何可能破坏 pc/bc 的调用之前**，就把 pc、bc 存到 thread_local，JIT 返回后再从 thread_local 恢复。

---

## 3. 最终综合修复方案（仅 Clang Win64）

以下仅针对 **Clang + Win64**（`(defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)`）。其他平台保持「直接 `result = fn(slots, ctx)`」。

### 3.1 组件概览

| 组件 | 作用 |
|------|------|
| `jit_call_wrapper` | 在**独立 TU**（trampoline.cpp）中实现，`extern "C"` + `noinline`；前后各一条 `asm volatile("" ::: "memory");`，中间 `return fn(slots, ctx);`。避免编译器在 gotovm 内对「call fn 后」栈/寄存器做错误优化。 |
| `s_jit_pc` / `s_jit_bc` | `thread_local` 保存 pc、bc，不被 JIT 或中间调用破坏。 |
| `JIT_SAVE_PC_BC()` | 将当前 `pc`、`bc` 写入 thread_local。必须在**分支内第一句**执行，早于任何 `getFuncExtraGraph`、`framePool_.acquire` 等调用。 |
| `JIT_RESTORE_PC_BC()` | JIT 返回后从 thread_local 恢复 `pc`、`bc`，再执行 release/set/NEXT。 |
| `JIT_CALL_ASM(result, fn, slots, ctx)` | 展开为 `(result) = jit_call_wrapper((fn), (slots), (ctx));`。 |

### 3.2 gotovm.cpp 修改

**位置**：`#elif (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)` 分支（约 40–53 行）。

**内容**：

```cpp
#elif (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
/* Clang Win64: wrapper in trampoline.cpp. pc/bc in thread_local so JIT cannot overwrite stack. */
__attribute__((noinline)) extern "C" slot_t jit_call_wrapper(
    slot_t (*fn)(slot_t *slots, void *ctx), slot_t *slots, void *ctx);
static thread_local size_t s_jit_pc;
static thread_local const Bytecode *s_jit_bc;
#define JIT_CALL_ASM(result, fn, slots, ctx) ((result) = jit_call_wrapper((fn), (slots), (ctx)))
#define JIT_SAVE_PC_BC() do { s_jit_pc = pc; s_jit_bc = bc; } while (0)
#define JIT_RESTORE_PC_BC() do { pc = s_jit_pc; bc = const_cast<const Bytecode *>(s_jit_bc); } while (0)
#else
#define JIT_CALL_ASM(result, fn, slots, ctx) ((result) = (fn)((slots), (ctx)))
#define JIT_SAVE_PC_BC() ((void)0)
#define JIT_RESTORE_PC_BC() ((void)0)
#endif
```

**两处 JIT 调用点的统一模式**：

1. **直接 JIT 路径**（`label_FUNC` 内 `if (bc->fastop[1] == 0)` 块）  
   - 该 `if` 块内**第一句**：`JIT_SAVE_PC_BC();`  
   - 然后：`Graph *targetGraph = getFuncExtraGraph(bc);` … 直至 `funcFrame->slotBase()[0] = ...;`  
   - 接着：`slot_t result;`  
   - `JIT_CALL_ASM(result, fn, funcFrame->slotBase(), currentJitCtx_);`  
   - `JIT_RESTORE_PC_BC();`  
   - 然后：`l.in(...).debug(...); framePool_.release(funcFrame); currFrame->set(bc->result, result); NEXT();`

2. **tiered JIT 路径**（`else` 里 `if (tierPolicy_.shouldJit(count))` → `bc = &bytecodes_[pc];` → `if (bc->fastop[1] == 0)` 块）  
   - 该内层 `if (bc->fastop[1] == 0)` 块内**第一句**：`JIT_SAVE_PC_BC();`  
   - 然后：`Graph *g = getFuncExtraGraph(bc);` … 直至 `funcFrame->slotBase()[0] = ...;`  
   - 接着：`slot_t result;`  
   - `JIT_CALL_ASM(result, fn, funcFrame->slotBase(), currentJitCtx_);`  
   - `JIT_RESTORE_PC_BC();`  
   - 然后：`framePool_.release(funcFrame); currFrame->set(bc->result, result); NEXT();`

**要点**：`JIT_SAVE_PC_BC()` 必须在**任何**可能破坏 `pc`/`bc` 的调用（如 `getFuncExtraGraph`、`framePool_.acquire`、`currFrame->get`）**之前**执行，否则 Build 下 pc 已被破坏再保存无意义。

### 3.3 trampoline.cpp 修改

**位置**：在 `jitDebugTraceNoOp` 之后、`#if defined(__GNUC__) || defined(__clang__)`（jitDebugTrace naked）之前。

**条件**：`#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)`

**内容**：

```cpp
#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
__attribute__((noinline)) extern "C" slot_t jit_call_wrapper(
    slot_t (*fn)(slot_t *slots, void *ctx), slot_t *slots, void *ctx) {
    asm volatile("" ::: "memory");
    slot_t ret = fn(slots, ctx);
    asm volatile("" ::: "memory");
    return ret;
}
#endif
```

---

## 4. 不采用的尝试（回退时可删除）

以下曾尝试但**不纳入最终方案**，回退时可直接删或还原：

- **jit_call_trampoline.s**：独立 asm 用「sub rsp,32; call fn; add rsp,40; jmp return_addr」消除返回地址。因 Clang 下 `&&label` 与 computed goto 冲突（跳转绕过变量初始化）未使用；若 CMake 中已加入该 .s，可移除。
- **trampoline.h 中 `jit_call_trampoline` 声明**：与上述 .s 配套，若不链接 .s 可删除该声明。
- **内联 asm goto**：GCC 可用，Clang 下操作数/标签替换异常，且 asm goto 不能带输出操作数，未在 Clang 上采用。
- **setjmp/longjmp + trampoline**：需取 `&&label`，同样触发「间接 goto 可能跳到 stub」的编译错误。
- **仅栈上 `char __jit_shadow[40]`**：无法避免 pc/bc 在 472–479 段被中间调用破坏。
- **仅 noinline 包装（同 TU）**：仍无法避免 pc/bc 被提前破坏；且「先保存再调 JIT 再恢复」若保存时机晚于中间调用，仍无效。
- **NDEBUG 下 BuildTrace 的 std::cerr**：可继续注释或改用 noinline 辅助（见 `build-fib-crash-investigation.md` 方案 B），与本次「JIT 调用 + pc/bc 保护」方案独立。

---

## 5. 修改清单（回退后按此逐项施加）

1. **gotovm.cpp**  
   - 在 Clang Win64 分支内：增加 `jit_call_wrapper` 声明、`s_jit_pc`/`s_jit_bc`、`JIT_SAVE_PC_BC`/`JIT_RESTORE_PC_BC` 及 `JIT_CALL_ASM` 宏；在 `#else` 中增加 `JIT_SAVE_PC_BC`/`JIT_RESTORE_PC_BC` 空宏。  
   - 两处「调用 JIT」的块：块内第一句为 `JIT_SAVE_PC_BC();`，然后原有逻辑，再 `slot_t result;` + `JIT_CALL_ASM(...)` + `JIT_RESTORE_PC_BC();`，最后 release/set/NEXT。

2. **trampoline.cpp**  
   - 在 Clang Win64 条件下增加 `jit_call_wrapper` 实现（前后 `asm volatile("" ::: "memory");`，中间 `return fn(slots, ctx);`）。

3. **（可选）清理**  
   - 若存在 `jit_call_trampoline.s` 的 CMake 引用及 `trampoline.h` 中 `jit_call_trampoline` 声明，可删除，避免未使用符号。

4. **保留**  
   - JIT 内 Build 下 debug_trace 不 call（nullptr + 16 字节 NOP）的现有实现不变。

---

## 6. 验证

- Build（Release）下运行 fib，应不再在 JIT 返回后或 NEXT() 处崩溃，且结果正确。
- Debug 下行为应与之前一致。
