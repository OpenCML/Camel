# std 与前奏重写说明

两个文件：
- `std.cml`：`stdlib/std.cml`，核心的遍的声明（`camel m.cml std::opt …`）。
- `prelude.signatures.cml`：换进 `stdlib/signatures/globals.cml`（不编译的参考文件），它**是前奏**——不需要导入的内建函数。`std.cml` 本身不含前奏函数，所以"前奏里该放什么"的决定落在这个文件上。

## 一、前奏该放什么

判据：几乎每个程序都要用，且不属于某个领域模块。其余都在自己的模块里。

**留在前奏**：
- 转换：`int`、`i32`、`float`、`f32`、`bf16`、`f16`、`c64`、`c128`（以及 `i64`、`f64` 两个拼写），`string`、`parse_int`、`parse_float`；
- 错误与故障：`error`、`fail`（新增）、`attempt`；
- 输出：`print`、`println`、`eprintln`，格式化 `format`、`join`；
- 序列：`len`、`range`、`last`、`contains`、`concat`、`zip`、`append`，高阶函数 `map`、`filter`、`reduce`、`foreach`；
- 映射与集合：`len`、`get`、`get_or`、`put`、`remove`、`has`、`keys`、`values`、`items`、`merge`、`add`、`union`、`intersect`、`difference`、`is_subset`、`contains`（它们是内建类型的操作）；
- 命名空间里的 `std::mem::blocks`（调试用，不占前奏的名字）。

**删除或移走**：

| 名字 | 处理 | 理由 |
|---|---|---|
| `head(xs)`、`tail(xs)` | 删除 | `xs[0]`、`xs[1:]` 就是它们；一个意思一种写法。`last` 保留，因为不支持负数下标。15 个文件迁移 |
| `slice<a, b>(xs)`（数组的两参数形式）、`slice(t, axis, start, end, step, written)`（圆括号形式） | 删除 | 重复的前奏形式。**保留一条核心的 `slice`**：`with <axis, start, end, step = 1> slice(x)`，就是下标 `x[a:b:s]` 降到的同一个节点，管道可以写 `xs->slice<0, a, b>`；它也作用于字符串、`Bytes`（取代已删的 `str::slice`）。62 个文件里的 `xs->slice<a, b>` 机械迁移为 `xs->slice<0, a, b>` 或 `xs[a:b]` |
| `extend<other>` | 删除 | 与 `concat(xs, ys)` 是同一件事；原地追加是引用计数为 1 时的性能事实（10.5），不是另一个函数。`array.flatten` 的体改成 `flat = concat(flat, xs)`。5 个文件迁移 |
| `apply<f>(xs)` | 删除 | "f 保持元素类型的 `map`"，没有独立的意思。2 个文件 |
| Set 的 `has` | 删除 | 集合问成员用 `contains`（与数组、字符串一致）；映射问键用 `has`。9 个文件检查 |
| `input(prompt)` | 移到 `io` | 读标准输入不是每个程序都做的事；`io` 已经有 `stdin` 的函数。`io.cml` 要增加 `input(prompt: string): string uses stdin, stdout`。0 个测试用例调用它 |
| `sleep`、`exit` | 已在 `process` | 批次 1 已定，前奏里的两份删除 |

**改**：
- 转换函数：每个目标类型一个泛型声明，源类型是实数或字符串（`is_real::<S>() || is_string::<S>()`），另有一条对数组逐元素的声明（`int(xs: S[..]): int[..]`）。原来 8 个目标 × 6–7 个源类型共 56 条声明合并；文件里只写出 `int`、`float` 两个，其余相同。用 `is_real`（`types` 新增）。
- `len`：文档更正为"码点数"（原写"bytes"，是错的），加 `len(Bytes)`。
- `error`：文档改成 `return error(...)`（`throw` 已删除）。
- `fail<'Kind'>(message): never`：新增的核心原语，结果类型 `never`，故障的种类是 `with` 参数（编译期常量，目录里的名字）。
- 全文删除 `@pure`、`@effect`（纯度与资源是 C++ 登记的事实，不是文档标签）。

## 二、`std.cml`（遍的声明）

| 项 | 处理 | 理由 |
|---|---|---|
| `module std` 行 | 删除；加 `import { is_function } from types` | 文件名即模块名；`is_function` 原来没有导入 |
| 路径参数 `json`、`stats`、`effect_trace` 的默认 `""` | 改为 `string? = null` | `""` 表示"不写"是哨兵值；不存在用 `T?` |
| 字符串引号 | 双引号改单引号 | 与库的其余部分一致 |
| `arraytype` | 删除 | 它"打印数组实现的自检"，是测试钩子，不是遍。`test/plans/feat/compile/type_array.plan.toml` 里的用例改成直接的单元测试 |
| `gir_regions`、`rgir_regions` 的说明 | 更正 | 原写"boundary roles"，其实是把区域画成簇（角色标注是 `agir`/`argir`）。四个遍都保留，它们不重复 |
| `order` 的说明 | 去掉"控制边仍被消减" | 设计原则 2.1/3.1：控制边这个种类不存在 |
| 其余（`nop`、`macro`、`gir`、`rgir`、`jgir`、`cml`、`agir`、`argir`、`stats`、`profile`、`cpp`、`topo_node_seq`、`tfdump`、`cpp::*`、`gc::*`、`nvm`、`fvm`、`fastvm::*`、`taskflow`、`opt`、`opt::*`） | 保留 | 声明与 C++ 注册的内核一一对应（`registerCoreKernel`）；`opt::small/arm/hybrid` 是内联策略的三个独立遍，已有别名 `std::opt::inline::small` 等，不动 |

遍的参数（`with`）是命令行 `key=value` 的来源（9.3），没有 `fail`、`!->`、`pure`。遍的签名 `(f: F): F!` 里的 `!` 是错误值（遍失败），与 `?->`/`!->` 的拆分无关。

### C++ 侧

- `src/execute/pass.cpp`：删除 `arraytype` 的内核登记；`profile`、`fvm`、`jit` 的 `json`、`stats`、`effect_trace` 读为可空字符串（`null` 即不写）。其余不动。
- 参数解析：命令行上 `json=out.json` 仍读为字符串；省略即 `null`。

## 三、C++ 侧（前奏）

- 删除内建：`head_arr`、`tail_arr`、`slice_arr`（函数形式；下标节点的内核 `:op/slice_sub` 保留，下标语法用它）、`extend_arr`、`apply_arr`、Set 的 `has`、`input`（移到 `io` 模块的执行器）。
- 新增：`:op/fail`；转换算子对数组逐元素（一个节点，非 `map`）；`len(Bytes)`（批次 1 已列）。
- `diagnostics`：未知名字 `head`、`tail`、`slice`、`extend`、`apply` 成为普通的"未知名字"，不留指向新写法的提示。

## 四、迁移

`head`/`tail`（15 个文件）、`slice`（62）、`extend`（5）、`apply`（2）、Set 的 `has`（9）、`arraytype`（1）；文档里这些名字的出现处（`docs/language`、`docs/tutorial`、`docs/reference`）；`stdlib-reference.md` 与 `camel-for-ai-stdlib-signatures.md` 由生成器重生成。

## 待决策者裁定的问题

1. （已定）核心 `slice` 保留一条，见上；原问题：删除数组的 `slice` 函数形式。（推荐：删，与字符串、`Bytes` 一致。）代价是流水线里的 `xs->f->slice<1, 3>` 要写成 `(xs->f)[1:3]`；若你要保留流水线写法，保留 `slice<start, end>` 一条、只删 `slice(t, axis, …)`。
2. （已定）`concat` 留，`extend` 删。
3. **`input` 放 `io`。**（推荐：是。）它同时用到 `stdin` 和 `stdout`（提示符），说明里写明。
4. （已定）保留 `i64(x)`、`f64(x)`：转换函数跟着每个存在的类型名；`i64` 与 `int` 是否都作类型名另议。原问题：**`i64`、`f64` 作为 `int`、`float` 的另一个拼写。** 类型位置 14 个文件在用；转换函数也有 5/3 个文件。推荐：保留类型拼写，但转换函数只留一个名字（`int(x)`、`float(x)`），把 `i64(x)`、`f64(x)` 迁成 `int(x)`、`float(x)`（"一个意思一种写法"）。草稿的前奏文件里没有再列这两个函数。
5. **转换函数的源类型条件**用 `is_real` 加 `is_string`；复数到实数的转换没有（取 `re`/`im`），复数目标 `c64`/`c128` 的源再加 `is_complex`。确认这条规则。
6. **`?->` 与 `!->`**：本批没有任何一处用到错误传播的 `?->`（`std`、`types`、`tree`、`stream`、`this` 都不涉及），待负责人定了不需要机械改动。

## 修订（决策者 2026-10-26）

- 核心 `slice` 留一条（见上），前奏里其余的 `slice` 形式删除；`prelude.signatures.cml` 里的条件 `is_sliceable::<T>()`（数组、`string`、`Bytes`）是 `types` 需要加的一个谓词（或写成三条 `when` 分臂）。
- `extend` 删，`concat` 留。
- `i64(x)`、`f64(x)` 保留（转换函数跟随每个存在的类型名）；前奏文件里只写出 `int`、`float`，其余目标同形。
- **核心规则（跨模块同名）**：不同模块里同名、类型不同的函数（前奏的数组 `map`、`stream` 的流 `map`、`bytes` 的 `len`）在两者都可见时构成**一个重载集**；只有真正有歧义的调用才是错误，不存在遮蔽。这是核心的重载解析规则，不是每个模块各自处理。
