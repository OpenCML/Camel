# 前向替换落在哪一层，以及它缺的那一块

日期：2026-10-31　车道：`lane-recheck`　状态：**未落地**（这一页是地图，不是结论）

## 判据

**替换的验收不是“跑通了”，是“缺一条规则时，诊断落在缺它的那个节点上”。** 一个能跑出正确数字的
替换，若在 `x / w` 处悄悄用一个猜出来的公式顶上，它验的是“我认得 `/`”，不是“索引里有没有 `/`”。

## 落点（已定，(`a`) 裁定）

`reinterp<H>` 与 `with_grad` 同层：**`modules/autodiff`，紧挨着 `buildGradientGraph`**。依据写在
`modules/autodiff/pullback.h` 的头注释里：“After macros have run, a program is a static graph, so
derivatives are a graph rewrite rather than a runtime tape.” 微分是**宏之后对运行期图的重写**，
前向替换是同一件事的另一个方向：不是 `grad`，是 `jvp`。

已经有的入口（下一笔从这里起）：

```cpp
// modules/autodiff/pullback.h —— 反向的入口，前向要写的是它的对称物
GradientGraph buildGradientGraph(
    const camel::core::context::context_ptr_t &context,
    const camel::runtime::Program *program,
    camel::runtime::GCGraph *function,
    bool withValue, bool hasAux = false);
```

- 图：`Program::graph(GraphId)`、`GCGraph` 的节点遍历、`DraftWritings`（宏之后图的写入口）；
- 索引：`camel::parse::ast::lookupRule(family, head, arity)` 与 `rhsCalleeOf(family, rule)`
  （`8d882ea74`：规则调的那个函数，从规则本身读，不在 C++ 里另抄一份）；
- 键：`(family, head, arity)`，`-x` 与 `x - w` 是两个原语。

## 缺的那一块（下一笔第一个要答的问题）

**名字怎么在宏这一步变成一个图 id。** 运行期的 `Program` 没有按名字取函数的入口（只有
`graph(GraphId)`、`root()`、`graphs()`、`calleeOf`）；`Builder` 那一侧有名字解析
（`ownNativeEntity(const Reference &)`、导入表），但 `buildGradientGraph` 拿到的只有
`program` 与 `function`。

三条可能的路，按代价排：

1. **从规则的图里取**：规则值带着它的两侧图（`RuleObject` 的 `lhs`/`rhs`），右端图里那个对
   `add_tangent` 的调用节点已经指向实例化后的图 id——按 `rhsCalleeOf` 的名字找不到它，但可以按
   “右端图里第一个调用节点”找到。**不需要新入口。**
2. **让变换在 `Builder` 里跑**（与宏算子同处），那里名字能解析：`addCallNodeOf(calleeGraph)` 已经
   能建调用节点。代价是变换要拆成两份（构造侧与重写侧）。
3. **给运行期 `Program` 加按名字取的入口**——**跨层，除非前三层都走不通，不加**（作者的纪律：
   只用编译期已有的能力）。

## 验收（缺一步不算完）

1. **前向**：`f(x) = x*x + 3/x` 在 `x = 2` 处，`f = 5.5`，`f' = 4 - 0.75 = 3.25`。两个数都要打出来。
2. **缺规则**：`rulesets.probe_missing`（已有，四条、缺 `/`）走同一个 `jvp`，**诊断必须落在 `/`
   那个节点的 `file:line` 上**——这是 `probe_missing` 那条红“从替换层出来”的形态。
3. **还原**：第 2 步的红消失，第 1 步的两个数不变。