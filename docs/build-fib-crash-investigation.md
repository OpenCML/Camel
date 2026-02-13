# Build 模式 fib 崩溃排查结论

## 现象

- **Debug 构建**：fib(3) 等正常执行，有 trace 输出，结果正确。
- **Build（Release/NDEBUG）**：执行到某次 JIT 返回后崩溃，Exception 0xc0000005（User-mode data execution prevention，DEP），崩在数据地址被当代码执行。

---

## 根因一：从 JIT 内 call debug_trace wrapper 时返回地址被覆盖

- **调用链**：JIT 内 `sub rsp, N` → 保存寄存器 → `call jitDebugTraceWrapper` → wrapper/body 执行 → 恢复 → `add rsp, N` → 后续 `mov 0x20(%rdi), %rax` → `ret`。
- **Win64**：callee 可使用 [rsp..rsp+31] 的 shadow space。wrapper 在 Build 下会写这块，把 **call 压入的 8 字节返回地址** 覆盖成数据指针（如 Graph*、堆地址）。
- **结果**：JIT 内某条 **ret** 弹出被覆盖的地址，跳到数据区执行 → DEP（如 0x24aaf747868、0x27e72281868 等，`memory region` 显示为 rw-）。
- **验证**：关闭 debug_trace（Debug 下传 nullptr 不发射 call）后 Debug 仍正常；说明崩点确认为 debug trace 路径。

**修复方向**：Build 下不 call wrapper——传 `debugTraceFn = nullptr`，encoder 对 debug_trace 块仍发射相同长度代码，但把 `mov rax, addr` + `call rax`（12 字节）换成 12 个 NOP，保持布局、不执行 call，从而无 callee 覆盖返回地址。

---

## 根因二：gotovm 中 NDEBUG 的 std::cerr 返回时返回地址被覆盖（稳定复现）

- **复现步骤**：JIT 返回到 gotovm → 执行 `std::cerr << "[BuildTrace] gotovm(tiered): JIT call just returned (first line after call)"` → **该条 cerr 执行完后、从 operator<< 返回时** 崩溃，DEP 于 0x1ab8f8a3868。
- **原因**：Win64 下 **cerr 的 callee**（如 `operator<<` 或 ostream 内部）使用 [rsp..rsp+31] 的 shadow space，写坏了 **call 压入的返回地址**。返回时弹出被写坏的 8 字节（如数据/虚表指针），跳到该地址执行 → DEP。
- **结论**：崩点不在「从 JIT 返回到 gotovm」（那条 ret 的 [rsp] 可为正确的 0x00007ff739c5756b，即 gotovm 内紧接着的指令），而在 **从 std::cerr << ... 返回到 gotovm** 的那条 ret。

**为何“允许”写 [rsp]**：Win64 ABI 规定 **caller 在 call 前必须预留 32 字节 shadow**（即栈上 [返回地址+8] 起 32 字节给 callee 用）。若 caller 未预留，callee 仍会按 ABI 使用 [rsp]..[rsp+31]，就会覆盖“返回地址”那 8 字节。

**澄清：没预留的是谁？** 不是 JIT 编译的 `fn`。`fn` 已经正常返回，控制流已回到 gotovm。没按要求预留的是 **gotovm 里调用 `std::cerr << ...` 的那次 call** 的 caller（即 gotovm 这段 C++）。gotovm 里直接 `std::cerr << ...` 时，若编译器为这段生成的代码没有为这次 call 预留 32 字节，cerr 的 callee 就会写坏 gotovm 压入的返回地址。

**JIT 都返回了为什么还能影响后面？** JIT 返回后不会继续执行。崩点发生在 **gotovm 里某次 call（如 cerr）返回时**，被写坏的是 **gotovm 调用 cerr 时压入的返回地址**，不是 JIT 的返回地址。因此根因是「gotovm 对 cerr 的这次 call 在 Release 下未正确预留 shadow」，而不是 JIT 在返回后改栈。

**主要是优化导致吗？** 是。Debug 下优化少、栈帧和 shadow 更完整，故不崩；Release 下内联/省略帧等优化可能导致 **这一处** 对 cerr 的 call 未预留 32 字节，callee 写 [rsp] 即 DEP。本质是 **Release 优化导致 gotovm 对 ostream 的 call 未满足 Win64 ABI**，与 JIT 是否预留 shadow 无关。

---

**「中间插入 fn 是否打破编译器假设？」**

是的，可以这么理解，而且不一定是“C++ 代码写错”，而是 **Release 下编译器在“有中间 call fn”的路径上做了不当假设**：

- gotovm 里有多处会 call 别的函数（例如先 `call fn`，后面再 `call operator<<`）。Win64 要求 **每次** call 前都为 callee 预留 32 字节 shadow（即这次 call 压入的返回地址要在“预留区”之下，callee 用的是 [rsp+8]..[rsp+40]，不能动到 [rsp]）。
- 编译器在优化时，常会 **复用同一块栈空间** 给多次 call 当 shadow：在 prologue 里一次性 sub rsp, 32，后面多次 call 都“共用”这 32 字节。这在 **没有中间再压栈** 时是安全的。
- 但在 **先 `call fn` 再 `call operator<<`** 时：`call fn` 会多压 8 字节（返回地址），所以 rsp 比 prologue 结束时再少 8。若编译器仍认为“那 32 字节还在 [rsp+8]..[rsp+40]”，**没有在 `call operator<<` 前再 sub rsp, 32**，那么此时 [rsp] 就是“返回 gotovm 的地址”，而 ABI 允许 callee 使用 [rsp]..[rsp+31]，operator<< 就会把这 8 字节写坏。
- 也就是说：**中间多了一次 `call fn`，多了一次压栈，但编译器仍按“没有这次压栈”来复用 shadow**，相当于对“call 之后 rsp 变了”的假设错了，触发了未定义行为（返回地址被写坏）。

所以：fn 之外的代码是符合 C++ 的，问题在于 **在“fn 返回后、再 call 别的（如 cerr）”这条路径上，优化器对栈布局的假设在插入 fn 之后不再成立**；其它路径没有“先 call 一个 opaque 的 fn 再 call ostream”，所以不会崩。这不是你写错了 C++，而是 **Release 下“opaque call 插入 + shadow 复用”组合触发了错误的代码生成**；用 noinline 辅助或关掉这段的 cerr 都是绕过这种错误假设的稳妥做法。

**稳妥修复（推荐）**：**通过 trampoline 调 JIT，在返回时恢复 rsp**（见下「方案 A」），使回到 gotovm 时栈与“未做过这次 call”一致，编译器对后续 call 的 shadow 复用假设成立，可继续正常使用 std::cerr 和任意 C++ 调用。替代做法：用 noinline 辅助调用 cerr，或注释 NDEBUG 下 BuildTrace。

**临时修复**：注释掉 NDEBUG 下所有 BuildTrace 的 cerr（不鲁棒：NEXT() 等路径上若有其他 cerr/ostream 调用仍可能崩）。

---

## 当前采用：Build 下不 call wrapper（解决 fn 内部 DEP）

- **做法**：Build（NDEBUG）下 `debugTraceFn = nullptr`；encoder 对 `MirOp::DebugTrace` 仍发射整块，但把 `lea rcx,[rsp]` + `mov rax,addr` + `call rax`（共 16 字节）换成 16 个 NOP，不执行 call。
- **效果**：JIT 内不再 call wrapper，不会出现「callee 写 [rsp] 覆盖返回地址」→ **fn 内部** 的 DEP 消失。代码布局不变。
- **实现**：`x64_backend.cpp` 中 NDEBUG 时传 `nullptr`；`mir_encode.cpp` 对 DebugTrace 始终调用 `emitDebugTraceCall(m.pc, debugTraceFn)`；`x64_encoder.h` 中 `fnAddr == nullptr` 时发射 16 字节 NOP。
- **回到 gotovm 之后**：若仍出现 cerr/NEXT 等崩，可保留 NDEBUG 下 BuildTrace 的 cerr 注释，或改用 noinline 辅助输出（见方案 B）。

**已废弃**：jitCallWithShadowTrampoline（trampoline 只影响「从 fn 返回后」的栈，且经实测会引发「在 fn 内部崩」，已移除）。

---

## 方案 B：noinline 辅助函数（仅 BuildTrace 用 cerr 时）

在 `gotovm.cpp` 中：

1. **在 `#include <iostream>` 后增加** `#include <string>`，并在 `#if ENABLE_FASTVM_COMPUTED_GOTO` 后、第一处使用前加入：

```cpp
#ifdef NDEBUG
// Win64：caller 须为 callee 预留 32 字节 shadow，否则 ostream 会写 [rsp] 导致 DEP。noinline 辅助保证独立栈帧。
#if defined(_MSC_VER)
#define BUILD_TRACE_NOINLINE __declspec(noinline)
#else
#define BUILD_TRACE_NOINLINE __attribute__((noinline))
#endif
BUILD_TRACE_NOINLINE static void buildTraceLog(const char* msg) {
    std::cerr << msg << std::endl;
}
BUILD_TRACE_NOINLINE static void buildTraceLog(const std::string& msg) {
    std::cerr << msg << std::endl;
}
#endif
```

2. **把所有 NDEBUG 下的** `std::cerr << "[BuildTrace] ..." << std::endl;` **改为** `buildTraceLog("...");`；**带格式的**（如 pc、result）先拼成 `std::string` 再 `buildTraceLog(str)`，例如：
   - `buildTraceLog(std::format("[BuildTrace] gotovm(tiered): JIT returned pc={} result={}", pc, result));`
   - 或 `buildTraceLog(std::string("[BuildTrace] gotovm: RETN result=") + std::to_string(result) + " isRoot=" + (currFrame == rootFrame ? "1" : "0"));`

3. **恢复** 之前注释掉的 tiered 路径中所有 BuildTrace 调用（改为通过 `buildTraceLog`），保证 NEXT() 等路径上不再直接 `std::cerr << ...`。

---

## 调试器确认摘要

| 项目 | 内容 |
|------|------|
| DEP 地址示例 | 0x24aaf747868、0x27e72281868、0x1ab8f8a3868 |
| `memory region` | rw-（数据区，不可执行） |
| 正确返回地址示例 | 0x00007ff739c5756b → `FastVMSchedPass::call` 内、cerr 之后指令 |
| 崩点（根因二） | 执行完 `[BuildTrace] gotovm(tiered): JIT call just returned` 的 cerr 后，**从该 cerr 返回** 时 |

---

## 建议修复汇总

1. **JIT 内 debug_trace**：Build 下 `debugTraceFn = nullptr`，encoder 遇 nullptr 时发射 12 字节 NOP 替代 mov rax,addr + call rax，不调用 wrapper，保持代码长度不变。
2. **JIT 内 debug_trace（已采用）**：Build 下 `debugTraceFn = nullptr`，encoder 遇 nullptr 时发射 16 字节 NOP 替代 lea+mov+call，不调用 wrapper，消除 fn 内部 DEP。gotovm 仍直接 `fn(...)`。若「回到 gotovm 后」仍崩，可注释 NDEBUG 的 cerr 或改用 noinline 辅助（方案 B）。

两者均为「Win64 callee 使用 [rsp..rsp+31] 覆盖 caller 压入的返回地址」导致；Build 下优化/栈布局使问题暴露，Debug 下往往不触发。
