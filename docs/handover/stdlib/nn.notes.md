# nn 重写说明

稿件：`docs/handover/stdlib/nn.cml`。未编译。`stdlib/signatures/nn.cml` **整个删除**：本稿里 nn 的每个名字都能用声明或 Camel 函数体写出，不再有计算签名（见下）。

## 总思路

1. **只有三个原子**：`conv2d`、`max_pool2d`、`avg_pool2d`（设计原则 10.4：需要布局控制的内核）。其余都是 Camel 定义（"softmax、规范化、损失、优化器……用 Camel 定义"，编译器看得穿，求导自动得到）：`batch_norm`、`batch_norm_train`、`embedding`、`softmax_cross_entropy`。
2. **计算签名消失**。旧的 15 个张量算子留在 C++ 的 `OpDef::infer` 里，是因为结果形状取自实参。现在签名里直接写维度：`conv2d(input: f32[n, c, ?, ?], kernel: f32[o, c, ?, ?], bias: f32[o]): f32[n, o, ?, ?]`——秩、通道数一致由类型检查保证，输出的 `OH`、`OW` 含除法，写 `?`（5.4：线性式以外的结果为未知）。形状未知的数组传进来要 `as`（5.4），这是语言的规则，不是新增的限制。
3. **配置在尖括号，权重是普通参数**：`stride`、`padding`、`kernel`（窗口）、`eps`、`momentum` 决定"这是哪个操作"→ `with`；`kernel`/`bias`/`gamma`/`beta` 是权重数组，像 `x @ w` 里的 `w` 一样是普通参数。**模型**才是 `with <m: Model>` 的函数（`logits<m>(x)`，权重在 `m` 里，`grad` 对第一个 `with` 参数求导）。
4. **可选参数不再按元数重载**：`stride?`、`padding?`、`eps?` 改为带默认值的 `with` 形参（4.4）；"步长默认等于窗口"写成 `stride: int? = null`（`T?` 表示没有，不用哨兵）。
5. **梯度算子不再是公开函数**：`conv2d_input_grad`、`conv2d_kernel_grad`、`conv2d_bias_grad`、`max_pool2d_grad`、`avg_pool2d_grad`、`batch_norm_train_grad`、`embedding_table_grad`、`softmax_cross_entropy_grad`、`*_labels_grad` 共 10 个删除：它们是求导的内部内核（C++ 里 `setVjp` 登记的导数），用户拿梯度只用 `grad`/`with_grad`。原子的导数由 C++ 的 VJP 引用这些内核，不需要导出；Camel 定义的函数由自动微分展开其函数体求导。
6. `save_params` **删除**：检查点属于 `ckpt`（`save_tree`/`load_tree`，原则 13.5；它的原子替换与清单、校验和比 `save_params` 完整）。`nn` 因此不再有任何效应，整个模块纯。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `conv2d` | 保留（原子）；`with <stride = 1, padding = 0>`，声明式签名 | 见上 |
| `max_pool2d`、`avg_pool2d` | 保留（原子）；`with <kernel, stride: int? = null, padding = 0>` | |
| `batch_norm` | 改为 Camel 体；`mean`/`variance` 作普通参数（旧参数名 `var` 是关键字，改名 `variance`）；`eps: f32` | 推理形式，一个表达式。写成 `input` 的 `[N, C, H, W]` 形式；任意秩（至少二维） |
| `batch_norm_train` | 改为 Camel 体 + `#impl(batch_norm_train_kernel)` | 原有融合内核保留为 `#impl`，函数体是语义；返回 `(y, running_mean, running_var)`，运行方差用无偏估计（与 `modules/onnx/lowering.cpp` 的导出一致） |
| `embedding` | 改为 Camel 体 `table[indices]`，泛型 `::<T>` | 就是按整数数组取行（5.4 的下标规则）；梯度是散射加，来自 `take` 的导数 |
| `softmax_cross_entropy`（分布目标、整数标签两个重载） | 改为 Camel 体 + `#impl` | 标签形式 = 分布形式的 one-hot；f64 累加后返回 `float`，与旧内核同 |
| 十个 `*_grad` | **删除** | 见上 |
| `save_params` | **删除** | 属于 `ckpt` |
| 旧文件头里的"optimizers"一节与 `OptimizerState` | **删除**（C++ 的 `nn:sgd/adam/adam_state` 也删，见 `optim.notes.md`） | |

## C++ 侧改动（`modules/nn`）

- `module.cpp`：不再导出 `OptimizerState`、不再导出 `optimizerOperatorGroups()`；`kDeclaredOperators` 列出新的声明式名字（`conv2d`、`max_pool2d`、`avg_pool2d`）和 `#impl` 内核；`save_params` 的 `declare`/`declareResources` 删除。
- `tensor_ops.cpp`：`OpDef` 里只保留三个原子的 kernel 与 VJP（`conv2d`、`max_pool2d`、`avg_pool2d` 以及它们各自的 `_grad` 内核，后者只供 VJP 用，不再 `exports`）；`conv2dInfer` 等 `infer` 规则删除（签名已声明）；`batch_norm`、`embedding`、`*_grad` 的导出删除。`batch_norm_train` 与 `softmax_cross_entropy`（两个）保留 kernel，作为 `#impl` 内核，分别登记为 `nn:batch_norm_train_kernel`、`nn:softmax_cross_entropy_kernel`、`nn:softmax_cross_entropy_labels_kernel`（URI 随私有声明的名字）。
- 保留的内核要把 `with` 实参当作普通实参接收（`#impl` 的内核签名里 `momentum`、`eps` 在参数表里，稿件末尾的三条私有声明写出了这一点）。
- 内核只做 f32；`with <eps: f32>` 的类型是 `f32`，因为数值类型之间没有隐式转换。

## 调用点

| 变化 | 位置 |
|---|---|
| `conv2d(x, k, b, stride, padding)` → `x->conv2d<stride: s, padding: p>(k, b)`；`max_pool2d(x, k)` → `x->max_pool2d<kernel: k>`；`avg_pool2d` 同 | `test/cases/modules/nn/{tiny_cnn,spatial_ops,mnist,conv2d_grad_sanity,conv2d_shape_error}.cml` 等、`test/cases/modules/onnx/`（17 个文件中与 nn 有关者）、`test/cases/perf/` |
| 直接调用 `*_grad` 的用例改为 `grad`/`with_grad` 与数值差分 | `test/cases/modules/nn/{conv2d_grad_sanity,embedding_grad_sanity,bn_train_grad_sanity,softmax_grad_sanity}.cml`（`softmax_cross_entropy_sanity.cml` 同） |
| `batch_norm(x, mean, var, gamma, beta)` → `x->batch_norm(mean, variance, gamma, beta)` | `test/cases/modules/nn/` 与 onnx 用例中用到者 |
| `embedding(table, indices)` 签名对参数类型更严（`indices: int[k]`） | `test/cases/modules/nn/{embedding_mf,embedding_index_error,matrix_factorization}.cml` |
| `softmax_cross_entropy(logits, labels)` 标签类型 `int[batch]` | `softmax_labels.cml`、`softmax_labels_oob.cml`、`softmax_classifier.cml`、`test/cases/modules/optim/training.cml` |
| `save_params(tree, dir)` → `ckpt::save_tree(dir, tree)`（注意参数顺序） | `test/cases/compile/{entry_args/struct_param,native/nn_arity,native/nn_declaration_import,native/nn_type}.cml`、`test/cases/modules/onnx/{export_train_step_adam_dump,ort_gated_gru_step,ort_train_dynamic}.cml`、`examples/models/naive-in-expert-out/README.cn.md`、`test/plans/feat/compile/native_declarations.plan.toml`、`test/plans/feat/modules/resources.plan.toml`（`nn` 的资源断言） |
| 清单与参考 | `stdlib/signatures/nn.cml` 删除；`docs/stdlib-reference.md`（生成）、`docs/technical/` 里 nn 相关 |

## 开放问题

1. 已裁定：`#impl` 的内核与声明签名逐字相同，`with` 形参包括在内。稿件里 `batch_norm_train_kernel` 因此带 `with <momentum, eps>`，C++ 内核从 `with` 视图读它们（`str` 模块的原生算子已是这样读）。
2. 已裁定：`batch_norm`、`batch_norm_train` 对 `T[n, c, ..s]` 的任意秩（至少二维）成立，沿通道轴（1）以外的全部轴归一化；`T` 是任意浮点元素类型，内核只服务 `f32`（`is_same::<T, f32>()`）。写法上用 `channel_shape` 和逐轴的 `mean`；`loop acc = x as T[..]` 与最后的 `as T[c]` 是需要编译核对的写法。
3. **标签越界是故障**依赖 `tensor::one_hot` 的行为，需核对；不符则在函数体里先检查，种类 `IndexOutOfRange`。
4. **`embedding` 的 `T[?, dim]`**：`indices` 为多维（`[batch, seq]`）时旧算子不支持，本稿也不支持。
5. **`stdlib/tensor.cml` 的名字**：本稿用 `tensor::sum`/`mean`（带轴）、`log_softmax`、`one_hot`、`numel`、`dim`、`shape`、`reshape`、`sqrt`、`range`；`cast` 已不用。张量模块或 `array` 若改名，这里同步。
6. 训练/推理两个形式不合成一个带开关的函数：返回类型不同，开关会让返回类型依赖值。
