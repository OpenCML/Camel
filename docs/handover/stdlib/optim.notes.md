# optim 重写说明

稿件：`docs/handover/stdlib/optim.cml`。未编译；语法点见末尾"待验证"。

## 对照 JAX/Optax

| Optax | 本稿 |
|---|---|
| `opt = optax.adam(lr)`，对象带 `init`/`update` | 没有对象。优化器就是函数 `(params, grads, state) => (params, state)`，超参数在尖括号里配好：`adam<schedule: cosine_decay<peak: 0.001, total: n>>` 是一个函数值 |
| `opt.init(params)` | `sgd_state(params)`、`adam_state(params)`：状态是普通结构体 |
| `updates, state = opt.update(grads, state, params); apply_updates` | 一步完成：`opt(params, grads, state)`（Camel 的参数树本来就是值，不需要先算 updates 再 apply；要 updates 时用 `tree_map` 自己相减） |
| 学习率调度是函数 `step -> lr` | 同：`(step: int) => float`，且 `constant<rate>`、`linear_decay<peak, total>`、`cosine_decay<peak, total>` 本身就是"配好的步数函数"，不再是返回闭包的构造函数 |

## `with` 判据的应用

- 超参数（`schedule`、`beta1`、`beta2`、`eps`、`decay`）是"决定这是哪一个优化器"的配置，在数据到来之前就定了 → `with`（4.4：超参数、函数要调用的别的函数都在此列）。
- `params`、`grads`、`state` 每一步都变，都是普通参数。`->` 只传一个值，所以管道写法是 `params->adam<schedule: s>(grads, state)`（管道调用可带额外实参，4.4）；更常见的写法是先 `let opt = adam<…>` 再 `opt(params, grads, state)`，循环里一目了然。
- 调度函数的"数据"是步数、配置是 `peak`/`total`/`floor`，所以 `3->cosine_decay<peak: 0.1, total: 100>` 符合判据，也让 `schedule: cosine_decay<peak: …>` 成为部分应用即可传入。
- `floor` 默认 0.0（有默认值的 `with` 形参必须在最后）。

## 函数表

| 项 | 处理 | 理由 |
|---|---|---|
| `Sgd`、`Adam` 结构体类型，`sgd(schedule)`、`adam(schedule, …)`、`adamw(schedule, …)` 构造函数 | **删除**；`sgd`、`adam`、`adamw` 改为直接做一步更新的函数，超参数为 `with` | 优化器不是对象；去掉 `init`/`update` 按类型的重载分派 |
| `init(opt, params)` 两个重载 | **删除** → `sgd_state(params)`、`adam_state(params)` | 同上；名字也与 C++ 里将被删的 `nn:adam_state` 一致 |
| `update(opt, params, grads, state)` 两个重载 | **删除**（就是 `sgd`/`adam`/`adamw` 本身） | |
| `SgdState`、`AdamState<P>` | 保留 | 纯值 |
| `constant`、`linear_decay`、`cosine_decay` | 改：从"返回闭包"改为带 `with` 的步数函数 | 见上；少一层闭包 |
| `Rates` 内部结构 | 保留（私有），加 `keep = 1 - rate*decay` | 把单独一遍的 `decay_tree` 并进 Adam 叶子，少一个树遍历 |
| `decay_tree`、`decay_leaf`、`decay_value_leaf`、`sgd_value_leaf`、`adam_value_leaf`、`adam_wide`、`narrow_*`、`zero_leaf` 的按元素类型分臂 | **删除** | 叶子写成泛型 `::<T>` + `when is_float::<T>()`，标量叶子（`T`）和数组叶子（`T[..]`）各一个定义，f32/f64/bf16/f16 各实例化一份；不再有 `case f32[?]`、`case f64[?]` 这类按拼写分臂 |
| `dtype_of`、`cast` 的所有用法 | **删除** | 转换逐元素作用于数组：`float(xs)`、`T(xs)`，与算术同一条规则 |

## 数值与行为

- 计算规则不变：标量与数组叶子在双精度里算、存回时舍入一次；`sgd` 的数组叶子里学习率先舍入到元素类型（与 `modules/tensor/kernels/update.h` 一致）。
- **修了一个潜在错误**：旧的 `narrow_tensors` 对"不是 f64 的所有叶子"一律存回 f32，`bf16`、`f16` 的叶子会悄悄变成 f32 并改变状态的类型；现在按 `dtype_of::<T>()` 存回自己的元素类型。
- **不训练的叶子**：整数、布尔（标量或数组）原样放过，与梯度规则一致（离散参数的梯度是自己类型的零，更新不动它），由 `untrained::<T>()` 的重载实现，状态里它的矩原样携带、不被读。字符串、函数在 `grad` 处已被拒绝。
- 调度的 `n` 仍从 1 起。

## C++ 侧改动

- 删除 `modules/nn/optim.{h,cpp}` 里的 `nn:sgd`、`nn:adam_state`、`nn:adam` 三个算子、`OptimizerState` 类型、`OptimizerResolver`，以及 `module.cpp` 里的 `exportType("OptimizerState")`、`optimizerOperatorGroups()` 与 `OperatorTable::set("nn:sgd" …)`。
- `modules/tensor/kernels/update.h` 的融合内核**保留**。按 10.4，热路径配 `#impl`：先量；若 Camel 版明显更慢，给 `adam_leaf` 的数组重载配 `#impl(adam_kernel)`（私有无体声明，返回三元组）。本稿不预先加。
- 测试里"Camel 优化器与原生优化器逐位一致"的差分用例（`test/cases/modules/optim/agreement.cml`、`training.cml` 的原生对照半边）随原生路径消失而失去对照对象；保留 Camel 版自身的断言，差分改成对 `update.h` 的内核单测。

## 调用点

| 变化 | 位置 |
|---|---|
| `adam(schedule, …)` + `init` + `update` → `adam<schedule: …>` + `adam_state` + 直接调用 | `test/cases/modules/optim/`（8 个文件）、`docs/reference/`、`docs/technical/`（optim 一篇）、`examples/` 中用到 optim 的训练例子 |
| `import { adam, adam_state, sgd } from nn`（原生）用了约 74 处（`sgd(` 44、`adam(` 18、`adam_state(` 12） | `test/cases/modules/nn/`（23 个文件）、`test/cases/modules/onnx/`（17）、`test/cases/perf/`（2）、`test/cases/modules/tensor/`（2）、`test/cases/feat/mm/gc/`、`test/cases/compile/{generics,namespace,native}/`、`test/cases/modules/autodiff/`、`examples/models/naive-in-expert-out/`、`examples/ml/`。这些调用 `sgd(params, grads, lr)` 改成 `sgd<schedule: constant<rate: lr>>(params, grads, sgd_state(params))`（取 `.0`），或在 ONNX 导出用例里直接用 `params - lr * grads` 的 `tree_map` |
| ONNX 导出用例中被导出的训练步里的 `nn::adam`/`nn::sgd` | `test/cases/modules/onnx/export_train_step_adam_dump.cml`、`ort_train_dynamic.cml`、`ort_gated_gru_step.cml`：导出器看到的将是 Camel 展开后的更新图，需确认可导出 |

## 开放问题

1. 已裁定：转换逐元素作用于数组（`float(xs)`、`T(xs)`），本稿处处这样写；`cast` 不再出现。
2. 已裁定：非浮点叶子放过（见上）。
3. 同名 `when` 重载（标量 `T` 与数组 `T[..]`）是否算重叠：保持现状，**编译检查点**，交集成者核对；若检查器不计 `when`，改为树遍历里 `match type P` 两臂分派。
4. `math::pi` 已导出，本稿已用。
5. **后续要加但本稿没加**：梯度裁剪（`clip_by_global_norm`）、预热调度（`warmup_cosine`）、`amsgrad`、`lion`、`sgd` 的动量。动量与裁剪都是"再包一层状态转换"，可用 Camel 函数组合写成，不需要新的原子。
