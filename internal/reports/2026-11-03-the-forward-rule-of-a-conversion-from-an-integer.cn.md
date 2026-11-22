# 一条被"没有规则"挡住的转换：`int → float` 的前向律，和它的键为什么只能手工绑定

> 2026-11-03 01:55，`lane-tloop`。这一页记 `transpose_loop_escape.int_is_a_zero_tangent_space` 的第一道墙怎么没了，以及为什么"补一条律"这句话在这一条上不够。

## 1. 第一道墙是 `:op/ltod`，不是循环

原读数（`transpose_loop_escape_int.cml:35:44`，`float(n)` 的括号里）：

```
REINTERP jvp function=int_escape skip=rule uri=:op/ltod reason=the operator has no forward rule bound at all
```

`:op/ltod` 是 `float(x: int)`（`stdlib/prelude.cml#float`），也就是"把一个整数读成 double"。这个用例问的正是它：`int_escape(x) = let n = int(3x); loop s = x for i in 0..3 do s + float(n)`，其中 `float(n)` 读到一个整数，而整数是**零切空间** ⇒ 它对导数没有贡献 ⇒ `int_escape` 的导数是 1（`x` 自己被读了一次），不是 0，不是 3，也不是 13。

## 2. 答案：零值，不是"没有"

`float(n)` 的结果是 `float`，`Tangent<float>` 是 `float`，所以这条律必须**答一个 `0.0`**；"没有余切"的是它的**输入**——`Tangent<int>` 是 `()`，所以右式不取参数。这一条不是我发明的，是同一文件里 `jvp_idx_arr` 已经写过的处理方式，原文：

```cml
/// `d(xs[i]) = dxs[i]`: the tangent of an element is that element of the array's tangent. The index
/// carries no tangent of its own -- `Tangent<int>` is `()` -- so it does not enter the right side.
/// @example index_into([0.5, 0.25], 1)    // the tangent of `xs[1]` when `dxs` is `[0.5, 0.25]`
export rule jvp_idx_arr::<T>(xs: T[], i: int) {
    index_into(xs, i) => (dxs: T[]) => index_into(dxs, i)
}
```

`jvp.cpp` 的取值处也证实了这一点（`modules/transforms/jvp.cpp`，收集切量的循环）：`if (input != nullptr && !camel::core::tangentSpaceOf(input->dataType()).hasSpace()) continue;` —— 没有切空间的操作数**不进**实参表。所以 `ltod` 的右式是 `() => 0.0`，零参数的 lambda。

我照它写的律：

```cml
/// `float(n)` where `n` is an `int`: the body of `:op/ltod`, and the left side its rule is filed
/// under.
/// @example float_of_int(3)    // 3.0
func float_of_int::<T>(n: T): float when is_same::<T, int>() {
    return float(n)
}

/// `d(float(n)) = 0`: an integer is a step, not a slope.
/// @example float_of_int(3)    // 3.0; lifted, its tangent is 0.0
export rule jvp_ltod::<T>(n: T) when is_same::<T, int>() {
    float_of_int(n) => () => 0.0
}
```

## 3. 一条律不够：这个键拼不出来

第一次只加律，读数变成：

```
REINTERP jvp function=int_escape skip=rule uri=:op/ltod reason=the operator has no forward rule bound at all
```

一个字没变——因为 `ruleKeyOf` 对 `:op/` 的名字只认 `add`、`sub`、`mul`、`div`、`neg` 五个（`src/core/forward.cpp` 的 `kWrittenOperations`），其余一律 `return std::nullopt`，注释写着 "No key, no guess"。`ltod` 这个名字拼不出 `float`，而 `float` 在**一个操作数**上是**所有转 double 的转换共用的写法**（`float(x: i32)`、`float(x: float)`、`float(x: string)`、`float(x: bf16)`……），一个键只能挂一条律，所以它也不该是那个键。

结论：这条律必须像 `:op/idx_arr` 那样，由一个"手工绑定"挂到一个**具名调用**上。`kRuleKeyExceptions` 因此从 2 条变 3 条（`src/core/forward.cpp`）：

```cpp
{":op/ltod",
 "float_of_int",
 1,
 "`float(n)` is the spelling, and `float` at one operand is a spelling every conversion to a "
 "double shares (`float(x: i32)`, `float(x: float)`, `float(x: string)` and the rest), so one "
 "key could not name this one; its rule is filed under the call `float_of_int(n)`, whose body "
 "the operator is"},
```

排在 `:op/idx_arr` 之后、`tensor:multiply` 之前——那张表自己写着"in uri order"。

这条绑定是**跨层**的：`include/camel/core/forward.h` 的注释原话是"those two are the whole of `ruleKeyExceptions()`"，`feat/compile/rulesets` 的 python 用例更是把"恰好两条"当断言（`check(len(exceptions) == 2, ...)`，还有一行列出两条的 uri 与键）。都跟着改了：两句注释、一条 desc、两句断言（`== 3`，键表加 `(":op/ltod", "float_of_int/1")`）。改的是它们的**计数与列表**，不是它们的判据。

## 4. 律必须是泛型的

第二次写成 `export rule jvp_ltod(n: int)`（非泛型），读数变成：

```
REINTERP jvp function=int_escape skip=rule uri=:op/ltod reason=the rule is named in the index but its templates are not registered
```

`jvp.cpp` 拿到名字后要去 `genericRuleTable` 里找同名条目（`jvp.cpp:108-121`），找不到就是这句。泛型规则表里只有**模板**——没有 `::<T>` 的规则没有实例可造，也就没有条目。所以照 `array.cml` 的 `sum_kernel::<T>(xs: T[..]) when is_same::<T, int>()` 的写法，把类型写成参数、用 `is_same` 收到 `int` 上（`float` 对 `i32` 和对 `int` 是两个不同的操作数，`is_integer` 收不到那一个）。

顺带：词左式从 `float(n)` 换成 `float_of_int(n)`。不是体面问题——`float(n)` 的 head 是 `float`，正是第 3 节说的那个共用的键；换成一个具名调用，键才唯一，绑定才有东西可指。

## 5. 读数

改完，这个用例的墙换了位置：

```
REINTERP jvp function=int_escape skip=loop
```

`:op/ltod` 那道墙没了；现在挡着它的是 `int_escape` 里的**循环**的前向变换，也就是 `skip=loop`——和另外 5 条 `transpose_loop_escape.*` 同一道墙，`737a7e896`（原 `lane-loopreverse`，该分支已删）的活。

它自己的对照组**通了**，而且是纸上的数（同一个 `g`，`float(int(5.0x)) + x`，无循环）：

```
REINTERP jvp function=g graph=58 ... rules=jvp_mul,jvp_ltod,jvp_add
6.0
(1.0)
(3.0)
```

`g(1.0) = 6.0`（`x=1` 加 `int(5)`），`transpose(backG)(1.0) = (1.0)`、`(3.0) = (3.0)` —— 比例对，整数支路贡献 0，`x` 支路贡献 1。注意 `rules=` 里**没有** `jvp_dtol`：`int(3.0x)` 的截断方向的**结果**是 `int`，没有切空间 ⇒ 它不需要律，也就没有被问到（同一段代码里"没有切空间的操作数不进实参表"这条规则的另一半）。

## 6. 数

| | 改前 | 改后 |
|---|---|---|
| `feat/compile/transforms` | 47 / 60 | **47 / 60**（同样那 13 条） |
| `feat/transforms/split` | 18 / 18 | **18 / 18** |
| `feat/compile/rulesets` | 全绿 | **全绿**（"两条"改"三条"之后） |
| 全量 fast | 8526 / 8492 / 14 / skip 10 / known 10 | **8526 / 8492 / 14 / skip 10 / known 10**，killed 0 |
| `transpose_loop_escape.int_is_a_zero_tangent_space` | 红（`skip=rule uri=:op/ltod`） | 红（`skip=loop`） |

全量一条没动，`registry.docs_complete` 是既有的红（`stdlib/graph.cml` 的 `with_inputs` 一行，改前改后同形）。

## 7. 顺手确认的两件事

- **`dtol`（`int(3.0x)`，float→int）不需要律**：第 5 节的 `rules=` 里没有它，而 `g` 的四个数全对。真要有天一条程序去问"一个整数的余切"，它才会浮出来。
- **前向律不改变拒绝的形状**：`int_escape` 从"`:op/ltod` 没有律"变成"循环没有前向变换"，两者都是 `skip=`，所以这个用例的 `expect_output_not_contains = ["skip="]` 仍然守着它的意思——**拒是一个还没答的问题，不是答错**。

## 8. 我按默认做的

1. 把 `:op/ltod` 排进例外表的第二位（uri 序），而不是追加在末尾。
2. 词左式用具名调用 `float_of_int`，而不是把 `float/1` 塞进 `kWrittenOperations`（那会让所有转 double 的转换抢一个键）。
3. 改了另一条车道的断言（`feat/compile/rulesets` 的"恰好两条"）——因为"第三条绑定"这件事本身就是那个断言在否定的东西，留着不改是让一条断言说谎，改数字是让它继续守着"能推的键不必绑"。