# random 重写说明

文件：`random.cml`。依据：设计原则 13.5（无状态随机数，Philox4x32-10，整数与均匀分布跨设备逐位相同）、`docs/design/training/README.cn.md` 第 4 节（键式随机数）、3.3（资源）、10.4；JAX 的 PRNG 键模型（`key` / `fold_in` / `split`，抽样是键的纯函数）。没有编译过。

## 总体设计

1. **键是主模型，全局流是它上面的一层薄壳。** 带键的函数是纯函数（同键同位，不读不写任何状态），优化器可以合并、移动、批量化它们。唯一的状态是资源 `rng` 上的一条全局流，只有两个原子：`seed(v)` 与 `next_key()`；`rand()`、`randn()` 都是 `next_key()` 加一个带键函数。用户要全局随机的 `randint`、`choice`、`sample`、`shuffle`，写 `next_key()->randint<1, 7>`、`xs->shuffle<next_key()>`；不再各有一份"全局版本"（旧的两套同名函数是重复）。
2. **一个原语，其余全用 Camel。** 唯一的带键原子是 `philox(k, counter): int[]`（Philox4x32-10 的 4 个 32 位字，私有）。`key`、`fold`、`split`、`uniform`、`normal`、`randint`、`permutation`、`shuffle`、`choice`、`sample` 全部是它之上的 Camel 定义。键派生与抽样用不相交的计数器区间（抽样用 0 以上，`split` 用 -1、-2，`fold` 用 -3 - i），所以一个键的抽样与它的子键不共用任何一个字。
3. **配置与数据（4.4）**：键是流过抽样的数据，所以 `k->uniform::<f32>`；决定分布的是配置，所以 `k->randint<1, 7>`。`shuffle`/`choice`/`sample` 的数据是数组，键是"这是哪一个洗牌"的配置：`xs->shuffle<k>`、`xs->choice<k>`、`xs->sample<k, 3>`。`fold(k, i)`、`permutation(k, n)` 两个参数地位对等，都是普通参数。
4. **`Key` 是命名结构体 `{ hi: int, lo: int }`**（原来是匿名结构体，设计稿里写作不透明的 `Key`）。目前用结构体，是因为所有后端已经能携带它；以后要做成不透明类型，只改这一行类型声明。
5. **精度**：`uniform::<T>` 的位数随 `T` 取：`float` 53 位，`f32` 24 位，`f16` 11 位，`bf16` 8 位，保证舍入进 `T` 后不会得到 1。`normal` 在 f64 里做 Box–Muller 再舍入一次，依赖 `log`/`sqrt`/`cos`，按 `math` 的误差上界，不要求逐位。`randint` 用拒绝法，无偏。

## 函数表

| 名字 | 处理 | 理由 |
|---|---|---|
| `philox(k, counter)` | 新增（原子，私有） | 唯一的带键原语 |
| `Key` | 新增类型名 | 见上 |
| `key(seed)` | 保留，改为 Camel：`{ hi: 0, lo: seed }` | JAX 的 `PRNGKey` 同样是原始种子；散列由 `philox` 完成，不必在种子上再散列一次。**结果与旧 `key(42)` 的值不同**（无兼容要求） |
| `fold(k, i)` | 新增（Camel） | 设计稿的 `fold`；第 i 个子键。`i` 为负是故障（`fail`，用处 1），因为负计数器区间归 `split` 使用 |
| `split(k)` | 保留，改为 Camel（`derive` 两次） | 与 `fold` 的计数器区间不相交 |
| `uniform::<T>(k)` | 新增 | 设计稿的 `uniform`，标量；`when is_float::<T>()` |
| `normal::<T>(k)` | 新增 | 设计稿的 `normal`，标量 |
| `randint<low, high>(k)` | 改：原 `randint(low, high)` 是全局状态的；现为带键、**半开区间** `[low, high)` | 与 `0..n`、切片、JAX 的 `randint` 一致；`high - low` 不在 `1..2^62` 是故障（`fail`，用处 2）。旧的"两端含、low > high 自动交换"删除 |
| `permutation(k, n)` | 保留，改为 Camel：`argsort` 每个位置一个 `uniform::<float>(fold(k, i))` | 一行；并列（概率约 n²/2^54）由 `argsort` 的稳定次序决定，仍是确定的。n 不为正得空数组，不再是故障 |
| `shuffle<k>`、`choice<k>`、`sample<k, n>` | 改为带键的 Camel 定义 | `shuffle` 是 `xs[permutation(...)]`，`choice` 是 `xs[k->randint<0, len(xs)>]`（空数组由 `randint` 的 `fail` 报），`sample` 是洗牌后取前 n 个（n 越界由下标报 `IndexOutOfRange`） |
| `seed(v)` | 保留（原子），签名加 `uses rng` | 全局流的起点；未调用时从种子 0 开始，所以每次运行结果相同（红线 6） |
| `next_key()` | 新增（原子，`uses rng`） | 全局流的下一个键；C++ 里是"流的键 `fold` 计数器，计数器加一" |
| `rand()`、`randn()` | 保留，改为 Camel（`next_key()->uniform::<float>` 等） | 常用的便利写法；效应由 `next_key` 推出 `rng` |
| 全局 `randint`、`choice`、`sample`、`shuffle` | **删除** | 见上 |

## `pure`、`fail`、资源

- 没有写 `pure`（带键的函数是纯的，由编译器从体推出，`philox` 的纯度在 C++ 登记）；`seed`、`next_key` 写了 `uses rng`，因为它们是原子，原子的资源必须写在签名里（3.3）。
- `fail<'InvalidArgument'>` 两处：`fold`（负下标）、`randint`（区间不合法）。

## C++ 侧改动（modules/random）

- 新增 `rand:philox`：直接用 `camel/utils/philox.h`，输入 `Key` 结构体与计数器，输出 `int[4]`（每项 0..2^32-1）。纯。`makeKey`/`readKey`（key.cpp）保留，类型由 `keyType()` 改为 cml 里的命名类型 `Key`（身份是"名字加模块"，C++ 取这个类型对象，不再自己造匿名结构体）。
- 新增 `rand:next_key`：全局状态改为（键，计数器）；`seed(v)` 设键为 `{0, v}`、计数器为 0；`next_key()` 返回 `derive(键, 计数器)` 并加一，其中 `derive` 与 Camel 里的 `fold` 用同一套计数器规则或另取一个固定标签均可，只要对 `philox` 的输出确定。资源 `rng` 沿用。
- 删除：`rand:rand`、`rand:randn`、`rand:randint`、`rand:choice`、`rand:sample`、`rand:shuffle`、`rand:key`、`rand:split`、`rand:permutation` 的内核与 `declare` 登记；全局 MT19937-64 生成器 `g_rng`、`rnd::unitDouble`、`rnd::NormalSource` 的使用。`include/camel/utils/random.h` 里这些工具若 `tensor` 的随机函数还在用，保留，不在本模块用。
- 注意：设计稿 13.5 与训练设计稿第 4 节规定了 `uniform` 由 32 位输出按 `(bits >> 8) * 2^-24` 构造 f32、`normal` 用 Box–Muller。本草稿的 `f32` 路径与之一致；`float` 路径用两个字取 53 位。张量形状的整块抽样（`normal(k, shape)`）见问题 2。

## 需要迁移的调用点

按 `import … from random` 或 `random::` 统计，共 35 个文件（用例、文档、示例混计）。要改写的：
- 全局 `randint(a, b)` → `next_key()->randint<a, b + 1>`（半开）：6 个文件；全局 `choice`/`sample`/`shuffle` → `xs->choice<next_key()>` 等：5/6/9 个文件（`test/cases/modules/std/random/basic.cml`、`test/cases/compile/native/random_*.cml` 六个、`test/cases/para/core/{bootstrap_medians,merge_sort,block_sort_merge,merge_sort_small,concurrent_alloc}.cml`、`test/cases/modules/autodiff/collapse_random.cml`、`test/cases/modules/onnx/export_impure.cml`、`test/cases/resources/{effects,conflict_per_resource}.cml`、`test/cases/modules/tiles/{batch_matmul,mixed_matmul}.cml`、`test/cases/compile/numeric/{gemm_library,grad_library}.cml` 等，逐个核对是用 `rand()`、`seed` 还是别的）。`seed`（18 个文件）、`rand`（12）、`randn`（3）的写法不变，**但序列变了**：所有记录了具体随机数输出的 golden（`test/plans/**/golden`）要重录。
- 带键的用例：`test/cases/modules/random/{keyed_random,permutation_negative,random_independence}.cml`：`key(42)` 的值变了，`split`/`permutation` 的值变了；`permutation_negative` 不再是故障（n 为负得 `[]`），改成值用例。`compile/native/random_*` 里钉的是各原子的参数个数/类型错误，随删除的原子一起改成针对 `seed`、`next_key` 或 `philox` 的错误用例。
- 文档：`docs/language`、`docs/design/training/README.cn.md`（4.1 的 `randint(k, 0, 999)` 现为 `k->randint<0, 999>`；`normal(ks, [batch, 784])` 的整块形式见问题 2）、`stdlib-reference.md` 与 `camel-for-ai-stdlib-signatures.md`（重生成）。

## 待决策者裁定的问题

1. **全局流改成 Philox（丢掉 MT19937-64）。**（推荐：是。）一套算法、跨平台逐位相同、与键式一致；代价是所有 `seed(42)` 后的具体数字都变，旧 golden 重录。
2. **整块抽样与张量形状。** 设计稿的 `normal(k, [batch, 784])` 是"第 i 个元素取计数器 i 的输出"，一次出整个张量，与分块划分无关。本草稿只给标量与 `fold` 逐元素（`xs = [normal::<f32>(fold(k, i)) for i in 0..n]`），值由定义确定，但每个元素一次完整的 `philox` 调用。推荐：整块形式属于 `tensor`：`tensor::normal(k, shape)` 的定义就是"第 i 个元素是 `normal(fold(k, i))`"，C++ 内核（CPU/Tiles）逐位照此实现并用 `#impl` 挂上；不要在 `random` 里再写一份。这与设计稿"计数器 i"的写法差一次 `fold`，要在训练设计稿里一并改。
3. **`randint` 改成半开区间**（推荐：是；与 `..`、切片、JAX 一致）。若坚持两端含，只改一行 `high - low + 1`，并处理溢出。
4. **`Key` 是命名结构体还是不透明类型？** 草稿用结构体（用户能读 `k.hi`）；设计稿说不透明。推荐：先结构体，等名义类型声明可用再改。
5. **`fold` 的负下标与 `split` 共用 `philox` 的负计数器区间**，是实现细节，但它决定了 `fold(k, i)` 的值；若设计稿将来要与别的实现（JAX 的 threefry 之外）对位，需另行规定。
6. **未设种子时从种子 0 开始**，以满足"同样输入同样结果"（红线 6）；若要真随机的默认值，必须是显式的 `seed(now())`。

## 修订（决策者 2026-10-26）

- **已定**：全局流改 Philox，旧 golden 重录；`randint` 半开区间；`Key` 先用结构体。
- **整块抽样放在 `random` 里**（随机归 `random`，`tensor` 不再有自己的 `random`/`randn`/`rand` 张量版本，删除）：`k->uniform::<f32><shape: [3, 4]>`、`k->normal::<f32><shape: [3, 4]>`，`shape` 是 `with` 参数。定义逐元素：第 i 个（行优先）元素是标量 `uniform`/`normal` 在 `fold(k, i)` 上的值，体是 `reshape([… for i in 0..n], shape)`；`#impl(uniform_array_kernel)`、`#impl(normal_array_kernel)` 指向 C++ 内核（`rand:uniform_array`、`rand:normal_array`；CPU 与 Tiles 逐位相同，与分块划分无关，整数与均匀分布逐位相同，正态按 `math` 的误差上界）。
- 集成时要验证的三点：(a) 标量 `uniform::<T>(k)` 与带 `shape` 的 `uniform::<T>(k)` 只差 `with` 参数与返回类型，`with` 参数不参与重载选择，可能报歧义——若报，把整块形式改名（`uniform_array`、`normal_array`）或让 `shape` 成为必有的 `with` 参数并去掉标量重载的同名；(b) `reshape` 目前只有 `float[..]` 的签名（`signatures/tensor.cml`），这里需要对 `T[..]` 泛型；(c) `::<f32><shape: …>` 两个尖括号相接的写法能否解析。
- `docs/design/training/README.cn.md` 4.1 的 `normal(ks, [batch, 784])` 改写为 `ks->normal::<f32><shape: [batch, 784]>`；`tensor.random`/`tensor.randn` 的调用处（`test/cases/modules/tensor/thread_values.cml` 等）迁移到 `random`。
