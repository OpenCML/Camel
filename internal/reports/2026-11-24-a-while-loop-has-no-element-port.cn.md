# 一个 `while` 循环没有元素端口

**2026-11-24。** `C-1123-63` 的归组结论与修法判据。那 7 条 `transpose_*` 红不是转置算错了，
是 `transpose` 走查里的一条**端口约定**在别处被合法作废以后，没有人告诉它。

## 1 读数

| 树 | `feat/compile/transforms` |
|---|---|
| `6b844be42b`（`lane/range-ri` 合入之前） | 216 条：**214 pass / 0 fail** / 2 known |
| `e49bdeae88`（"a..b 的值是 Range，for 走它的迭代器"） | **7 fail** |
| `46e257734d`（merge `lane/range-ri` 进 `develop`） | 同 7 条 |
| `develop d04a238519`（基线） | 同 7 条 |

7 条是 `transpose_runs_a_loop_rounds_backwards`、`transpose_loop_escape.{names_the_value_one_read,
one_value_read_twice_is_one_capture, two_values_of_two_kinds, two_states_before_the_escape,
second_state_of_the_same_loop, int_is_a_zero_tangent_space}`，全部同一句：

```
TRANSPOSE function=<name> node=<n> state=0 no-cotangent-space
```

（`transpose/walk.cpp:2805-2808`。`two_values_of_two_kinds` 另有一条 `known_fail` 改成"改变了主意"。）

## 2 缺陷的名字

没有一行代码变错，也没有一条原则被违背。`e49bdeae88` 把 `a..b` 落成 `prelude.Range`，让 `for`
走该类型自己的走法（`__iter__` → `prelude.cml` 的 `iter(r: Range) => int..`），同笔改了
`docs/design-principles.cn.md`。**合法。** 变的是 `transpose` 依赖的前提。

`for i in 0 .. 3` 现在是 `LOOP while`，值类型 `(int.., bool, float)`
（GIR 实测，`test/cases/compile/transforms/transpose_loop_escape_reads_one.cml`）：

- `int..` —— 迭代器走过的范围，
- `bool` —— "还有没有下一个"，
- `float` —— 程序自己的状态 `s`。

`While` 没有元素端口（`loopHasIterable(LoopKind::While) == false`，`loop_kind.h:33`），端口就是这三个
状态。走查仍按"元素排在最前"数：

```
walk.cpp:2669    const size_t stateCount = regionPorts.size() - 1;   // "the element followed by one per state"
walk.cpp:2802    const size_t portIndex = 1 + i;                     // state i 的端口
```

于是 state 0 取到端口 0+1 —— 迭代器的范围状态，一个**真的**没有余切空间的类型 —— 于是
`no-cotangent-space`。**这句话说的是真话**：那个状态确实没有余切空间。它只是不该被当成
"用户要微分的那个状态"。

同一条假设的另外两张脸：

- `walk.cpp:2755-2758`：`intervalForm` 为假时把 `regionPorts.front()` 当元素
  （`elementType = regionPorts.front()->dataType()`，`forwardOf[regionPorts.front()] = elementPort`）。
  对 `while` 它把 state 0 当成了元素。
- `walk.cpp:2693/2701/2766` 的 `intervalForm` 已经是**死支**：`setLoopIterableInterval` 在 `src/`
  里只剩定义（`kernel/runtime/draft_construct.cpp:557`），前端不再产出 `LoopIterableForm::Interval`。

## 3 端口约定应当由循环形式推出来

判据：

```
端口布局 = f(循环形式)，不是"总数减一"
  有元素端口（loopHasIterable(kind) 为真，即 For / ForWhile）：端口 = 元素 + 每状态一个，状态 i ↔ 端口 1+i
  没有元素端口（While）：端口 = 每状态一个，状态 i ↔ 端口 i
  LoopIterableForm（Array / Interval）只决定**元素怎么从序列里取**，不改变"有没有元素端口"
```

要改的是三处，不是一处：`stateCount`（2669）、状态的 `portIndex`（2802）、以及 2755-2758 在
`While` 上不该有值。

## 4 端口改对，这 7 条**不一定**绿 —— 第二级

把映射改对以后，`state=0` 会退回成"迭代器状态没有余切空间"。这仍要一个决定，因为一个
**真的没有**余切空间的状态（`int`、`bool`、`Range`、流）是**零切空间**而不是错误
（`Tangent<int>` 是 `()`，见 `walk.cpp:3845` 附近同一句），它应当被带着走、不贡献余切，
而不是让整次转置拒绝。

但带着走之后，反序循环用什么把轮次倒过来？现在的反走查有两条路：`Interval` 复制
`(:struct/range)` 的两个界，`Array` 复制序列并按 `round` 用 `:op/idx_arr` 取元素
（`walk.cpp:2673-2710` 的注释与代码）。对一个由迭代器驱动的 `while`，两条都不成立 ——
序列是**状态**，被一轮一轮吃掉。这是 `D-1123-41 ②(b)` 里"转置反向"那一项的设计问题：
`while`-over-iterator 的轮次要么靠 tape 倒（`src/stdlib/autodiff/pullback.cpp` 对 `while`
就是这么做的），要么由作者裁"这一形态不转置"。端口约定回答不了它。

7 条对它的敏感度不同，大致三档：

- `thrice` / `quad`（`transpose_runs_a_loop_rounds_backwards`）：只要轮数，乘法可交换。
  端口改对 + 零切空间放行后**可能直接绿**（它的 body 不读状态以外的每轮值）。
- `read_one` / `read_twice` / `read_outer` / `carried` / `carried_p`：要 ESCAPE 通道与每轮的加和。
  端口改对后才会走到 `carried=` / `answer=` 那一段，那里还有自己的工作。
- `int_is_a_zero_tangent_space`：它本身就是"零切空间"那条的用例，端口改对后才测得到。

## 5 反例（不许放宽的地方）

- 有元素端口的循环形式（序列是数组的 `LOOP for`）**仍要**按"元素 + 每状态一个"映射。
  把映射改成与循环形式无关，就是这 7 条红真正在说的事被丢掉。
- `no-cotangent-space` 不许整句删掉或降级：一个**本该有**余切空间的状态端口没有余切空间，
  仍然是缺陷，仍然要说出来。改的是"哪个端口对应哪个状态"，以及"零切空间的状态是带着走
  还是拒绝"，不是"不再报这类错"。

## 6 归组

属 **A**：引入点 `e49bdeae88`，随 `46e257734d`（merge `lane/range-ri`）进 `develop`
（2026-11-23 23:39），与 `red-bisect` 的 A=229 里 `lane/range-ri` 的 122 条同源。
不在这条车道修。