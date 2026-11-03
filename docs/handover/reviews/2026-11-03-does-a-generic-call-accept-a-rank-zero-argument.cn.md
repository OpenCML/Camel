# 泛型函数调用的实参匹配，要不要照 5.4 收秩 0？

原则 5.4 的原话是"`T[..]` 包括秩 0，秩为 0 的数组就是那个元素，运行期是同一个值，类型上也没有单独的'秩 0 数组'"。这句话在**算子**那一层是实现了的：`x * s`（`float[..]` 乘 `float`）和 `s * x` 都能编、都能跑。在**泛型函数调用的实参匹配**那一层没有实现——一个形参写着 `T[..]`，一个 `float` 实参就不匹配。同一句话不能在一层成立、在另一层不成立。

## 一、障碍原文（三行探针，原文照抄）

```camel
import { sum_to } from tensor
let xs = [1.0, 2.0, 3.0] as float[..]
println(`sum_to(6.0, xs) = ${sum_to(6.0, xs):.1f}`)
```

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (float, float[]).
   --> sum_to_rank0_blocked.cml:26:40
help: Check the number and types of arguments to ensure they match one of the available overloaded
      function signatures. Available overloads:
          <tensor:sum_to>: (t: T[..], like: S) => S
camel: 1 error
```

探针两份，留给要改匹配器的那条车道当判据（**别自己再造一遍**）：

- `D:\Temp\mulsc-probes-pending\sum_to_rank0_blocked.cml` —— 今天必红（上面这句），改完必须答 `sum_to(6.0, xs) = 6.0`。
- `D:\Temp\mulsc-probes-pending\sum_to_rank0_ok.cml` —— 今天就这样，改完**一个字节都不许变**：

  ```
  sum_to(xs, 6.0) = 6.0
  sum_to(xs, xs)  = 1.0 2.0 3.0
  ```

## 二、作者 12:10 那条裁法为什么卡在这里，以及三条试过的路

要让 `f(x: float[..], s: float) = sum(x * s)` 的 `d/ds` 是 `6.0`，律的右式必须让归约的答案类型跟着**另一个操作数**走：`sum_to::<T, S>(t: T[..], like: S): S`（这个声明编译通过，也已经写好）。可一旦某个操作数是数字，`ct * w` 本身就是 `float`，那句调用变成 `sum_to(float, array)`——正是上面那条 `NoMatchingFunction`。三条路，各自读数：

| 写法 | 读数 |
|---|---|
| `<X, W>(x: X, w: W)` 律 + `expandRuleByName` 按**实参类型**实例化（已实现，`rule_expand.cpp` 能按 `(float[..], float)` 构建） | `No matching function found for argument types: <> (float, float[..])`；`float[3]`×`float[3]` 那一格仍 **2/2 绿**，说明实例化改动本身是对的 |
| 保留 `<T>` 律，只改 `sum_to::<T, S>` | 律实例化在 `T=float`，`S` 被钉成 `float[..]`，**回到 `d/ds = 0.0`** |
| 只删 `sameOperandShape`（照裁法先删） | 直接 **`d/ds = 0.0`**——这个数不能留，所以没提交，树退回 `75368d5d0` |

中间那条和第三条记在这里，只有一个用处：它们是"不能留"的理由。9.9 说跑通却给出错值不允许，`0.0` 就是那个错值。

## 三、默认：收

**默认照 5.4 收秩 0**：`T[..]` 形参接受秩 0 实参，`float` 就是 `float[..]` 的一种，匹配用它自己那句"可赋值性"（`assignableFrom`）而不是"相等性"。这也是作者 12:10 裁法唯一缺的一块——律一行都不用改，`sum_to::<T, S>` 就能写，`d/ds` 直接是 `6.0`。

按默认要改的是**泛型调用的类型参数绑定**（在 `src/compile/gir/` 的匹配器里，把 `T[..]` 形参与秩 0 实参按可赋值性对上），这件事**已另派一条车道做**；`lane-mulsc` 不碰那处，本页用于留档与作者复核。那条车道落地后，本页第二节的第三种写法在它之上重做一遍，`d/ds = 6.0` 就是把两张页一起关掉的那条读数。