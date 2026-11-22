# 一个未使用的类型参数，该在律上报一次还是每侧面一次？

一条律在编译器里是**三条声明**：`name__lhs`、`name__rhs`，有条件的还有 `name__when`。每条都把律的头部类型参数当作自己的，所以"这个类型参数没人用"这条判据每侧面各判一次、各报一次。今天在 `rule unused_head_t::<T, X>(x: X) { sum(x) => (ct: X) => sum(x) }`（`T` 两边都没写）上实测：

```
warning[SemanticDiag::TypeVariableUnused]: The type parameter 'T' is declared at the head but never written in the signature.
warning[SemanticDiag::TypeVariableUnused]: The type parameter 'T' is declared at the head but never written in the signature.
```

**同一条错，两条话**；带 `when` 的律是三条。而用户在源文件里只写了一个 `T`。

## 这与今天另一件事同族

作者 2026-11-03 13:30 那条裁定说的是：**能脱离实物漂移的计数要钉在实物上**。同一个错报 N 次是同一个病的另一个面——话数与用户写下的东西脱了钩：用户写一个 `T`，编译器说三句话。今天同一族的另外两个例子是残余表的重复成员（`[x, x, s]` 把参数挤出槽位）和 `split_two_params` 里"五个残余"的台账。

## 默认：一个未使用的类型参数，只在律上报一次

**默认：一个类型参数未使用，只在律的头部报一次；位置取律本身，不取 `__lhs` 侧面。**

理由：用户面前"一个错 = 一条话"。侧面是编译器自己写的声明，不是用户写的东西，让它们的数量决定告警数量，等于让**实现结构**决定**用户看到的读数**——这正是今天是反复出现的那类缺陷。

## 副作用（要写清，所以它是独立一笔）

- 会改既有的告警**计数**与 **goldens**：凡是有"未使用类型参数"用例的 plan，期望的告警条数从 N 变 1；
- 需要一个位置：律本身的 token 范围（`rule` 关键字或头部 `::<...>`），不是某条侧面的 `FuncType`；
- 因此**冲刺后做**，与残余去重（`2026-11-03-should-a-value-read-twice-be-one-residual.cn.md`）同一批。

## 与已落地那一笔的边界

`ac3afe8d0` 修的是**另一件事**：律的 `T` 只在右面写（`(ct: T[..])`），而判据只读单条侧面的签名，于是在 `__lhs` 上误判为未使用——仪器输出是

```
UNUSEDHEAD decl=mul_tensor_t__lhs name=T body=0 when=0
UNUSEDHEAD decl=mul_tensor_t__rhs name=T body=1 when=0
```

改后 `mul_tensor_t` **一条都不报**（改前每个编译 `rulesets.transpose` 的程序都有一条），而上面那个真未使用的律**仍然报**。那一笔改的是"判据读哪块书写面"，**没有**改"一个错报几条"——本页问的才是后者。