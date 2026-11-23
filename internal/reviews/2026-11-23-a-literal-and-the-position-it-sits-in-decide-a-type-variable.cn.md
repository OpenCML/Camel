# 位置上的载荷与字面量，谁决定类型变量的取值

## 事实（`lane/data-arg-migration`，实测）

`with` 的配置参数改成普通参数之后，4 个用例转红：`context.boxed_position.{nvm,fvm,jit,prl}`。

根因已定位到一处：

```
**`src/compile/src/gir/builder_call.cpp` 那个 `if (hasDeferredWith)` 块（约 507–530 行）**

它先 `unifyTypeVars(argumentSignature->exitType(), determinedByPosition(expectedResult), bindings)`,
再按参数给 `with` 实参定类型。而**普通实参的类型在它之前就定了**（同一函数里
「The one place the ordinary argument types are built」那段，约 460 行），按节点自己的类型取。
```

于是同一条写法在改动前后走了不同的顺序：

| | `initial` 在 `with` 里（改造前） | `initial` 是普通参数（改造后） |
|---|---|---|
| 顺序 | 位置先定 `U` = `f64`，`initial` 后定 | `initial` 先定：`0` 定成 `int`，**把 `U` 绑成 `int`** |
| 结果 | `0` 跟着变 `0.0`，`(a, b)` 拿到 `(f64, f64)`，得 `4.0` | lambda 形参成 `(int, float)`，`a + b` 被拒 |
| 读数 | 绿 | 红（4 条） |

`int?` 那几行仍绿，因为两边都是 `int` —— 所以这 4 条红的差别**只在「位置是 `f64` 而字面量写成整数」这一种情形上**才看得见。

## 为什么车道没有自己修

要恢复旧行为，就得把「载荷 → 类型变量」这一步挪到普通实参定类型**之前** —— 也就是让 `determinedByPosition` 的结果对**每一个**站在 `T?` 位置的调用都生效。

而 `builder_call.cpp` 那段注释写明这块是要**收窄**的（收窄才换来「位置的载荷不决定值是哪一形」）。挪回去就是一次语义变更，且方向与既有注释相反。

车道按「不许改语义」落在这里，没有动编译器。

## 需要裁的

1. **「位置的期望类型」与「字面量自身的类型」冲突时，谁说了算？** 设计原则 3.8 与 5.2 没有写这一条判定。
2. 若裁决是「位置说了算」：那 `determinedByPosition` 必须对普通实参也生效，`builder_call.cpp` 那段的收窄需要**重新划界** —— 请指出收窄的是哪一类、放开的又是哪一类。
3. 若裁决是「字面量说了算」：那这 4 条红是**新语义下的正确读数**，用例该改，而 `context.boxed_position` 这组用例的名字与期望值要重写。**这种情况下车道会改用例，但那必须是一句裁决，不是一次重录。**

## 相关的两条，一并请裁（同属「前提已废」类）

- `contains_string`：改后的读数是 `expected exit 1, got exit 0` —— 程序**编译通过**，没有任何建议被印出来。它钉的那句 `write contains<'b'>('abc')` 是旧编译器拒绝时的提示，而新签名 `contains(s: string, sub: string)` 下 `contains('abc', 'b')` 就是合法拼法。**这条用例现在测的是一个不存在的拒绝。** 请裁：退役这条，还是给它一个新前提。
- `with_defaults.callee_unknown`：`format` 现在有具名参数，前提同样不成立。

## 顺带查出的一处（已修，报告备查）

四处源码**注释**仍在教一个已经不成立的写法，已改为现行拼法：
`std/cpp/include/camel/standalone/array.h:1484`、`str.h:142`、
`std/builtin/operators/collection.h:10`、`collection.cpp:208`。

**这四处不改任何行为，但它们会被下一个读者当成判据** —— 而它们教的写法，编译器已经不接受了。
## 决策者答（2026-11-23 05:55）

这页的四条红、`contains_string`、`with_defaults.callee_unknown` 都是把 `with` 参数改成普通参数带出来的，而那次迁移本身不该做：`reduce` 的 `init`、`contains`、`format` 留在 `with` 里是对的（负责人的判据：`->` 只传一个值，其余参数放尖括号便于串成流水线），决策者对 `fill` 那页第二段的推广是错的，已更正，迁移停止、不合入（inbox D-1123-20）。迁移撤掉后旧读数回来，"位置的载荷与字面量谁决定类型变量"不再被这组用例触发，本页关闭；若以后在别处真碰到这个冲突，带着那处的读数另开一页。
