# 函数类型的形参：没有切线类型，也没有输入边，调用点的实参怎么绑上去

## 问题

`test/cases/modules/autodiff/grad_function_values.cml` 的 loss9：

```cml
func twice(f: (t: Tensor) => Tensor): (t: Tensor) => Tensor {
    return (t: Tensor): Tensor => f(f(t))          // :36:35
}
...
let h = twice(g)                                    // g 是捕获了 p 的 lambda
return sum(h(p))
```

`grad(loss9)` 在 `twice` 的函数体（角色 `Callee`）里拒绝：

```
autodiff: cannot differentiate through this call of a function value: which function it calls is
not evident where it is called: the function is a parameter, or a captured value, of the function
making the call, and what is passed for it is not a function that is named or built where it is
passed (at grad_function_values.cml:36:35)
```

`grad_function_values.default/.macro/.macro.simplify/.macro.fvm` 四项因此红（`.macro.*` 三项是同一个
运行期拒绝被 `std::macro` 提前求值后变成 `MacroEvaluationFailed`）。**这不是新缺陷，是既有能力被本分支打断**：
交接报告 §7 记 `base d07306b71` 上 loss9 绿。

## 实测（本车道核对过的部分）

- 拒绝发生在 `modules/autodiff/pullback.cpp`：`Backward::activeFunctionInput`（:3336）要求被传的函数值
  `holdsFunction(type) && active_.contains(node)`；`active_` 的入口只有两处——
  `differentiated_.contains(node) && tangentTypeOf(type) != nullptr`（:3256）与 `inputActive || inputTainted`
  （:3161–3169，来自数据/携带输入）。
- `twice` 的形参 `f` 是一个 `Port` 节点：**按构造没有输入边**（形参由调用方绑定，不建值边），
  函数类型也**没有切线类型**。两条路都不成立 ⇒ 该 port 永远进不了 `active_` ⇒ `f(f(t))` 无处可见。
- 上面这条是代码事实；「哪个提交打断了它」沿用报告 §7 的结论（`94d163d6f` 把调用拼成挂链的
  `BLOCK` ＋ `2d8676670` 把区域的值改从所属 `BLOCK` 读），**本车道没有重新二分验证**。
- 与本分支已落地的规则的关系：`ungated`（`pullback.cpp:215`）已按
  `include/camel/runtime/graph.h#forwardsValue` 看穿 `BLOCK`，所以「读穿 BLOCK」这件事**不是**这项红的
  原因；缺的是**调用点实参 → 形参 port 的那条绑定本身**。

## 裁定没有定的

一个函数类型的形参，在**每次调用都可能是不同的函数值**（`twice` 的 `f` 在 loss9 是捕获了 `p` 的 lambda，
换个调用点可以是纯函数）：它在被微分的图里是不是「可携带（carrier）」？判据必须来自**调用点**，
而引擎为 callee 建的是**一张图**。判词需要回答两件事：

1. 形参 port 可以有值边吗？（图模型今天写着 port 没有输入边。）
2. 若可以，是按**每个调用点**特化一张 callee 图，还是在图上留一条「形参读调用点实参」的边（多调用点即多写入，
   与「一个值一个写法」冲突）？

## 选项与代价

- **(a) 恢复绑定：为 callee 建图时，把调用点实参接成形参 port 的值边。** 报告 §7 指的修法。
  代价：需要上面两条裁定；且 `normInputsOf(port)` 今天被当作恒空的地方要逐一看过（`Port` 是 14 个程序可见
  种类之一，引用 `GCNodeKind` 的文件 99 个）。多调用点时要么特化、要么拒绝并点名——两种都要写清楚。
  估计 **1–2 天**（含一次全层验证）。
- **(b) 把「函数值」当携带类型，判据由调用点给出。** 不碰 port 的形状：引擎在 callee 图上给「形参 port 被
  一个 active 的函数实参传入」建一条**携带**记录（`pullback.cpp:3020–3024` 已经把「捕获了 active 值的闭包」
  当 carrier，这里是同一条规则的补齐）。代价：要定「一次调用传进来的实参算不算这张图的输入」，
  同样要多调用点的答案；改动面比 (a) 小，但语义面要判词确认。估计 **半天–1 天**。
- **(c) 保持红，按 `known_fail` 记档。** 代价：fast 层 4 项长期红；`grad_function_values` 的
  「函数值作为实参」这条能力**不可用**（不只是测试红）。只有在 (a)/(b) 都判为「不该支持」时才可取。
- **(d) 改断言/删用例。** 不取：用例写的是设计要的能力（实参是就地建的 lambda 时必须能微分），
  删掉等于把能力删掉。

## 建议

先请作者裁 (a)/(b) 里那两条（port 能否有值边、多调用点怎么办）。**建议 (b)**：它与 `pullback.cpp` 已有的
carrier 规则同源，不动图的形状；若判词要求「形参 port 就是实参的值」（与 `BLOCK` 转发端口同一条思路），
则走 (a)。本车道在裁定前**不动这 4 项**，红着记录。

## 本车道（h408-splice-rest）的复测

- 到这个分支的 tip（`9f556ea8f` 之后）这 4 项**仍红**，措辞与上面第一段一字不差，即
  `RuntimeDiag::RuntimeError`（`pullback.cpp` 的 `whyNotEvident`，`GraphRole::Callee`），
  `.macro.*` 三项仍是同一个拒绝被 `std::macro` 提前求值后变成 `MacroEvaluationFailed`。
- 本分支落地的三项改动都没有碰它：`std::opt::fold` 的 screen（`45dca4fa2`）、
  `std::opt::dce` 对「没人读也没人等的 BLOCK」的新规则（`9f556ea8f`）、以及两处图钉改为读穿 BLOCK
  （`65a479266`）。第三项与它最相关（读穿 BLOCK 的规则），但如上文实测：`ungated` 早就读穿 BLOCK，
  缺的不是读穿。
- 因此本车道**不动这 4 项**，按题面「红着记录」，等作者裁 (a)/(b)。develop 合并后再复测一次，
  若结论有变（例如合并进 develop 后变成别的拒绝），以合并树的实测为准。

## 来源

- 现场：`docs/handover/reports/loop-grad-nested-segfault.md` §7；
  `test/plans/feat/modules/autodiff.plan.toml` 的 `grad_function_values.*`。
- 代码：`modules/autodiff/pullback.cpp#Backward::activeFunctionInput`（:3336）、`#Backward::run`（:3004，
  carrier 规则 :3020–3024）、`#Backward::whyNotEvident`（:3395，`GraphRole::Callee` :3413）、
  `#ungated`（:215）；`include/camel/runtime/graph.h#GCNodeKind::Port`。
- 打断它的改动：`94d163d6f`、`2d8676670`（引自报告 §7，未二分复验）。