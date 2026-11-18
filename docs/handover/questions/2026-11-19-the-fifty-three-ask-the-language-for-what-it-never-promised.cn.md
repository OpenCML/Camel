# 那 53 条在要求语言没承诺的东西

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `5695e055c`）
**状态**：待裁（本页写死一条默认建议）
**触发**：`linear` 的 113 条红里，53 条被逐一归因到一处判定；判定读的是形参，不是类型，于是「读者读错层」这个解释对它们不成立。

## 1. 现象

53 条（`var_grad` 33 + `var_rule_subpasses` 20），诊断逐字：

```
Invalid call to 'with_grad' with argument types <m: P> (x: float[...], var cnt: int) => (float, int)
uses var cnt: an operator calls a function argument with values, and a function with a var parameter
must be given a variable.
```

## 2. 条件逐字，以及它拒的是什么

`src/compile/gir/builder_operators.cpp:3972-3985`：

```cpp
bool takesVariable = false;
for (size_t i = 0; i < func->normTypesCount(); ++i) {
    takesVariable = takesVariable || func->normIsVarAt(i);
}
if (takesVariable) {
    diags_->of(SemanticDiag::ArgumentsRejected)
        .atOrigin(input.expr->origin())
        .commit(reportGroup, type->toString(),
                "an operator calls a function argument with values, and a "
                "function with a var parameter must be given a variable");
    throw BuildAbortException();
}
```

**它拒的是「给了值」，而算子那一侧必然给值**：`input` 是**算子自己形参表**里的一个实参，`takesVariable` 问的是「这个**函数实参**有没有 `var` 形参」；只要有，算子**用值调用它**这件事就不成立——`var` 形参要从调用点拿变量，而算子内部没有变量可给。调用者在自己那行写了什么，对这条判定没有影响。

## 3. 为什么改读者不可能让它变绿

`takesVariable` 遍历 `normIsVarAt(i)`，问的是**形参是不是 `var`**，与出口类型无关。所以 `declaredFuncType` 那一类改动**改得了印出来的类型，改不了判定**：加入它之后 53 条的计数与分布一字未变（实测）。⇒ **这是语义层的结论，不是读者层的**；把读者改十遍也换不来一条绿。

## 4. 两条路

- **(甲) 改那 53 条用例的写法。** 它们要的是「算子接受带 `var` 形参的函数」，而这要求算子**把它当变量调用**；`std` 的 `with_grad` 做不到，因为它内部那次调用点没有变量。⇒ 按现在的语言，**是用例写错了，不是编译器错了**。
- **(乙) 让 `with_grad` 支持变量调用。** 那是 `std` 的语义扩展：算子要把「我有一个变量」的能力向下传，并让 `var` 形参在算子内部有一条真实的写入路径。**远超这一笔**，而且要先由作者定语言上是否允许。

## 5. 默认（写死：走甲，但**动用例要作者点头**）

**默认甲**，因为「改用例」等于**改判据**，那不是车道能自己决定的事：53 条现在是语言拒绝的正确答案，把它们改绿只能靠改语言文字，而改语言文字会把一条正确的拒绝变成一条错误的接受。**所以：页在这里，等作者点头，不动用例。**

**与 sNaN 那一页同形**：那页说「向 IEEE 754 索取它未承诺之物」的用例会永远红，正确的收尾是说清它绿不起来；这 53 条同理——**它们向这门语言索取「算子内部也能给变量」，而语言现在没有承诺这件事**。车道的收尾不是让它绿，是把它写成一条可以被裁决的句子。

**于是 113 分成两半**：**44 条是真缺陷**（分支检查把打包后的出口当语言类型，读者可修），**53 条是正确的拒绝**（用例该改，等作者）。这是今晚这条线的终点形状。
## 7. 更正（2026-11-19 晚，作者 `af2cab221`）

**作者 `af2cab221`：「with_grad keeps a var parameter by promise, the 53 are defects; packing is total, so no bit, land it with the derivation」。**

⇒ **这 53 条是缺陷，不是正确的拒绝。** `with_grad` **按承诺**保留 `var` 形参，所以「算子内部没有变量可给」不是**语言的限制**，而是 **`with_grad` 该做到而没做到的事**。本页第 1–3 节的现象与条件仍然成立（它确实在那里被拒、确实由 `takesVariable` 判定），**错的是第 4、5 段的结论**：第 4 段 (甲)「改那 53 条用例的写法」**作废**——改用例会把一个缺陷写成正确答案；第 5 段「默认甲」随之作废。要修的是 `with_grad`（以及同形的算子）把变量交下去，不是用例。

**⇒ 打包是全的，所以没有位。** 本页第 3 节说「位回答『这个图打没打包』」，那是按当时的测量写的；作者裁定打包**对所有图都成立**，因此「该不该打包」这个问题不存在，`graphExitTypeOf` 对每一个图都是对的。⇒ 入口图**也**该打包，所以 `builder.cpp:155/167` 那两处旧的 `funcType()->exitType()` 才是**错的那一个**；`feat/compile` 那 5 条（`(0, 1)`）不是新改动引入的，是它们一直在掩盖的真错。
