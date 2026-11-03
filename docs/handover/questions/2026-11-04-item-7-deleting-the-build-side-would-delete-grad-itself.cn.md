# 第 7 项「先删 `pullback.cpp` 的建图」今天能删吗：`buildGradientGraph` 仍是 `grad` 的唯一实现

日期：2026-11-04　状态：**默认按 A 落地（暂不删），等你裁**

**一句话**：2026-11-03 15 时 55 分那条裁「第 7 项先删 `pullback.cpp` 的建图部分，`GradResolver` 留着」，但建图侧的**唯一入口 `buildGradientGraph` 今天仍是 `grad` / `with_grad` / `grad_aux` / `with_grad_aux` 四个算子的 kernel 实现**，库里没有第二条声明。**删掉它等于删掉这四个算子本身**：`test/cases` 下 **240 个 `.cml`** 在链接期一起消失。这不是不执行，是执行不了——**裁定的前提是建图侧已有替代实现，实测没有。**

## 1. 出处

- **裁定**（2026-11-03 15 时 55 分，第 7 项）：方向是"类型层能像值层一样拆包"，具体写法交负责人定；**在他定之前，先删 `pullback.cpp` 的建图部分，`GradResolver` 留着并标明等这一条裁定**；终态是"求导都在库里"。
- `stdlib/signatures/autodiff.cml` 页头自认：**"`grad` has no declaration in standard library types: its result type is computed here, so there is no one place where its shape is written down to be pinned against"**。
- D2 已单独开页：`docs/handover/questions/2026-11-03-can-gradient-of-a-function-be-a-library-type-function.cn.md`（`grad(f)` 的类型能不能写成库层类型函数）。**本页是第 7 项的执行侧，不是 D2 的重复**：D2 问类型能不能写进库，本页问在类型还没写进库的今天，那一刀砍下去砍掉的是什么。
- 本页每个数字都是施工方在本轮的实测，基线 `origin/develop` = `c69a110fa`。

## 2. 现象：删建图 = 删掉四个算子

`buildGradientGraph` 的调用点全集（`git grep -n buildGradientGraph`，命中的代码两处，其余全是注释）：

| 调用点 | 在哪个文件 | 它是什么 |
| --- | --- | --- |
| `modules/autodiff/operators.cpp#gradFunction` | `modules/autodiff/operators.cpp` | **四个 kernel 唯一的实现**：`#gradKernel` / `#valueAndGradKernel` / `#gradAuxKernel` / `#valueAndGradAuxKernel` 全部经它造图再 `::Function::create` |
| `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee` | `modules/autodiff/pullback.cpp` | 被微分的图里本身有 `grad` 节点（嵌套求导）时，在建图期再造一张梯度图；由 `#ForwardRewrite::rewriteIndirectCall` 调用 |

名字怎么落到这两个 kernel 上：`operators.cpp#operatorKernels` 把 `grad` / `grad_aux` / `with_grad` / `with_grad_aux` 四个名字指到那四个 kernel，`operators.cpp#registerOperatorMetadata` 又把它们声明成 `macro = true`、`readsGraphs = true` 的算子。`stdlib` 里只有 `stop_gradient` 是编译进库的函数（`stdlib/autodiff.cml#stop_gradient`），`grad` 和 `with_grad` **没有库层声明**，所以这四个算子今天没有第二条路可走。

用量读数（`c69a110fa`）：

| 量 | 读数 |
| --- | --- |
| `test/cases` 下用 `grad(` 或 `with_grad(` 的用例文件 | **240 个**（其中 `test/cases/compile/transforms` **16 个**） |
| `test/plans/feat/modules/autodiff.plan.toml` 提到 grad 的行 | 699 行 |

⇒ 删建图不是"可能撞到仍被调用的入口"，是**必然**：这四个算子的 kernel 在删掉的那一刻没有实现，编译能过、链接不过，`grad` 从语言里消失。

## 3. 边界：删掉 4,847 行文本，留下 212 行（签名侧 3 个函数 149 行）

`modules/autodiff/pullback.cpp` 共 5,059 行、129 个声明（120 个函数 + 9 个类）。按簇（每簇的行数是该簇内函数/类声明的行数之和；差额是命名空间壳与空行）：

| 簇 | 声明数 | 行数 | 哪一类 |
| --- | --- | --- | --- |
| 图构造原语（`#typeOf`、`#kindOf`、`#addAccs`、`#addOper`、`#addFill`、`#addPort`、`#setResult`、`#valueOrder`、`#pruneUnreachable`、`#portTypesOf`、`#carriesGradient`…） | 30 | 401 | 建图 |
| 切线类型助手与拒绝措辞（`#tangentTypeFor`、`#tangentElementType`、`#fail`、`#failAt`、`#whereIs`…） | 10 | 72 | 建图 |
| 计划类型（`#PullbackPlan`、`#CallSite`、`#GraphScope`、`#LoopBodyPlan`、`#makePlanTypes`） | 6 | 102 | 建图 |
| 前向重写（`#ForwardRewrite` 与它的 12 个方法：`#ForwardRewrite::run`、`#rewriteCall`、`#rewriteClosureCall`、`#macroCallee`、`#rewriteLoop`、`#rewriteBranch`、`#split`…） | 13 | 1,214 | 建图 |
| `#Engine` 壳与 `#Engine::run`/`#retarget`/`#redirectAnchors` | 7 | 295 | 建图 |
| 反向回路构造（`#ReverseLoop`、`#addZeroTangent`、`#addTangentSum`、`#buildReverseCond`、`#buildReverseBody`、`#buildReverseClosure`…） | 12 | 360 | 建图 |
| 反向遍历与材料化（`#Backward` 的 35 个方法：`#Backward::visit`、`#accumulate`、`#zero`、`#materialize`、`#ref`、`#refuse`、`#reportRefusals`…） | 37 | 1,222 | 建图 |
| 计划分配与建图（`#reserve`、`#planFor`、`#planForClosure`、`#build`、`#buildLoopBody`、`#Engine::gradient`） | 10 | 560 | 建图 |
| **入口 `#buildGradientGraph`** | 1 | 14 | 建图 |
| **签名侧**：`#memberWithoutTangent`(46) / `#differentiationRefusal`(61) / `#gradientSignature`(42) | 3 | **149** | 签名 |

⇒ 建图侧 126 个声明、声明行数之和 4,240 行；**删掉的是 4,847 行文本，留下 212 行**（文件头 + 签名侧 149 行 + 两个命名空间壳）。

三条读数让这一刀可以机械地切：

1. **签名侧只依赖核心类型**：`#gradientSignature` 用 `camel::core` 的 `gradientTypeOf` 与 `type::*`，第三个 `#memberWithoutTangent` 只是把没有切线的成员按字段名/位置指出来——**它不调建图侧任何一个函数**。
2. **依赖方向单向**：建图侧用签名侧（`#Engine::gradient` 调 `#gradientSignature` 给要造的图定类型），签名侧不用建图侧。所以删建图不会连坐签名。
3. **切完签名侧的唯一调用者就是 `modules/autodiff/operators.cpp#GradResolver`**，正是裁定要留的那一处。

## 4. 一句话说清这是"裁定与代码事实的冲突"

裁定的句子是"**先**删建图"，它成立的前提是**建图侧已经有替代实现**（作者自己的终态是"求导都在库里"）。今天的实测是：库层 `grad` 不存在（D2 未裁），`grad` 的四个算子只有引擎这一份实现。**在这个前提下，"先删建图"不是"删一份多余的实现"，而是"先把 `grad` 从语言里拿掉、等以后再加回来"。**

这是第一次量清；此前 `docs/handover/reports/2026-11-03-what-deleting-the-engine-pullback-takes.cn.md` 只写到"两个调用点"与"删引擎要满足什么"，没有把它与第 7 项的"先删"对上。**对上之后，结论变了：这一刀今天不能砍。**

## 5. 要裁的那一句

**问：第 7 项的"先删 `pullback.cpp` 的建图部分"，是在库层 `grad` 存在之前就删（B），还是等类型层拆包裁定落地、`buildGradientGraph` 自然失去调用者再删（A）？**

## 6. 默认建议与代价：三条路各带代价（A 是默认）

- **A（默认建议）：暂不删，等类型层拆包裁定落地、库层 `grad` 存在之后再切。**
  理由：作者自己的终态是"求导都在库里"，那一天 `buildGradientGraph` 的调用者会自然归零，这一刀自己就成了删死代码；现在删等于**把 `grad` 从语言里拿掉**，与"求导都在库里"不是一个方向。
  **代价要写清**：在第 7 项这条裁定落地之前，**第 7 项不会有机器可验的进展**——它今天的产出只有本页的分类表与备好的切法（§9），没有提交、没有读数变化。
- **B：现在就删建图，四个算子留空。**
  代价：`grad` / `with_grad` / `grad_aux` / `with_grad_aux` 没有 kernel，240 个用例文件与 `test/cases/compile/transforms` 那 16 个一起红，闸门不可能绿。**不建议**：这等于把一条语言特性下架，不是删实现。
- **C：保留算子名与 `GradResolver` 签名，把 `#gradFunction` 的实现从"引擎建图"改成"在运行期拼一张调用库层 `vjp` 的图"，然后再删建图。**
  代价：不碰类型、不碰 `stdlib` 声明（`run-time` 拼图不需要库层 `grad` 的类型），但要求库层 `vjp` 能从 kernel 里被调用——`vjp` 今天还在 `modules/transforms` 那几条车道里收尾，**这条由其他车道决定能不能做**；要不要这么做是设计判断（一个算子 kernel 里再造一张库调用图，与"求导都在库里"是不是同一个意思），不是本车道能定的。

## 7. 判据 / 哨兵：A 路怎么算走通，B 路撞上的红长什么样

三条都是机器可验的读数，不需要人读代码判：

1. **A 路的等待条件（判据）**：`git grep -n 'buildGradientGraph' -- modules src` 的**代码**调用点今天正好两处——
   `modules/autodiff/operators.cpp#gradFunction` 与 `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee`。
   **这两处归零的那一天，A 的等待条件就成立**，这一刀自动变成删死代码；在它们还是两处之前动刀，动掉的是 `grad` 本身。
   （数调用点、不数行：`git grep -c` 把注释一起数进来，第一版就是这么数错的。）
2. **绿哨兵（删了会红）**：`test/cases/modules/autodiff/scalar_grad.cml` 的计划项 `scalar_grad.macro.nvm`
   （`test/plans/feat/modules/autodiff.plan.toml`）今天答 `scalar_grad df=12.5 vg=(17.5, 12.5) dg=12.0`——
   一条用例同时走 `grad`、`with_grad`、`stop_gradient`，是这四个算子最小的一条正面读数。
   **判据：删建图之后它必须仍答这一行**；答不出来（链接期少 `buildGradientGraph`）或 `dg=` 变了，B 路当场被证伪。
3. **红的爆炸半径（不该出现的红出现，就回滚）**：`test/cases` 下用 `grad(` / `with_grad(` 的 **240 个** `.cml`，
   以及 `test/plans/feat/modules/autodiff.plan.toml` 的 **225 个计划项**（其中 699 行提到 grad）。删除后其中任何一个以
   "unknown operator / 未声明" 出现，说明删掉的是语言特性而不是多余实现——**立刻回滚，不许改用例期望来掩盖**。

## 8. 一条更正：旧报告的第二调用点写错了

`docs/handover/reports/2026-11-03-what-deleting-the-engine-pullback-takes.cn.md` 写第二个调用点是 **`#ForwardRewrite::rewriteClosureCall`**；**实测是 `#ForwardRewrite::macroCallee`**（`rewriteClosureCall` 不调 `buildGradientGraph`，它自己不碰 `grad` 节点）。

这一处留在页里，是因为它今晚的教训：**没有出处的断言会被下一个人当结论用**。旧报告那句被读到过、也被引用过；照它去删，删的是错的函数。

## 9. 备件：切法与标注（本轮真正的产出）

切法（在本轮文本层切过一遍、`git grep` 残留已核、随后 `git checkout --` 还原；**没有提交**）：

- `modules/autodiff/pullback.cpp`：删**匿名命名空间的整段建图**与 `modules/autodiff/pullback.cpp#buildGradientGraph`；保留文件头、`#memberWithoutTangent` 所在的匿名命名空间、`#differentiationRefusal`、`#gradientSignature` 与最外层命名空间的闭合。**两段边界都用符号定，不用行号**：第一段是装 `#TangentAudience` 的那个匿名命名空间整段——从它开头的 `namespace {` 起，到 `#Engine::gradient` 之后那条 `} // namespace` 为止；第二段就是 `#buildGradientGraph` 一个函数，从它的函数头到右花括号；`#include` 随后按"编译期未用"清理。
- `modules/autodiff/pullback.h`：删 `#GradientGraph` 与 `#buildGradientGraph` 两个声明，留 `#GradientSignature`、`#differentiationRefusal`、`#gradientSignature`。
- **两处前置（缺一不可）**：`modules/autodiff/operators.cpp#gradFunction` 与 `modules/autodiff/pullback.cpp#ForwardRewrite::macroCallee` 必须同时换成别的实现，否则链接期没有 `buildGradientGraph`。

删掉位置该留的标注（原文，可直接粘在 `#differentiationRefusal` 之前）：

```cpp
// The engine's graph construction for `grad` used to be here. The author's ruling of
// 2026-11-03 15h55 (item 7) keeps the signature side above (`gradientSignature`, consumed
// by `GradResolver` in modules/autodiff/operators.cpp) and holds the build side back until
// the type-layer question is decided: a gradient's type is the type of the function's first
// `with` parameter, which standard library types cannot name yet. The end state is that every
// derivative is built in the library. Nothing is missing here -- what stood here was a second
// implementation of the same transform, not a part of this one.
```

`GradResolver` 那一侧的"等这一条裁定"标注，建议落在 `modules/autodiff/operators.cpp#GradResolver` 的头注释里，写清"签名留在这里是因为类型层还写不出 `Tangent<With 的第一个>`"，与 D2 页互指即可，不重复论证。

## 10. 本轮没有做的事

没有删任何代码、没有改用例期望、没有动 `GradResolver`、没有动 `stdlib` 里 `grad` 的声明；上一轮的临时工作树已删，本轮只提交这一页。