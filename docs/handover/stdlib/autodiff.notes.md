# autodiff 重写说明

稿件：`autodiff.cml`、`autodiff.signatures.cml`（换进 `stdlib/signatures/autodiff.cml`）。未编译。

## 对照 JAX

| JAX | Camel |
|---|---|
| `jax.grad(f, argnums)` | `grad(f)`：对 `f` 的**第一个 `with` 参数**求导（权重），其余 `with` 参数与全部普通参数是常量；要对数据求导，用 lambda 把数据变成 `with` 参数（9.9）。`argnums` 不需要 |
| `jax.value_and_grad` | `with_grad(f)`：名字保留（设计原则 9.9 的规范名；改名要连测试、教程、原则一起改，没有收益） |
| `jax.vjp(f, x)` | `vjp(f)`：目标，签名在签名文件里：`(f 的参数) => (结果, 回拉函数)`，回拉函数把结果的余切向量变成对第一个 `with` 参数的梯度；雅可比、Hessian 建在它之上 |
| `jax.jvp` | `jvp(f)`：目标，前向模式：多一个 `with` 参数（方向），`jvp(f)<w, dw>(x)` 给 `(结果, 沿 dw 的切向量)`。需要在自动微分引擎里新增前向规则（每个算子的 JVP），工程量大 |
| `jax.lax.stop_gradient` | `stop_gradient` 保留 |
| 梯度与参数同型的 pytree | 同：结构体给结构体，元组给元组，浮点数组给同元素类型同形状的数组 |

## 梯度的类型（用户收到的东西）

写进了模块说明（这是该模块最该被读到的一条）：梯度的类型就是被求导的那个参数的类型。整数、布尔叶子的梯度是**它自己类型的零**（离散量对参数的导数按定义为零，9.9，与 JAX 一致），所以带计数器的参数树可以直接交给 `optim`，优化器原样保留它；字符串、函数叶子没有梯度，在编译期拒绝并点名叶子的路径；依赖被求导参数却没有梯度通道的连续值，一律带位置拒绝，不悄悄丢掉梯度。

## 函数表

| 项 | 处理 | 理由 |
|---|---|---|
| `grad::<F>(f: F)`、`with_grad::<F>(f: F)` | 改（签名文件）：写成 `::<F>(f: F)` 的函数变换，结果类型 `Gradient<F>` / `ValueAndGradient<F>` 是计算出来的 | 原先写成 `(x: float) => float` 的一个实例，读不出规则；`f` 是被变换的函数，按 4.4 是普通参数 |
| `stop_gradient::<T>` | 保留，签名加 `pure`，删 `@pure` | 原生算子必须写子句 |
| `vjp` 标签 | **移到 `.cml`**：`export tag vjp::<F>(rule: F) on func when is_function::<F>()` | 标签可以在 Camel 里声明（测试里已有 `tag ... on field` 的先例），C++ 里的 `declareVjpTag()` 删除 |
| `@effect differentiates the function's graph` | 删除 | 文档标签是第二份事实；`grad`/`with_grad` 是宏算子，`macro = true` 已在 C++ 的算子性质里 |

## C++ 侧改动（`modules/autodiff`）

- `module.cpp`：删除 `declareVjpTag()`、`kVjpTag`、`exportTag(...)`；`registerKey("autodiff::vjp", ...)` 的属性登记留在 C++（标签的事实类型是图引用，由 `.cml` 的标签声明引用它）。需要确认 `.cml` 里的 `export tag` 能带出 `ValueKind::Graph` 的事实；不能就保留 C++ 声明，`.cml` 里只留文档注释，这一条回问。
- `GradResolver` 保留（计算签名的唯一来源）；规则文本改为"参数列表同 F、结果是第一个 `with` 参数的类型"，整数/布尔叶子给同型的零。
- 不新增算子。

## 调用点

`grad(`、`with_grad(` 的写法不变，221 个文件无需迁移。需要处理的：
- `test/cases/modules/autodiff/` 里钉"梯度对整数叶子"的用例，期望改为同型的零（若原先是拒绝）；
- 声明了 `vjp` 标签的用例（`test/cases/modules/autodiff/{custom_rule,macro_round_lifetime_gc}.cml`、`test/cases/compile/tags/func_param.cml`）用 `#vjp(rule)`，写法不变，但要 `import { vjp } from autodiff`（若标签声明移到 `.cml` 后需要导入才可见，核对一下现状）。

## 状态：`vjp`、`jvp` 是目标

两个变换已经写进 `autodiff.signatures.cml`（参考页和评审用），**随自动微分车道落地**：在它们真正能跑之前不并入 develop，不让未实现的声明进库。当前 C++ 只有 `grad`、`with_grad`、`stop_gradient`，它们照常接入。

## 开放问题

1. **函数 `vjp` 与标签 `#vjp(rule)` 同名**：标签用 `#` 写，应当不在同一名字空间；接入 `vjp` 变换时核对。冲突就改标签名（比如 `#derivative(rule)`），不改变换名。
2. **`export tag` 能否带 `Graph` 值事实**（见 C++ 侧第一条）。
3. **`Gradient<F>`、`Pullback<F>`、`Jvp<F>` 写进签名文件的类型名**只是参考文字，不是语言里能写的类型；将来有"函数类型的类型函数"之后才能改成真正的声明。
4. `jvp` 的方向参数是 `with` 参数的第二个位置（紧跟被求导的权重）。若 `f` 本身已有多个 `with` 参数，方向放在**最后**更不易与已有的混淆，这一点随引擎实现时定。
