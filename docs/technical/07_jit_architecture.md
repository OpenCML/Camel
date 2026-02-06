# FastVM JIT 模块架构设计

## 1. 概述

本文档描述为 Camel 语言 FastVM 虚拟机添加 JIT（Just-In-Time）编译能力的代码模块架构设计。JIT 将字节码在运行时动态编译为 x86-64 机器码，以实现热点代码加速；同时预留跨平台架构，便于未来支持 AArch64 等平台。

---

## 2. 现状分析

### 2.1 字节码特性

- **指令集**：约 50 种 opcode，分为定长（RETN, JUMP, LADD 等）和变长（FUNC, OPER, BRCH 等）
- **布局**：BytecodeHeader 8B + operands[] + BytecodeExtra 8B，每指令总长 `opsize * 8` 字节
- **数据模型**：`slot_t = uint64_t`，Frame 的 `dynamicArea_[]` 为 slot 数组，索引为 `data_idx_t`（正=动态区，负=静态区）

### 2.2 当前执行模型

- **分发方式**：Computed Goto（`gotovm.cpp`），通过 dispatch table 间接跳转
- **调用约定**：栈式调用（pcStack_、frameStack_），支持尾调用（TAIL）
- **不可内联指令**：OPER（调用 `operator_t` C++ 函数）、CALL、部分 FUNC/TAIL 需与运行时协作

### 2.3 适合 JIT 的指令

| 类别 | 指令 | 说明 |
|------|------|------|
| 算术 | IADD/LADD, ISUB/LSUB, IMUL/LMUL, IDIV/LDIV 等 | 纯计算，易直接翻译 |
| 比较 | LLE, LLT, LEQ, LNE 等 | 分支条件生成 |
| 控制流 | JUMP, BRCH（简单 if-else） | 跳转与分支 |
| 数据 | COPY, RETN（简单路径） | slot 拷贝与返回 |

### 2.4 需运行时支持的指令

- **OPER**：必须 trampoline 到 C++ `operator_t`
- **FUNC/TAIL**：涉及 Frame 切换、pc/frame 栈
- **ACCS, FILL**：依赖 `TypeCode`、`Object*` 等运行时类型信息
- **CALL**：闭包与动态分发

---

## 3. 模块架构设计

### 3.1 顶层目录结构

```
src/builtin/passes/sched/linear/fastvm/
├── bytecode.h/cpp          # (已有) 字节码定义
├── fastvm.h/cpp            # (已有) FastVM 主逻辑
├── gotovm.cpp              # (已有) 解释执行
├── compile.h/cpp            # (已有) 编译与链接
│
├── jit/                    # 新增 JIT 模块根目录
│   ├── jit.h               # JIT 公共接口与类型
│   ├── jit_config.h        # 编译期 JIT 开关与平台检测
│   │
│   ├── backend/            # 机器码生成后端（平台相关）
│   │   ├── backend.h       # 抽象后端接口
│   │   ├── x64/            # x86-64 实现
│   │   │   ├── x64_backend.h
│   │   │   ├── x64_backend.cpp
│   │   │   ├── x64_codegen.h
│   │   │   └── x64_codegen.cpp
│   │   └── fallback.h      # 无 JIT 时的占位实现
│   │
│   ├── compiler/           # 字节码 → IR / 机器码
│   │   ├── bc_to_machine.h
│   │   ├── bc_to_machine.cpp
│   │   └── lowering.h      # 各 opcode 的 lowering 规则
│   │
│   ├── runtime/            # JIT 运行时支持
│   │   ├── trampoline.h    # OPER / CALL 等 trampoline
│   │   ├── trampoline.cpp
│   │   └── stub.inc        # 汇编 stub（可选，用于关键路径）
│   │
│   └── tier/               # 分层执行策略
│       ├── tier_policy.h   # 何时触发 JIT
│       └── tier_policy.cpp
│
└── (现有文件保持不变)
```

### 3.2 核心模块职责

```
┌─────────────────────────────────────────────────────────────────────┐
│                        FastVMSchedPass (现有)                         │
│  apply() → precompile() → call() [解释 or JIT]                        │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    ▼                               ▼
        ┌───────────────────┐           ┌───────────────────────┐
        │   Interpreter     │           │   JIT Execution       │
        │   (gotovm.cpp)    │           │   (jit/compiler +     │
        │                   │           │    backend)           │
        └───────────────────┘           └───────────────────────┘
                                                    │
                                    ┌───────────────┼───────────────┐
                                    ▼               ▼               ▼
                            ┌──────────┐   ┌──────────────┐   ┌──────────┐
                            │ Backend  │   │ BC→Machine   │   │ Runtime  │
                            │ (x64)    │   │ Compiler     │   │ Stubs    │
                            └──────────┘   └──────────────┘   └──────────┘
```

---

## 4. 模块详细设计

### 4.1 JIT 配置层 (`jit/jit_config.h`)

```cpp
#pragma once

#include <cstddef>

// 编译期开关
#ifndef ENABLE_JIT
#  if defined(__x86_64__) || defined(_M_X64)
#    define ENABLE_JIT 1
#  else
#    define ENABLE_JIT 0
#  endif
#endif

#if ENABLE_JIT
#  define JIT_TARGET_X64 1
#else
#  define JIT_TARGET_X64 0
#endif

namespace camel::jit {

// 运行时 JIT 策略
enum class JitPolicy {
    Disabled,       // 始终解释执行
    OnDemand,       // 热点触发 JIT
    Always,         // 启动时全量 JIT（调试/基准）
};

struct JitConfig {
    JitPolicy policy = JitPolicy::OnDemand;
    size_t hotThreshold = 1000;   // 调用次数阈值
    size_t maxCodeCacheSize = 4 * 1024 * 1024;  // 4MB
};

}  // namespace camel::jit
```

### 4.2 抽象后端接口 (`jit/backend/backend.h`)

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "bytecode.h"
#include "compile/gir.h"
#include "core/context/frame.h"

namespace camel::jit {

// 编译单元：一个 Graph 对应的字节码区间
struct CompilationUnit {
    GraphIR::Graph *graph;
    std::span<const Bytecode> bytecodes;
    size_t entryPc;
};

// 编译结果：可执行机器码 + 元信息
struct CompiledCode {
    std::vector<uint8_t> code;        // 机器码
    size_t entryOffset;               // 入口在 code 中的偏移
    std::vector<RelocInfo> relocs;    // 重定位（跨函数调用等）
};

// 跨平台后端抽象
class IJitBackend {
public:
    virtual ~IJitBackend() = default;

    // 编译字节码为机器码
    virtual std::unique_ptr<CompiledCode> compile(const CompilationUnit &unit) = 0;

    // 注册 trampoline 地址（OPER、Frame 分配等）
    virtual void registerTrampoline(const char *name, void *addr) = 0;

    // 将机器码加载为可执行内存，返回可调用入口
    using EntryFn = slot_t (*)(Frame *frame, void *ctx);
    virtual EntryFn load(std::unique_ptr<CompiledCode> code) = 0;

    // 释放已加载代码
    virtual void unload(EntryFn fn) = 0;
};

// 工厂：按平台返回对应后端
std::unique_ptr<IJitBackend> createBackend();

}  // namespace camel::jit
```

### 4.3 x86-64 后端 (`jit/backend/x64/`)

**设计要点**：

- 使用 **AsmJit** 或 **Xbyak** 作为 x64 汇编/机器码生成库（header-only 优先，减少构建依赖）
- 若暂不引入第三方库，可先实现最小子集：手写常用指令编码（mov, add, sub, jmp, cmp, ret 等）

**调用约定**（与 C 互操作）：

- `Frame*` → `rdi`（Linux/macOS）或 `rcx`（Windows x64）
- `Context*` 或 JIT 上下文 → `rsi` / `rdx`
- 返回值 `slot_t` → `rax`

**Slot 访问**：

- `Frame::dynamicArea_` 相对于 `Frame` 的偏移固定（由 `FrameMeta` 确定）
- `currFrame->get<T>(idx)` → `mov rax, [rdi + baseOffset + idx*8]`
- `currFrame->set(idx, val)` → `mov [rdi + baseOffset + idx*8], rax`

**简单指令 lowering 示例**（LADD）：

```text
LADD result, fastop[0], fastop[1]
  →  mov rax, [frame + offset(fastop[0])]
     add rax, [frame + offset(fastop[1])]
     mov [frame + offset(result)], rax
```

### 4.4 字节码编译器 (`jit/compiler/bc_to_machine.cpp`)

职责：遍历 `CompilationUnit.bytecodes`，按 `pc` 顺序为每条指令调用后端生成对应机器码。

```cpp
// 伪代码
void BcCompiler::compile(IJitBackend &backend, const CompilationUnit &unit) {
    for (size_t pc = unit.entryPc; pc < unit.bytecodes.size(); ) {
        const Bytecode &bc = unit.bytecodes[pc];
        switch (bc.opcode) {
            case OpCode::LADD:
                backend.emitLAdd(bc.result, bc.fastop[0], bc.fastop[1]);
                break;
            case OpCode::JUMP:
                backend.emitJump(resolvePc(bc.fastop[0]));
                break;
            case OpCode::OPER:
                backend.emitTrampolineCall(kTrampolineOper, pc, bc);
                break;  // 无法内联，回退到 stub
            // ...
        }
        pc += bc.opsize;
    }
}
```

对 `OPER`、`FUNC`、`TAIL`、`CALL` 等，生成 trampoline 调用而非内联代码。

### 4.5 运行时 Trampoline (`jit/runtime/trampoline.cpp`)

将 JIT 不可内联的指令桥接回 C++ 实现：

```cpp
// OPER trampoline: 从 JIT 回调到 operator_t
slot_t trampolineOper(Frame *frame, size_t pc, const Bytecode *base, Context *ctx) {
    const Bytecode &bc = base[pc];
    // 复用现有 OPER 逻辑
    FrameArgsView withView(*frame, bc.wargs());
    FrameArgsView normView(*frame, bc.nargs());
    return bc.extra()->func(withView, normView, *ctx);
}

// FUNC/TAIL: 切换到解释器执行目标函数，或递归 JIT
slot_t trampolineFunc(Frame *frame, size_t targetPc, GraphIR::Graph *graph, ...);
```

JIT 生成的代码在遇到这些指令时，`call` 到对应 trampoline，trampoline 内部可再调用 `FastVMSchedPass::call()` 或解释器逻辑。

### 4.6 分层策略 (`jit/tier/tier_policy.cpp`)

```cpp
class TierPolicy {
public:
    // 是否对给定 (graph, pc) 进行 JIT
    bool shouldJit(GraphIR::Graph *graph, size_t pc) const;

    // 记录调用计数，达到阈值时触发 JIT
    void recordCall(GraphIR::Graph *graph, size_t pc);

private:
    std::unordered_map<std::pair<GraphIR::Graph*, size_t>, size_t> callCounts_;
    size_t hotThreshold_;
};
```

`FastVMSchedPass::call()` 在解释执行时，可先 `recordCall`；当 `shouldJit` 为真时，触发一次编译并缓存 `EntryFn`，后续直接调用 JIT 版本。

---

## 5. 与 FastVM 集成

### 5.1 FastVMSchedPass 扩展

```cpp
// fastvm.h 扩展
class FastVMSchedPass : public LinearSchedPass {
    // ... 现有成员 ...

#if ENABLE_JIT
    std::unique_ptr<jit::IJitBackend> jitBackend_;
    std::unordered_map<GraphIR::Graph*, jit::EntryFn> jitCache_;
    jit::TierPolicy tierPolicy_;
#endif

    slot_t call(size_t pc, Frame *rootFrame);
};
```

### 5.2 执行路径选择

```cpp
slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
#if ENABLE_JIT
    GraphIR::Graph *graph = rootFrame->graph();
    if (auto it = jitCache_.find(graph); it != jitCache_.end()) {
        return it->second(rootFrame, /* jit context */);
    }
    tierPolicy_.recordCall(graph, pc);
    if (tierPolicy_.shouldJit(graph, pc)) {
        compileAndCacheJit(graph);
        return jitCache_[graph](rootFrame, nullptr);
    }
#endif
    // 回退到解释执行
    return callInterpreter(pc, rootFrame);  // 原 gotovm 逻辑
}
```

---

## 6. 跨平台架构

```
                    ┌─────────────────────┐
                    │   IJitBackend       │
                    │   (抽象接口)         │
                    └──────────┬──────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ X64Backend      │  │ AArch64Backend  │  │ FallbackBackend │
│ (x86-64)        │  │ (未来)          │  │ (无 JIT)        │
│ AsmJit/Xbyak    │  │ -               │  │ 直接走解释器    │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

- **x64**：主平台，完整实现
- **AArch64**：接口与 `BcCompiler` 共用，仅需新后端实现
- **Fallback**：`compile()` 返回空，`load()` 返回 nullptr，上层始终走解释器

---

## 7. 实现阶段建议

| 阶段 | 内容 | 产出 |
|------|------|------|
| P0 | 搭建 jit/ 目录、JitConfig、IJitBackend 接口 | 可编译骨架 |
| P1 | X64Backend + AsmJit 集成，实现 LADD/LSUB/LLE/JUMP/RETN | 最小可运行 JIT（如 fib 纯算术路径） |
| P2 | OPER/FUNC/TAIL trampoline，与解释器互调 | fib 全路径可 JIT |
| P3 | TierPolicy、jitCache、与 FastVMSchedPass 集成 | 分层执行 |
| P4 | 更多 opcode、优化（寄存器分配、窥孔） | 性能提升 |

---

## 8. 依赖与构建

### 8.1 可选依赖

- **AsmJit**（推荐）：header-only 或静态库，用于 x64 机器码生成
- 或 **Xbyak**：轻量 x64 JIT，需在构建中启用

### 8.2 CMake / 构建集成

```cmake
option(ENABLE_JIT "Enable JIT compilation" ON)
if(ENABLE_JIT AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"))
  add_subdirectory(third_party/asmjit)
  target_sources(camel PRIVATE
    src/builtin/passes/sched/linear/fastvm/jit/...
  )
  target_compile_definitions(camel PRIVATE ENABLE_JIT=1)
endif()
```

---

## 9. 风险与约束

- **二进制可移植性**：JIT 生成的机器码与宿主 ABI 绑定，跨进程/跨机器不可直接复用
- **GC 与 JIT**：若未来引入精确 GC，需在 JIT 代码中插入 GC 安全点（当前可忽略）
- **调试**：JIT 代码需支持符号/映射，便于 Profiler 与调试器

---

## 10. 参考资料

- [AsmJit - Machine Code Generation](https://asmjit.com/)
- [LuaJIT 2.0 SSA IR](https://wiki.luajit.org/SSA-IR-2.0)
- [V8 Ignition → TurboFan pipeline](https://v8.dev/blog/ignition-interpreter)
