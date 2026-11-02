# 键方案拼不出来的算子：171 条里 129 条，36 条今天不可能有律

> 2026-11-03 02:10，`lane-tloop`。这一页是 `jvp_ltod` 那一笔的余量：`ltod` 不是个别问题，它是"键方案"（`camel/core/forward.h#ruleKeyOf`）的缝里漏出来的一个样本。下面把缝量到底。

## 1. 怎么量的

三条读数，不靠印象：

1. **映射表**：`ruleKeyOf(family, uri)`（`src/core/forward.cpp`）只有三条分支——先查例外表（`kRuleKeyExceptions`），再对 `:op/` 名字取 `operationOf`（第一个 `_` 之前）去 `kWrittenOperations` 里找**写法**（`add→+`、`sub→-`、`mul→*`、`div→/`、`neg→-`，**共 5 条**），最后对模块算子（非 `:op/`）用 `nameOf(uri)` + `arityOfHead(family, name)`。`:op/` 名字走到表里找不到，就 `return std::nullopt`，注释原话 **"No key, no guess"**。
2. **算子清单**：`#op(':op/…')` 在 `stdlib/prelude.cml` 里 **171 条**（去重后）；再用 `git grep -oh ':op/[a-z_0-9]*' stdlib/` 补齐 prelude 没有声明的内部算子。
3. **结果有没有切空间**：按 `tangent_space.the_type_answers` 钉的口径——`float`/`f32`/`f16`/`bf16` 与它们的数组是自成一格，整数、`bool`、`string`、`Bytes`、`never`、以及含整数的元组**一格都没有**。这一栏是判断"这条要不要律"的唯一依据：**结果没有切空间 ⇒ 变换永远不问它 ⇒ 没有键也不是缺口。**

结论先给：**(甲) 42 条能拼，(乙) 129 条拼不出**；129 条里 **73 条按设计不需要律**、**2 条走例外表（已绑）**、**36 条结果有切空间却今天不可能有律**、**18 条判不了**（复数）。36 ≫ 3，所以有这一页。

## 2. (甲) 能拼的：42 条，就是五个 token 的各类型后缀

```
+  : add_f add_d add_i add_l add_f6 add_bf add_c add_z add_s add_bytes
-  : sub_f sub_d sub_i sub_l sub_f6 sub_bf sub_c sub_z          neg_f neg_d neg_i neg_l neg_f6 neg_bf neg_c neg_z
*  : mul_f mul_d mul_i mul_l mul_f6 mul_bf mul_c mul_z
/  : div_f div_d div_i div_l div_f6 div_bf div_c div_z
```

注意这条分支的一个副作用：**键只看名字的第一个词**，所以 `add_s`（string 相加）、`add_bytes`、`add_c`（复数）也能拼出 `+/2`，只是它们的律对 `is_float`/`is_numeric` 有约束，于是"键存在、律对不上"是另一条**已被命名的**拒绝路径（`the ruleset holds no rule for this operator and operand count`），不是静默。**能拼 ≠ 有律**，但至少可问。

## 3. (乙) 拼不出的：129 条，四栏

### 3.1 结果没有切空间 ⇒ 没人问（73 条，按设计，不是缺口）

| 结果类型 | 条数 | 算子 |
|---|---|---|
| `bool` | 43 | `not and or`、`eq*`（12 个）、`lt_* le_* gt_* ge_*`（各 7） |
| `i32` | 10 | `inv_i mod_i pow_i`、`*toi`（`itoi ltoi ftoi dtoi stoi bftoi f6toi`） |
| `int` | 10 | `inv_l mod_l pow_l`、`*tol`（同上 7 个） |
| `string` / `Bytes` / `Error` / `never` / `int!` | 7 | `atos idx_str slice_str slice_bytes error_new fail parse_int` |
| 无切空间的数组 | 3 | `atoi atol`（`WithElement<A,i32>` / `WithElement<A,int>`）、`mem_blocks`（全 int 的结构） |

**现在怎么办**：什么都不用办。`jvp.cpp` 收集一条律的实参时会 `continue` 掉没有切空间的操作数；一个**结果**没有切空间的节点，它的切量也没有人读。证据在同一段代码的另一半：`int_escape` 里的 `int(3.0x)`（`:op/dtol`）从来没被问过——`g(x)=float(int(5.0x))+x` 的 lift 读数 `rules=jvp_mul,jvp_ltod,jvp_add` 里没有 `dtol`。

### 3.2 走例外表（2 条，已绑，律在）

| uri | 键 | 律 |
|---|---|---|
| `:op/idx_arr` | `index_into/2` | `jvp_idx_arr`、`idx_arr_t`、`vmap_rule_idx` |
| `:op/ltod` | `float_of_int/1` | `jvp_ltod`（2026-11-03 新增） |

**现在怎么办**：例外表 `kRuleKeyExceptions` 里各占一行，`std::rulesets forward=true` 每次运行都打印。

### 3.3 结果有切空间、今天**不可能有律**（36 条）——这就是缺口

| 族 | 条数 | 算子 | 为什么拼不出 |
|---|---|---|---|
| 转成 double，`float(x)` | 6 | `dtod ftod itod stod bftod f6tod` | `float` 在一操作数上是**所有**转 double 的转换共用的写法，一个键一条律 |
| 转成 `f32`，`f32(x)` | 7 | `bftof dtof f6tof ftof itof ltof stof` | 同上 |
| 转成 `f16`，`f16(x)` | 6 | `dtof6 f6tof6 ftof6 itof6 ltof6 stof6` | 同上 |
| 转成 `bf16`，`bf16(x)` | 6 | `bftobf dtobf ftobf itobf ltobf stobf` | 同上 |
| 数组转换 | 4 | `atod atof atobf atof6` | 同上（`WithElement<A, float>` 等） |
| 可失败转换 | 1 | `parse_float`（`float!`） | 同上 |
| 幂 | 4 | `pow_d pow_f pow_f6 pow_bf` | `^` 是一个**写出来的**算子，但 `kWrittenOperations` 只有 5 条，`pow` 不在里面 |
| 数组取/切片 | 2 | `idx_take slice_sub` | 无 token（`t[idx]`、`xs[a:b]`），且没进例外表 |

**现在怎么办：没有律，变换直接拒绝。** 两次实测（今天，本树）：

```
# x ^ 2.0 里的 ^
REINTERP jvp function=p skip=rule uri=:op/pow_d reason=the operator has no forward rule bound at all
# f32(x) 里的 f32(...)
REINTERP jvp function=q skip=rule uri=:op/dtof reason=the operator has no forward rule bound at all
```

两处都指到源码位置（`tmp_pow.cml:4:14`、`4:18`），都是**普通写法**，不是奇技淫巧。第二行尤其说明问题：`f32(x)` 只是把 float 窄一档，它在余切空间里有**现成**的对应物（窄一档），却因为拼不出键而没有律。

### 3.4 判不了（18 条，⚠️ 单列）

```
c64 : ctoc dtoc ftoc itoc ltoc pow_c pow_c_i pow_c_l ztoc
c128: ctoz dtoz ftoz itoz ltoz pow_z pow_z_i pow_z_l ztoz
```

**判不了的原因**：复数有没有切空间，我没有读到任何钉住它的读数——`tangent_space.the_type_answers` 只钉了 `float f32 f16 bf16 float[] i32 string i32[] (float,i32) (float,float)` 十行，`c64`/`c128` 不在其中，`tangent_space.h` 的注释我也只读到"float 与它的数组自成一格、整数与字符串没有"。**若复数有切空间，这 18 条并入 3.3**（缺口变 54 条）；若没有，并入 3.1。另有两类我按"有切空间"保守记进 3.3，但保留疑问：`dtod`/`ftof`/`bftobf`/`f6tof6` 这类**同型转换**（编译器可能把它折掉，于是永远不被问）。

## 4. 一句话结论

**这条缝是方案性的，不是少数特例**：按"结果有切空间却拼不出键"计，**36 条**（把复数算进去 54 条），而例外表只能一条一条地救——今天救了两条。缝的形状有两个：

1. **转换族没有键**：`<src>to<dst>` 的名字里其实**写着**目的地（`dtod` 是 d→d、`itof` 是 i→f、`ltod` 是 l→d），而键只认 `operationOf` 拿到的第一个词（`dtod`/`itof`/`ltod`），于是 30 条转换全被挡在门外。可能的修法两条，**都是设计决定，不是我这边的改法**：
   - (a) **每 uri 一行例外**（30 行），键指向具名调用；
   - (b) **让 `ruleKeyOf` 从 `<src>to<dst>` 里推出"目的地类型的构造子 + 1"**，键自明，例外一行都不用——`ltod` 今天那条例外会被这个推导取代。

   但**两种修法下律都省不下来，这是第二条结构限制**：一条前向律的右式实参个数 = 它**有切空间**的操作数个数，而 `dst(x)` 的源类型可能是 `int`（没有切空间 ⇒ 右式 0 个参数 ⇒ 答 `0.0`）、也可能是 `float`（有 ⇒ 右式 1 个参数 ⇒ 答 `dst(dx)`）。**同一个键下这两件事写不进同一条律**——泛型参数 `T` 的实参表在一个律里是固定的。所以律的条数是"目的地写法 × 源类型的切空间类"：4 个目的地（`float f32 f16 bf16`）×「1 条收所有无切空间源 + 4 条有切空间源（d/f/bf/f6）」= **20 条律**覆盖那 26 条标量转换（数组转换与 `parse_float` 再加上去）。`jvp_ltod` 写成 `when is_same::<T, int>()` 而不是"任意整数源"，正是这条限制的产物。
2. **`kWrittenOperations` 只列了 5 个写法**：`^`（`pow`）是写出来的算子、结果是 float，却不在表里；`%`、`==`、`<`、`&&` 也不在，但它们的**结果没有切空间**，所以今天不要紧。`[]` 在数组上是例外表，在字符串上没人问。这一条修起来最便宜：`^` 补进表（一行），`pow` 的律按 `is_float` 写（乘方律）。

## 5. 顺带修的一处

`test/cases/compile/rulesets/rulesets_lookup.py` 的断言从"两条"变"三条"，旁边加了一行注释说明为什么（`:op/ltod` 走例外表绑到 `float_of_int/1`，因为 `float` 在一操作数上是所有转 double 转换共用的写法），免得下一个人以为有人为了让用例绿而放宽了断言。