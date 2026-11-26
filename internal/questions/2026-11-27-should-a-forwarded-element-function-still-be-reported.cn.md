# 库里的包装转发元素函数之后，原地写的那个函数体还该不该报“丢弃了值”

**状态**：等负责人回答（默认见末节，按默认不改语义、只记为已知红）

## 问句

`UnusedPureResult`（丢弃了一个纯值）今天在**调用点**报：实参的构造节点得在 `lambdaGraphs_` 里找得到。
但标准库有一层 Camel 包装把元素函数**以名字**转发下去，于是调用点那个字面量在真正丢弃它的地方已经看不见了：

```
`foreach` 这个丢弃值的算子确实声明了标志（`src/stdlib/std/builtin/executors/builtin.cpp:932` `:mark/foreach_arr`）
包装：`src/stdlib/prelude.cml:1139-1144`（`30ba175fab`，2026-11-26 12:05 加的）
      `with <f: (item: int) => void>` + `to_array(r)->foreach<f>`   ← 元素函数以名字 `f` 下传
判定点：`src/compile/src/gir/builder_call.cpp:2003-2018` 要求
        ① 解析到的算子带 `callSpec->discardsCallbackResult`，② 该实参的构造节点在 `lambdaGraphs_`（登记在 `src/compile/src/gir/builder.cpp:1176-1178`，`name.empty()` 时）
⇒ 经过包装后 ② 落空 ⇒ **不报、stderr 空、exit 0**
```

问题就是：**规则管的是“有没有把一个纯值丢掉”，还是“丢掉的那个值是不是原地写的字面量”？**

## 读数

**① 静默是这一条用例的现状**（`consistency.new_values` / `new_values.dropped_foreach_lambda`）：
```
期望 exit 1、钉 `UnusedPureResult` @ 10:9；实际 exit 0、stderr 0 字节（10:9 与 12:33 都没报）
```

**② 同族别的写法都还活着** ⇒ 不是规则整体失效：
```
· `: void` 函数体                → 走 `src/compile/src/gir/builder.cpp:1758`（`dropped_void_body.cml` 绿）
· 语句形态                        → 走 `builder.cpp:2020-2021`（`unused_in_lambda.cml` 绿）
· 只有“原地 lambda + 经范围包装转发”这一种静默
```

**③ 用例与计划写的是规则本身**，不是某一种写法（plan 的 `desc` 与用例注释都在说“丢弃了值就该报”）。

## 为什么这一问要负责人定

- 判“该报” ⇒ 修法要看穿一层包装（动 `lambdaGraphs_` 的登记点或调用点判定），属语义邻域，不是修笔误；
- 判“可静默” ⇒ 规则就变成“只看原地字面量”，那用例要改用例形态，而且**这条规则从此有一处不覆盖的缝**。

设计页没有覆盖这一点（`:267` 讲的是未使用的 `T!` 值，不是本规则）。

## 默认（不等回答也照这个开工）

**判“该报”，但这一格不修**：记为**已知红 + 机制**，单开格时再动。
**理由**：规则是“丢弃了一个值”，与那个值经过了哪一层包装无关；而“可静默”会把规则变成依赖写法的规则，那是发明新语义。**修它要改“调用点还能不能看见被转发的函数体”，属实现层，等单开格。**

**可被这两条推翻**：① 负责人说“包装后的写法可以静默”；② 设计页里能找到“这条规则只看原地字面量”的原句（给 `file:line`）。

## 影响面

- 一条红：`consistency.new_values` / `new_values.dropped_foreach_lambda`（在 `065bfb1690` 与尖端都红，不是新红）。
- 同源但**不该一起改**的两条：`int_mat_assign`（`a @= 2` 报内部名 `__assn_mat__` 未导出，已单开修复格）；
  张量上的 `@=` 是否也不通（`src/stdlib/tensor/ops/linalg_ops.cpp:184` 只导出 `__mat__`/`matmul`）——只报不实现。
## 答复（决策者）

该报。规则管的是"把一个纯值丢掉"，与这个值经过几层包装无关。判定不靠看穿包装，而是看形参的类型：形参声明的函数类型交回 `void`（如包装的 `with <f: (item: int) => void>`），实参却是函数体以一个非 `void` 的纯值结尾的函数，就在实参处报 `UnusedPureResult`。被调用者的签名已经说了它会丢掉结果，这一条在每个调用点都看得见，包装多少层都一样。这样 `callSpec->discardsCallbackResult` 与 `lambdaGraphs_` 的登记就不再需要（原生 `foreach` 的形参也写成交回 `void` 的函数类型），判据只有一处。修法单开一格，用例 `new_values.dropped_foreach_lambda` 不改。
