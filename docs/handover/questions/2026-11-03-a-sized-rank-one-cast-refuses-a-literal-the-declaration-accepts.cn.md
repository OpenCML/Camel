# 一个定长的秩一 `as`，拒了声明和传参都接受的同一个字面量

**问：`[1.0, 2.0, 3.0] as float[3]` 被拒、而 `let v: float[3] = [1.0, 2.0, 3.0]` 与 `g([1.0, 2.0, 3.0])`（`g(x: float[3])`）都收——这是有意的（`as` 比声明更窄），还是缺陷？默认建议：**缺陷**，`as` 应当和另外两处同一条规则；修法是让 `as` 走声明那一条，不是把声明改窄。**

## 1. 八条读数（同一份二进制，同一棵树）

探针跑在 `bc063b9e7` 上，每个都是独立程序，`expect_exit = 0`，跑的是一次临时计划（已删）。

| # | 写法 | 结果 |
| --- | --- | --- |
| t1 | `[1.0, 2.0, 3.0] as float[3]` | **拒** |
| t2 | `[1.0, 2.0, 3.0] as float[]` | 过 |
| t3 | `[[1.0, 2.0, 3.0]] as float[1, 3]` | **过** |
| t4 | `[[1.0, 2.0], [3.0, 4.0]] as float[2, 2]` | 过 |
| t5 | `[1.0, 2.0, 3.0] as float[2]` | 拒（长度也不对） |
| t6 | `[1, 2, 3] as int[3]` | 拒（不是浮点特有） |
| t7 | `[1.0, 2.0, 3.0] as f64[3]` | 拒 |
| t8 | `[1.0, 2.0] as f64[2]` | 拒 |
| t9 | `[[1, 2, 3]] as int[1, 3]` | **过**（`int` 也一样：嵌套过、平铺拒） |
| u1 | `let v: float[3] = [1.0, 2.0, 3.0]` | **过** |
| u2 | `let v: float[1, 3] = [[1.0, 2.0, 3.0]]` | 过 |
| u3 | `g([1.0, 2.0, 3.0])`，`func g(x: float[3])` | **过** |
| u4 | `h([[1.0, 2.0, 3.0]])`，`func h(x: float[1, 3])` | 过 |

拒绝原文（t1，`3:13` 指着字面量的起点）：

```
The literal '[1.000000, 2.000000, 3.000000]' cannot be represented as type 'float[3]':
the value cannot become that type. A literal the type cannot hold is not wrapped around
or cut off, and a value is never cut to fit a type. Write a value the type holds, or give
the literal a wider type.
```

## 2. 三条形状，只有一条红

上表按形状重排，结论只有一句——**同一件事在三处用了两套判据**：

| 同一件事：把一个 3 元素的 `f64` 字面量放进 `float[3]` | 结果 |
| --- | --- |
| 声明注解 `let v: float[3] = [...]` | 过 |
| 调用实参 `g([...])`，`g(x: float[3])` | 过 |
| `as` 转换 `[...] as float[3]` | **拒** |

而且 `as` 自己在**秩二**上是收的（t3、t4、t9）：`[[1.0, 2.0, 3.0]] as float[1, 3]` 的目标形状里也有一个 `3`，也是同一个字面量的三个元素，也是 `f64` → `float`（`f64` 的打印名就是 `float`，t7 写 `f64[3]` 报错里印的正是 `float[3]`），它过了。

⇒ **元素类型没变、长度没变、只差"秩一 + `as`"这一个变量，结果就从过变拒。** 这不是"秩一与秩二规则不同所以两个秩要分别写"能给的说法：`as` 与声明之间就已经不一致了。

## 3. 代码里确实有两条路（只读，未改）

`src/compile/gir/builder_control.cpp:406-443` 的 `arrayLiteralFitsShape` 开头就按秩分叉：

```cpp
const std::span<const ExtentForm> extents = type->extents();
if (extents.size() < 2) {
    return holdsStatedLength(type, array.get());   // 秩一：只比长度
}
// 秩二及以上：flattenLiteral 摊平、逐层比较每个定长 extent、收集叶子
```

`holdsStatedLength`（同文件 351-354）只做一件事：`array->raw().size() == statedLength(type)`。t1 是 3 对 3，这一支**本来应当返回 true**，所以拒绝不是从这里发的。发这条诊断的是 `Builder::rejectLiteralData`（`src/compile/gir/builder_diagnostics.cpp:1688`），而它拿到的 `reason` 是最后那个兜底分支 `"the value cannot become that type"`（`:1715`）——**不是** `literalNotFitReason` 给出的具体理由，也就是说"装不下"这条具体判断根本没被触发过，是上游先决定拒、再来要理由。

⇒ 这条诊断的措辞（"写一个类型装得下的值，或者给字面量一个更宽的类型"）**在 u1/u3 上是被自己否证的**：那个值装得下，而且不用更宽的类型。

## 4. 为什么库里 0 处用秩一定长、53 处用秩二定长

| 写法 | 命中 |
| --- | --- |
| `as float[N]`（秩一定长） | **0** |
| `as float[M, N]`（秩二定长） | **53**（`stdlib/array.cml` 6、`rulesets/vmap.cml` 17、`signatures/tensor.cml` 21、`tensor.cml` 9） |

于是"惯例"这个说法站不住：**秩一那个写法一次都没活下来过**（唯一一处是 `5b4f2e1a9` 新写的 `sum_to` 例子，编不过，已在 `bc063b9e7` 改成 `as float[]`）。53 处秩二不是偏好，是**唯一能过的那一半**。这条读数本身就是"秩一定长 `as` 不成立"的旁证：写了 53 处秩二、0 处秩一，只可能是秩一从来编不过。

## 5. 三条路与代价

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲（默认）** | 判为缺陷：让 `as` 走声明那条（`compositeDataInType` + `holdsStatedLength`），秩一与秩二都收 | 要找到 `as` 早于 `compositeDataInType` 的那道判断（§3 指出的"上游先拒"），改动在一处；风险是 `as` 变宽后，某些原本靠它挡住的错写法也能过了，要一条用例证明"长度不对仍然拒"（t5 就是那条用例） |
| 乙 | 判为有意：`as` 是显式转换，比声明更严格，秩一定长**要求**写 `float[]` | 那就得把 §2 那张表写进设计原则，并解释为什么秩二可以——今天解释不了；且 t7/t8 的 f64 同类型也拒，与"显式转换"无关 |
| 丙 | 不动，只把库里写法固定成 `as float[]`（今天的状态） | 0 代价，但留下一个"同一件事两个答案"的洞，下一个人还会写 `as float[3]` |

**默认甲**。理由是 §2：**一个已经过、一个被拒，被拒的那个还与自己的诊断措辞矛盾**；设计原则里没有一条说 `as` 要比声明窄，而 `as` 在秩二上又比声明宽。这不是取舍，是同一把尺子量出了两个数。

## 6. 默认已落地（本页仍在等作者裁定）

§5 的**甲**已经做了，一处改动，没有第二份判据：

`src/compile/gct/builder.cpp#statesARank` 原来第一句是 `if (extents.size() < 2) return false;` —— **秩一永远不是"一个形状"**。于是 `[1.0,2.0,3.0] as float[3]` 走普通构建得到 `float[]`，再由 `as` 按 `castSafety` 拒掉；而声明和传参走的是 `visitDataInto`，那里 `float[3]` 被同一句判成"不是形状"、同样落到普通构建，**只是它们后面还有一道注解 CAST 把它救回来**，`as` 没有那一道。§2 的三行表就是这两条路的差。

改成"所有 extent 都是常量就是一个形状"（秩一也算）之后，三种位置都走 `visitRankedArrayInto`：

| 读数 | 结果 |
| --- | --- |
| `take3([1.5, 2.5, 3.5] as float[3])`，`take3(x: float[3])` | 过（值真的是 `f32[3]`，`take3` 只收这个） |
| `[1.5, 2.5, 3.5] as float[2]` | **仍拒**，理由具体：`The array literal written here has extent 3, but the position states 2 for this dimension.` |
| `[[1.5, 2.5], [3.5, 4.5]] as float[2, 2]` | 过（秩二没被弄坏） |

守卫不是靠 `rejectLiteralData` 的兜底话，而是靠 `visitRankedArrayInto` 里本来就有、秩二一直在用的那条逐层 extent 检查——**长度检查没有拆，反而从"发散到兜底"变成"点名哪个维度差多少"**。

四层判据：`feat/compile` 2193 / **2188 pass / 2 fail**（两条红是 `transpose_loop_escape.two_values_of_two_kinds` 与 `.second_state_of_the_same_loop`，非本改动带出）、`feat/parse` **115/115**、`feat/compile/transforms` **80 = 77/2/1**、`feat/transforms/split` **19/19**。永久用例三条在 `test/cases/compile/casts/array_literal_*.cml`，`casts.plan.toml` 各一条。

**留给作者两件事**：

1. **`arrayLiteralFitsShape` 那一支没动**（`builder_control.cpp:420`：`extents.size() < 2` 只比长度、不符时返回 false 落兜底）。改完之后 `as` 不再走到那里——字面量在 GCT 就已经是 `float[3]` 值。所以"具体理由"是 `visitRankedArrayInto` 给的，不是这一支给的。这一支只剩在我没找到的路径上才会露兜底话；要不要另外硬化，按"发现第二份先报再统一"的规矩**报上，未改**。
2. `statesARank` 现在对**所有**秩一定长位置生效（声明、传参、返回、循环状态一样）。这是"同一条规则"的含义，也意味着 `[]` 进 `float[3]`、`[a, b, c]`（元素是计算值）进 `float[3]` 这几条以前走普通构建的路径现在都走 rank 构建——四条判据无红。若作者要缩小影响面，可把放宽只用在 `As` 分支，但那会是第二处判据，本车道没做。