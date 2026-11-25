# 问作者：Range 上的方法在 prelude 之外不可用，这是缺口还是写法错

2026-11-25。发现于 `lane/3a-transforms-autodiff`（`C-1124-191`）。最小复现已在本树 `camel v0.2.47`（`out/latest`）上实跑，不是推演。

## 一、最小复现

`gap.cml`，两行：

```camel
func dbl(x: int) => int { x * 2 }
func main() => int sync { return (0 .. 3)->map<dbl>()->len() }
```

真实输出（exit 1）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <(x: int) => int pure> (Range).
help: ... Available overloads:
          <:mark/map_arr>: <f: (item: T) => U> (xs: T[]) => U[] uses
```

## 二、为什么这是缺口，不是写法错

1. **同一拼写在数组上是对的。** `let xs: int[] = [1, 2, 3]; xs->map<dbl>()->print()` 打印 `[2, 4, 6]`。只有接收者是 `Range` 时失败，唯一候选 `:mark/map_arr` 要的是 `T[]`。
2. **一个字符不差地应验了 `src/stdlib/prelude.cml` 里的注释**（约 1080 行）：Range 的 `map`/`filter`/`reduce` 只是 prelude 的**导出**；调用方模块是按同名 **operator 组**解析名字的，而该组没有 Range 规则，于是 `:mark/map_arr` 成了唯一候选。
3. **换一个方法会换一张诊断、同一条缺口。** `(0 .. 3)->len()`：

```
error[SemanticDiag::WhenConditionUnmet]: No candidate of 'len' applies to (<> (Range)):
  every candidate's 'when' condition is false.
  Candidates: (x: A) => int — when is_array::<A>(); (x: C) => int — when is_map::<C>() || is_set::<C>()
```

即 Range 不在这些 `when` 的覆盖内，而不是参数类型打错。

## 三、必须分开记的两个可见面

| 诊断 | 含义 |
|---|---|
| `NoMatchingFunction` | operator 组里没有 Range 的规则（`map`/`filter`/`reduce` 这类） |
| `WhenConditionUnmet` | prelude 里那个 `when` 条件不含 Range（`len` 这类） |

**两个面、一条缺口：Range 上的方法在 prelude 之外不可用。** 报错信息会让人以为「自己写错了」。

## 四、今天的唯一出路（已证）

```camel
(0 .. 3)->to_array()->map<dbl>()->print()   ⇒  [0, 2, 4]
```

元素个数与顺序不变。这也解释了为什么 `lane/3a-transforms-autodiff` 里那 8 条用例**期望原文一字不改**就绿了 —— 它们原先根本没有跑到那一行。

## 五、三个可选方向（请作者定）

1. **在 operator 组里给 Range 加规则**（`map`/`filter`/`reduce`/`len` 等），让 `(0 .. 3)->map<f>` 与 `xs->map<f>` 一样能用。改的是语义层，最彻底；代价是 operator 组要认识 Range。
2. **让 prelude 的导出在调用方模块里按名字可见**（即「导出的名字」与「operator 组规则」不只是两张表）。这会动名字解析的规则，影响面最大。
3. **宣布 `to_array` 是 Range 的方法入口**，把这条写进文档与诊断（诊断里直接说 `add to_array` 这个 help），并在测试侧统一走它。零语义变化，代价是把一个可用性缺口固定成设计。

## 六、协调者的默认做法（未获作者裁决前）

- **走方向 3 的写法**（用例与库代码一律 `to_array` 再调方法），因为它已证、零语义变化，且现行 8 条用例就是这样绿的。
- **不**在测试车道里改语义；`C-1124-191` 挂在本页下，等作者裁决后再决定要不要开语义车道。
- 无论选哪个方向，**诊断都该把 `to_array` 写出来**：今天这两张诊断都没有指出可行的那一步，读的人只能自己翻 `prelude.cml`。

## 七、给未来写用例的人一条提醒

「**没跑到的期望**」与「**错的期望**」在红表里长得一样。这 8 条用例的期望值从一开始就是对的 —— 它们只是从未被执行到那一行。判断一条红属于哪一种，唯一可靠的读数是**真实输出的第一句**（不是最后一句 `FAIL`）。
## 决策者答（2026-11-25）

这不是新问题，不用交给负责人。设计原则"范围是值，不是数组"（负责人 2026-11-23）已经写明：`map` 等作用在范围上给出结果数组。11-24 的问题页 `stream-range-value-map-no-instance` 也已答 A，D-1124-60 交给了 `lane/front-facts`：`to_array` 等数组函数对 `Range` 有规则，并且经同一个算子组解析。所以方向是 **1**。方向 3 把缺口固定成设计，与原则相反，不采用；协调者的默认做法同样撤回，库代码和用例不再为绕开这个缺口加 `to_array`（`random.cml`、`str.cml` 正是要改回 `0 .. n` 的那两处）。诊断里提一句 `to_array` 可以，那只是过渡期的帮助，不能代替修复。`C-1124-191` 并进 front-facts 那件。
