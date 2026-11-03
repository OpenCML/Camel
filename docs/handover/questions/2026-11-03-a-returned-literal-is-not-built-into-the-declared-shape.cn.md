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

## 7. 作者已裁"甲：修"；这是修之前量到的机制（2026-11-03 21:35，`05f9f7439` 的树）

处方是"让 `checkDeclaredReturn` 先给返回表达式期望类型再比"。**这一句已经在了**：`visitExitNode`（`src/compile/gir/builder_control.cpp:4138`）第一句就是

```cpp
ExpectedValueScope expectedScope(expectedValueType_, expectedExitTypeHere());
```

而 `expectedExitTypeHere()` 在函数体里返回 `declaredExitTypeHere()`，也就是 `float[3]`。所以缺的不是这一步。探针读数（`return [1.5, 2.5, 3.5]`，`func three(): float[3]`）：

```
EXITPROBE  declared=float[3] written=float[] array=1/1 eq=0
EXITPROBE2 arr=1 raw=3 fits=1 elem=float
```

三件事因此确定：字面量到出口时是**已经成型的一个常量**（`ArrayData`，3 个元素，元素类型 `float`）；位置要的形状判据 `arrayLiteralFitsShape` **返回 true**，不是它挡的；`declared` 与 `written` 只差 extent 的写法（`float[3]` 对 `float[]`）。

两个"看起来该用"的转换各试一次，读数：

| 走法 | 结果 |
| --- | --- |
| `compositeDataInType(written, declaredExit, gct->at(0))` | 返回原值不变。它第一步 `type->assignableFrom(data->type())`（`builder_control.cpp:854`）就为真——`ArrayType::assignableFrom`（`src/core/type/composite/array.cpp:662-665`）对**源 extent 不是常量**的情形按"接受任意长度"处理，于是"形状相符"根本到不了下面的逐元素构建（905-913 的 `literalElementInType`） |
| `written->convertTo(declaredExit)` | 换掉 extent、**没换元素**：`ElementTypeMismatch: Cannot insert data of type 'float' into a(n) Array with element type 'f32'`。`feat/compile` 从 2 红变 **10 红**（`expected.same_value_in_every_position` 四变体等），**已回滚** |

⇒ 真正的缺口不是"给不给期望类型"，而是**一个已经成型的常量怎么按位置重建**：逐元素那条路（按写下的值转一次、不按落下来的 double 转）从没被叫到，因为 `assignableFrom` 先把话说了。动它 = 动 `compositeDataInType` / `assignableFrom` 这条**声明、传参、`as` 共用**的转换，影响面是"每个位置上的每个字面量"，不是一个出口。

按"面大先报"的规矩停在这里：**回滚后本页仍是未修状态，`feat/compile` 2195 / 2190 pass / 2 fail / 3 known 不变**，两条待补的用例也没进计划。

## 8. 第二次尝试（限 20 分钟，未到点内达标，已回滚）

按裁定把触发条件收窄到一条后改了两处，读数分半：

| 一半 | 结果 |
| --- | --- |
| 长度不符那条（`return [1.5, 2.5]` 进 `float[3]`） | **成立**：报 `the literal writes 2 element(s) and the type states 3`，用例过。`compositeDataInType` 返回 nullptr 后由 `rejectLiteralData` + `arrayLengthReason` 说出长度 |
| 秩一放过那条（`return [1.5, 2.5, 3.5]`） | **仍卡**：`three()` 第 14 行还是 `ReturnNeedsConversion` |

改的两处：① `compositeDataInType` 的早退条件加了"目标有常量 extent 且源在同一维没写 extent"这一条（`assignableFrom` 本身没动），于是它会走到 905-913 的逐元素重建；② 该重建的秩一分支补了 `factory.setExtents(lengths)`（只在位置有常量 extent 时），否则重建出来的还是 `float[]`。

**第①条仍不过说明还缺一层**：逐元素重建已经走到并返回了值，但出口那道 `declaredExit->equals(built->type())` 没成立，所以没装上——下一步要打印 `built->type()` 才能定（是 extent 没进 type，还是 `ArrayDataFactory::build()` 仍按元素给类型）。**不是形状判据挡的，也不是期望类型挡的**（§7 那两组探针仍然成立）。

时间账：20 分钟里约 6.5 分钟是在等主机构建槽（`build 401.0 s` 里绝大部分是排队），只跑了两次 `casts`。回滚后树与 `5a1d52eca` 逐字节相同。