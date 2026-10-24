# `null` 是可空类型的编译期常量：落地了什么，以及要作者裁定的四件（实测，2026-10-24，h433 lane）

Lane H433（`handover/h433-null-constant`）。规范来源是
`docs/handover/reviews/questions-2026-10-23.md` 的 `### check-program-parameter`：(b) 单开一条车道，
`null` 是可空类型的编译期常量；设计原则 4.4 的参数默认值要求常量表达式，`T?` 位置上的 `null` 就是
常量，今天被拒是实现缺口（`NullData` 只有 `void` 类型）。裁定同时说「修好后对所有 `T?` 的默认值都
成立」，并且 `check` 的签名按 `### generic-rule-check-surface-and-multi-parameter-instances` 一、
写成 `check(rules, samples: int = …, seed: int = …, program: Program? = null)`。

本片落地的是缺口的修（红前实测见报告：`func f(p: int? = null)` 原本是
`error[SemanticDiag::ParamDefaultNotConstant]: ... the value of 'null' is computed at run time.`），
以及 `Program? = null` 这条端到端。下面四件裁定没有覆盖，需要作者一句话。

## 一、原生声明的调用点不接受 `T` 隐式进入 `T?`（与本 lane 的端到端正面相关）

实测（本树，`test/cases/modules/rules/check_program_parameter.cml` 的写法）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (Rule[], int, int, Program).
help: Check the number and types of arguments to ensure they match one of the available overloaded function signatures. Available overloads:
          <rewrite:check>: (rules: Rule[], samples: int, seed: int, program: Program?) => CheckReport
```

把同一个实参交给一个**签名字形完全一样、有函数体**的 Camel 函数就通过：

```camel
func take5(rs: Rule[], s: int, sd: int, p: Program? = null): string { ... }
println(take5([add_zero], 100, 1, current()))   // 通过，打印 some
```

同一个 `Program` 值先落下类型再交给 `check` 也通过（本 lane 的用例就是这样写的）：

```camel
let here: Program? = current()
println(check([add_zero, sum_swap], 100, 1, here))
```

所以 `check` 的 `program` 参数只能用 `Program?` 的写法喂。问题是：设计原则 3.8「值自己进入 `T?`／`T!`」
在**原生声明**（`export func` 无函数体、由 C++ 内核实现）的调用点是否适用？普通函数适用，原生不适用。
如果答案是要适用，那是一个独立缺口（原生调用点缺隐式进入），本 lane 没有顺手改它——判据是它属于
原生声明与签名来源那条线（同文件 349 行记的「一个原生算子只有一份声明来源」），不是可空类型这条。

## 二、`samples` 与 `seed` 的 `…` 取值

裁定把这两个默认值写成 `…`。本片取 `samples: int = 100, seed: int = 0`：既有测试都写 `check(rules, 100, 1)`，
哪一对都能让它们不动；而定裁定 359 行那句 `check([add_zero])` 要求两者都有默认值。请确认这两个数，
或给出要的数（改一处声明即可）。

## 三、字面量「在位置上取该位置类型」落在图的哪一处

设计原则 5.1：字面量在位置上取该位置类型。本片的落点分两种，都是一个 `coerceTo` 咽喉点：

- **默认值、`with` 参数默认值、带位置的调用实参**（走 `visitInPosition`）：字面量**直接**造成该类型的
  常量，图里是 `N [label="null", type="DATA", value_type="int?"]`，就是一个 `T?` 常量，没有 CAST。
- **`let x: T? = null`、`return null`、`??` 的两侧、分支臂**：这些位置先造出 `DATA(void)` 才轮到
  `coerceTo`，所以图保持原样（`DATA(void)` → `CAST(T?)`），本片让那个 CAST **携带** `T?` 的常量
  （`constantOfNode` 现在认它），于是常量求值（默认值、`constas`）看到的是 `T?` 的 `null`。
  这样做的唯一理由是**既有 `null` 用例的图读数一个字节没动**（`let`／`return` 的 `std::gir` 前后一致）。

要裁定的是：CAST 携带常量这条算不算「字面量取位置类型」的正解，还是要求所有位置都把字面量造成 `T?`。
后者的代价是移动既有 `null` 用例的图读数，并且那个已经造出来的 `void` 字面量节点会留在图里没有使用者
（`GCGraph` 没有删节点的接口；`literalDataInType` 的注释正是不愿意留这种节点）。本 lane 选了前者。

## 四、给了 `program` 时并集的次序（有可观测后果）

裁定说「并上程序里出现的类型」，没有说次序。本片是**注册类型在前、程序的类型在后**，这样
`check(rules)` 这一路的读数与参数出现之前逐字节相同。代价是：元组按 `kCheckTupleCap`（64）截断时，
接在后面的 program 类型常常轮不到——本 lane 的用例是特意用「算子组没注册过的类型」把并集测出来的：

```
（不给 program）... sampled sum_swap<Tensor>: proposition equality, evidence samples 100, fired 8 of 8
（给 current()）... sampled sum_swap<Tensor>: proposition equality, evidence samples 100, fired 8 of 8
                 sampled sum_swap<Tensor<float32, [3]>>: proposition equality, evidence samples 100, fired 8 of 8
```

如果作者要的是「程序自己的类型优先」（这个参数的用意看起来是这个），次序要翻过来，被抽到的元组随之
改变；这是本片里唯一一处「裁定没写、我按最小改动选」的地方。

## 另外记两件不需要裁定的实测

- `null` 在**没有可空期望类型**的位置上的诊断没有变：`let x: int = null` 仍是
  `SemanticDiag::LiteralNotRepresentable`（`the value would be truncated`），
  `func f(p: int = null)` 仍是 `SemanticDiag::ParamDefaultWrongType`（`has type 'void', not 'int'`）——
  字面量自己的类型仍是 `void`（设计原则 5.1「字面量有它自己的类型」），没有变成「任意可空类型」的通用值。
- `let n = null` 之后再 `let x: int? = n`、`let y: int? = if b then 1 else null`、模块级
  `let g: int? = null`、`var z: int? = null` 四条路径都实测通过（探针不进树）。