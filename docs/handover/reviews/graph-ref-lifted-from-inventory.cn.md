# `kGraphRefLiftedFrom` 的删除清单：每个读点凭什么知道同一件事

状态：**清单完成，删除未做**（2026-11-02）。按裁定 ②，删之前先逐点回答"删掉它之后，这个地方凭什么知道同一件事"；**有三处，只有两处有承接判据，第三处没有**——按判据 3，这里停手，不补新字段顶上。

## 字段是什么

`include/camel/runtime/graph.h:345`

```cpp
kGraphRefLiftedFrom = 1 << 4, // the graph a lift of this one took its body from (`jvp`)
```

它记的是"**这张图是从哪张图提升来的**"：`jvp` 建每张图时写一次，然后三处读它。它的存在理由是"绕过名字"——`graph.h:752` 的注释自己说："so a transform looking at a call of a lifted graph can reach the function the call meant **without guessing from the name**"。

## 全部命中（`git grep -n kGraphRefLiftedFrom`，删前 10 处）

| # | 位置 | 角色 | 删掉后凭什么知道同一件事 |
|---|---|---|---|
| 1 | `include/camel/runtime/graph.h:345` | 枚举值 | —— 被删的对象本身 |
| 2 | `include/camel/runtime/graph.h:757` `liftedFrom()` | 唯一的读入口 | **随字段删除**（它的三个调用者见 #5、#6、#7） |
| 3 | `src/runtime/graph.cpp:1777` `addRefs(..., kGraphRefLiftedFrom)` | 写侧：`jvp` 交图时登记 | **随字段删除** |
| 4 | `src/runtime/draft.cpp:950` `draft->liftedFromRefs_ = …` | 草稿里保存这份引用 | **随字段删除** |
| 5 | `src/runtime/draft_clone.cpp:274` 克隆时搬这份引用 | 复制时跟着走 | **随字段删除** |
| 6 | `modules/transforms/split.cpp:296` `retargets()` | 判断"这个 callee 是不是提升图" | **有，但不等价**：提升图的签名形状是 `(..Lift<P>) => Lift<R>`，即参数与出口都是 `(T, T)` 对。**形状能被一个"长得像"的图满足，身份不能**——今天靠字段，明天靠形状会把"用户自己写的、签名恰好成对的图"误判成提升图。**这一处要裁定**：判定用形状，还是用"调用点就是 `jvp` 的产物"这一事实（后者需要另一条信息，见 #8 的对照）。 |
| 7 | `modules/transforms/split.cpp:518` `const GraphId primal = lifted->liftedFrom();` | 值半边**直接调原函数** | **有，且正是裁定 ② 要的**：前一半**从提升图本身取**——把调用照抄成提升图的调用，取 `.0` 作为值，切线那一路的输出交 DCE 去掉。**这不是"换个判据"，是换实现**：今天 forward half 调原函数（`addFuncNode(primal, …)`），之后变成调提升图再投影。 |
| 8 | `modules/transforms/transpose.cpp:794` `const GraphId primalId = callee->liftedFrom();` | `transpose` **穿过提升图去转置原函数** | **没有承接判据**。`transpose` 需要原函数的**图 id** 才能构建"转置原函数"这个成员；它的理由与 `split` 不同（切线的转置要按原函数的参数逐个回答），**不是"从提升图取"能替代的**。今天这条信息只由这个字段提供。 |
| 9–10 | `docs/handover/.../graph-store-step2-b-ledger.cn.md:1286`、`m2-split-call-and-branch.cn.md:30` | 文档引述 | 随实现改，属记录 |

## 结论：删不了，卡在 #8

- **#6、#7 可以脱钩**（#7 是裁定 ② 的实质工作：#7 做完，值半边不再需要"原函数 id"，#6 的判定也可以改由"调用点的形状 + 它确实是 `jvp` 产物"来解释）。这两处合起来就是"`split` 的前一半从提升图本身得到"。
- **#8 不行**：`transpose` 要把"提升调用"转置成"原函数的转置成员"，它需要原函数那张图；**提升图里没有任何一处写着它**，除非读这个字段。删掉它而 `transpose` 还在，就是**删了就没有答案**——按判据 3，我停在这里报。

## 三条可能的路（都不是我能定的）

1. **`transpose` 也改成不经原图**：把"提升调用的转置"做成"按 `Lift` 的结构就地拆开"（切线 i 的 cotangent = 值 i 的 cotangent），不需要原函数。**这可能是对的**——注释自己写着"the cotangent of tangent i is the primal's cotangent of value i"，是结构性的，不依赖那张图；但**它要重写 `transpose` 的调用处理**，远大于一个字段。
2. **把"我是从哪提升的"放进图的内容**（例如提升图里留一个指向原图的静态引用），**仍然是一个字段**，只是换了位置——与原则 2.3 的"能否从内容推出"正面冲突。
3. **先做 #7，字段只留 #8 一个读者**：那时字段的用途收敛成"`transpose` 穿过提升图"，删它的讨论就变成"`transpose` 要不要走原图"这一件事——**这是我能做的下一步，但它不是删除**。

## 与裁定 ② 的关系

裁定 ② 的原话是"`split` 的前一半从提升图本身得到（只留值那一路输出，切线计算由 DCE 去掉），**不再去调'来源图'**"。**这句话对 `split` 成立，对 `transpose` 没有说**。#8 是 `transpose` 的读点，不是 `split` 的。所以：**#7 可以做，#8 有待裁定**——上面第 3 条路是"先做 #7，把字段缩到一个读者"，第 1 条路是"让 `transpose` 也不需要它"，两条都不该我选。