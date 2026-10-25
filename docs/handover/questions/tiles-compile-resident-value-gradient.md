# 没有 `#gpu` 标签的梯度程序：`tiles::compile` 进来之后运行时拒绝

## 结论

`test/cases/gpu/nofallback/grad_expand_plain.cml`（一个 `with_grad` 调用点没有任何标签覆盖）

- 流水线 `std::nvm`：通过，`gradient tensor([5., 7., 9.])`、`wrong 0.0`。
- 流水线 `tiles::compile std::nvm`：拒绝。

```
error[RuntimeDiag::RuntimeError]: An error has occurred: autodiff: a value of type 'Device<Tensor<float32, [2, 3]>>' depends on a differentiated parameter but has no gradient channel, and it is used as data here (at test/cases/gpu/nofallback/grad_expand_plain.cml:25:20); the parameter's contribution would be dropped without a diagnostic. ...
```

**这不是这次改动引起的**：在未改动的 develop 检出（`~/camel-work/verify`）上跑同一条命令，得到逐字相同的拒绝（只有行号因注释改动而不同）。原因是 `tiles::compile` 注册的设备算子让 `m.x` 变成驻留值（`Device<Tensor<...>>`），而运行时 autodiff 不会穿过驻留值求导——`tiles::compile` 只要在流水线里，这个程序就走不通。

## 需要裁定的

1. **可接受吗？** 现在的说法是"这个程序不该把 `tiles::compile` 放进流水线"（没有作用域就没有段可编）。但用户看到的是"加了一个遍，原本能跑的程序死了"，而且死在运行时、离原因很远。
2. **要不要在编译期说话？** 例如：一个没有 `#gpu` 作用域、却含驻留值路径的梯度程序，由 `tiles::compile` 在编译期给出诊断（而不是让运行时 autodiff 报"驻留值没有梯度通道"）。
3. **测试钉的是哪一半**：`grad_expand.untagged_graph_keeps_the_runtime_operator` 只钉"这个遍自己既不展开也不拒绝"（两条消息都不许出现），运行时那段拒绝逐字进了 `expect_output_contains`——如果第 1 或第 2 条被裁定为要改，这条期望要跟着改。