# 返回位置不肯把字面量建成声明说出的形状

## 1. 一句话

`func three(): float[3] sync { return [1.5, 2.5, 3.5] }` 被拒，说返回值是 `float[]`、比 `float[3]` 不精确、要写 `as`。**同一份字面量在声明、传参、`as` 三处都过**。而秩二 `return [[1.5, 2.5], [3.5, 4.5]]` 在返回位置**过**——但它靠的是字面量自己成形，不是位置给的。

## 2. 读数（2026-11-03，主机 `triage`，`cb0147d9b` + 未提交改动）

| 位置 | 秩一 `float[3]` | 秩二 `float[2,2]` |
| --- | --- | --- |
| `let v: float[3] = [...]` | 过 | 过 |
| 传参 `take([...])` | 过 | 过 |
| `as float[3]` | 过（本车道修的，`cb0147d9b`） | 过 |
| `return [...]` | **拒** | 过 |

拒的原文（`test/results/log/20261103_081936_1436229`，`kind = "behavior"` 的用例 `array_literal_in_return_shape`，跑出 `exit 1`）：

```
[Error]: The returned value needs an explicit conversion to the function's declared
return type 'float[3]': the value ('[1.5, 2.5, 3.5]') has type 'float[]', which is
less exact than 'float[3]'. ... Where the value is known to fit, write the conversion:
'[1.5, 2.5, 3.5] as float[3]'.
```

## 3. 为什么秩二过、秩一不过

秩二的嵌套字面量自身就带着每层的长度（`[[a, b], [c, d]]` 没有位置时也是 `float[2,2]`），所以 `checkDeclaredReturn` 看到 `declared == resultType` 就放行——**碰巧**，不是位置给的。秩一的扁平字面量自身类型是 `float[]`（长度留空），于是 `float[]` 对 `float[3]` 就成了"less exact"。

这条读数说明返回位置现在**按秩给出两个答案**，而两个答案都不来自"位置说出的形状"。

## 4. 机制上它不是 `statesARank` 的副本

`checkDeclaredReturn`（`src/compile/gir/builder_diagnostics.cpp:1875`）只比较返回值的类型与声明的出口类型，**不给返回表达式一个期望类型**；声明、传参、`as` 走的是 GCT 的 `visitDataInto`，那里按位置建。所以不是同一句判据的第二个副本，是**返回位置根本没有"期望类型"那一道**。

（顺带一条读数：把这一支加探针跑 `feat/compile`，`arrayLiteralFitsShape` 的秩一分支被走到 **15 次**——说明除了 `as`，还有别的路径由 GIR 去比秩一的长度。那些 15 次全是"长度相符"的放行，没有一次是拒绝。）

## 4.1 仓库自己的注释已经判过这一题

`test/cases/compile/array_shape/rank_zero_slot_return.cml`（一直在跑、一直是绿的）开头写着：

```
// The `return` slot, both writings: a function returning `f32[..]` returning a literal (`return 1.0`)
// and returning a value the program computed (`return v`). The return is a position like a
// declaration: the value takes the declared return type there (design principles 5.4).
```

**"The return is a position like a declaration: the value takes the declared return type there."** 这是当下树里已经写下的规则，秩零那一条按它做、也过了；秩一这一条没按它做。所以本页不是新提一个问题，是**指出一条已经写成文字的规则在秩一上没被执行**——与 `as` 那一题同一类（自证型矛盾）。

## 5. 问

返回位置是否应当与另外三处一致——把字面量建成声明说出的形状？

- **甲（默认）**：一致。理由是 §4.1：树里的注释已经这么写了；再加 §3 的读数，同一份字面量写在同一份声明下，四个位置不该有四个答案，而秩二"碰巧过"本身就说明返回值是被期待那个形状的。
- 乙：不改。那要先把 §4.1 那句注释改掉，并解释为什么秩二可以、秩一不行——这与"显式转换"无关，秩二并没有写 `as`。

## 6. 本车道没改

补这一道要改返回表达式的构建（要不要给 `ExpectedValueScope` 声明类型），面比 `statesARank` 一句大得多，按"**发现第二份先报不统一**"的规矩**报上，未改**。本车道想加的第三条永久守卫（秩一返回位置的形状）也因此没进计划——它得等这条裁定；现在加进去只会是一条红。