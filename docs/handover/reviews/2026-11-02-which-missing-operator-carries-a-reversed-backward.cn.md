# 倒序的 backward 该由哪一条来承载：新增 `:struct/reverse_arr`，还是让 draft 能造动态下标读？

**状态**：待裁；三条路都不动，见第 3 节。

## 1. 事实（今天量过的）

- **A 缺算子**：`:struct/*` 全集是 `append_arr*`、`concat_arr`、`contains_*`、`copy_arr`、`iter_str`、`last_arr`、`len_*`、`range`、`zip`（`src/builtin/operators/struct.cpp`）——**没有数组反转**。
- **B 缺算子**：同样没有"按运行期下标读数组"的算子；唯一与索引有关的是 `:op/set_index`（**写**）。`xs[i]` 的读走 `src/compile/gir/builder_operators.cpp` 的路径降级，那是编译器内部的构造，`draft` 层调不到。
- **C（调已有的 `stdlib/array.cml#reverse`）也不通**：机制存在——`draft.addCallNode(type, camel::runtime::GCCallBody)`，现成调用点在 `modules/transforms/split.cpp`（复制源图里的 `Call`，`body = source->nodeBodyAs<GCCallBody>(ref)`）。但那**复制一个已经存在的调用**，不是"按名字调一个 CML 函数"：`reverse::<T>` 是泛型，要从 C++ 造一个对它的调用，得在胶带元素那个类型上**实例化**一份，而程序里并没有这个实例。所以 C 不是最小，它比 A/B 都大。

## 2. 三条路的成本

| 路 | 做法 | 成本 | 契约 |
| --- | --- | --- | --- |
| **A** | 新增 `:struct/reverse_arr`（执行器 + 语义 + 规则），`transpose` 与 `cutLoopGraph` 各多一个节点 | 算子层一处，用户可见 | `reverse([]) == []`、`reverse([1,2,3]) == [3,2,1]` |
| **B** | 让 `draft` 能造"动态下标读"（内部 API 或新算子） | 内部机制外露 | 无用户可见契约 |
| **C** | 调 `stdlib/array.cml#reverse` | 要实例化泛型函数：比 A/B 都大 | 有，但够不着 |

## 3. 我方默认

**优先 A**；C 验过不通（第 1 节），B 是把内部机制暴露出来、没有用户可见契约。**在你裁之前，A、B、C 都不动。**

## 4. 一句要写进去的实话

**无论 A 还是 B，`split.cpp` 的 backward 半程都要重做一遍**：胶带今天是 `Accs(loop, 1)` 在循环**外**读的闭包成员，按轮倒序要求它成为循环的**不变输入**（或 With 输入），`n` 还要靠 `len_arr`、`n-1`、`:struct/range` 三个节点拼出来。这不是"换个口"的事。

## 要作者答的一句

**「按轮倒序需要一个『倒序的数组值』：`:struct/*` 没有数组反转（A 缺），也没有运行期下标读（B 缺），而调已有的 `stdlib/array.cml#reverse` 要在胶带类型上实例化一个泛型函数（C 比 A/B 都大）。我默认新增 `:struct/reverse_arr`（有用户可见契约）；`split.cpp` 的 backward 半程无论选哪条都要重做，先把 P1-9 的四处 lift 做完再动它。」**