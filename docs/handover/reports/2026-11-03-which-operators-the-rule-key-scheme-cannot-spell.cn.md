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

## 5. 修法二选一：两栏代价（**(b) 经复核不成立（§5.1）；(b') 键侧成立、律侧不成立（§5.2），出路见 §5.2 末表**）

| | **(a) 每 uri 一行例外** | **(b) 让 `ruleKeyOf` 从 `<源>to<目的>` 推键** |
|---|---|---|
| **例外表** | 3 → 35 行 | 3 → 6 行（`parse_float`、`idx_take`、`slice_sub` 推不出，仍需例外） |
| **覆盖** | 32 / 32 | 29 / 32（25 条标量 + 4 条数组；`a` 当源字母也能推） |
| **新律条数** | 20 条标量律（4 目的地 × 「1 条收全部无切空间源 + 4 条有切空间源」）+ 4 条数组律 | **同样 20 + 4**——键自明不减少律 |
| **core 改动** | `kRuleKeyExceptions` 添 32 行（纯数据） | `ruleKeyOf` 添一条推导分支：解析 `<源>to<目的>`、8 个类型字母（`a bf c d f f6 i l s z`）、再查目的地构造子的 1 参键 |
| **以后每加一条转换** | 一行例外（+ 可能的律） | 零行 | 
| **风险** | 表长、逐条过期、`float` 一个键对 8 条 uri 要靠例外区分 | 名字形状是**弱约定**（`atod` 的 `a` 是"数组"，`parse_float` 根本不是这个形状）；推导错时是**静默错键**，而不是拒绝 |

**为什么律省不下来（两栏共同的硬约束）**：一条律右式的实参个数 = 它有切空间的操作数个数。`dst(x)` 的源可能是 `int`（无切空间 ⇒ 0 个实参 ⇒ 答 `0.0`）、也可能是 `float`（1 个 ⇒ 答 `dst(dx)`），**同一个键下这两件事写不进一条律**（泛型 `T` 的实参表在一条律里是固定的）——`jvp_ltod` 钉 `is_same::<T, int>()` 正是这条限制的产物，而今天那条 `jvp_pow` 能用一条律盖四个 uri，恰恰是因为它的两个操作数**都有**切空间、实参个数不随类型变。

### 5.1 复核 (b)：一条键只放一条律，所以"从名字推键"建不起来（实测）

作者 2026-11-03 选了 (b)（例外 3→6、覆盖 29/32、律仍 20 条），并要求先答"这个名字可信吗"。答：**名字作为数据是可信的，但从名字推出来的键装不下 20 条律**——(b) 的算术假设了"键能编码源类型"，而键是**拼写**。

**名字是谁写的**：`#op(':op/<name>')` 是写在每条声明上方的**手写标注**（`stdlib/prelude.cml:643` 起），不是从类型推出来的。而且 `:op/dt od` 这一条 uri 在 `float(x: float)` 与 `f64(x: float)` 上**各写一次**——同一个 uri 对应两种拼写；反过来，拼写 `float(x)` 在**7 条**源类型声明上重复出现。没有任何测试检查字母与类型是否一致（`--verify-ops` 检查的是"uri 在注册里存在"，不是"`ltod` 就是 int→double"）。所以"名字可信"只在"它是人写的、有意的"这个意义上成立，它不携带机器可校验的源类型。

**从名字推键之后会怎样（关键读数）**：把 32 条缺口按"它们各自需要的那条律的写法定键"归一下类（脚本按 `stdlib/prelude.cml` 的 `#op` + 声明统计）：

- prelude 里带转换 uri 的声明 **75 条**，转换 uri **59 个**，拼写（函数名）**11 个**，目的地类型 **4 个**（`float f32 f16 bf16`，外加复数 `c64 c128` 未算）；
- 32 条缺口里 **29 条**在 prelude 里（每条 `#op` 都写在声明上方），按"它那条律必须定在哪个键上"归属：

      f32/1   <-  atof bftof dtof f6tof ftof itof ltof stof        8 条
      f16/1   <-  atof6 dtof6 f6tof6 ftof6 itof6 ltof6 stof6       7 条
      bf16/1  <-  atobf bftobf dtobf ftobf itobf ltobf stobf       7 条
      float/1 <-  atod bftod dtod f6tod ftod itod stod             7 条
      f64/1   <-  atod bftod dtod f6tod ftod itod stod             同 7 条（`f64` 是同一 uri 的第二种拼写）

  （剩下 3 条 `parse_float`/`idx_take`/`slice_sub` 不是转换形状。）**29 条缺口只落在 5 个键上**，而它们至少需要 **20 条标量律 + 4 条数组律**——`float/1` 一个键要同时装 `itod`（源无切空间，0 实参）、`dtod`（1 实参）、`atod`（数组，1 个数组实参）三种形状。**5 个键放不下 24 条律。**

**代码里的硬约束**（两处，同一句话）：`include/camel/parse/ast/rule_head.h:101-105` 的 `indexRule`——"The name of a *different* rule already filed under that key is returned -- the caller reports the duplicate, and **the two are never silently merged**"；`src/core/forward.cpp:57` 把同一句话当作 `tensor:multiply` 那条例外的**理由**——"`*/2` is already `:op/mul_d`'s key and **one key holds one rule**"。

**所以 (b) 的两种结局都不好**：说 `float(x)` 的律只能有一条，其余 5 条在装载时按重复键报错（rulesets 直接红、家族建不起来）；若真按"先到先得"放过去，`dtod`（源有切空间，实参 1 个）会被派到 `ltod` 那条零实参的律上——**实参个数不符**，正是本分支早先撞过的 `Runtime indirect call norm-arity mismatch`。这不是"缺规则"，是"套错规则"，比拒绝更糟。

**三项候选（等作者裁）**：

| | 做法 | 例外表 | core 改动 | 代价 |
|---|---|---|---|---|
| (a) | 每 uri 一行例外（已写进 §4 的 32 行） | 3 → 35 | 纯数据 32 行 | 表长；但每条都写着"为什么"，且 20 条律全部可以用具名调用写（`float_of_int(n)` 那样） |
| (b') | **键 = 拼写 + 操作数类型**：`indexRule` 允许同一拼写下多条律，靠各自的 `when` 约束（`float::<T>(x: T) when is_same::<T, float>()`）分派；`lookupRule` 收操作数类型 | 3 → 2 | 索引身份改变（重复判定要改成"约束真重叠才算重"）+ 约 8 处调用点传类型 | 20 条律终于写得下；代价是律的约束写错会变成**静默套错律** |
| (c) | **键 = 算子 uri 本身**：`ruleKeyOf` 整张表与 3 条例外全部消失，律自己声明它给哪些 uri | 3 → 0 | 规则集语法 + 索引重定键 + 现有约 100 条律要带 uri | 最彻底，也最大 |

**(b') 是 (b) 的真身**：它保留"从名字推"的便宜（例外 3→2），把省不下的那条硬约束（20 条律）放到索引里解决，而不是假装它不存在。**我不擅自改索引身份**——那是设计决定；这一节只把"选 (b) 会撞到什么"钉在纸上。
### 5.2 复核 (b')：键能带类型，但**律不能声明具体参数类型**（实测，已回退）

作者改裁 (b')（键 = 拼写 + 操作数类型，索引按 `when` 约束分派）。按"先答、再改、再量"走了三步：

**① 新键的形状（先答，答得出来）**：`:op/dt od` → `float@1#float`，`:op/itod` → `float@1#i32`，两者**不同** ✓。推导表（`d`→`float`、`f`→`f32`、`f6`→`f16`、`bf`→`bf16`、`i`→`i32`、`l`→`int`、`s`→`string`；`a` 不给签名，因为数组重载的参数类型是类型变量 `A`，它的律按拼写归档）。停在①的停手条件没有触发。

**② 实现**（已写、已编译、已跑）：`RuleHead` 添 `paramTypes`（声明处读 `x: float` 的类型拼写），`indexRule` 对转换拼写与全具体类型的律按 `head@1#<类型>` 归档，`lookupRule` 先查签名再查拼写，`arityOfHead` 跳过带 `#` 的键；`ruleKeyOf` 添 `conversionKeyOf`（名字 → 键）；`:op/ltod` 的律从 `float_of_int(n)` 改成 `float(n)`，参数声明为 `n: int`；例外表 **3 → 2** ✓（`rulesets` **67/67**，没有掉条数、没有重复键报警）。**②③ 停手条件都没触发。**

**③ 量出来的硬阻塞：律不能声明具体参数类型。** 改完后 `transforms` 从 48/61 变成 46/61，多出的两条不是图号平移，而是这一行：

```
REINTERP jvp function=rulesets.jvp::jvp_ltod__lhs skip=rule uri=:op/ltod
    reason=the rule is named in the index but its templates are not registered
```

`jvp.cpp:110-121` 在一个**模板表**（`genericRuleTable`）里按律名找 id，找不到就拒绝。只有**带类型参数**的律才会进那张表——`export rule jvp_ltod(n: int)` 没有 `::<T>`，所以它在索引里（键 `float@1#int` 找到了）却**展开不了**。于是 (b') 的键侧成立、**律侧不成立**：要让键带上 `int`，律就得写 `(n: int)`；要能被展开，律就得写 `::<T>` ⇒ 两者互斥。

**两条出路（等作者裁）**：

| | 做法 | 代价 | 风险 |
|---|---|---|---|
| (b'-1) | 签名不从参数读，而从**条件**读：律保持 `::<T> when is_same::<T, int>()`，`RuleHead` 再捕获 when 子句文本，索引把 `is_same::<T, X>` 里的 `X` 当签名 | 一处文本解析；律的形状不变 | 条件的一种写法变成键（`is_same` 换个写法就推不出） |
| (b'-2) | **一个键放多条律**：`indexRule` 每个键存一个 vector，`lookupRule` 返回候选表，三个 transform 依次试到"这个类型有实例"为止。引擎**已经**按操作数的元素类型实例化律（`genericRuleInstance(context, ruleId, {elementType}, {})`），类型不符时它给的是**具名拒绝**（`no instance of the rule at float`），不是错律 | `indexRule`/`lookupRule` 改成 vector + `jvp/vmap/transpose` 三处改成循环 | 无：键**完全不需要推导**，`float` 就是一个键，32 条缺口**一条例外都不用加**，例外表自动回到 2 |

**(b'-2) 是 (b') 的真身，而且比 (b') 更小**：键不需要带类型（`float@1` 就够），因为类型分派是引擎已有的能力，不是键要解决的问题。**我按"不许留下一个红的树"回退了这一版**（`git checkout` 七个文件，`rulesets` 回 **67/67**、`transforms` 回 **48/61**、例外表回 **3**），把量到的东西写在这里，等点名再动。
### 5.3 落 (b'-2)：一个键放多条律，类型分派留在引擎里（已落地；**"引擎已有分派"这一句在 §5.4 被实测推翻**）

作者裁 (b'-2)。落地的形状比 (b') **更小**，因为键不再编码类型：

| 文件 | 改动 |
|---|---|
| `include/camel/parse/ast/rule_head.h` | `RuleHead` 添 `condition`（律的条件文本）；新 `lookupRules(family, head, arity)` |
| `src/parse/ast/rule_head.cpp` | 索引从 `map<key, string>` 变成 `map<key, vector<RuleSlot>>`（`RuleSlot` = 律名 + 条件）；`indexRule` 追加而不是拒绝——**同键同条件才是重复**（`DuplicateRuleForOperator` 仍在，`rulesets_duplicate_head_refused` 仍绿）；`lookupRules` 按归档顺序返回候选；`rulesOf` 列出每一条 |
| `src/parse/ast/builder.cpp` | 在声明处读 `when` 条件的文本（`is_same::<T, int>()`） |
| `include/camel/core/forward.h`、`src/core/forward.cpp` | 新 `forwardRulesOf(uri)`（候选表）；新 `conversionKeyOf`：**只认目的地字母**（`d`→`float`、`f`→`f32`、`f6`→`f16`、`bf`→`bf16`、`c`/`z`→`c64`/`c128`），源不进键；`:op/ltod` 的例外删除 |
| `modules/transforms/jvp.cpp` | 按归档顺序逐个试候选：在 `genericRuleTable` 里找模板、按**操作数自己的类型**取实例，实例不成则试下一条，全不成报最后一条原因 |
| `stdlib/rulesets/jvp.cml` | `jvp_ltod` 的左侧从 `float_of_int(n)` 改回**算子本身** `float(n)`（条件与 `::<T>` 都保留）；于是它的键是 `float@1` |
| `test/cases/compile/rulesets/rulesets_lookup.py` | 例外 3 → 2，并列出剩下两条 |

**读数**：例外表 **3 → 2**；`feat/compile/rulesets` **67/67**（没有掉条数、没有重复键报警）；`feat/compile/transforms` **48/61**（与基线同一份 13 条红，新增红 0）；缺口 **32 → 3**（脚本复核：prelude 里 29 条转换 uri 现在都有键，剩下的 `parse_float`/`idx_take`/`slice_sub` 不是转换形状）。

**安全阀实测（这一条是 (b'-2) 的关键）**：`float(x)` 作用在 **float** 上（`:op/dt od`，没有律）——

```
REINTERP jvp function=f skip=rule uri=:op/dtod
    reason=the rule's right side does not take the values and tangents the rule binds
[Error]: transforms: jvp has no forward rule for `:op/dt od`
```

它**拒绝**，没有把 `jvp_ltod` 的那条零实参律套到 1 个切量的算子上——这正是"试候选"不会变成"套错律"的原因：候选能不能用，由**实例的实参个数与条件**决定，不是由顺序决定。同一份探针里 `float(n)`（`:op/ltod`，`n: int`）给出 `rules=jvp_ltod`、**`3.0` / `0.0`**，即派生出来的键确实找到了那条律。

**候选顺序**：按**归档顺序**（`indexRule` 的追加序，也就是模块里写的顺序）。今天没有任何键持有第二条律（旧的不变式保证了这一点），所以顺序不影响任何结果；等第一批转换律落下来，谁先谁后仍不影响——只有一条会在该类型上成实例。
### 5.4 写 27 条转换律：归档成立，**分派不成立**（⚠️ 停手条件 (i) 触发，已回退）

(b'-2) 落地后键的缺口是 3，于是这一笔把转换律写了下来——**32 条里的 27 条**（`ltod` 已有；`at od`/`atof`/`atof6`/`atobf` 这 4 条**写不出**：prelude 的数组重载用 `when is_compact_array::<A>()`，而那个谓词是 `func` 不是 `export`，别的模块叫不到；数组形状又是不定的，没法直接写类型）。律的形状统一：有切量空间的源写成 `(dx: T) => <dest>(dx)`，没有切量空间的源（`i`/`l`/`s`）写成 `() => <zero>`。

**归档侧成立**：`rulesets` **67/67**、没有重复键报警（`float@1` 现在真的持 7 条律），`transforms` **48/61**、新增红 0。**这一条说明 5.3 的索引改动是对的**：一个键放 7 条律，家族照常建、用例照常绿。

**纸上算一个数、实测一个数（5 条，全部对上）**：

| 算子 | 纸上 | 实测 |
|---|---|---|
| `:op/itod` `jvp(itod)(3)` | `3.0` / `0.0` | `3.0` / `0.0` |
| `:op/dtod` `jvp(d2d)(3.5, 1.0)` | `3.5` / `1.0` | `3.5` / `1.0` |
| `:op/ftod` `jvp(ftod)(f32(3.5), f32(1.0))` | `3.5` / `1.0` | `3.5` / `1.0` |
| `:op/f6tod` `jvp(f6tod)(f16(3.5), f16(1.0))` | `3.5` / `1.0` | `3.5` / `1.0` |
| `:op/dtof` `jvp(dtof)(3.5, 1.0)` | `3.5` / `1.0` | `3.5` / `1.0` |

**但日志里写着是哪条律答的，而它答错了律**：

```
REINTERP jvp function=ftod   ... tangent=computed source=ruleset rules=jvp_dtod
REINTERP jvp function=f6tod  ... tangent=computed source=ruleset rules=jvp_dtod
```

`:op/ftod`（f32→float）是**用 `jvp_dtod` 那条 float→float 的律**答的。数对了，是巧合：所有转换律的右侧都是"同一个转换作用在切量上"，端口数正好一致。换一条端口数相同、正文不同的律（比如某个 `() => 0.0`），同一个机制会给一个**错的数**。

**根因（实测）**：`genericRuleInstance(context, ruleId, {elementType}, {})` 对 `jvp_ltod`（条件 `is_same::<T, int>()`）在 **float** 上返回 instance 0、`made=1`、`why` 为空——**`is_same` 条件在这里不参与实例判定**。所以上一笔我以为"类型分派是引擎已有的能力"，**在这条路径上并不成立**：真正决定哪条律被用的是**端口数**（`parameterPorts.size() == tangents.size()`，`rule_expand.cpp:227`），而它把"这个类型对不对"漏掉了。

**按 ⚠️ 停手条件 (i) 回退**：`stdlib/rulesets/jvp.cml` 与 `modules/transforms/jvp.cpp` 已 `git checkout` 回 `a9061b77a`，读数回到 `rulesets` **67/67**、`transforms` **48/61**。

**要作者裁的一句**：**"一个键里的哪条律答这个类型"该由谁判**——是 `genericRuleInstance` 真的执行条件（那么 5.3 的候选表就够了，27 条律可以立刻写回），还是索引侧要有一个按类型的显式查表（那就回到 (b') 的键带类型，再看 5.2 里"律不能声明具体类型"那条约束怎么绕）？
### 5.5 让 `when` 在实例判定里生效（作者裁 (a)，已落地）

**改的是"说了不算"这件事**：一条律写了 `when is_same::<T, int>()`，却在 `float` 上被当成实例——**那条 `when` 是假话**。假话比缺失危险：缺失会拒绝，假话会算错。

**一处代码，十二行**（`src/compile/gir/rule_templates.cpp#genericRuleInstance`）：

```cpp
std::string conditionWhy;
if (owner != nullptr && ruleConditionAt(context, ruleId, types, conditionWhy) ==
                            RuleConditionAt::Fails) {
    instance.why = std::format("its condition does not hold at {}", tupleText(types));
    entry.instances.push_back(std::move(instance));
    return entry.instances.size() - 1;
}
```

`ruleConditionAt` 早就存在（`rule_templates.h:148`，`check` 用的就是它），`genericRuleInstance` 从没问过它。**只有 `Fails` 会拿走这个元组**：`ValueDependent`（类型部分成立、其余读值）与 `Unknown`（问不出）照旧保留——问不出的条件不许吃掉实例。

**一处刻意的顺序**：条件判 `Fails` 之后，**仍然让模板去实例化一次**，好让"体说了什么"留在 `why` 里（`operator '+' is not defined for these operand types` 这种报告是 `rules` 计划里两条用例钉住的）；体编译得过、只有条件不成立时，`why` 才是`its condition does not hold at …`。**实例一律不发布**——条件是在发布之前问的，这才是"律关于 `int` 就不会答给 `float`"的保证。

**没有动 `rule_expand.cpp:227` 的判定顺序**（作者写的 ⚠️(i) 小实验因此没必要做）：门在实例之前，端口数检查留在原地当第二道筛。

**②③ 各算子的日志（`rules=` 就是"哪条律答的"）**：

```
function=ltod   rules=jvp_ltod     3.5 / 1.0   （:op/ltod，int 源）
function=f6tod  rules=jvp_f6tod    3.5 / 1.0   （:op/f6tod，f16 源）
function=dtof   rules=jvp_dtof     3.5 / 1.0   （:op/dtof，float 源）
function=ftod   rules=jvp_ftod     见下面的安全阀
```

`itod`/`dtod` 走的是常量折叠（`jvp(dtod)(3.5, 1.0)` 全是常量，不建图）——数对（`3.5`/`1.0`），但没有 `rules=` 行，所以安全阀用了一个不可折叠的输入（`math::sin(1.0)`）。

**④ 安全阀：修前给错的数，修后给对的数。** 构造办法：把 `jvp_ftod` 的正文**临时**改成 `float(dx) * 2.0`（`jvp_dtod` 是 `f64(dx)`，两条律端口数都是 1，**端口数相同、类型不同**），再用不可折叠的 `f32(x)` 调用 `:op/ftod`：

| | 日志 | 切量 |
|---|---|---|
| 门关（= §5.4 的状态，并让候选继续往下试） | `rules=jvp_dtod` | **`1.0`** ← 错的（`jvp_dtod` 的 `f64(dx)`） |
| 门开（本笔） | `rules=jvp_ftod` | **`2.0`** ← 对的（`jvp_ftod` 的 `float(dx) * 2.0`） |

两个数都是实测：`[stdout] 1.0` / `[stdout] 2.0`，同一份探针、只差那十行。**这一条同时证明了 §5.4 的担心不是假想**：端口数相同就会套错律，而套错的律会给出错数。

**⑤ 读数**：`rulesets` **67/67**（27 条律全部归档、无重复键报警）；`transforms` **48/61**（同一份 13 条红，新增红 0）；全量见下。

**⑥ 页里被纠正的一句**：§5.3 说"类型分派是引擎已有的能力"——**实测证明这条路径上没有**，纠正它的是 `:op/ftod` 那行 `rules=jvp_dtod`，不是文档推断。§5.3 的标题已标注。
### 5.6 四条数组转换律：`export` 不是障碍，**`ruleElementTypeOf` 是**（实测，已回退）

作者裁"给 `is_compact_array` 加 `export` 再写 4 条数组律"。**`export` 做了，律也写了，仍然选不中**——两处读数：

**① 谓词该在 `types`，不在 `prelude`。** `prelude` 里的 `is_compact_array` 是私有的 `func`；只加 `export` 还不够，因为 `rulesets.jvp` 里 `is_float`/`is_same` 都来自 `types`，而 `prelude` 的导出面不被规则库当作名字来源。所以试的是：**把 `is_compact_array` 移到 `stdlib/types.cml` 并 `export`**（`types` 是类型谓词的家，`is_array`/`is_numeric` 都在那儿），`prelude` 改成 `import { …, is_compact_array, … } from types`，`jvp.cml` 也 import 它。这一版**编译通过**，说明"写得出"这一步是成立的。

**② 但选不中：`ruleElementTypeOf` 把数组压成元素类型。** 4 条律的探针（`jvp(atod)(xs, [1.0, 1.0])`）给出的日志是：

```
REINTERP jvp function=atod graph=242 ... tangent=computed source=ruleset rules=jvp_dtod
[SUMMARY] total: 1, pass: 0, fail: 1, killed by a signal: 1
```

`:op/at od` 是**数组**算子，答它的却是 `jvp_dtod`（标量 float→float 的律）——因为 `modules/transforms/rule_types.h#ruleElementTypeOf` **刻意把数组剥到元素类型**（注释写明：正是这一条让 `index_into<T>` 在 `float[]` 上被按 `float` 归档，review P1-9）。实例因此在 `float` 上成立，我的数组律（条件 `is_compact_array::<A>()`，A 必须是数组类型）永远不成立。**这不是"谓词私有"能解的**。

**③ 而且它崩了。** 同一份探针在**有律**和**无律**两种情况下读数完全一样（`rules=jvp_dtod`、`killed by a signal: 1`），所以崩溃是**数组 lift 既有路径**的问题，不是这 4 条律引起的——与 `grad_array_*` 一直是红的一致。但它同时说明另一件事：**标量律被套到数组算子上，而且这一套就崩**。`when` 门在这里救不了，因为实例是按 `float` 问的、`jvp_dtod` 的条件在 `float` 上真成立。

**按 ⚠️(二) 回退**：`stdlib/types.cml`、`stdlib/prelude.cml`、`stdlib/rulesets/jvp.cml` 全部 `git checkout` 回 `04720eb7c`；读数不动（`rulesets` 67/67、`transforms` 48/61、全量 8527/8493/14）。页里只留这一节。

**这两条要作者裁（记成缺陷，不自己决定）**：
1. **数组算子的律该按什么类型归档**——`ruleElementTypeOf` 的"剥到元素"是 P1-9 的决定，改它要重开那一页；不改，则数组转换永远没有律。
2. **标量律被数组算子选中并崩**（`rules=jvp_dtod` + `killed by a signal`）：这是"选错律"的数组版本，和 §5.5 修掉的 `:op/ftod` 是同一类，只是入口不同——该在 `ruleElementTypeOf` 里挡，还是在"实例的端口类型与算子端口类型不符"时挡？
### 5.7 数组 lift 的崩改成具名拒绝（作者把"不许崩"排在写律之前）

§5.6 里那张读数表还有一行没被读懂：

```
REINTERP jvp function=atod ... tangent=computed source=ruleset rules=jvp_dtod
[SUMMARY] total: 1, pass: 0, fail: 1, killed by a signal: 1
```

前一行说"算完了"，后一行说"进程被打死"。按"对 > 明确拒绝 > 错数 > 崩"的排序，**这是最坏的一格，而它一直挂在 `grad_array_*` 名下当一个红**。

**崩因**：规则是**按操作数的元素类型**问的（`rule_types.h#ruleElementTypeOf` 刻意剥掉数组，注释写明那是让 `index_into<T>` 能在 `float[]` 上按 `float` 归档的同一件事，review P1-9）。于是 `:op/at od`（`float[]` → `float[]`）问到的实例是 `jvp_dtod`——一条端口类型为 `float` 的标量律。`expandRuleRightSide` 把 `float[]` 的操作数接到 `float` 的端口上，建出来的图不是任何编码器/VM 能跑的，运行时就那么死了。

**改法（一处，`modules/transforms/jvp.cpp`）**：候选律在展开之前，先比**形状**——

```cpp
if (!portsTakeOperands(instance, operandTypes, why)) { shapeWhy = why; continue; }
```

`portsTakeOperands` 拿实例左侧的 `normTypeAt(i)` 和算子各操作数的**自身类型**逐个 `equals`；对不上就换下一条候选，全不成时用**形状那条**原因（比"条件不成立"更具体）。`operandTypes` 由调用点在 `originals` 上读出，穿进 `buildRuleTangent`。

**改前 / 改后（同一份探针，`jvp(atod)(xs, [1.0, 1.0])`）**：

```
改前: REINTERP jvp function=atod ... rules=jvp_dtod
      [SUMMARY] total: 1, pass: 0, fail: 1, killed by a signal: 1

改后: REINTERP jvp function=atod skip=rule uri=:op/atod
          reason=the rule takes (float), and the operator has (float[])
      [Error]: transforms: jvp has no forward rule for `:op/atod`
               (the rule takes (float), and the operator has (float[])).
```

**这一笔只挡实测到的这一处**（⚠️(i)）：`buildRuleTangent` 的候选循环；数组路径上**其他**崩点没有被走过，也就不在这里假装挡住。

**读数**：`rulesets` **67/67**；`transforms` 见下（新增红 0 才算过）；全量见下（**`killed` 必须是 0**，这是本笔的关键读数）。
## 5.8 附：P0-2 第二半的清单（只调查，不改代码）

```
git grep -n -E 'ctrlInputsOf|ctrlEdgesOf|orderPredsOf' -- src modules include
```

**命中 206 行 / 60 个文件**（`ctrlInputsOf` 188，`orderPredsOf` 19，`ctrlEdgesOf` 0——这个名字不存在）。按区域：

| 区域 | 行数 | 它在回答什么 |
|---|---|---|
| `src/runtime/` | 76 | 运行时的等待边本身（`draft`/`graph`/`control`/`writings`/`draft_inline`/`draft_clone`） |
| `src/passes/`（opt 等） | 29 | **编译期 pass 的决定**——唯一可能有"该读没读"的一组 |
| `src/passes/test/` | 23 | 测试 pass 的断言与构造 |
| `src/compile/gir/` | 18 | 建图（`builder_control`）与 `GraphRead` 读法 |
| `src/execute/` | 18 | 执行计划与调度（ctrl 就是等待边，语义正确） |
| `include/` | 11 | 访问器本身的声明/实现（不是读者） |
| `src/passes/trans/` | 10 | printer（dot/json/stats） |
| `modules/tiles` | 7 | 区域切分 |
| `modules/autodiff` | 6 | pullback 的边搬运 |
| 其它 `modules/` | 8 | `rewrite`/`graph`/`onnx` |

**编译期那一组（`src/passes/opt` + `modules/transforms` + `modules/tiles`）共 32 行 / 12 文件**，逐行看过：**19 行同处也读了 sync**（`generic.cpp:203`、`rewrite.cpp:377`、`screen.cpp:231`、`split.cpp:373`、`gates.cpp:36` 是范本——`split.cpp` 那处干脆三个都读），**13 行只读 ctrl**：

| 文件#符号 | 它在回答什么 | 该换成 | 备注 |
|---|---|---|---|
| **`src/passes/opt/generic/algebra.cpp#constant`** | "这个值能不能折成常量"（`!ctrl.empty() \|\| written(id)`） | ③ + ①：加 `!syncInputsOf(id).empty()` | **最小可换**（见下） |
| `algebra.cpp` 同函数 | 同上（`written(id)` 是效应那半） | ③ 已答 | — |
| `gates.cpp:62/90/94` | gate 的 frontier / 代价 | ① 该并读 sync | 需先定"代价"口径 |
| `rewrite.cpp:512`、`rewrite.h:115` | "这个 user 能不能改写" | ① 该并读 sync | 与 512 同一判断的两处写法 |
| `screen.cpp:274` | 等待 frontier 的规模 | ① 该并读 sync | 与 `screen.cpp:231` 成对，231 已读 |
| `tiles/compiler.cpp:536`、`region_extract.cpp:108`、`graph_regions.cpp:513` | 区域切分的依赖闭包 | ① 该并读 sync | 三处同一件事 |
| `defun/defunctionalize.cpp:878`、`tiles/region_outline.cpp:518/531` | 把已有边列**搬**到新节点 | **写侧**（另组） | 读是为了写，不是判断 |

**挑出的一处：`src/passes/opt/generic/algebra.cpp#constant`。** 为什么最小：它是**一个纯谓词里的一个合取项**（`!draft.ctrlInputsOf(id).empty()` → 再加一个 `!draft.syncInputsOf(id).empty()`），不新增 API、不动图、不改任何 pass 的遍历；唯一的效果是**少折几个常量**（把"带着 sync 等待的节点"排除在常量之外），也就是只会更保守。它和 `842f4c1e9` 之前那笔 `fault` 的改法同一类：同一个谓词，同一处加减一个条件。

**⚠️ 写边被单独分组**：`setCtrlInputs(..., ctrlInputsOf(...))` 这类（`draft_inline` 12 处、`draft_clone` 4 处、`pullback.cpp:1662/2646/2719`、`defunctionalize.cpp:304`、`rewrite.cpp:323`、`region_outline.cpp:518`）是**把已有的等待边原样搬走**，读是为了写，和"读边判断"不是一件事，不混进上表。

**本轮不改代码。**
### 5.9 P0-2 第二半 · 第 1 处：`algebra.cpp#constant` 加读 sync —— **实测不生效，已撤**

§5.8 清单里挑出的最小一处。改动是一行合取项：

```cpp
    if (!h || h->kind != GCNodeKind::Data || h->dataType == nullptr || h->dataIndex >= 0 ||
        h->dataType->code() != ops.element || !draft.normInputsOf(id).empty() ||
        !draft.withInputsOf(id).empty() || !draft.ctrlInputsOf(id).empty() ||
        !draft.syncInputsOf(id).empty() || written(id)) {
```

理由写在函数里：**常量是"没有任何链握着它的值"**；只看控制边，一个挂在 sync 链上的节点看起来是自由的（`edges.cn.md` 5.3：控制边消失之后，命名链尾的正是 sync 边），把它折走就是跨过它前面的语句。`gates.cpp#carriesWait` 与 `generic.cpp#waits` 早就是这么问的。

**读数（改前 = `942f35372`，改后 = 本笔）**：

```
全量     8527 / 8493 pass / 14 fail / skip 10 / known failures 10   （改前改后完全一致）
红名单   14 条逐名 diff：新增 0，减少 0
killed   log 里出现 0 次
feat/opt     1171 / 1171 全绿
```

**停手探针（作者裁：造不出来就撤）**：手工探针

```cml
func main(): int sync {
    var total = 0
    let k = 2 + 3        // 常量折叠的候选
    total = total + k
    println(total)
    return 0
}
```

`std::opt std::stats` 读数 **有这一行 21 节点 / 去掉这一行 21 节点**——**两次相同**。为什么：

**builder 把 sync 边和 ctrl 边成对写**。`builder_helpers.h#writeSyncStatement` 就是
`writeSyncLink(from, to); writeControlLink(from, to);`——所以**任何挂在 sync 链上的语句，`ctrlInputsOf` 本来就非空**，再加一个 `syncInputsOf` 合取项**永远改不了答案**。这不是"用例没覆盖"，是**这行今天不可达**。

**这一行已撤**（`git checkout` 回 `ede6d0e54` 之前的 `algebra.cpp`）。结论记在这里的是**负结果**：今天没有任何程序能把"常量候选且只有 sync 等待"的节点放到图上——`constant()` 的候选是无输入的数据槽节点，而 `sync` 链上的节点从出生起就带着 ctrl 边。**只有某个 pass 把 ctrl 边单独丢掉之后，这行才会咬人**；那一天的到来，本身就是一个该立用例的事件。

因此 §5.8 里"9 处该补 sync"要修正为 **8 处**：这一处经实测证明**不必补**（补了也不生效）。
## 6. 一句话结论

**这条缝是方案性的，不是少数特例**：按"结果有切空间却拼不出键"计，**32 条**（把复数算进去 50 条），而例外表只能一条一条地救——今天救了两条、补了一条（`^`）。缺口的形状有两个：

1. **转换族没有键**：`<源>to<目的>` 的名字里其实**写着**目的地（`dtod` 是 d→d、`itof` 是 i→f、`ltod` 是 l→d），而键只认 `operationOf` 拿到的第一个词，于是 29 条转换全被挡在门外。两条修法见 §5，**都是设计决定，不是我这边的改法**。
2. **`kWrittenOperations` 漏列**：`^` 曾是写出来的算子、结果是 float，却不在表里（**今天已补**）；`%`、`==`、`<`、`&&` 也不在，但它们的**结果没有切空间**，所以不要紧。`[]` 在数组上是例外表，在字符串上没人问。这一条是三者里最便宜的：一行表 + 一条律。

## 7. 顺带修的三处

1. `test/cases/compile/rulesets/rulesets_lookup.py` 的断言从"两条"变"三条"，旁边加了一行注释说明为什么（`:op/ltod` 走例外表绑到 `float_of_int/1`，因为 `float` 在一操作数上是所有转 double 转换共用的写法），免得下一个人以为有人为了让用例绿而放宽了断言。**补 `^` 没有再加例外，表仍是 3 条**。
2. `test/plans/feat/compile/transforms.plan.toml` 里两条钉 `GraphId` 的用例（`reinterp_builds_its_own_graph` 的 `closure graph=107`、`jvp_is_a_function_value` 的 `built graph=56`）**改成了 286 / 236**：这个 id 是**按位置编的**（pass 用一个计数器给建出来的图编号，ruleset 里的律图先消耗号段），所以往 jvp 族里加一条律，后面的 id 全部平移。两条钉子旁边各加了一行注释说明这件事，免得下一个人以为这是"为了绿而改数"。
3. 两份**生成物**随 `pow_tangent` 重生：`docs/stdlib-reference.md`（`node scripts/stdlib-reference.mjs --write`）与 `docs/reference/camel-for-ai-stdlib-signatures.md`（`node scripts/camel-for-ai.mjs --write`）。不做的话 `registry.reference_is_generated` 与 `docs.ai_reference.current` 两条用例会红——它们是"生成物必须等于生成它的脚本的产物"，我当时就是被这两条逮住的。

4. **一个不可信的数**：这一轮里有一次全量报出 `31 失败 / 2 killed`，其中 17 条是 `module_import_*`、`simplify_*`、`fusion_group` 这种与本次改动无关的用例，`got exit 127` 与 `SIGBUS (rc=135)`——远端 `out/latest` 被并发构建换掉了（`camel-remote.ps1` 在共享目录 `/home/zjwei/camel-work/tloop` 上跑，两个 run 撞在一起就会出现这个）。**单独重跑即清**（14 红）。看到 `127`/`SIGBUS` 就重跑，别去查代码。