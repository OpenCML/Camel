# math 重写说明

文件：`math.cml`。依据：设计原则 5.1（不做隐式转换）、5.5、7.1（超越函数 ≤2 ulp、特殊值精确）、7.7（`min`/`max`）、10.4；`stdlib-conventions` 6.5。没有编译过。

## 总体设计

1. **一个函数一个泛型定义。** 浮点函数写 `::<T>` + `when is_float::<T>()`，对 `f32`、`float`、`bf16`、`f16` 同一份体：`T(exp_kernel(float(x)))`。原来每个函数的 `_f`（f32）、`_d`（f64）两个内核，合并为一个 f64 内核，f32/bf16/f16 由 f64 结果舍入一次得到。好处：C++ 里每个函数只剩一个内核；`bf16`/`f16` 原来没有的超越函数自然获得；f32 的误差由"f64 结果舍入一次"给出（在 f64 内核 ≤2 ulp 的前提下，f32 结果几乎总是正确舍入），比原来 `expf` 更稳。代价：f32 的标量超越函数多一次宽化与舍入，在 CPU 标量上可以忽略；若测量表明要紧，按 10.4 在某个函数上配 `#impl` 的 f32 内核即可，体不变。`sqrt` 与取整四个函数在 f64 里是精确的，舍入一次得到的就是正确舍入的结果。
2. **内核是原子，且只有 f64 一份**：标量数学函数符合 10.4 的第三种情形。内核是本模块私有的无体声明（`sqrt_kernel` 等），公开的泛型函数直接调用它。
3. **`min`/`max`**：浮点走 `min_kernel`/`max_kernel`（IEEE 754-2019 `minimum`/`maximum`，7.7）；`bool` 用 `&`/`|`；其余有序类型（整数、`string`）用比较，一个泛型定义。`bf16`/`f16` 宽化到 f64 精确，选出的是操作数本身，转回 `T` 也精确，所以不丢位。NaN 传播和 `-0 < +0` 由内核给出；NaN 一律是标准安静 NaN（内核负责）。
4. **数组上的逐元素数学：不在 `math` 里另写数组版本。** 同一种语义（逐元素就是 `map`）：`xs->map<exp>`（泛型函数可作回调，5.5），与标量版本是同一份定义，没有第二套规则；编译器看到 `map` 套标量原子可以融合。`tensor` 里现有的 `exp(t: float[..])` 等逐元素算子（signatures/tensor.cml）与它语义重复，是 `tensor` 模块重写时的事：保留内核、用 `#impl` 挂在 `map` 的语义上，或改成规则；本批不动。
5. **不做隐式转换**：`sqrt(int)` → `float` 的重载删除（写 `sqrt(float(n))`）；`shl`/`shr` 的 `count` 不再有 `i32` 与 `int` 两种。字面量按上下文取型，所以 `sqrt(9.0)`、`pow(2.0, 10.0)` 在 `f32` 位置里也成立。

## 函数表

| 名字 | 处理 | 理由 |
|---|---|---|
| `pi`、`e` | 改为 Camel 的 `export let` | 它们只是两个常量，不必由 C++ 导出（C++ 里的 `pi`、`e` 删掉，免得两份值）。类型 `float`；要 `f32` 写 `f32(pi)` |
| `inf`、`nan` | 保留（C++ 值，`// value`） | 在 Camel 里写不出（`1.0 / 0.0` 是常量折叠的边界情形） |
| `sqrt`、`exp`、`expm1`、`log`、`log2`、`log10`、`log1p`、`sin`、`cos`、`tan`、`asin`、`acos`、`atan`、`sinh`、`cosh`、`tanh`、`floor`、`ceil`、`round`、`trunc` | 保留；各合并为一个泛型定义 | 见上；每个一行体，内核 `<name>_d` |
| `pow`、`hypot`、`atan2` | 保留；同上，双参数 | |
| `sqrt(i32/int)` | 删除 | 隐式转换 |
| `exp(c64/c128)` | 保留，改写成 Camel 体 | `exp(re) * (cos(im) + i sin(im))`，原来的 `exp_c`、`exp_z` 内核删除 |
| `abs` | 整数与浮点合并为 `max(x, -x)` 一个泛型定义；复数两条保留（结果类型是实部类型） | `max(x, -x)` 用 IEEE maximum，清掉 `-0` 的符号，NaN 保持；最小整数的 `abs` 回绕（与 `-x` 一致），不再是"未定义"。`abs_i/l/f/d/bf/f6` 六个内核删除；复数的 `hypot(re, im)` 取代 `abs_c/abs_z` |
| `re`、`im` | 保留（原子，两条各一） | 返回的类型不是参数的类型，没有类型函数就不能写成一个泛型；`re_c/re_z/im_c/im_z` 内核保留 |
| `conj` | 保留（原子），改为两个 `when` 分臂的泛型声明 | 保留零的符号；内核 `conj_c/conj_z` 不变 |
| `min`、`max` | 保留；八条声明合并为一个泛型定义 | 见上；内核剩 `min_d`、`max_d`（原 `_i/_l/_f/_d/_s/_b/_bf/_f6` 删除） |
| `clamp` | 保留；四条合并为一个泛型定义 `min(max(x, lo), hi)` | NaN 的 x 保持 NaN（来自 IEEE `min`/`max`）；`lo > hi` 或界是 NaN 用 `fail` 报故障（用处 1）。`clamp_i/l/f/d` 内核删除 |
| `sign` | 保留；合并为一个泛型定义 | `if x > 0 then 1 else if x < 0 then -1 else x`：零与 NaN 原样返回。四个 `sign_*` 内核删除 |
| `is_nan`、`is_inf` | 保留；改为 Camel 体（`x != x`、`abs(float(x)) == inf`） | 内核删除。`is_nan` 对 `bf16`/`f16` 也成立 |
| `is_finite` | 新增 | `abs(float(x)) < inf`，常用且一行 |
| `gcd`、`lcm` | 保留；改为 Camel 体，`gcd` 用尾递归的欧几里得算法 | 原先的溢出故障保留：最小整数不接受（`gcd`，用处 2），`lcm` 乘法溢出（用处 3）。`gcd_*`、`lcm_*` 内核删除 |
| `bin`、`oct`、`hex` | **删除** | 它们是格式化，不是数学：`'{:x}'->format<n>`（需确认 `format` 的 `b`、`o`、`x` 说明；见问题 3）。原先负数按 64 位补码输出，格式化输出 `-ff`，与 Python 一致 |
| `bit_and`、`bit_or`、`bit_xor` | 保留；每名两条 `when is_same` 分臂的泛型声明（`i32`、`int`） | 每个宽度的算子名不同（`_i`、`_l`），不能是一个体；名字和签名是一个泛型 |
| `bit_not` | **删除** | 与运算符 `~` 同义（"一个意思一种写法"） |
| `shl`、`shr` | 改：`count` 进 `with`（`x->shl<4>`），只接受 `int` 的 count；每名两条分臂 | 移位量是配置，x 是数据；不再有 `i32` count 的 4 种组合。越界仍是内核的故障 `InvalidArgument` |

未新增：`erf`、`cbrt`、`asinh`/`acosh`/`atanh`、`copysign`、`fma`、`gamma`（标量原子的候选，`erf` 是精确 GELU 要用的；按需加一个 f64 内核、一行泛型体即可）。

## `pure` 与 `fail`

- 没有写任何 `pure`、`@pure`：没有要对外承诺的函数，没有回调参数。内核的纯度仍在 C++ 的 `declare`。
- `fail<'InvalidArgument'>` 共三处：`clamp`（界不合法）、`gcd`（最小整数）、`lcm`（溢出）。其余故障（`shl`/`shr` 的移位量、整数除零）来自内核或运算符。

## C++ 侧改动（modules/math）

- 内核只留：`sqrt_d`、`exp_d`、`expm1_d`、`log_d`、`log2_d`、`log10_d`、`log1p_d`、`sin_d`、`cos_d`、`tan_d`、`asin_d`、`acos_d`、`atan_d`、`sinh_d`、`cosh_d`、`tanh_d`、`floor_d`、`ceil_d`、`round_d`、`trunc_d`、`pow_d`、`hypot_d`、`atan2_d`、`min_d`、`max_d`（IEEE minimum/maximum，输出标准安静 NaN）、`re_c/re_z`、`im_c/im_z`、`conj_c/conj_z`、`bit_and/or/xor_{i,l}`、`shl_{i,l}`、`shr_{i,l}`。`sqrt_d` 的 URI 原来是不带后缀的 `math:sqrt`，此次统一为 `math:sqrt_d`。
- 删除：所有 `_f`、`_bf`、`_f6`、`_s`、`_b`、`_i`、`_l`（除位运算外）后缀的内核；`abs_*`、`exp_c/exp_z`、`clamp_*`、`sign_*`、`is_nan_*`、`is_inf_*`、`gcd_*`、`lcm_*`、`bin/oct/hex_*`、`bit_not_*`、`min_*`/`max_*`（除 `_d`）。`half.h`（半精度宽化）若只服务这些内核也可删。
- 位运算内核的 `shl`/`shr`：移位量从 `with` 视图读取（原来是第二个普通参数），名字加 `_i`/`_l` 后缀。
- `module.cpp` 的 `declare` 表与 `semantics.cpp` 的特殊值/精度登记同步缩减；`pi`、`e` 从 C++ 的导出值里删除。
- 精度的差分测试：`T(kernel(float(x)))` 对 `f32` 与旧 `_f` 内核逐点比较，误差要求在 1 ulp（f32）内，记入用例。

## 需要迁移的调用点

按 `import … from math` 或 `math::` 统计，文件数：`sqrt` 21、`abs` 20、`max` 23、`sign` 18、`exp` 11、`log` 9、`min` 8、`hypot` 7、`log1p` 6、`round` 6、`floor` 5、`clamp` 5、`is_nan` 8、`is_inf` 4、`gcd` 3、`lcm` 2、`shl`/`shr` 4/3、`bit_and/or/xor` 2/1/1、`bit_not` 1，其余各 1–3。需要实际改写的：
- `sqrt` 带整数实参的调用：`test/cases/modules/std/math/basic.cml`（`sqrt(4)`）、`test/cases/compile/native/math_sqrt_type.cml`（`sqrt('9')` 仍是类型错误）、`test/cases/modules/tiles/arithmetic_bits.cml` 等，改成 `sqrt(4.0)` 或 `sqrt(float(n))`。
- `shl(x, n)`、`shr(x, n)` → `x->shl<n>`、`x->shr<n>`：`test/cases/modules/stdlib/` 里的位运算用例与 `math_numbers.cml`，约 4 个文件；`bit_not(x)` → `~x`（1 个文件）。
- `bin`/`oct`/`hex`：没有测试用例调用（`docs/stdlib-reference.md` 里有条目，由生成器重生成）。
- `fault_clamp_bounds.cml`、`fault_clamp_nan.cml`、`fault_gcd_overflow.cml`、`fault_lcm_overflow.cml`：故障位置（报在 `fail` 所在的 Camel 函数里还是调用处）随实现变，golden 需要更新。
- `test/cases/consistency/values/max_min_special.cml`、`test/cases/modules/stdlib/math_functions.cml`、`math_numbers.cml`：值不变，按新的 `pi`/`e` 来源重跑。
- `docs/language` 与 `docs/tutorial` 里 `bin/oct/hex`、`sqrt(整数)` 的出现处；`stdlib-reference.md`、`camel-for-ai-stdlib-signatures.md` 由生成器重生成。

## 待决策者裁定的问题

1. **f32 及以下的超越函数是否都经 f64？**（推荐：是。）草稿如此；若你要 f32 走 f32 内核（GPU 上标量路径不同、或为了和 `tensor` 的 `expf` 逐位一致），需要给 `exp` 等配 `#impl` 的 `when is_same::<T, f32>()` 分臂，体不变。
2. **类型函数 `real::<T>()`：** `abs(c64): f32`、`re`、`im` 因为结果类型不是 `T` 而写成两条。若类型函数能在签名里用（"`c64` 的实部类型是 `f32`"），这三个各合成一个泛型。推荐：等类型函数落地后合并。
3. **`bin/oct/hex` 用 `format` 代替**需要 `format` 支持 `{:b}`、`{:o}`、`{:x}`、`{:X}`（及 `#`）。我没有核实；若不支持，保留一个 Camel 实现还是补 `format`，请裁定（推荐补 `format`，数字转文字只有这一个入口）。
4. **`pi`、`e` 的类型**是 `float`。写 `f32` 的代码要 `f32(pi)`；是否另给 `pi::<T>()` 这类泛型形式？（推荐：不给，`T(pi)` 已经够短。）
5. **`is_same` 依赖：** `conj` 与位运算的分臂用 `types::is_same::<T, U>()`，与 `array` 草稿同一个依赖；位运算分臂的互斥检查（`WhenDeclarationOverlap`）能否判定 `is_same::<T, i32>` 与 `is_same::<T, int>` 互斥，要编译验证。若不能，退回成固定签名的两条重载（`bit_and(a: i32, b: i32)`、`bit_and(a: int, b: int)`），这是 conventions 第 1 节原有的写法。
6. **`tensor` 的逐元素数学**与 `xs->map<exp>` 重复，处理方式（`#impl` 挂内核 / 规则 / 保留为独立内核）留给 `tensor` 的重写，本批不动；确认这个分工即可。

## 修订（决策者 2026-10-26）

- **数组上的逐元素数学放在 `math` 里**：每个一元函数（`sqrt`、`exp`、…、`trunc`、`abs`、`sign`）多一条 `::<T>(x: T[..]): T[..]`，体是 `x->map<f>`；`is_nan`/`is_inf`/`is_finite` 的数组版返回 `bool[..]`。一个函数一份语义，`tensor` 里重复的逐元素算子随 `tensor` 重写删除，只保留内核并用 `#impl` 挂在这个定义上。二元函数（`pow`、`hypot`、`atan2`、`min`、`max`、`clamp`）暂不给数组版（`zip_with` 或算术运算符的广播规则，另议）。待编译验证：`T[..]` 与 `T` 两条泛型声明在标量实参上不重叠；`map` 作用于多维 `T[..]` 时按第一维逐行还是逐元素（定义要求逐元素，需要时在体里递归或由核心的 `map` 对 `T[..]` 定义）。
- **核心改动（不在本模块）**：转换函数 `f32(xs)`、`float(xs)`、`int(xs)` 等逐元素作用于数组，与算术同一条规则；因此 `tensor` 的字符串 dtype `cast(xs, 'f64')` 删除（`statistics.cml` 头注释里提到的 `cast(xs, 'f64')` 要同步改成 `float(xs)`）。
- **`bin`/`oct`/`hex` 删除，改用 `format`**：C++ 项——`format` 的说明要支持 `{:b}`、`{:o}`、`{:x}`、`{:X}` 与 `#`，若缺则补；负数输出 `-` 号加绝对值的数字（Python 的写法），不再是 64 位补码。
- **位运算的 `is_same` 分臂**：草稿保留泛型写法。集成时的编译期检查：`WhenDeclarationOverlap` 是否判定 `is_same::<T, i32>()` 与 `is_same::<T, int>()` 互斥；若拒绝，退回每个宽度一条固定签名的重载（`bit_and(a: i32, b: i32)`、`bit_and(a: int, b: int)`，`shl`/`shr` 同，`conj(z: c64)`、`conj(z: c128)`）。
- **复数 `abs`/`re`/`im`**：保持 c64、c128 各一条的固定重载，不引入类型函数（问题 2 作废）。
- 问题 3、6 已定；问题 1（f32 经 f64）、4、5 按推荐处理，不再需要裁定。
