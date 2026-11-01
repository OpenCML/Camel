# 发现：`split` 要给"切线实参"造零，而树上已经有四份造零的写法，没有一份能直接用

状态：**停手报**（2026-11-02）。这是做 #7 之前的第一步（"先找按类型造零的现有入口"）的结果：**入口有四份，互不兼容，没有一份是 `split` 能直接复用的**。按判据，**不写第五份**。

## 四份写法

| # | 位置 | 能造什么 | 聚合类型怎么办 | `split` 能用吗 |
|---|---|---|---|---|
| 1 | `modules/transforms/jvp.cpp:794`（`draft.materializeStaticValue(toSlot<double>(0.0), Type::Float64(), kGCNodeFlagConstant)`） | **只有 `float`**。同一文件 1052 行是同一写法 | **不处理**（外层已按 `node->dataType() == Type::Float64()` 挡掉） | 不行：`split` 要的是"该切线类型的零"，切线可能是元组/数组 |
| 2 | `modules/autodiff/pullback.cpp:1992` `addZeroTangent(transaction, primal, primalValue)` | **任意有切线空间的类型**：标量走 `materializeStaticValue`，**聚合递归**（`isAggregate` → 逐字段造零再 `addAggregate`），离散标量走 `discreteZeroSlot`，都没有就 `fail("no zero gradient for type '%s'")` | **处理得最完整**：逐字段递归 + 每个元素带自己的切线元素索引 | **最接近，但接口不合**：它收 `rt::GraphTransaction &`，而 `split` 全程只握 `draft`（模板参数，为的是不新增 `GraphDraft` 直接引用）；跨模块复用 `split → autodiff` 还是新依赖 |
| 3 | `src/builtin/operators/struct.cpp:830` `moduleZeroTangent(ctx, primalType, tangentType, primal)` | 运行时算子（`struct` 模块里），值层 | 值层递归 | **不行**：那是**运行时取值**，不是建图期的节点 |
| 4 | `split.cpp` 自己：`fillTuple` 里的常量、`zeroTangent` 相关分支 | 只在残差/元组的既定位置造常量 | —— | 不是通用入口 |

## 结论

- **没有一份能"给任意数值类型造零并返回建图节点"、同时又能被 `split` 的 `draft` 直接用**。
- **#2 是语义上唯一完整的**（聚合递归 + 离散 + 失败时具名），**它缺的只是"接 `draft` 而不是 `transaction`"**。
- 这三条都是事实，不是偏好，所以这一笔**不动代码**。

## 三条路（都不是我能定的）

1. **把 #2 的算法抽成模板**（在 `draft` 上工作），`pullback` 与 `split` 各调一次——**一份算法、两个调用点**，代价是 `autodiff` 与 `transforms` 共享一个头（依赖方向要作者点头）。
2. **`split` 只要 `float` 与其元组**：如果切线实参的类型能被"提升图签名 + 参数类型"限定在 `float` 及其元组内，那 #1 的写法 + `fillTuple` 组合就够，**不必碰 #2**。**这条要先确认"#7 里切线实参的类型到底能多宽"**。
3. **`split` 造不出零就具名拒绝**（`there is no zero for the tangent of parameter N`），只支持它今天必然见到的类型。这与裁定给的那条止损一致，**但它把"支持面"缩小了，需要作者确认可接受**。

## 与 #7 的关系

#7 的原计划是"零常量 + DCE 兜底"；**零常量这一步现在卡在这里**。若走第 2 或第 3 条，`split.cpp:518` 的改动本身不变（`addFuncNode(liftedId, …)` + `addAccsNode(valueType, 0, 0)`），只是"切线实参从哪来"要按上面选一条。

**建议**：先确认第 2 条（切线实参类型的上界），若上界就是 `float` 与其元组，则可以就地做 #7，不需要动 `autodiff`。

## 实测：`Lift<P>` 在树上不是一个类型，"切线实参"的类型就是参数的切线空间

- `git grep -n "Lift<" -- src include` 只有**两处，且都是文字**：`semantic.cpp:1788` 的诊断文本、`rule_head.cpp:146` 的注释。**没有 `Lift<T>` 这个类型**——它是文档记号，不是构造子。
- 提升图的签名是**元组对**：每个参数是 `(T, dT)`，出口是 `(R, dR)`；`split` 的 `splitHalvesWith` 就是把这对拆成两半边。所以"切线实参的类型" = **每个参数类型的切线空间**（`camel::core::tangentSpaceOf(type)`）。
- **切线空间对张量/数组是有定义的**（张量的切线是张量）——所以上界**不只是标量与其元组**。

**按裁定的三选一，命中第 2 条：上界含数组/张量 ⇒ 停手报，不就地做。**

`split` 不能靠"标量零 + `fillTuple`"覆盖它：给一个张量切线造零要按张量的形状造，那正是 `pullback.cpp:1992` 的算法（第 2 份）。所以 #7 的"切线实参"这一步要么用第 2 份（跨 `autodiff` → `transforms` 的头依赖，待裁），要么把支持面收窄成"只能切标量参数与标量元组的提升调用"，其余具名拒绝 `there is no zero for the tangent of parameter N`（待裁）。

## 裁定：收窄支持面 + 具名拒绝；为什么不能一步做对

**#7 的做法（已裁）**：值半边调提升图取 `.0`；切线实参——参数切线空间是**数值标量**就造零常量（照 `jvp.cpp:794` 的 `materializeStaticValue` 形状），是**标量元组**就用 `fillTuple` 按元素递归；**其余（含数组/张量）**具名拒绝 `there is no zero for the tangent of parameter N`（**N 从 0 起**）。

**为什么"复用第 2 份"不是顺手的事，而是另一笔**：

1. **依赖方向反了**：`transforms` 不依赖 `autodiff`，`pullback.cpp` 在上面一层。让下面的 `transforms` 反过来依赖它**就是环**。所以复用不是加个头文件，而是**把那套算法搬到两边都能站的地方**——独立一笔。
2. **接口也不合**：它收 `GraphTransaction &`，而 `split` 只有模板参数 `draft`（**为了不新增 `GraphDraft` 直接引用**，见 `runtime.graph_draft_ledger`）。搬的时候要连这个一起改。
3. **具名拒绝比虚假支持好**：今天拒绝一次、名字写清，胜过"给张量造了个形状不对的零"——那正是"图里写了个假操作数"。

**记号的澄清（判据 5）**：`Lift<P>` **不是类型，是记号**——`src` 里只有两处文字（`semantic.cpp:1788` 的诊断串、`rule_head.cpp:146` 的注释）。提升图的签名是**元组对** `(T, dT)`，`split` 的 `splitHalvesWith` 拆它；**切线实参的类型 = `core::tangentSpaceOf(参数类型)`**。**"造零"今天有四份写法（本页表格），正确答案是一份共享实现——但那要先把依赖方向反过来搬，所以本笔只做收窄。**