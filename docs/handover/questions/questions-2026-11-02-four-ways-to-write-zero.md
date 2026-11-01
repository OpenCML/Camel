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