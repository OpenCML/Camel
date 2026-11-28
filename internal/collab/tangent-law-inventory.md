# 旧法（`Tangent<T>` = 切空间）遗留处清单

只读清单（零构建、零提交、零计划读数）。树 `D:\Projects\Camel-wt\witness`，HEAD `c12c06f527`。
背景：`lane/tensor-pow-law` 与 `lane/arg-parse-uaf-w` 上的五笔把 `Tangent<T>` 的答案改成"用户梯度"
（`typecall.cpp` 的 `tangentSpaceOf` 绑到 `gradientTypeOf`），作者自述这是有意变更、中间态会红
（`4a5bb88856` 正文："the transposed member that must answer that member is the next commit, so the two cases
pinning the answer stay red between these two commits"）。**新法认不认归决策者；本页只清点旧法遗留处。**

## 1. 正文原句（`src/stdlib/types.cml:153-162`）

```
/// The tangent space of `T` (design principles 9.9): the type a tangent of a value of `T` has. A
/// float and a tensor of floats are their own tangent space; a tuple and a struct take the tangent
/// space of each member in that member's own place; every other type -- an integer, a `bool`, a
/// `string`, a function -- takes the empty tuple, the value that carries no data and that a rule
/// ignores (JAX's `float0`).
```

四条 `@example`（逐条，标注是否随新法变）：

```
:158  @example is_same::<Tangent<float>, float>()              // true     ← 不变（float 自己就是自己的切空间，也是自己的用户梯度）
:159  @example is_same::<Tangent<f32[2, 3]>, f32[2, 3]>()      // true     ← 不变（同上）
:160  @example is_same::<Tangent<(int, float)>, ((), float)>() // true     ← **随新法变**：新法是 `(int, float)`
:161  @example is_same::<Tangent<string>, ()>()                // true     ← **随新法变**：新法下 string 无梯度，答案会是"没有"或 string 自身，取决于新法对"无梯度"的处置
```

正文里"every other type -- an integer, a `bool`, a `string`, a function -- takes the empty tuple"这一句
**整体随新法变**（新法：`Tangent<int>` 是 `int`）。

## 2. `src/stdlib/rulesets/jvp.cml:46-48` 注释

```
/// `x` lifted: the value, and the zero tangent of its type -- the empty tuple for a type that has
/// no tangent, a tensor of zeros for a tensor, and each member's own zero for a tuple or a struct.
```

配套实现的离散兜底：`jvp.cml:69  case _ => ()`（与上面注释一致；新法下这里的 `()` 就与声明 `=> Tangent<T>` 冲突）。

## 3. `docs/**` 里按旧法写的（`git grep` 原文）

`git grep -n "Tangent<" -- docs` 命中文件与条数（逐文件）：

```
docs/design/program-transforms/README.cn.md                7 处
docs/design/program-transforms/route5-one-derivative-census.cn.md  1 处
docs/design/program-transforms/vjp-parity-baseline.cn.md    2 处
docs/language/06_expressions.md                            1 处
docs/language/en/06_expressions.md                         1 处
docs/reference/camel-for-ai-stdlib-signatures.md           6 处
docs/stdlib-reference.md                                   8 处
```

其中**明确按旧法表述 `()`** 的（必须随新法改）：

```
docs/design/program-transforms/vjp-parity-baseline.cn.md:80
  | E 引擎有、库无：没有切空间的值 | 14 | 比较、离散转换、形状/标签读 | 不搬：`Tangent<i32>` 是 `()`，没有东西可搬 |
docs/design/program-transforms/vjp-parity-baseline.cn.md:161
  1. **先把 F 里的 4 行归错类**：`argmax` `argmax@1` `argmin` `argmin@1` 答的是整数索引，`Tangent<int>` 是 `()`——它们与
```

其余命中是**结构性引用**（`Tangent<T>` 出现在签名里，不述 `int` 的答案），与新旧法无关：
`README.cn.md` 的 `:27-30/:91/:124-125/:141`、`route5-one-derivative-census.cn.md:235`、
`stdlib-reference.md`（含 `:8340` 的 `x` lifted 注释转述）、`camel-for-ai-stdlib-signatures.md` 的签名行。

`git grep -niE "zero tangent|empty tuple" -- docs` 命中 10 条，其中**与旧法绑定**的 3 条：

```
docs/stdlib-reference.md:8340   `x` lifted: the value, and the zero tangent of its type -- the empty tuple for a type that has
docs/stdlib-reference.md:8322   let (y, dy) = lift(3.0);   // 3.0 and its zero tangent
docs/stdlib-reference.md:8123   tangent and a parameter's tangent is its own, so a transform lifts the first to a zero tangent
```

其余为 `.en.md`/语言语法/技术文档里的 unrelated 用法（`empty tuple` 讲语法位置、`zero tangent` 讲 ACCS 的零）。

## 4. 按旧法钉 `()` 的用例（断言原文）

```
test/plans/feat/compile/type_packs.plan.toml:109-117
  name = "type_packs.tangent_space"
  desc = "`Tangent<T>` is the type a tangent of a value of `T` has: a float and a tensor of floats are their own
          tangent space, a tuple and a struct take the tangent space of each member in that member's ..."
  backends = ["nvm", "fvm"]
  expect_stdout_exact = "true\ntrue\nfalse\ntrue\ntrue\ntrue\ntrue\nfalse\n"      ← **8 个 true/false 里钉着旧法答案**

test/plans/feat/compile/transforms.plan.toml:1697-1705
  name = "tangent_space.the_type_answers"
  expect_stdout_contains = ["TANGENT type=float space=self", "TANGENT type=f32 space=self",
    "TANGENT type=f16 space=self", "TANGENT type=bf16 space=self",
    "TANGENT type=i32 space=none", "TANGENT type=string space=none",
    "TANGENT type=float[] space=self", "TANGENT type=i32[] space=none",
    "TANGENT type=(float, i32) space=none", "TANGENT type=(float, float) space=self"]
  ← **`i32 space=none`、`i32[] space=none`、`(float, i32) space=none` 三条是旧法；新法下 i32 有"梯度类型"，这三条要变**

test/plans/feat/compile/casts.plan.toml:185-190
  expect_stdout_exact = "(1.0, ())\n(2.0, 3.0)\n(4.0, 5.0)\n"      ← **第一行 `(1.0, ())` 是旧法**（int 的切向是 `()`）

test/plans/feat/compile/transforms.plan.toml:1250
  name = "transpose_loop_escape.int_is_a_zero_tangent_space"        ← **名字与整条 desc 都按"整数是零切空间"写**，
  （desc 见该条：`x` 的切向贡献 0、截断贡献 0，整数是 zero tangent space）
```

用例侧的粗计数：`test/plans` 里含 `())` 的行 **94** 条（多数与切空间无关，如 `format`/脚本片段，本页只列与切空间断言直接相关的上面四条）。

## 5. 一句话总结：若走 (b)（认新法、补规范），要改几处

```
正文 1 处     types.cml:153-157（"every other type … takes the empty tuple" 整句）
示例 2 条     types.cml:160、:161（:158/:159 不变）
注释 1 处     jvp.cml:46-48 + 配套的 :69 `case _ => ()`
文档 3 处     vjp-parity-baseline.cn.md:80、:161；stdlib-reference.md:8340（其余为签名引用，不需改）
用例 4 条     type_packs.plan.toml:117（8 个布尔）· transforms.plan.toml:1705（3 个 space=none）·
              casts.plan.toml:190（`(1.0, ())`）· transforms.plan.toml:1250 的整条（名字+desc）
⇒ 合计 11 处（正文 1 + 示例 2 + 注释 2 + 文档 3 + 用例 4）
若走 (a)（回退到切空间），要改的是那五笔补丁本身（`typecall.cpp` 的 `tangentSpaceOf` 绑定），本页列的 11 处**不用动**。
```

## 6. 边界

- 只读：未 build、未 test、未跑任何 plan、未改文件、未提交、未推、未建枝。
- 本格为**静态清单、无计划读数** ⇒ 按 `D-1128-98` 不写"甲 0"。
- `docs/design-principles.cn.md` 9.9 的原文本格未抄（上格声明过未取）；本页判据用 `types.cml:153-162`
  的正文与四条 `@example`，它们是可执行判据。