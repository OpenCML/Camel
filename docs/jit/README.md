# JIT 文档

本目录存放 FastVM JIT 的**实现级注解**，与 [../technical/07_jit_architecture.md](../technical/07_jit_architecture.md) 配合使用。

| 文档 | 说明 |
|------|------|
| [bytecode-to-asm.md](bytecode-to-asm.md) | **字节码到机器码翻译注解**：以 `fib.cml std::inline std::asm` / `std::lbc` 为例，说明字节码如何经 **MIR（V* 虚拟寄存器）→ 线性扫描寄存器分配 → 编码** 生成 x64 汇编；含槽模型、调用约定、MIR 层与 V* 指令。 |
| [optimization-architecture.md](optimization-architecture.md) | **基础优化与架构改进方案**：L1 发射时状态、L2 发射缓冲（MIR）+ 优化 pass、L3 完整 MIR 的三层方案；当前已实现 L2 形态的 MIR（V* + linearScanVReg）。 |

生成汇编、MIR 与字节码的参考命令：

```bash
camel test/run/linear/fib.cml std::inline std::lbc   # 链接后字节码
camel test/run/linear/fib.cml std::inline std::rmir  # JIT MIR（带 pc/槽注释，优化+regalloc 后）
camel test/run/linear/fib.cml std::inline std::asm   # JIT 汇编（最终机器码对应汇编）
camel test/run/linear/fib.cml std::inline std::mir   # JIT MIR（优化后，不生成机器码）
```
