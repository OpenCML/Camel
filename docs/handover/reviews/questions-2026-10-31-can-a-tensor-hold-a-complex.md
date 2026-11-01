# 张量能装复数吗：签名说能，张量的存储层说不能

**问题**：`zeros`/`ones` 的签名写 `when is_numeric::<T>()`（`stdlib/signatures/tensor.cml:20`、`:26`），而 `is_numeric` 的定义是 `is_real || is_complex`（`stdlib/types.cml`），量得 `is_numeric::<c64>()` 为 `true`；但 `zeros::<c64>([2])` 在**两个后端上都匹配不到**这个签名。要问作者的是：**复数元素要不要成为张量的一部分**——两条路，我不该替作者选。

**今天拒绝的原文**（`std::fvm` 与 `std::cpp` 逐字相同，2026-10-31，`379e70c85` 之上）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (int[]).
  --> t.cml:3:25
   |
 3 |     println(zeros::<c64>([2]))
   |                         ^^^^^
help: Check the number and types of arguments to ensure they match one of the available overloaded function signatures. Available overloads:
          <tensor:zeros>: (shape: int[]) => T[..]
```

拒绝发生在**语义分析的重载匹配**里，不在 `std::cpp` 的规则里：张量的存储层只认 `i32`、`i64`、`f32`、`f64`、`bool`、`bf16`、`f16`（`modules/tensor/dtype.cpp:23` 的 `isSupportedTensorScalar`），结果类型推不出来，`tensorOf` 就抛 `Unsupported array element type`（`modules/tensor/ops/support.cpp:189`），于是这次调用没有匹配。所以 `operator_rules.cpp` 里 `tensorBuilder` 的 `default` 分支对 `c64`/`c128` **永远走不到**——今天挡住复数张量的是签名与存储层的不一致，不是那条规则。

**两条候选路**（各自一句代价）：

- **A. 张量模块加复数 dtype**：`isSupportedTensorScalar`/`dispatchDType`、张量内核、`npy` 交换、桥的转换、打印与四个后端都要各加一格——等于再加一个元素类型层。
- **B. 签名收窄到实数**（`when is_real::<T>()`）：改 `stdlib/signatures/tensor.cml` 两行并重新生成 `docs/stdlib-reference.md`——代价是声明与今天的行为一致，复数从此不是张量算子的一部分（要写就是另一条库）。

**等作者裁。**