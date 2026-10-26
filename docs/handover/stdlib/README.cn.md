# 标准库重写第一批（决策者定稿）

本目录是决策者定稿的新版本：`str`、`bytes`、`re`、`json`（含 `json.signatures.cml`）、`fs`、`io`、`env`、`process`、`time`、`hash`、`array`、`statistics`。每个 `<m>.cml` 是完整的新文件，`<m>.notes.md` 写着逐函数的去留、要改的 C++、要迁移的调用处。notes 末尾的"修订"一节以它为准。`error-kinds.md` 是错误种类总表，决策者会把它并进 `docs/stdlib-conventions.cn.md`。

这些文件没有编译过（决策者这边没有构建环境）。接入要求：
1. 把各文件换进 `stdlib/`（`json.signatures.cml` 换进 `stdlib/signatures/json.cml`），按 notes 做 C++ 侧的增删改，迁移调用处。每个模块一个提交，每个提交都是绿的。
2. **编译不过或语义不对时，按草稿的意图修**：语法写错、调用写错，照本意改过来。修不了，或者要改语义（函数的去留、签名形状、错误还是故障），写进问题页问决策者，不要自己重设计。修了哪些草稿错误列进报告，决策者据此改进写法。
3. notes 里标出的风险点先编译验证：`flatten` 的两个重载在 `int[][]` 上会不会同时匹配（会的话，`T[..]` 那一个加 `when !is_array::<T>()` 区分）、`match` 作 `loop` 体、可空数组的推导式。
4. **`fail<'Kind'>(message)`**（在 Camel 函数体里主动报故障的核心原语）还在等负责人决定。`array`、`statistics` 里用到它的五处，在决定之前先不接；这两个模块的其余部分照接。
5. 已定的跨模块改动：
   - 字符串与 `Bytes` 的范围下标 `s[a:b]` 进核心，`str::slice`、`substr` 删除；
   - `len(Bytes)` 并进核心的 `len`，`Bytes + Bytes` 是拼接；
   - `sleep`、`exit` 只在 `process`，前奏里的两份删掉；
   - `sum`、`mean`、`min`、`max`、`argmax`、`argmin` 作用于 `T[..]`，归 `array`，`tensor` 的整体形式删掉；
   - 空归约是故障（设计原则 7.7），`sum` 也是；
   - `time` 只有 UTC，C++ 里写死的 8 小时偏移是缺陷，随之删掉。
6. 文本组三个小问题已定：正则方言 ECMAScript；`replace` 的替换文本不展开 `$1`（用 `replace_with`）；`bytes` 暂不加十六进制函数。

## 第二批：`math`、`random`、`tensor`、`nn`、`optim`

文件同样在本目录（`tensor.signatures.cml`、`array.signatures.cml` 分别换进 `stdlib/signatures/`；`array.cml` 随第二批有修订，以本目录的现版本为准）。接入要求同第一批（第 1–3 条），另加：
1. **核心改动先落**（各模块依赖它们）：
   - 重载选择把 `with` 参数（个数与名字）计算在内，与普通参数一样，于是 `sum(x)` 与 `sum<axis>(x)` 是同一组；
   - 类型转换逐元素作用于数组（`f32(xs)`、`T(xs)`），用字符串写类型的 `cast` 删除（旧的 `'float'` 字符串表示 f32，迁移时照此对应）；
   - `fail` 与 `never`（见评审"删除 `throw`"一节）。
2. **随机数换成 Philox**，带固定种子的金标全部重录，这是有意的改动。`randint` 是左闭右开。原 `tensor` 的 `random`/`randn`/`seed` 删除，约 157 处调用迁到 `random`。
3. **`math` 的逐元素函数作用于数组**，由标量定义映射得到，`tensor` 里的同名函数删除。`format` 补上 `{:b}`、`{:o}`、`{:x}`（缺的话），`bin`、`oct`、`hex` 删除。
4. **`nn` 的层，权重是 `with` 参数**：`x->linear<w, b>->relu`，卷积、归一化、`embedding` 同样如此。原 `tensor` 的激活函数与 `linear`、`layer_norm` 归 `nn`。10 个对外的 `*_grad` 算子删除，梯度从 `grad` 拿。
5. **`optim`**：优化器是纯的状态转换 `(params, grads, state) => (params, state)`，超参数是 `with` 参数，非浮点叶子原样保留。约 74 处原生优化器调用、约 10 处 `save_params` 迁移（后者改用 `ckpt::save_tree`）。
6. **`tensor_definitions.cml` 删除**：规范定义跟着各自的函数走。
7. 沿轴归约的结果类型暂时写作 `T[..]`，静态形状由以后的类型函数补上（与切片"结果类型保留静态参数能证明的长度"是同一件事），不算缺陷。
8. `?->` 是否拆成 `?->`（可空）与 `!->`（错误）正在请负责人定。定了以后，草稿里所有用于错误的 `?->` 机械地改。

## 第三批：其余全部模块

`types`、`tree`、`stream`、前奏（`prelude.signatures.cml` 换进 `stdlib/signatures/globals.cml`）、`this`、`graph`、`opt`、`rewrite`、`rules`、`autodiff`、`onnx`、`tiles`、`python`、`ckpt`、`para`、`bench`（`perf` 与 `sim` 合并）、`test_support`；`pyplot` 删除；`*.signatures.cml` 换进 `stdlib/signatures/`，`stdlib/signatures/graph.cml` 删除。接入要求同前，另加：
1. **核心改动**：
   - 不同模块里同名、作用于不同类型的函数，同时可见时合成一组重载，不互相遮蔽；只有真正有歧义的调用才报错；
   - 核心 `slice` 的函数形式作用于数组、字符串和 `Bytes`，它需要 `match type` 能认出数组（`is_array`、`is_sliceable`）；
   - `unfold` 是用 `(状态, 步进函数)` 构造流的核心原语。
2. `graph`、`opt`、`rewrite`、`rules` 按 H233 的目标写，与那条车道配合接入，不要两边各改一份。决策者对它们的答复：
   - 提取的代价是 Camel 函数 `(o: Op, inputs: float[]) => float`。原来按名字登记的 `nodes`、`weighted` 改成导出的 Camel 函数，字符串形式删除，`std::opt::extract` 也改用函数；
   - 调度组合子放进 `rewrite`。一个模块可以同时有原生声明和 Camel 函数体（`str` 就是这样），不另开 `schedule` 模块；
   - 翻倍律按各自的类型取精确上界，推广到 `f16`、`bf16`；五条数组律推广到整数数组，接受。`array_double` 的上界跟着 `T` 走，旧的 `tensor_double` 一律用 f32 的上界是缺陷；
   - `#impl` 可以指向一个原生的遍，签名相同即可。
3. `autodiff` 的 `vjp`、`jvp` 是目标，随自动微分车道落地，能用之前不合入。函数 `vjp` 与标签 `#vjp` 如果冲突，标签改名 `#vjp_rule`。
4. 错误传递一律写 `->`（设计原则 3.9 已改）。泛型参数带 `when` 条件时，包着的值匹配不上就拆开，所以 `f->pass` 会把错误照常传下去（原则里已写明）。
