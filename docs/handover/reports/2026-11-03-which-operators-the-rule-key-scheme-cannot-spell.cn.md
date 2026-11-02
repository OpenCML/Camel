# 键方案拼不出来的算子：171 条里 129 条，32 条今天不可能有律

> 2026-11-03 02:20，`lane-tloop`。这一页是 `jvp_ltod` 那一笔的余量：`ltod` 不是个别问题，它是"键方案"（`camel/core/forward.h#ruleKeyOf`）的缝里漏出来的一个样本。下面把缝量到底，并把 `^` 那条补掉（§3.5）。

## 1. 怎么量的

三条读数，不靠印象：

1. **映射表**：`ruleKeyOf(family, uri)`（`src/core/forward.cpp`）只有三条分支——先查例外表（`kRuleKeyExceptions`），再对 `:op/` 名字取 `operationOf`（第一个 `_` 之前）去 `kWrittenOperations` 里找**写法**（`add→+`、`sub→-`、`mul→*`、`div→/`、`neg→-`、**`pow→^`**），最后对模块算子（非 `:op/`）用 `nameOf(uri)` + `arityOfHead(family, name)`。`:op/` 名字走到表里找不到，就 `return std::nullopt`，注释原话 **"No key, no guess"**。
2. **算子清单**：`#op(':op/…')` 在 `stdlib/prelude.cml` 里 **171 条**（去重后）；再用 `git grep -oh ':op/[a-z_0-9]*' stdlib/` 补齐 prelude 没有声明的内部算子。
3. **结果有没有切空间**：按 `tangent_space.the_type_answers` 钉的口径——`float`/`f32`/`f16`/`bf16` 与它们的数组是自成一格，整数、`bool`、`string`、`Bytes`、`never`、以及含整数的元组**一格都没有**。这一栏是判断"这条要不要律"的唯一依据：**结果没有切空间 ⇒ 变换永远不问它 ⇒ 没有键也不是缺口。**

结论先给：**(甲) 42 条能拼，(乙) 129 条拼不出**；129 条里 **73 条按设计不需要律**、**6 条走例外表或键已补（2 例外 + 4 幂）**、**32 条结果有切空间却今天不可能有律**、**18 条判不了**（复数）。32 ≫ 3，所以有这一页。

## 2. (甲) 能拼的：42 条，就是六个 token 的各类型后缀

```
+  : add_f add_d add_i add_l add_f6 add_bf add_c add_z add_s add_bytes
-  : sub_f sub_d sub_i sub_l sub_f6 sub_bf sub_c sub_z          neg_f neg_d neg_i neg_l neg_f6 neg_bf neg_c neg_z
*  : mul_f mul_d mul_i mul_l mul_f6 mul_bf mul_c mul_z
/  : div_f div_d div_i div_l div_f6 div_bf div_c div_z
^  : pow_d pow_f pow_f6 pow_bf pow_i pow_l pow_c pow_z pow_c_i pow_z_i
```

注意这条分支的一个副作用：**键只看名字的第一个词**，所以 `add_s`（string 相加）、`add_bytes`、`add_c`（复数）、`pow_i`（整数幂）也能拼出键，只是它们的律对 `is_float`/`is_numeric` 有约束，于是"键存在、律对不上"是另一条**已被命名的**拒绝路径（`the ruleset holds no rule for this operator and operand count`），不是静默。**能拼 ≠ 有律**，但至少可问。

## 3. (乙) 拼不出的：129 条，四栏

### 3.1 结果没有切空间 ⇒ 没人问（73 条，按设计，不是缺口）

| 结果类型 | 条数 | 算子 |
|---|---|---|
| `bool` | 43 | `not and or`、`eq*`（12 个）、`lt_* le_* gt_* ge_*`（各 7） |
| `i32` | 10 | `inv_i mod_i`、`*toi`（`itoi ltoi ftoi dtoi stoi bftoi f6toi`） |
| `int` | 10 | `inv_l mod_l`、`*tol`（同上 7 个） |
| `string` / `Bytes` / `Error` / `never` / `int!` | 7 | `atos idx_str slice_str slice_bytes error_new fail parse_int` |
| 无切空间的数组 | 3 | `atoi atol`（`WithElement<A,i32>` / `WithElement<A,int>`）、`mem_blocks`（全 int 的结构） |

`pow_i`/`pow_l` 现在有了键（`^/2`），律的 `is_float` 约束对不上，但它们的结果是整数、没人问，所以同样落在这一栏的实际效果里。

**现在怎么办**：什么都不用办。`jvp.cpp` 收集一条律的实参时会 `continue` 掉没有切空间的操作数；一个**结果**没有切空间的节点，它的切量也没有人读。证据在同一段代码的另一半：`int_escape` 里的 `int(3.0x)`（`:op/dtol`）从来没被问过——`g(x)=float(int(5.0x))+x` 的 lift 读数 `rules=jvp_mul,jvp_ltod,jvp_add` 里没有 `dtol`。

### 3.2 走例外表（2 条，已绑，律在）

| uri | 键 | 律 |
|---|---|---|
| `:op/idx_arr` | `index_into/2` | `jvp_idx_arr`、`idx_arr_t`、`vmap_rule_idx` |
| `:op/ltod` | `float_of_int/1` | `jvp_ltod`（2026-11-03 新增） |

**现在怎么办**：例外表 `kRuleKeyExceptions` 里各占一行，`std::rulesets forward=true` 每次运行都打印。

### 3.3 结果有切空间、今天**不可能有律**（32 条）——这就是缺口

| 族 | 条数 | 算子 | 为什么拼不出 |
|---|---|---|---|
| 转成 double，`float(x)` | 6 | `dtod ftod itod stod bftod f6tod` | `float` 在一操作数上是**所有**转 double 的转换共用的写法，一个键不能同时是 8 条 uri 的律 |
| 转成 `f32`，`f32(x)` | 7 | `bftof dtof f6tof ftof itof ltof stof` | 同上 |
| 转成 `f16`，`f16(x)` | 6 | `dtof6 f6tof6 ftof6 itof6 ltof6 stof6` | 同上 |
| 转成 `bf16`，`bf16(x)` | 6 | `bftobf dtobf ftobf itobf ltobf stobf` | 同上 |
| 数组转换 | 4 | `atod atof atobf atof6` | 同上（`WithElement<A, float>` 等） |
| 可失败转换 | 1 | `parse_float`（`float!`） | 名字不是 `<源>to<目的>` 的形状，且是可失败的 |
| 数组取/切片 | 2 | `idx_take slice_sub` | 无 token（`t[idx]`、`xs[a:b]`），且没进例外表 |

### 3.4 判不了（18 条，⚠️ 单列）

```
c64 : ctoc dtoc ftoc itoc ltoc pow_c pow_c_i pow_c_l ztoc
c128: ctoz dtoz ftoz itoz ltoz pow_z pow_z_i pow_z_l ztoz
```

**判不了的原因**：复数有没有切空间，我没有读到任何钉住它的读数——`tangent_space.the_type_answers` 只钉了 `float f32 f16 bf16 float[] i32 string i32[] (float,i32) (float,float)` 十行，`c64`/`c128` 不在其中，`tangent_space.h` 的注释我也只读到"float 与它的数组自成一格、整数与字符串没有"。**若复数有切空间，这 18 条并入 3.3**（缺口变 50 条）；若没有，并入 3.1。另有一类我按"有切空间"保守记进 3.3，但保留疑问：`dtod`/`ftof`/`bftobf`/`f6tof6` 这类**同型转换**（编译器可能把它折掉，于是永远不被问）。

### 3.5 今天修好的一条：`^`（求幂），四条 uri

`pow_d pow_f pow_f6 pow_bf` 之前在同一栏里，实测拒绝是：

```
REINTERP jvp function=p skip=rule uri=:op/pow_d reason=the operator has no forward rule bound at all
```

改动两步，都在本树：

1. `kWrittenOperations` 添一行 `{"pow", "^", 2}`（5 条 → 6 条）。**没有加例外**——`^` 是写出来的算子，键本来就该推得出来，`pow` 只是漏列。
2. `stdlib/rulesets/jvp.cml` 添 `jvp_pow::<T>(x, w) when is_float::<T>()`，右式 `(dx, dw) => pow_tangent(x, w, dx, dw)`；`pow_tangent` 就在同一个文件里（`w * x^(w-1) * dx + x^w * log(x) * dw`），**没有**放进 `stdlib/autodiff.cml`。

   **为什么放在律旁边而不是和 `add_tangent` 它们一起**（这一条是量出来的，不是偏好）：它要 `log` 和 `pow`，那是 `math` 的；而 `autodiff` 一旦多这个依赖，**每个声明了 `autodiff` 的程序的 profile 报告会多出 `math` 的编译阶段行**。实测代价：`fusion_group.profile.none` 直接从绿变红（"the report is the recorded report, byte for byte apart from the measured numbers"），因为那条用例的 golden 里记着每个模块的 `resolve/parse/gct/declare` 行；把依赖挪进 `jvp.cml` 之后，该计划 **17/17 恢复绿**。规则集是"想要这条律的程序才去 import 的地方"，代价就落在那儿。

**四条 uri 一条律**（和 `add_d`/`add_f` 共用一个键是同一件事）：键都是 `^/2`，律是 `is_float::<T>()` 泛型。

**读数**（`jvp(f)(x, 1.0)`，本树，Release）：

| 程序 | 纸上 | 读数 |
|---|---|---|
| `p(x) = x ^ 2.0`，`x = 3` | `p = 9`，`d = 2x = 6` | `9.0` / **`6.0`** |
| `p(x) = x ^ 2.0`，`x = -3` | `p = 9`，`d = 2x = -6` | `9.0` / **`-6.0`**（指数项是零切量，`ln(-3)` 不被求值） |
| `q(x) = x ^ 2`（整数字面量指数），`x = 3` | `d = 6` | `9.0` / **`6.0`**，`rules=jvp_pow`（**无 `jvp_ltod`**：整数字面量不生成转换节点） |
| `s(x) = x ^ x`（变量指数），`x = 3`，`dx = dw = 1` | `x^x(ln x + 1) = 27 × 2.0986 = 56.66` | `27.0` / **`56.66253179403897`**，`rules=jvp_pow` |
| `s(x) = x ^ x`，`x = -3`，`dx = dw = 1` | 值 `(-3)^(-3) = -1/27` 是实的；**对指数求导无定义** | `-0.037037037037037035` / **`nan`** |

**"无切量"与"零切量"必须分开**（这是作者 2026-11-03 的裁定，也是这一笔最后改对的地方）。指数的项是 `x^w · ln(x) · dw`：

- **`dw` 为零**（指数是常量）：一项乘以零，**真值就是 0**，`ln(x)` 根本不该被求值。实现里常量的切量是**零常量**（`jvp.cpp` 的 `zeroConstantFor`："the tangent a constant gets: the zero of the value's own type"），所以 `dw = 0` 是**零切量**而不是"没有切量"——这也意味着"该项不存在"在律的实参层面**不可能发生**（`Tangent<float>` 是 `float`，浮点常量的切量一定被传进来）。
- **`dw` 非零且 `x <= 0`**：`ln(x)` 无实值 ⇒ **让它 NaN**。NaN 是诚实的"无定义"，0 是假的"零"。

所以 `pow_tangent` 里是 `if dw == T(0) then T(0) else pow(x, w) * log(x) * dw`：**零只在"这项本来就是零"的地方取**，不是"x 小于零就把无定义写成零"。上一版那种 `x <= 0 ⇒ 0` 的特例是**错数**（对 `x^x` 在 −3 会答 0），已删除；也不存在第二个特例。

⚠️ 停手条件 (ii) 的答案：**`x ^ 2` 里的整数字面量不走别的算子**——它不生成转换节点，lift 只看到 `:op/pow_d` 一个算子（`rules=jvp_pow`，无 `jvp_ltod`），指数按浮点常量参与。所以没有"整数幂走 `pow_l` 再转回 float"这种事需要处理。

---

## 4. 32 条缺口的逐行清单（名字 + 目的地构造子）

| # | uri | 目的地 | 写法 | 源有没有切空间 |
|---|---|---|---|---|
| 1 | `:op/dtod` | `float` | `float(x: float)` | 有 |
| 2 | `:op/ftod` | `float` | `float(x: f32)` | 有 |
| 3 | `:op/bftod` | `float` | `float(x: bf16)` | 有 |
| 4 | `:op/f6tod` | `float` | `float(x: f16)` | 有 |
| 5 | `:op/itod` | `float` | `float(x: i32)` | 无 |
| 6 | `:op/stod` | `float` | `float(x: string)` | 无 |
| 7 | `:op/dtof` | `f32` | `f32(x: float)` | 有 |
| 8 | `:op/bftof` | `f32` | `f32(x: bf16)` | 有 |
| 9 | `:op/f6tof` | `f32` | `f32(x: f16)` | 有 |
| 10 | `:op/ftof` | `f32` | `f32(x: f32)` | 有 |
| 11 | `:op/itof` | `f32` | `f32(x: i32)` | 无 |
| 12 | `:op/ltof` | `f32` | `f32(x: int)` | 无 |
| 13 | `:op/stof` | `f32` | `f32(x: string)` | 无 |
| 14 | `:op/dtof6` | `f16` | `f16(x: float)` | 有 |
| 15 | `:op/f6tof6` | `f16` | `f16(x: f16)` | 有 |
| 16 | `:op/ftof6` | `f16` | `f16(x: f32)` | 有 |
| 17 | `:op/itof6` | `f16` | `f16(x: i32)` | 无 |
| 18 | `:op/ltof6` | `f16` | `f16(x: int)` | 无 |
| 19 | `:op/stof6` | `f16` | `f16(x: string)` | 无 |
| 20 | `:op/bftobf` | `bf16` | `bf16(x: bf16)` | 有 |
| 21 | `:op/dtobf` | `bf16` | `bf16(x: float)` | 有 |
| 22 | `:op/ftobf` | `bf16` | `bf16(x: f32)` | 有 |
| 23 | `:op/itobf` | `bf16` | `bf16(x: i32)` | 无 |
| 24 | `:op/ltobf` | `bf16` | `bf16(x: int)` | 无 |
| 25 | `:op/stobf` | `bf16` | `bf16(x: string)` | 无 |
| 26 | `:op/atod` | `float[]` | `float(xs)`（`WithElement<A,float>`） | 随元素 |
| 27 | `:op/atof` | `f32[]` | `f32(xs)` | 随元素 |
| 28 | `:op/atof6` | `f16[]` | `f16(xs)` | 随元素 |
| 29 | `:op/atobf` | `bf16[]` | `bf16(xs)` | 随元素 |
| 30 | `:op/parse_float` | `float!` | `parse_float(text)` | 无（字符串） |
| 31 | `:op/idx_take` | `float[]` | `t[idx]`（取 slab） | 随元素 |
| 32 | `:op/slice_sub` | `float[]` | `xs[a:b]` | 随元素 |

第 1–25 行是**同一个形状**：一个写法、一个目的地、若干源。第 26–32 行各自有形状（数组转换、可失败解析、两个没有 token 的数组算子），不能靠同一条规则救。

## 5. 修法二选一：两栏代价

| | **(a) 每 uri 一行例外** | **(b) 让 `ruleKeyOf` 从 `<源>to<目的>` 推键** |
|---|---|---|
| **例外表** | 3 → 35 行 | 3 → 6 行（`parse_float`、`idx_take`、`slice_sub` 推不出，仍需例外） |
| **覆盖** | 32 / 32 | 29 / 32（25 条标量 + 4 条数组；`a` 当源字母也能推） |
| **新律条数** | 20 条标量律（4 目的地 × 「1 条收全部无切空间源 + 4 条有切空间源」）+ 4 条数组律 | **同样 20 + 4**——键自明不减少律 |
| **core 改动** | `kRuleKeyExceptions` 添 32 行（纯数据） | `ruleKeyOf` 添一条推导分支：解析 `<源>to<目的>`、8 个类型字母（`a bf c d f f6 i l s z`）、再查目的地构造子的 1 参键 |
| **以后每加一条转换** | 一行例外（+ 可能的律） | 零行 | 
| **风险** | 表长、逐条过期、`float` 一个键对 8 条 uri 要靠例外区分 | 名字形状是**弱约定**（`atod` 的 `a` 是"数组"，`parse_float` 根本不是这个形状）；推导错时是**静默错键**，而不是拒绝 |

**为什么律省不下来（两栏共同的硬约束）**：一条律右式的实参个数 = 它有切空间的操作数个数。`dst(x)` 的源可能是 `int`（无切空间 ⇒ 0 个实参 ⇒ 答 `0.0`）、也可能是 `float`（1 个 ⇒ 答 `dst(dx)`），**同一个键下这两件事写不进一条律**（泛型 `T` 的实参表在一条律里是固定的）——`jvp_ltod` 钉 `is_same::<T, int>()` 正是这条限制的产物，而今天那条 `jvp_pow` 能用一条律盖四个 uri，恰恰是因为它的两个操作数**都有**切空间、实参个数不随类型变。

## 6. 一句话结论

**这条缝是方案性的，不是少数特例**：按"结果有切空间却拼不出键"计，**32 条**（把复数算进去 50 条），而例外表只能一条一条地救——今天救了两条、补了一条（`^`）。缺口的形状有两个：

1. **转换族没有键**：`<源>to<目的>` 的名字里其实**写着**目的地（`dtod` 是 d→d、`itof` 是 i→f、`ltod` 是 l→d），而键只认 `operationOf` 拿到的第一个词，于是 29 条转换全被挡在门外。两条修法见 §5，**都是设计决定，不是我这边的改法**。
2. **`kWrittenOperations` 漏列**：`^` 曾是写出来的算子、结果是 float，却不在表里（**今天已补**）；`%`、`==`、`<`、`&&` 也不在，但它们的**结果没有切空间**，所以不要紧。`[]` 在数组上是例外表，在字符串上没人问。这一条是三者里最便宜的：一行表 + 一条律。

## 7. 顺带修的三处

1. `test/cases/compile/rulesets/rulesets_lookup.py` 的断言从"两条"变"三条"，旁边加了一行注释说明为什么（`:op/ltod` 走例外表绑到 `float_of_int/1`，因为 `float` 在一操作数上是所有转 double 转换共用的写法），免得下一个人以为有人为了让用例绿而放宽了断言。**补 `^` 没有再加例外，表仍是 3 条**。
2. `test/plans/feat/compile/transforms.plan.toml` 里两条钉 `GraphId` 的用例（`reinterp_builds_its_own_graph` 的 `closure graph=107`、`jvp_is_a_function_value` 的 `built graph=56`）**改成了 286 / 236**：这个 id 是**按位置编的**（pass 用一个计数器给建出来的图编号，ruleset 里的律图先消耗号段），所以往 jvp 族里加一条律，后面的 id 全部平移。两条钉子旁边各加了一行注释说明这件事，免得下一个人以为这是"为了绿而改数"。
3. 两份**生成物**随 `pow_tangent` 重生：`docs/stdlib-reference.md`（`node scripts/stdlib-reference.mjs --write`）与 `docs/reference/camel-for-ai-stdlib-signatures.md`（`node scripts/camel-for-ai.mjs --write`）。不做的话 `registry.reference_is_generated` 与 `docs.ai_reference.current` 两条用例会红——它们是"生成物必须等于生成它的脚本的产物"，我当时就是被这两条逮住的。

4. **一个不可信的数**：这一轮里有一次全量报出 `31 失败 / 2 killed`，其中 17 条是 `module_import_*`、`simplify_*`、`fusion_group` 这种与本次改动无关的用例，`got exit 127` 与 `SIGBUS (rc=135)`——远端 `out/latest` 被并发构建换掉了（`camel-remote.ps1` 在共享目录 `/home/zjwei/camel-work/tloop` 上跑，两个 run 撞在一起就会出现这个）。**单独重跑即清**（14 红）。看到 `127`/`SIGBUS` 就重跑，别去查代码。