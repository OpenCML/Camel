# 模块 `optim`：写在 Camel 里的优化器

`optim` 是用 Camel 写的模块（`stdlib/optim.cml`），把优化器变成值：`sgd`、`adam`、`adamw` 各是一个值，学习率表也是一个值，两个纯函数 `init` / `update` 把一个参数树推进一步。形状取自 Optax（`init(opt, params)` 给状态、`update(opt, params, grads, state)` 给 `(params, state)`），每一步的算术取自 `nn` 原来的原生优化器与融合内核 `modules/tensor/kernels/update.h`：本模块在同样的输入、同样的学习率表上给出与它们**逐位相同**的轨迹，写明的 f64 张量叶子也在内——原生路径原来把它窄化成 f32，2026-10-23 已按「缺口」第 3 条改正，现在两边都留在写下的元素类型上。逐个名字的签名与例子见 [`docs/stdlib-reference.md`](../stdlib-reference.md) 的 `optim` 一节，那一节由 `stdlib/optim.cml` 生成。

设计稿（`docs/design/stdlib/README.cn.md`）第 3 节把它列在标准库布局的“数值与 AI”里，第 5 节又把 `optim` 列在官方扩展库的第一档（单独维护、单独发布）：今天它在 `stdlib/`，与它建立在之上的 `tree` 放在一起，将来随打包方式移动。

## API 与类型

| 名字 | 是什么 |
| --- | --- |
| `constant(rate)`、`linear_decay(peak, total, floor)`、`cosine_decay(peak, total, floor)` | 学习率表：一个 `(step: int) => float` 的函数值 |
| `sgd(schedule)` | 优化器 `Sgd`：只有学习率表；状态与其他两个同型，但 `sgd` 不读矩 |
| `adam(schedule)`、`adam(schedule, beta1, beta2, eps)` | 优化器 `Adam`：默认 `0.9` / `0.999` / `1e-8`，与原生调用处的默认值相同 |
| `adamw(schedule)`、`adamw(schedule, beta1, beta2, eps, decay)` | 优化器 `Adamw`：默认权重衰减 `0.01`，解耦（先按 `1 - rate * decay` 移动参数，矩只看梯度） |
| `init(opt, params)` | 初始状态 `{ m, v, step: 0 }`：两棵零树与参数树同形（三个优化器共用一个类型，见第 2 条），外加步数 |
| `update(opt, params, grads, state)` | 一步：`(params, state)`，矩与步数都前进一格 |

- 三个优化器类型是 `export type`，`update` 用 `match type O` 按第一个实参的类型分派（`Sgd` / `Adam` / `Adamw` 各一支），调用点不需要类型标注：语言不允许一个模块用同一个名字声明两个带函数体的函数（`SemanticDiag::Redeclaration`）。
- 状态的形状写在 `update` 的签名里：`{ m: P, v: P, step: int }`（`P` 是参数树的类型），结果类型也写在签名里，参考文档因此渲染出完整类型。语言不能按优化器重载，所以三个优化器共用这一个状态类型：`sgd` 带两棵它永不读取的零树（见「缺口」第 4 条）。
- 学习率表只按步数取值：第 `n` 步（从 1 数起）用 `schedule(n)`，`update` 读的是 `state.step + 1`。`total` 步之后 `linear_decay` / `cosine_decay` 给的就是 `floor`，是那个数本身，不是近似。
- `params` 是嵌套的结构体与元组的树，叶子是数（`float`、`f32`）与张量；`grads` 是它的梯度，即 `with_grad` / `grad` 对同一组参数给出的值。`nn` 的优化器要求第二个实参确实是梯度：把一棵仅仅同类型的树交给它会得到 `No matching function found for argument types: (T, T, float)`，两侧的对照用例因此都把 `with_grad` 的结果交给两边。
- 模块不注册任何算子：没有 `stdlib/signatures/optim.cml`，不在 `stdlib-declarations.mjs` 里，`--verify-ops` 这一半与它无关。

## 一步的算术

一个叶子的更新在双精度里算，只在存回去时舍入一次，与原生路径（`modules/nn/optim.cpp` 的 `sgdLeaf` / `adamLeaf`，以及它们调用的融合内核 `adaptiveUpdateOne`）相同。按叶子的种类：

| 叶子 | `sgd` | `adam` |
| --- | --- | --- |
| `float` | `p - rate * g`，双精度，存一次 | 双精度公式，存一次 |
| `f32` 数 | `f32(float(p) - rate * float(g))`：双精度算、存一次 f32 | 双精度公式，结果存一次 f32 |
| 写明的 `Tensor<f32, …>` | `p - f32(rate) * g`，f32 算术 | 叶子内转 f64、在 f64 上算、结果存一次 f32 |
| 写明的 `Tensor<f64, …>` | `p - rate * g`，仍是 f64 | 叶子内保持 f64、在 f64 上算、不降精度 |
| 元素类型没写的张量 | 按**值的**元素类型走：f64 值算 `p - rate * g`（仍是 f64），f32 值算 `p - f32(rate) * g`（f32 算术） | f32 值：叶子内转 f64、在 f64 上算、结果存一次 f32；f64 值：保持 f64、不降精度 |

- 张量的二元算子取两个元素类型里**更宽**的那个（`stdlib/signatures/tensor.cml` 的 `add` 一节），所以**写明的** f32 叶子与 `float` 学习率相乘会被抬成 f64：写明的元素类型要留住，就必须写成 `f32(rate)`。元素类型没写时算子的结果类型也不写，运行时按**张量值自己的**元素类型算、`float` 标量先被舍入到它（实测：元素类型没写、值是 f32 的张量乘 `0.5` 仍是 4 字节一元素），所以 `case _` 支直接写 `p - rate * g` 就与写明的 f32 支逐字节相同——`agreement.cml` 里 f32 值的叶子逐位为零就是这条的证据。这一条是移植里最容易踩的地方，`sgd_leaf` / `decay_leaf` 的每一支都按它写。
- 原生路径与上表逐行对应：叶子写下的元素类型经 `leafElementType` 决定执行支与输出缓冲（`leafTensor` 不再铸造，`newLike` 与 `zerosLeaf` 用叶子自己的元素类型），f64 叶子走 `modules/tensor/kernels/update.h` 的 `adaptiveUpdate64`（逐元素走同一个 `adaptiveUpdateOne`，值不经过任何 float），f32 叶子仍旧是 `adaptiveUpdate`，字节与轨迹都没变。守卫用例：`test/cases/modules/optim/native_dtype_exact.cml`（`optim.native_tensor_leaf_keeps_its_element_type`，四个后端，按字节比较）。
- `adam` 的偏差修正与原生一样用 `pow`：`correct1 = 1 - beta1^step`、`correct2 = 1 - beta2^step`（`math::pow`），`step` 从 1 数起。
- 逐元素算子的每一遍各自舍入一次，与融合内核“每元素三次正确舍入的双精度除法与一次双精度平方根”的结果相同，前提是编译不把乘加合成 FMA（项目构建用 `-ffp-contract=off`，设计原则的数值许可默认不做重结合）。这条依赖由测试兜住：三个用例都在 `std::nvm` / `std::fvm` / `std::jit` / `std::prl` 四个后端上跑，并且要求四个后端的标准输出逐字相同。
- 融合内核的位置（性能）属于 H51 第 5 项：原生路径一趟遍历完成，本模块一张量一算子（5 至 7 遍）。`#impl` 的接法已裁定（`docs/handover/reviews/questions-2026-10-23.md` 的 `impl-kernel-site` 一节：声明模块可以有函数体、泛型函数可以带 `#impl`），裁定同时写明 Adam 的内核要等 `optim` 落地之后接上，所以本模块只标注位置：`adam_number` 与 `adam_wide` 就是那个接口。

## 缺口即任务

写明的元素类型这一条已经修好（`handover/h353-f64-leaf`，2026-10-23）：原生路径按叶子**写下的**元素类型取执行支与输出缓冲（`modules/nn/optim.cpp` 的 `leafElementType` / `leafTensor` / `newLike` / `zerosLeaf`；`floatTensor` 那个「不是 f32 就铸成 f32」的函数没有了），f64 叶子走 `modules/tensor/kernels/update.h` 的 `adaptiveUpdate64`，逐个元素走同一个 `adaptiveUpdateOne`，字节不经过任何 float；`Tensor<f32, …>` 叶子的字节与轨迹一点没变。守卫是用例 `test/cases/modules/optim/native_dtype_exact.cml`（`optim.native_tensor_leaf_keeps_its_element_type`，四个后端，按字节比较：f64 叶子 16 字节、值是 f32 装不下的 `16777216.5`，f32 叶子 8 字节；两步 `adam` 的 f64 叶子与 `adam_wide` 逐字节相同）。

元素类型**没写在类型里**那一半也已修好（`handover/h362-untyped-leaf`，2026-10-24，按 `docs/handover/reviews/questions-2026-10-23.md` 的「f64-tensor-leaf」裁定 A）：两侧都按**值的**元素类型走——原生 `leafElementType` 读值的元素类型、`leafTensor` 那个「不是写下的类型就铸过去」的转换没有了，本模块 `sgd_leaf` / `decay_leaf` / `adam_leaf` 的 `case _` 支不再转换参数——所以 `full([3, 2], 0.1)` 这样的叶子在两侧都是 f64，而 f32 值的叶子逐字节与改动前相同（`test/cases/modules/nn/mlp.cml` 与 `embedding_mf.cml` 那类程序没写下的参数现在在 f64 里训练；两个用例的断言是不等式与梯度检查，四个后端上都不用改就仍然通过）。守卫是用例 `test/cases/modules/optim/unstated_dtype_exact.cml`（`optim.unstated_tensor_leaf_follows_its_value`，四个后端，按字节比较：值是 f64 的叶子 8 字节一元素、值是 f32 的 4 字节，两侧 `sgd` 与两步 `adam` 的参数字节逐个相同，`adam` 的两棵矩也按参数的元素类型；两个元素类型各看 2 叶 4 元素，`adam` 看 4 叶 8 元素）。下面是还缺的。

| 原生路径做什么 | 现在缺什么 | 今天的结果 |
| --- | --- | --- |
| 只把有切线的成员交给叶子（`tangentTypeOf` 去掉没有切线的成员） | 一个类型级的形状投影：写出“与 `P` 同形、叶子换成切线”的类型。`grad` 的结果类型就是这样算出来的，Camel 还没有这种类型级操作 | `update` 的遍历由参数树驱动、按字段名去梯度里取对应叶子，而泛型遍历问不出“这个字段在不在”，所以梯度必须与参数**逐字段同形**：`test/cases/modules/nn/residual_mlp.cml` 那种带 `depth: int` 的模型，它的梯度里没有 `depth`，实例化时报 `SemanticDiag`“该结构没有字段 `depth`”。原生路径用 `tangentTypeOf` 去掉没有切线的成员，照样训练这棵树 |
| `walkArray` 逐元素进入数组，数组元素是叶子（设计原则 9.9） | R3 把数组当叶子，`match type` 不能静态展开数组元素；`ckpt:tree_leaves` 缺的也是同一件事（`stdlib/tree.cml` 的模块注释写了原因：值元组不能拼接） | `examples/models/naive-in-expert-out/train_step.cml` 的 `blocks: Block[]` 写不出来：数组叶子既没有 `zeros_like`，也没有逐元素的减法 |
| 一个「元素类型没写在类型里」的叶子，它的元素类型只能在运行时从**值**里读 | 一个能在运行时说出张量元素类型的算子（或 `cast_like`）：类型层说不出（`Tensor` 的元素类型是**缺省**，不是某个默认值），算子层也没有 | `adam` 的 `case _` 支因此按值的宽度判元素类型（`size(to_bytes(p)) == numel(p) * 8`），代价是每个这样的叶子、每步复制一次数据；`cast` 的结果类型由 dtype 字面量静态决定，所以 `if` 的两支要用 `unstated`（把元素类型抹掉的接缝）合并。裁定见 `docs/handover/reviews/questions-2026-10-23.md` 的「f64-tensor-leaf」节（选 A，两侧一起，本模块的 `case _` 支不转换参数） |
| —— | 一个模块只声明一个 `init` / `update`（不能按优化器重载），Optax 那种“每个优化器一个状态类型”因此写不出来 | 三个优化器共用一个状态类型 `{ m: P, v: P, step: int }`：`sgd` 多带两棵零树，白占内存；要按优化器分状态，语言得先有重载或和类型 |
| 一次遍历完成一整棵树的更新（融合内核） | 一个把逐元素更新融成一趟的内核，以及 `#impl` 的落地（`impl-kernel-site` 裁定明确排在 `optim` 之后） | 每一步是 5 至 7 遍逐元素算子：正确，但比原生慢。性能属于 H51 第 5 项（H64 的 Adam 基线），本项不作门槛 |

## 对照与测试

四个用例都在 `test/cases/modules/optim/`，计划是 `test/plans/feat/modules/optim.plan.toml`：

- `agreement.cml`：同一个参数树、同一个梯度、同一个学习率表下，Camel 的 `sgd` / `adam` / `adamw` 与 `nn` 的 `sgd` / `adam` 走多步，每一步把两棵树逐叶、逐元素相减求和：恰好为零才算一致，并且打印比较过的叶子数，跳过叶子的遍不能冒充一致。两种性质另立：`adamw` 的零衰减就是 `adam`；`adamw` 的轨迹等于“每一步先把参数按 `1 - lr * decay` 移动、再用普通 `adam`”。
- `training.cml`：`showcase/c2-mlp-mnist/train.cml` 的训练循环缩到证明移植所需的部分（模型是 c2 那棵树的结构，只是去掉了 `int` 成员：本模块要求梯度与参数逐字段同形，见「缺口」第 1 条，而原生路径接受带 `depth: int` 的树）——`with_grad` 给梯度、`update` 消费它、循环收集损失曲线；原生 `nn:adam` 在同一条梯度、同一张表上走二十步，两步之间的参数漂移与最终漂移都必须恰好为零。
- `dtype_exact.cml`：写明的 f64 张量叶子留在 f64：`16777217.0` 走一步 `sgd`（rate 0.5）得 `16777216.5`，一个 f32 装不下的数，叶子仍是 16 字节。同一个用例把数叶子那条路钉在张量叶子上：同样的数、同样的梯度、同样一步，`{ w: float }` 的结果必须与一个单元素 `Tensor<f64, [?]>` 的结果相等（`adam_number` 对 `adam_wide`）——把 `adam_number` 的偏差修正写错，只有这条会红。这是模块这一侧；原生那一侧是 `native_dtype_exact.cml`。
- `native_dtype_exact.cml`：同一件事在**原生路径**上：一棵写着 `Tensor<f64, [?]>` 与 `Tensor<f32, [?]>` 两种叶子的树（梯度取自 `with_grad`，`nn` 要的是那种形状的梯度），一步 `nn:sgd` 后按 `to_bytes` 的字节比较——f64 叶子 16 字节、两个元素解出来是 `16777216.5` 与 `0.5`，f32 叶子 8 字节、是 f32 的 `16777216.0` 与 `0.5`；两步 `nn:adam` 后 f64 叶子的字节与 `adam_wide` 的逐字节相同（矩的元素类型就在这条性质里），f32 叶子同理。计划断言两侧各自看过的叶子数与元素数（sgd 4 叶 8 元素，adam 2 叶 4 元素），所以「一条都没看」不会冒充通过。改动前这条在四个后端全红（f64 叶子 8 字节、字节就是两个 f32，`failures=3`）。