# JIT 文档

本目录存放 FastVM JIT 的**实现级注解**，与 [../technical/07_jit_architecture.md](../technical/07_jit_architecture.md) 配合使用。

| 文档 | 说明 |
|------|------|
| [bytecode-to-asm.md](bytecode-to-asm.md) | **字节码到机器码翻译注解**：以 `fib.cml std::inline std::asm` / `std::lbc` 为例，逐条说明 TAIL、RETN、OPER、FUNC、DSUB、LLE、BRCH、JUMP、LSUB、LADD、JOIN 等如何被 x64 backend 编译为汇编，含槽模型、调用约定与寄存器用途。 |
| [optimization-architecture.md](optimization-architecture.md) | **基础优化与架构改进方案**：针对「prologue 后冗余 mov」等基础优化，给出 L1 发射时状态、L2 发射缓冲（MIR）+ 优化 pass、L3 完整 MIR 的三层方案与实施建议。 |

生成汇编、MIR 与字节码的参考命令：

```bash
camel test/run/linear/fib.cml std::inline std::lbc   # 链接后字节码
camel test/run/linear/fib.cml std::inline std::asm   # JIT 汇编
camel test/run/linear/fib.cml std::inline std::mir   # JIT MIR（优化后，不生成机器码）
```
