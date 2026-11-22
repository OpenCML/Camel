# 转置成员的闭包：只能继承，不能追加

明早的独立项。一页写完两侧要改的点、已经排除的方向、和三条被撤回的路，免得接手的人重走这一夜。

## 1 现象

七条 `grad_*` 红是**同一个签名**，逐条实测 `std::fvm`（2026-11-03）：

```
TRANSPOSE-NEEDS function=split_of_jvp_of_<f>_backward param=0
TRANSPOSE function=split_of_jvp_of_<f>_backward skip=call node=N arg=M
error[RuntimeDiag::RuntimeError]: transforms: transpose has no transpose for this function:
  this call's callee has no transpose of its own
```

涉及：`grad_array_no_int`、`grad_of_an_array_parameter`、`grad_array_one_element`、
`grad_array_two_elements_first`、`grad_array_sum`、`grad_array_product`、`grad_of_a_division`。
七条的差别只在 `node=` / `arg=` 两个数（`arg=` 打的是**节点号**，不是第几个实参 —— 这一点值得先记住）。

## 2 已排除：与端口排布无关

七条全部在 **`split` 成功之后**才失败：`split_of_jvp_of_<f>_backward` 成员已经建出来，错抛在
`transpose(lin)` 上。所以 grouped/interleaved 的排布歧义不是这里的根 —— 那条路若咬人，会卡在
`split` 的 `!tangent` 分支或 `valuePortsOf` 的配对，而七条一次都没走到那两句。

## 3 根：闭包只继承、不追加

`modules/transforms/transpose.cpp#buildTransposeMember` 里：

- 闭包口按**旧图的闭包**逐个建：`for (const gc_node_ref_t ref : closureOf)`，`appendClosureNode`，
  记进 `closurePortOf`；
- 类型**沿用**被转置函数的闭包：`draft.setClosureType(function->closureType())` —— 从头到尾**不加一个元素**。

而 `wantedValues`（`buildTransposeMember` 开头记录的那批“体要读、这一半却没有口”的参数）
要在签名上出现在今天被作者裁定否掉了（见 5），**正确归宿是进闭包** —— 但闭包这里根本没有“追加”
这条路。所以 `TRANSPOSE-NEEDS param=0` 记下了需要，`capturePortOf` 里却没有它，
调用点上 `forward(inputs[...])` 返回 invalid，整个调用被判“callee 没有自己的转置”。

## 4 两侧要同时改

| 侧 | 位置 | 要改成什么 |
|---|---|---|
| `transpose` | `#buildTransposeMember` 的闭包段 | `setClosureType` 从“旧闭包”改成“旧闭包 + 每个 `wantedValues` 条目的类型”，顺序按 `residualIndexOf` 的**出现序** |
| `transpose` | 调用点拼闭包的那一段 | 元组多一个元素 = 被前向的那个值 |
| `split` | `#projectResidual` / `residualIndexOf` | 转置所接收的那条闭包（即 `lin` 交给后一半的闭包）要多一位；`residualIndexOf` 的顺序就是两侧共同的语言 |

两侧的**顺序必须一致**，否则值会落到别人的槽里 —— 这是这一项唯一的难点，也是为什么它不能一个人改一半。

## 5 已试错并撤回的三条路

- **把 `wantedValues` 加成签名上的值参数** ⇒ `grad_array_no_int.fvm` 从**具名拒绝**退化成
  **SIGABRT**（`rc=134`）。作者 2026-11-03 裁定：准确形状是 `(ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`，
  **没有“调用方还要传的值参数”，那些值是捕获，进闭包**。此路不通，不是探针的问题，是“把需要放进签名”错了。
- **纯调用锚点** ⇒ 66 红（链位 ≠ 纯度）—— 相邻车道试过并撤回。
- **`readLoopCut` 那 4 行** ⇒ 内部错误 —— 相邻车道试过并撤回，另有一条线已修。

## 6 红线（这一项上手前先读）

1. 改动**必须让签名多出值参数** ⇒ 不做（违反作者裁定）。
2. 出现 **SIGABRT / internal error** ⇒ **回退**，不是就地修，也不再等第二轮。
3. 具名拒绝 → 崩溃是**掉一格**，比不改更坏。

## 7 判据

- `grad_array_no_int.fvm` 从**具名拒绝**变成**答出数字**（纸面值按用例正文）；
- 七条**逐条**名与结果；
- 全量 `killed` 必须是 **0**；
- 新增红逐名（这一项动的是 `transpose.cpp`，`transpose_loop_escape.*` 与 `grad_*` 两族的红要逐名对照）。