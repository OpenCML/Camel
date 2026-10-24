# 数值数组的 `!=`：核心的整值相等与库的逐元素比较，谁赢由元素类型是否述明决定（2026-10-25，h458 lane）

本文件只记读数与要请作者定的一条。全部实测在本 lane 的树（`handover/h458-rank0-nan`，`dc3c8968b`，
host `88zjwei`，`out/latest/bin/camel <case> std::nvm`，`timeout -k 5 60`）。本 lane 没有改任何算子分派、
没有改用例，也没有改核心或库的语义。

## 一、实测：同一个比较，两种类型

```
module probe_ne                                            stdout
import { Tensor, cast } from tensor                        A known-rank !=
func main(): int sync {                                    false
    let x = cast([1.0, 2.0, 3.0], 'float32')               B known-rank ==
    println('A known-rank !=')                             true
    println(x != x)                                        C Tensor-typed !=
    println('B known-rank ==')                             [false, false, false]
    println(x == x)                                        D done
    let t: Tensor = x
    println('C Tensor-typed !=')
    println(t != t)
    println('D done')
    return 0
}
```

`x: f32[3]` 的 `!=` 给 `bool`（整值相等），`t: Tensor`（元素类型未述、秩未知的数值数组）的 `!=`
给 `[false, false, false]`（逐元素，`modules/tensor/ops/elementwise_ops.cpp:535` 的 `__neq__`）。
两条路都活着，**选哪条只看元素类型述没述**：核心的数组算子要元素类型能匹配，未述元素类型的值
匹配不上，于是落到库的泛型逐元素算子。

把 `softmax` 插进来就能看到这个分派在实践里怎么切：

```
let t: Tensor = x                                            A Tensor-typed !=        -> [false, false, false]
let s = softmax(t, 0)                                        B softmax(Tensor) !=     -> false
println(t != t); println(s != s)                             C done
```

`softmax` 的结果类型由 `modules/tensor/ops/reduction_ops.cpp:162-180` 的 `softmaxInfer` 给出，
它把元素类型固定成 `f32`（`tensorOf(f32-or-f64, 输入形状)`），所以 `s` 的 `!=` 命中的是核心的整值相等。
即：**同一个值，先 `let s: Tensor = softmax(t, 0)` 再比较就是逐元素，直接用 `softmax(t, 0)` 的结果比较就是 `bool`。**

## 二、这挡住的用例（h450 已按他们的判据改写，本 lane 不改）

`test/cases/modules/tensor/numeric_contract.cml:57` 的 `sum(s != s)`：`s` 来自
`softmax(pick(...), 0)`，于是 `s != s` 是 `bool`，`sum(bool)` 报

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (bool).
   --> .../numeric_contract.cml:57:39
help: ... <tensor:sum>: (t: Tensor) => float / <tensor:sum_axis>: (t: Tensor, axis: int, keepdims?: bool) => Tensor
```

h450 的 `docs/handover/questions/h450-spelling-and-print-form-leftovers.md` 第二节已把这一行改写成
打印整条轴（并写明 `is_numeric` 拒绝 `bool` 是对的）。本 lane 只报告：**这条改写让用例变绿，但没有覆盖
下面这条语义问题**——develop 上 `Tensor` 是独立类型，`x != x` 一律是逐元素的；翻转后同一个值的 `!=`
在「元素类型述明与否」之间摇摆。

## 三、请作者定

1. 数值数组的 `==`/`!=` 应当是**整值**（核心 `ArrayType` 的相等，与 `T[]` 一致），还是**逐元素**
   （`modules/tensor` 注册的 `__eq__`/`__neq__`，与 develop 的张量行为一致）？
2. 若两者都要可用，那么「元素类型未述的 `Tensor` 落逐元素、述明的 `f32[..]` 落整值」这个分派
   是不是想要的？今天它是**隐式**的：写不写元素类型改变了比较的含义与结果类型。
3. 若判为整值比较：`numeric_contract.cml:57` 这类「逐元素计数 NaN」的写法要改用库的算子拼写
   （`ne(s, s)`），需要确认这类用例的终态拼写。
4. 若判为逐元素比较：核心数组的整值 `==`/`!=` 与库的逐元素算子需要一条明确的优先级规则，
   否则 `f32[..]` 仍然会被核心收走。

本 lane 的边界说明：这条与「秩 0 打印死循环」（`src/core/rtdata/print.cpp`，已按秩 0 裁定修好，
见 `dc3c8968b`）是两件事；死循环的根因已定位、已修、有前后读数，本文件只为这一条**没有裁定可依**
的语义分派请裁。