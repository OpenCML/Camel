# 卡：`argNameAt` 的 abort 与两条假红（借补丁取读数会毒化别人的读数）

## 一句话

**`616f65b436` 已经在 `origin/integration` 和 `origin/develop` 里**（"autodiff: a built signature carries
names, so a reader never asks for one it lacks"，作者 Zhenjie Wei，`src/stdlib/autodiff/pullback.cpp`，37/4）。
所以"借补丁"不是借一个未合的东西，而是**车道基龄**问题：基点在它之前的车道（例如 `lane/loop-tape-values`，
比 `origin/integration` 落后 151 笔）缺它，于是 `with_grad` 一类程序在构造 pullback 时会撞 `argNameAt` 的不变量。

## 复现（两条，都只读）

```powershell
# ① 不变量：函数类型没有参数名却被问参数名
#    基点缺 616f65b436 的树上，导出「收集循环的梯度」这类 with_grad 程序：
camel <case>.cml std::macro onnx::export_model out=/tmp/m.onnx std::nop
#    ⇒ Camel Invariant Violation: src/kernel/core/type/composite/func.cpp(364)
#       FunctionType::argNameAt(size_t)  Guarantee: hasNames_
#       Reading: The function type records no parameter names.

# ② 确认基龄（不是补丁缺失）
git merge-base --is-ancestor 616f65b436 HEAD   # 非 0 ⇒ 你的基点早于它
git rev-list --count HEAD..origin/integration  # 落后多少笔
```

## 为什么它是"正面"

`616f65b436` 加的正是 `rewrittenFunctionType(...)`（`src/stdlib/autodiff/pullback.cpp`）：transform 自己造的
图（forward、tape loop 的体、pullback、梯度本身）**总是**给形参起名——有源签名就沿用它的名字，没有就
`arg0`/`arg1`——理由写在它的注释里：*"a signature recording no name at all is a fact several readers take as
license to read nothing (the ONNX export names a model's inputs from them), and asking such a type for a name
it does not have is not the program's fault."* 所以它既是那个 abort 的**正面**（不让读者去问一个没有的名字），
也正是下面两条假红的**成因**（名字从此出现在打印出的函数类型里）。

## 两条假红（借补丁时才会出现；不借就绿）

| 用例 | 原名 | 期望 vs 实得 |
|---|---|---|
| `feat/modules/graph_inline` | `a_call_through_a_value_reaches_what_its_type_declares` | 期望 `[type] the value the call runs: <float> (float) => float pure`，实得 `<w: float> (x: float) => float pure` |
| `feat/compile/type_packs` | `type_packs.gradient_type.printed` | 期望打印里没有形参名，实得带名字 |

**验证（各一次，均只跑该计划）**：不借补丁 ⇒ `feat/modules/graph_inline` 3/3 PASS、`feat/compile/type_packs`
25/25 PASS；借了 ⇒ 上述两条各红一条。⇒ 它们是"在旧基上叠了新补丁"的产物，**不是任何真实回归**。

## 取读数的规矩（建议写进派工/skill）

1. 先 `git merge-base --is-ancestor 616f65b436 HEAD`；**非 0 就别借补丁跑整层**——先问是否该把基点上移。
2. 非借不可时，读数里**必须点名**上面两条假红，并附"不借时它们绿"的那一次读数。
3. 更好的做法：把车道基点更新到含该补丁的 `origin/integration`，之后一条假红都不会有。
   （`lane/loop-tape-values` 的干净读数已验证：不借补丁跑 `feat/modules/onnx` = `total: 160, pass: 149,
   fail: 10, known failures: 1`，与借补丁那次**逐名相同**，且 `provenance` 无 `dirty`。）