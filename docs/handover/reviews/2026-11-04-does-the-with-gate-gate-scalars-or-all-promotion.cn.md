# 删掉“只提升标量 `with`”那道门，删的是标量限制，还是提升本身？

**状态**：**已按默认甲落地**（2026-11-04，本条车道，派工授权）。没有新问题要作者答；这一页是把一句裁定的两种读法、两种读法的读数、以及我方选的那一种写下来，供复核时一眼对账。

## 1. 裁定的原话与两种读法

2026-11-04 04:20 的裁定（`docs/handover/reviews/questions-2026-10-29.md`，`## 裁定（2026-11-04 04:20）：48 页一次答完`，第 15:15 条）：

> `when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port`：`with` 值**不进**抬升后的值列表，按 15:15 变成闭包残差；**删掉“只提升标量 `with`”那道门，也删掉“does not reinterpret … yet”那句诊断**。

“门”和“诊断”各指代码里的什么，有两种读法：

| 读法 | “那道门” | “那句诊断” | 后果 |
|---|---|---|---|
| **甲（已落地）** | `modules/transforms/jvp.cpp#withIsPromoted` 里的**标量限制**（`Tangent<T> = T`，一个词宽） | `modules/transforms/jvp_operator.cpp#jvpKernel` 里 `does not reinterpret {} yet` 这个格式 | 提升留着，条件是“这个值的类型有切空间”；任何没有切空间的值（`var` 格子、`int`）仍是 `with` 端口 |
| 乙 | **整个提升**（`withIsPromoted` 恒假） | 同上 | `with` 值一律不进值列表，方向一律进闭包残差 |

## 2. 读数：乙会把一个对数字变成错数

树 `D:\Projects\Camel-wt\withport2` @ `d7f15f2b3`+本条车道改动，宿主 `/home/zjwei/camel-work/withport2`，二进制 `2026-11-03T23:18:19-05:00`（`-v`，`std::nvm`，宿主 1m load 22–24，读数是**负载下的**读数，只用于同一个数改动前后的对比）。

| 程序 | 手算 | 甲（已落地） | 乙（会把提升整个删掉） |
|---|---|---|---|
| `library_grad_of_a_with_weight.cml` 的 `weighted`（`with <ww: float>`，`x = 2.0`，`w = 3.0`） | `d/dw (x*w) = 2.0`… 该用例钉的是**引擎**的 3.0 与**库层**的同一个 3.0 | `weighted engine=3.0 value=9.0 grad=3.0` | `grad=0.0`——`ww` 变成 `with` 端口、切向是它类型的零。这正是 2026-11-04 已经从树上删掉的那个错数 |

按当晚定下的次序 **对 > 明确拒绝 > 错数 > 崩**，乙落在“错数”那一格，而且**已有用例钉着那个对数字**（`library_grad_of_a_with_weight.cml` 的 `grad=3.0`、`grad_first_with_parameter.cml` 的四组数）。所以甲。

补一条**为什么乙连“明确拒绝”都做不到**：`with` 值被提升之后，读它的那条规则在它自己的入口就拒了，走不到“这个 `with` 值没有切空间”这一步——实测（同一棵树，`-v`）：

```
REINTERP jvp function=g skip=rule uri=:op/idx_arr reason=the rule takes (float[], int), and the
operator has (float[..], int) (of which 2 is a data operand)
```

`with <w: float[..]>`、体 `x * w[0]`：拒绝发生在原语上，和提升与否无关。也就是说乙的“一律不提升”对这类形状**没有可观察的差别**，它唯一可观察的差别就是标量那一格——而那一格是错数。

## 3. 落地（三处，逐处对上裁定的字）

1. `modules/transforms/jvp.cpp#withIsPromoted`：`withValueIsCarriable`（`Tangent<T> = T` 的那个判断）整个删掉，函数现在只问两件事——`withIsVarAt(i)` 为假，且 `camel::core::tangentSpaceOf(type)` 有切空间。**门 = 标量限制，已删**。
2. `modules/transforms/jvp.cpp#liftGraph`：那段 `if (state.promoteWiths && !withIsVarAt(i))` 的拒绝连同它打印的 `REINTERP jvp function=… skip=with type=…` 一起删掉。删掉之后它是一个**不可达状态**，删的是死码，不是一种形状的判决。
3. `modules/transforms/jvp_operator.cpp#jvpKernel`：格式从 `transforms: jvp does not reinterpret {} yet ({})` 改成 `transforms: jvp cannot differentiate through {} ({})`。**“yet”这个承诺没有了**——拒绝是说这条规则管不到这个构造，不是许诺以后管。

改后读数（同一棵树、同一台宿主，`-v`，`std::nvm`）：

| 用例 | 改前 | 改后 |
|---|---|---|
| `jvp_loop_iterable_tangent.cml` | `jvp does not reinterpret a loop over a value that has a tangent` | `jvp cannot differentiate through a loop over a value that has a tangent (the element the loop hands its body has the tangent of the array's element, and no tangent of an element is carried)`，exit 1，不出数 |
| `library_grad_of_a_with_weight.cml` | `weighted engine=3.0 value=9.0 grad=3.0` | **逐字相同** |

第 2 条是这条读法的全部理由：**改前的对数字，改后还是对数字**。

## 4. 我方默认（已落地，无需再裁）

甲。一句话：这一条裁定的目的（`with` 值的方向进闭包、不进值列表）**不靠删提升达成**——15:15 说的“闭包残差”是 `split` 那一侧的事，提升这一侧只决定“这个 `with` 参数在抬升后的函数里算不算一个线性输入”。删掉提升等于把“算一个线性输入”改成“永远不算”，那是另一个判决（而且已经有对数字钉着它不成立）。

如果作者的本意是乙，那么要同时改的是那两处**钉着 3.0 的用例**，以及 2026-11-04 已经删掉的 `grad=0.0` 会回来——请在那条裁定时明确写“标量 `with` 也答零”，否则这一页的默认继续有效。