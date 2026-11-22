# `tensor` 重写说明

草稿：`tensor.cml`（声明与 Camel 写的函数）、`tensor_definitions.cml`（规范定义）、`tensor.signatures.cml`（计算签名，替换 `stdlib/signatures/tensor.cml`）。**未编译、未运行**；本稿的前提是 `array` 定稿（整体归约归 `array`，`xs[idx]` 取代 `take`，切片与下标是核心的）。

## 1. `tensor` 现在是什么

没有 `Tensor` 类型之后，`tensor` 是**数值数组（任意秩 `T[..]`）的运算库**：创建、形状与布局、逐元素数值函数、沿轴的归约、矩阵乘、数组的二进制与 NumPy 文件形式。判据：一个函数若对“任意秩、元素是数”才有意义（广播、轴、矩阵乘），在 `tensor`；若对“一串元素”就有意义（排序、查找、整体归约），在 `array`；标量函数在 `math`。

与旧版的三点区别：
1. **配置进尖括号**：轴、形状、排列、类别数、权重、`eps` 都是 `with` 参数（设计原则 4.4）：`x->sum<1>`、`x->reshape<[2, 3]>`、`x->linear<weight, bias>`、`x->layer_norm<gamma, beta>`、`x->softmax`（轴默认 −1）。数据永远是普通参数、`->` 只传一个。
2. **元素类型进签名**：每条签名写 `::<T>` 加 `when`；一个运算的所有数组参数共用同一个 `T`，不同元素类型由签名拒绝（`f32[..] * f64[..]`、`f32[..] * bool[..]`），没有“取较宽者”的规则；标量操作数取数组的元素类型（字面量适配）。旧签名文件里的 `float[..]`、`number` 一类占位和“较宽者”删净。
3. **能用 Camel 写的用 Camel 写**：`zeros`、`ones`、`eye`、`one_hot`、`stack`、`linear` 是 Camel 函数；`softmax`、`log_softmax`、`relu`、`sigmoid` 的规范定义对所有浮点元素类型泛型地写一次（旧版只写 `f32`）。

## 2. 函数一览

| 名字 | 处理 | 理由 |
|---|---|---|
| `new` | **删除** | 数组字面量就是数组；嵌套字面量在 `f32[2, 2]` 的位置上是二维数组（`questions-2026-10-24-evening.md` “rank-two-constant-array…”） |
| `empty` | **删除** | 内容未定的数组不是确定的纯计算；用 `zeros` |
| `zeros`、`ones` | 改为 Camel：`full(shape, T(0))`、`full(shape, T(1))`；**元素类型必须写出或来自位置**（`zeros::<f32>([2, 3])`），旧版固定 `f32` | 不再有隐含的默认元素类型 |
| `full` | 保留，声明 `full::<T>(shape, value: T)` | 创建的唯一原子 |
| `eye` | 改为 Camel：`where(row === col, T(1), T(0))` | 广播比较即是 |
| `range` | **删除** | 核心 `a..b` 与 `range(a, b, step)` 已是同一件事 |
| `random`、`randn`、`seed` | **迁出到 `random`**，不再在 `tensor` | 全局生成器、元素类型写死 `f32`；设计原则 13.5 的键式随机数（`uniform`、`normal`，纯函数）才是数组随机的归宿。迁出时机见 Q3 |
| `add` `subtract` `multiply` `divide` `pow` `mod` | **删除** | 与运算符 `+ - * / ^ %` 同一件事；一件事一种写法。内部算子 URI（`tensor:add`…）不变，运算符重载仍落在它们上 |
| `lt` `le` `gt` `ge` `strict_eq` `strict_neq` `neg` | **删除** | 同上（`< <= > >= === !==`、一元 `-`） |
| `maximum`、`minimum` | 保留 | 二元的逐元素 IEEE 极值，没有运算符；`relu` 的定义用它 |
| `where` | 保留；四条重载（数组/标量各取其一） | 掩码用它选值，不用算术（设计原则 5.1） |
| `abs` `exp` `log` `sqrt` `rsqrt` `tanh` `erf` `gelu` | 保留，`when` 限定（`abs` 整数与浮点，其余浮点） | 逐元素内核，是原子（标量数学函数的数组版） |
| `sigmoid`、`relu` | 保留算子，定义在 `tensor_definitions` | 见 §4 |
| `cast(t, dtype: string)` | 改为 `cast::<U>(x)`：目标元素类型是类型实参 | 字符串名是 Camel 之外的拼写；`'float'` 在旧版里竟是 `f32`，易误 |
| `shape` `numel` | 保留（声明），仍是 C++ 折叠 | 见 Q6：建议最终归 `array` |
| `dim(t, axis)` | 改为 `t->dim<axis>` | 轴是配置 |
| `reshape(t, shape)` | 改为 `x->reshape<shape>`（`-1` 通配保留） | 形状是配置；同时常量形状让结果类型带上各维 |
| `flatten(t, axis)` | **删除**（旧义“折成二维”） | 用 `reshape<[n, -1]>`；`array::flatten` 取其名，指元素的行主序视图 |
| `unsqueeze(t, axis)`、`permute(t, perm)` | 改为 `with` 参数 | 同上 |
| `transpose` | 保留（最后两轴） | |
| `concat(a, b, axis)` | 改为 `concat<axis = 0>(a, b)` | a、b 对等（11.4）；轴 0 时对一维数组就是核心的 `concat(xs, ys)`，建议核心的 `concat` 并入这一个（Q8） |
| `stack` | **新增**，Camel：`unsqueeze` 后逐个 `concat` | 常用；二次复制，数量大时再配内核 |
| `take`、`idx_take`、`take_grad`、`row_add_at` | **删除导出**；`idx_take`、`take_grad`、`row_add_at` 作为内部算子保留（下标 `xs[idx]` 与其伴随用） | `xs[idx]` 即 take（设计原则 5.4） |
| `one_hot(labels, classes)` | 改为 Camel：`one_hot::<T>(labels)<classes>`；元素类型由 `T` 给（旧版固定 `int64`），越界标签仍是故障 | 广播比较即是 |
| `sum` `mean` `max` `min`（整体） | **删除**，归 `array` | 已定 |
| `sum` `mean` `max` `min` `argmax` `argmin`（沿轴） | 保留，轴与 `keepdims` 是 `with` 参数（`x->sum<1>`、`x->max<0, keepdims: true>`）；`argmax/argmin` 答 `int[..]`（旧 `int64` 的拼写） | 见 Q1 |
| `softmax` `log_softmax` | 保留，`axis` 默认 −1，泛型于浮点 | |
| `softmax_grad` | **删除导出**，留作内部算子 | 自动微分的构件，用户不直接调 |
| `layer_norm(x, gamma, beta, eps?)` | 改为 `x->layer_norm<gamma, beta, eps>`（γ、β 是权重，`with` 参数） | 4.4 |
| `matmul` | 保留；也是运算符 `@` | 矩阵乘规范算法不变（7.1） |
| `linear(x, weight, bias?)` | 改为 Camel：`x->linear<weight, bias>`，体是 `matmul(x, weight) + bias` | 不是原子；C++ 算子 `linear` 及其 VJP 删除（见 §3），自动微分穿过体 |
| `zeros_like` `ones_like` | **删除导出**，内部保留 | `zeros::<T>(shape(x))` |
| `show` | **删除** | 打印数组只有一种形式，`println` 已是（设计原则 11.5 / 打印的裁定） |
| `to_bytes`、`from_bytes` | 保留（见 Q5） | |
| `save_npy`、`load_npy` | 保留，**返回 `T!`**，数据在前、路径进尖括号：`x->save_npy<'a.npy'>`、`'a.npy'->load_npy::<f32>` | 文件失败是环境失败（3.9），不再是 `RuntimeError` 故障；种类用总表里的 `NotFound`/`PermissionDenied`/`IOError`，格式或元素类型不符用 `ParseError` |
| `num_threads` `set_num_threads` `gemm_backend` `gemm_library` `gemm_threads` `gemm_library_threads` | **删除**（见 Q4） | 线程数是进程级设置（9.3：全局选项只留进程级的事），不是数值函数；矩阵乘后端的内省属于测试设施 |
| `fuse` | 保留 | 流水线的遍 |
| `tensor_definitions.mean` | **删除** | 整体 `mean` 是 `array::mean`；定义库里的规范式随之只在那里 |
| `tensor_definitions.softmax/log_softmax/relu/sigmoid` | 保留，泛化到所有浮点 `T` | |

## 3. 需要的 C++ 改动

`modules/tensor`：
1. **导出清单**：不再导出上表“删除”的名字（`catalog.cpp`/`module.cpp` 的 `isDeclaredOperator` 与 `exports`）；`__add__` 等运算符默认导入（`exportDefaultImportRef`）保持。
2. **整体 `sum`/`mean`**（`reduction_ops.cpp` 的 `sum`、`mean` 两个算子，`sumVjp`、`meanVjp`）删除或并入 `array` 的内核：`array:sum_*` 已有 f64 累加一次舍入的实现，`modules/math/minmax.h` 共用。
3. **`with` 参数化**：`OpDef` 的 `ParamKind` 里 axis、keepdims、shape、perm、classes、gamma、beta、eps 由普通操作数改成上下文参数，执行器从 `with` 视图读；图上的操作数序随之变化，**VJP（`vjp.cpp`、各 `*Vjp`）、`fuse.cpp`、`index_eval.cpp` 与 `declareDefinition` 的 `operand(i)` 下标都按新顺序改**。这是改动最大的一项。
4. **删除算子**：`linear`（含 `linearVjp`）、`one_hot`、`zeros`/`ones`/`eye`/`empty`/`new`/`range`/`random`/`randn`/`seed`/`show`/线程与 gemm 内省、`flatten`；`take` 的导出（内部 `idx_take` 保留）。
5. **`cast`** 由 `dtype` 字符串改为类型实参；`dtypeFromName`（`elementwise_ops.cpp`）的字符串表删除；`argmax/argmin` 的结果元素类型记为 `int`。
6. **故障种类**：轴越界、空轴、`perm` 非排列统一为 `InvalidArgument`（旧 `RuntimeError`）；形状不合保留 `TensorDimensionMismatch`；`DivisionByZero` 用于空轴的 `mean` 的旧路径删除。空轴的 `max/min/argmax/argmin/mean` 与整体归约一致，是故障（7.7）。
7. **`save_npy`/`load_npy`** 失败改为错误值（`fs` 的 `errorKind()` 映射）；`load_npy::<T>` 的声明由计算改为 `load_npy::<T>(path): T[..]!`，仍属计算签名。
8. **`sum(bool)`**：`bool` 不再是 `sum` 的元素类型（`sum_axis_bool` 的律删除），与 `is_numeric` 一致（`numeric_contract.cml:57` 的裁定）；`max/min/argmax/argmin` 的 `bool` 律同删。
9. **`tensor_definitions`**：`declareDefinition` 的 `softmax`、`log_softmax` 绑定 `axis` 到 `with` 参数；`mean` 的绑定删除；定义由 `f32` 改泛型后，“定义只对 `f32` 展开”的限制去掉，对 `f16/bf16/f64` 也展开。
10. **内核读元素类型**：设计原则 10.4 要求原生内核不在运行时读静态类型、一个元素类型一个执行器；`modules/tensor` 的内核现在按数组携带的 dtype 运行时分派。把它改成按类型的执行器是一项大工程，**本稿不要求**（Q9）。

脚本：`scripts/stdlib-reference.mjs` 的 `tensor` 条目 `computed: 'signatures/tensor.cml'` 路径不变。

## 4. 自动微分的规则挂在哪里

本稿不改自动微分。规则仍随 C++ 原子算子登记（`setVjp`）：
- 逐元素：`elementwise_ops.cpp`（运算符落到的 `add`…`pow`、`maximum/minimum`、一元函数、`where`、`cast`）。
- 归约：`reduction_ops.cpp`（`sum_axis`/`mean_axis`/`max_axis`/`min_axis`、`softmax`、`log_softmax`、`layer_norm`）；整体 `sum`/`mean` 的 VJP 随算子并入 `array` 的内核（`array:sum_*` 需带“均分/广播回输入形状”的伴随，`broadcast_like`）。
- 布局：`layout_ops.cpp`（`reshape`、`transpose`、`permute`、`concat`、`unsqueeze`、下标 `idx_take` 与伴随 `take_grad`/`row_add_at`）。
- 矩阵乘：`linalg_ops.cpp`（`matmul` 的 `matmul_grad_lhs/rhs`）。
- 伴随构件（内部，不导出）：`gradient_ops.cpp` 的 `zeros_like`、`ones_like`、`numel`、`dim`、`sum_to`、`broadcast_like`、`expand_axis`、`gelu_grad`、`erf_grad`、`layer_norm_grad`、`cast_like`、`permute_inverse`。
Camel 写的函数（`zeros`、`ones`、`eye`、`one_hot`、`stack`、`linear`）不需要规则：自动微分穿过函数体。`softmax` 等四个有规范定义的算子保留自己的 VJP，规范定义用来验证。

## 5. 调用点迁移

下表是在导入了 `tensor` 的 587 个 `.cml` 文件里，各名字调用形式的出现次数（上界：同名的 `array`/`math` 函数也计入，如 `sum`、`max`）。

| 名字 | 次数 | 迁移 |
|---|---|---|
| `reshape` | 746 | `reshape(x, s)` → `x->reshape<s>`，机械改写 |
| `sum` | 1103 | 整体 → `import { sum } from array`；`sum(x, axis)` → `x->sum<axis>` |
| `new` | 137 | 删，换数组字面量（需要的位置写 `as f32[2, 2]`） |
| `zeros` `ones` `full` `eye` | 193 143 41 4 | 写元素类型：`zeros::<f32>(s)` |
| `shape` `numel` `dim` | 178 47 14 | `dim(t, a)` → `t->dim<a>` |
| `save_npy` `load_npy` `to_bytes` `from_bytes` | 164 73 156 16 | 前两者改 `->save_npy<path>`、返回 `void!`/`T[..]!`；后两者不变 |
| `cast` | 101 | `cast(x, 'f32')` → `cast::<f32>(x)`（字符串名 `'float'` 注意原义是 `f32`） |
| `random` `randn` `seed` | 79 50 28 | 迁出，见 Q3 |
| `show` | 46 | 删，换 `println` |
| `tanh` `relu` `softmax` `exp` `log` `sigmoid` `sqrt` `abs` … | 97 48 40 40 28 22 21 20 | 不变（`softmax` 的轴由第二个普通参数改为尖括号） |
| `concat` | 37 | `concat(a, b, ax)` → `concat<ax>(a, b)` |
| `transpose` `permute` `unsqueeze` `take` | 27 14 8 10 | `permute(t, p)` → `t->permute<p>`；`take(t, i)` → `t[i]` |
| `mean` `max` `min` `argmax` `argmin` | 75 45 28 16 10 | 整体 → `array`；沿轴 `max(x, 1)` → `x->max<1>` |
| `linear` `layer_norm` | 23 4 | `linear(x, w, b)` → `x->linear<w, b>`；`layer_norm(x, g, b, eps)` → `x->layer_norm<g, b, eps>` |
| `add` `subtract` `multiply` `divide` `pow` `mod` `neg` | 12 5 15 3 6 2 3 | 换运算符 |
| `where` `one_hot` `maximum` `minimum` `matmul` | 9 9 16 6 12 | `one_hot(l, n)` → `l->one_hot::<f32><n>` |
| `num_threads` `set_num_threads` `gemm_backend` | 8 7 1 | 删，用例改用别的观测（Q4） |
| `range` `flatten` `zeros_like` `ones_like` `softmax_grad` `empty` | 14 6 4 2 3 6 | 按 §2 |

重点目录：`test/cases/modules/tensor/**`、`test/cases/modules/nn/**`、`test/cases/modules/autodiff/**`、`test/cases/modules/onnx/**`、`test/cases/modules/tiles/**`、`test/cases/compile/array_shape/**`、`examples/ml/**`、`examples/models/**`；`docs/tutorial/**`、`docs/language/03_data_model.md`、`docs/design/arrays-memory/plan.cn.md`；`docs/stdlib-reference.md`、`docs/reference/camel-for-ai-stdlib-signatures.md` 重新生成。ONNX 降低与 Tiles 引用的是 `tensor:<op>` 的 URI，未删除的算子 URI 不变；`tensor:linear`、`tensor:one_hot`、`tensor:sum`（整体）、`tensor:mean`（整体）、`tensor:flatten`、`tensor:take` 若被 ONNX 降低或 Tiles 引用，要改认它们的组成部分（`matmul`+`add`、广播比较、`array:sum_*`、`reshape`、`idx_take`）。

## 6. 与其他模块的重叠

| 重叠 | 建议 |
|---|---|
| 沿轴的 `sum/mean/max/min/argmax/argmin` 与 `array` 的整体形式同名 | 见 Q1 |
| `exp log sqrt abs tanh …` 与 `math` 的标量版同名 | 见 Q2 |
| `concat` 与核心 `concat(xs, ys)` | 见 Q8 |
| `softmax` `log_softmax` `layer_norm` `linear` `gelu` `relu` `sigmoid` `one_hot` 是“层/激活” | 见 Q7 |
| `to_bytes`/`from_bytes` 与 `bytes` 模块；`save_npy`/`load_npy` 与 `fs` | 见 Q5 |
| `statistics`、`array::mean` 的规范式 | 与 `array` 一致，已无重复定义 |

## 7. 待验证的写法

1. `zeros::<f32>(…)`：只在返回类型里出现的类型参数（5.9 第二层），Camel 函数体里调用 `full(shape, T(0))`。
2. `cast::<U = f32>(x)`：`U` 只在返回类型里，`T` 由实参定；按名字写类型实参。`cast::<U, T>` 的位置顺序按“首次出现”应是 `T, U`，所以文档示例按名字写。
3. `with <weight: T[..], bias: T[..]? = null>` 的 `with` 参数类型里用到函数头的 `T`、可空默认值与 `if bias != null` 对 `with` 参数收窄（4.4 / 3.8 只写了对 `let` 与参数收窄）。
4. 逐元素的 `row === col` 广播比较（`(0..n)->reshape<[n, 1]>` 与 `[1, n]`），`where(…, T(1), T(0))` 两个标量选值的重载。
5. `parts: T[..][]`（数组的开放秩数组）与 `stack` 的 `reduce`。
6. `fail<'Kind'>(…)` 在 `one_hot`（等 `array` 同样等待核心原语）；在它定下之前，`one_hot` 越界标签的检查先不接。
7. 示例里 `[[…]] as float[2, 2]->sum<0>` 的 `as` 与 `->` 优先级。
8. 定义文件里 `x->max<axis, keepdims: true>`：具名的 `with` 实参混在位置实参之后。

## 8. 开放问题（带推荐）

**Q1. 沿轴的归约放哪里。** 任务书提议 `sum` 带 `axis` 与整体 `sum` 是一个重载集。做不到：`with` 参数不参与重载选择（4.4 / 约定 4），两个签名的普通参数都是 `T[..]`，只差“有没有尖括号”，且结果类型不同（`T` 对 `T[..]`），一个重载集无法决定。推荐：**整体在 `array`，沿轴在 `tensor`，同名，由导入决定**（约定 4 的“参数分类：数组整体/轴，不同模块”），`tensor` 的头注释已写明；同时导入两者按名字会冲突，是一个清楚的编译错误，用限定名 `tensor::sum<0>` 解决。备选是沿轴的改名（`sum_along`），更啰嗦，不推荐。

**Q2. 数组版的 `exp`、`log`、`sqrt`、`abs`、`tanh`、`pow` 与 `math` 同名。** 设计稿说 `math` “逐元素作用于数组时由重载给出”，但声明只能命名本模块的算子，`math` 无法声明 `tensor:exp`。草稿让这些函数留在 `tensor`（它们是原子内核），与 `math` 的标量版同名同义、分处两个模块。推荐接受；若要“一个名字一个来源”，需要 `math` 在 C++ 里注册数组版的执行器，然后 `tensor` 不再导出这十几个名字，这是一次较大的搬家，建议在 tensor 的 C++ 重构时再做。

**Q3. `random`/`randn`/`seed` 迁出。** 键式随机数（13.5）才是数组随机的正确形式，但 `random` 模块现在还没有数组版 `uniform::<T>(key, shape)`、`normal`。推荐：`random` 的重写批次加这两个（Philox，纯函数，跨设备逐位相同的整数/均匀），之后删 `tensor` 的三个；在那之前保留旧三个**但不进新稿**，用例（约 150 处）随 `random` 批次迁移。

**Q4. 线程数与 gemm 内省。** 推荐全部删除（线程数是 `--threads` 一类进程级设置，gemm 内省测的是实现，应由 profiler/测试设施读）。用例 16 处要换观测方式。

**Q5. 二进制与 NumPy 形式的归属。** `to_bytes`/`from_bytes` 是“数组 ↔ `Bytes`”，`save_npy`/`load_npy` 是文件格式。推荐 `to_bytes`/`from_bytes` 最终归 `bytes`（与编码解码同处），npy 归 `fs` 旁的 `npy` 模块或留 `tensor`；本稿都留在 `tensor`，仅改签名风格与错误约定。npy 失败改返回 `T!`（环境失败）是本稿加的，请确认；`ParseError` 用于“不是 NumPy 文件/元素类型不符”。

**Q6. `shape`、`numel`、`dim`。** 它们对任何数组都成立，推荐并入 `array`（与 `len` 同类，`array` 的整体归约也需要）；C++ 只是执行器前缀从 `tensor:` 改 `array:`。本稿未动，因为 `array` 已定稿，请决定。

**Q7. 激活与层。** `softmax`、`log_softmax`、`layer_norm`、`linear`、`gelu`、`relu`、`sigmoid`、`one_hot` 按“其余用 Camel 定义、层与损失在 `nn`”应归 `nn`。本稿留在 `tensor`，因为内核与 VJP 都在 `modules/tensor`、调用者现在从 `tensor` 导入；推荐在重写 `nn` 时一并搬，`layer_norm` 届时给出 Camel 定义（现无）。

**Q8. `concat`。** 核心 `concat(xs, ys)` 只适用一维；`tensor::concat<axis = 0>(a, b)` 是它的推广。推荐核心的 `concat` 直接采用后者（任意秩、`axis` 默认 0），`tensor` 不再导出。同理 `slice` 在核心已是任意秩。

**Q9. 内核按元素类型分执行器。** `modules/tensor` 的内核按数组携带的 dtype 在运行时分派，与设计原则 10.4（一个元素类型一个执行器、内核不读静态类型）不一致。重写它是单独的大工程；推荐在 GPU/Tiles 路线稳定后，按热点逐个算子改，不在本批做。

**Q10. `linear` 的融合内核。** 删除 C++ `linear` 后，偏置加法不再在矩阵乘的收尾里完成。推荐先删，测量表明慢再以 `#impl` 带回。

---

# 第二轮修订（决策者批复后）

以下**取代**上文冲突部分（§1 第 3 点、§2 表中涉及的行、§3、§5 的相应行、§6、§8 的 Q1、Q2、Q3、Q5、Q6、Q7、Q9、Q10）。草稿现状：`tensor.cml`、`tensor.signatures.cml` 已改；`tensor_definitions.cml` 只剩一段说明，**整个文件删除**；新增 `nn.additions.cml`、`array.signatures.cml`；`array.cml` 并入沿轴归约、`shape`/`numel`/`dim`、`to_bytes`。

## T1. 现在的 `tensor`

只剩：`full`、`zeros`、`ones`、`eye`、`one_hot`（创建）；`stack`；`maximum`、`minimum`、`where`、运算符（逐元素的二元）；`reshape`、`unsqueeze`、`transpose`、`permute`、`concat`（布局）；`matmul`；`save_npy`、`load_npy`；`fuse` 遍。即“需要多个数组或改变布局的数值运算”。

## T2. 各条批复的落实

1. **沿轴归约**归 `array`（见 `array.notes.md` S1）；`tensor` 里删除，包括 `tensor.signatures.cml` 里的六项。
2. **没有 `cast`**：`tensor.signatures.cml` 的 `cast` 删除，`dtypeFromName` 与字符串表删除；用到 `cast(x, 'f32')` 的约 101 处按元素类型写成 `f32(x)`（`'float'` 在旧版里是 `f32`，迁移时不要译成 `float(x)`；掩码 `cast(mask, 'f32')` 改成 `where(mask, 1.0, 0.0)` 或 `f32(mask)`，以后者为准）。`tensor.cml` 里不再出现 `cast`。
3. **`random`/`randn`/`seed` 已从 `tensor` 删除**，不留过渡。要迁移的用例约 157 处（`random` 79、`randn` 50、`seed` 28 个调用形式，分布在 `test/cases/modules/tensor/**`、`nn/**`、`autodiff/**`、`onnx/**`、`tiles/**`、`examples/ml/**`、`examples/models/**`）：`random(shape, lo, hi)` → `k->uniform::<f32><shape: s>` 再按需 `* (hi - lo) + lo`；`randn(shape, mean, std)` → `k->normal::<f32><shape: s>`（再 `* std + mean`）；`seed(n)` → `random::seed(n)` 配 `next_key()`，或显式 `key(n)`。结果不再与旧生成器逐位相同，凡是钉了具体数值的 golden 要重录。
4. **`shape`/`numel`/`dim`** 已进 `array.cml`。
5. **逐元素的 `exp log sqrt abs tanh erf rsqrt` 等**属于 `math`（对数组按元素施用，由 `map` 定义），从 `tensor` 删除；其 C++ 内核与 VJP（`elementwise_ops.cpp`）是 `math` 数组重载背后的执行器，由 `math` 的批次登记在 `math` 名下（或经 `map` 的融合识别）。迁移：`import { exp } from tensor` → `from math`。`rsqrt` 若 `math` 没有，写 `1 / sqrt(x)`。
6. **`to_bytes`/`from_bytes`** 进 `array`（`when is_plain`）；**`save_npy`/`load_npy`** 留在 `tensor`。
7. **激活与层**搬到 `nn`：`relu`、`sigmoid`、`gelu`、`softmax`、`log_softmax`、`layer_norm`、`linear`，连同规范定义，写在 `nn.additions.cml`，供 `nn` 的负责人并入；`nn.cml` 里现有的 `tensor::relu` 改为 `relu`，`tensor::reshape` 改为 `->reshape<…>`。`tensor_definitions.cml` 因此整体删除；`declareDefinition` 把 `softmax` 等的定义指向 `nn` 里的函数。C++ 的这些算子（`softmax`、`log_softmax`、`layer_norm`、`relu`、`sigmoid`、`gelu` 与它们的 VJP）迁到 `modules/nn`；`softmax_grad`、`layer_norm_grad`、`gelu_grad` 等内部伴随随之迁移；C++ 的 `linear` 及其 VJP 删除（`nn.additions.cml` 的 `linear` 是 Camel 体，`matmul` 加偏置）。`nn.additions.cml` 的 `gelu`、`layer_norm` 的函数体是我按现有内核的公式写的规范式：内核 `gelu` 现在在 f64 里算，规范式每步在元素类型里，二者要对一下（取一者为准，不一致就改内核）。
8. **C++ 后续**（一行）：`modules/tensor` 与迁出的内核仍按数组携带的 dtype 在运行时分派，设计原则 10.4 要求一元素类型一执行器；待 GPU/Tiles 路线稳定后按热点逐个算子改，不属库表面。

## T3. 对 §3 的修正

- 导出清单再删：整体与沿轴的 `sum/mean/max/min/argmax/argmin`（进 `array`）、`shape/numel/dim/to_bytes/from_bytes`（进 `array`）、`cast`、`exp…erf` 一元函数、`relu/sigmoid/gelu/softmax/log_softmax/layer_norm/linear`（进 `nn`）、`random/randn/seed`。
- `with` 参数化的范围缩小为：`reshape`（shape）、`unsqueeze`（axis）、`permute`（perm）、`concat`（axis）、`one_hot`（classes）、`save_npy`（path）。其余（轴归约、`softmax`、`layer_norm`、`linear`）的 `with` 化在 `array`/`nn` 的 C++ 里做。
- 自动微分挂载点（§4）：逐元素一元与二元规则随执行器迁往 `math`（一元）和留在 `tensor`（`maximum/minimum/where` 与运算符）；归约规则迁 `array`；激活与归一化规则迁 `nn`；布局与 `matmul` 规则留 `tensor`。

## T4. 调用点（对 §5 的修正）

在 §5 的表上：`sum/mean/max/min/argmax/argmin` 的 `import` 全部改 `from array`；`shape/numel/dim/to_bytes/from_bytes` 改 `from array`；`exp/log/sqrt/abs/tanh/erf` 改 `from math`；`relu/sigmoid/gelu/softmax/log_softmax/layer_norm/linear` 改 `from nn`；`cast` 与 `random/randn/seed` 见 T2。仍从 `tensor` 导入的只有 §T1 列出的名字。

## T5. 要等核心的事项

1. 重载选择把 `with` 参数的个数与名字算进去（`array.notes.md` S2.1）。
2. 类型名逐元素作用于数组（`f32(xs)`），包括 `bool` 到数值。
3. `fail<'Kind'>`（`one_hot`、`stack` 的空参数）。
4. 沿轴归约结果的静态形状（`array.notes.md` S3）。

## T6. 对 §8 的回应

Q1、Q2、Q3、Q5、Q6、Q7 已按批复处理；Q4 的删除不变；Q8 仍建议核心的 `concat` 并入 `tensor::concat<axis = 0>`；Q9 见 T2.8；Q10 在 `linear` 搬到 `nn` 后由 `nn` 的负责人定。
