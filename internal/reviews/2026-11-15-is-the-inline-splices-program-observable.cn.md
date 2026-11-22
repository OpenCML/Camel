# 拼接进来的那段体，归属可观测吗

## 事实

`cloneRuntimeGraphIntoDraft`（`src/runtime/draft_clone.cpp#cloneRuntimeGraphIntoDraft`）有三个调用点。
前两个已经把归属钉住了，各有一条会逐字变红的用例：

| 调用点 | 克隆图去哪 | 读数 |
| --- | --- | --- |
| `modules/transforms/remat_operator.cpp#rematKernel` | 新程序（`GraphDraftGroup`） | `clone: 15 ones=0` / `given: 18 ones=1` |
| `modules/transforms/first_with_as_param_operator.cpp` 的重解释 | 新程序（同一族 `GraphDraftGroup`） | `copy 10 0` / `given 12 1` |
| `src/runtime/draft_inline.cpp#inlineCallableInDraft` 的拼接 | **调用方那张图**（不造新程序、不造值） | 造不出 |

第三个与前两个不是同一个形状，而它今天造不出可区分的读数，原因有三条，都可查：

- 拼接的源图来自 `GraphDraft::calleeGraphOf`（`src/runtime/draft.cpp#GraphDraft::calleeGraphOf`），
  读的是**草稿自己那张表**：`program_->graph(calleeIdOf(id))`。体拼进哪张图，就在哪张图的程序里读。
- 唯一把**别人的程序**带进来的入口是 `functionGraph`（`src/runtime/draft_inline.cpp#functionGraph`），
  它先 `session.adoptProgram(*function->program())` 再返回 `function->graph()`；而
  `RuntimeGraphDraftSession::adoptProgram`（`src/runtime/draft_session.cpp`）是
  `work_->adoptMissing(program)` —— **缺的才收，草稿自己已经有的版本不收**。
- 提交时 `committed` 对闭包里缺的图一律 `bind`（`RuntimeGraphDraftSession::commit` 的 closure 循环），
  所以收进来的图会跟着进新程序，不会留在外面。

于是这一处「源图所属的程序」和「克隆图所属的程序」是**同一个程序对象**。换句好懂的话：
另两处是「把体复制进**新程序**」，副本跟**源图**走；这一处是「把体复制进**调用方的图**」，副本跟**调用方**走。
两条都是对的，但它们是两条不同的规则，而今天两条都靠调用点各自记着：`adoptProgram` 在
`src/runtime/definitions.cpp#expandDefinition`、`modules/autodiff/pullback.cpp#Engine::graphOf`、
`functionGraph` 三处各写一次，漏一个就是静默错版本。

## 问什么

这一处要不要有读数？也就是：**跨程序拼接的归属，是不是一个应该被观测的量**。

## 会分叉的唯一形状，今天够不到

一个真能被读出来的形状是：**调用方程序与值所属程序都持有同一个 GraphId 的不同版本**。那时
`adoptMissing` 不覆盖，拼进来的体读调用方的版本，而原来那次调用读值的版本 —— 体被拼进去之后换了
一个被调版本，而且没有任何名称或读数说出来。

这个形状今天够不到：运行期造出来的程序都是从当前程序抄一份再做卫星（`GraphDraftGroup` 的构造、
`Program::makeSatellite`），版本在抄的那一刻就固定了，运行期再没有第二处能把同一个 id 的两个版本
同时留在两个活着的程序里。唯一可能让它们分叉的是 autodiff 的
`modules/autodiff/pullback.cpp#ForwardRewrite::retarget`，那是另一条线在裁的事。

所以这不是「今天错了」，是**规则还没有可观测的边界**：另一条线裁下来之前，谁也不知道拼进来的体该跟
调用方还是跟值。

## 默认建议（按此开工，不另等）

**把这一处登记为不可观测，不写「没崩」的用例，也不给 `cloneRuntimeGraphIntoDraft` 加源程序参数。**
理由：加参数要动 `include/camel/runtime/draft_clone.h` 一张签名加三个调用点共 4 处，而它换不来任何
今天能红的读数——那正是「用一个读不出来的参数假装钉住了」的样子。

等 `ForwardRewrite::retarget` 裁下来、运行期程序之间真能出现同 id 两个版本时，再拿那时的形状补一条
能区分「跟调用方」与「跟值」的读数；在那之前，「调用方版本胜」这条规则只写在
`RuntimeGraphDraftSession::adoptProgram` 的注释里，和它今天的实现挨着。
## 决策者答复（2026-11-15）

同意默认建议：今天够不到分叉的形状，不写读不出来的用例，也不加参数；"调用方版本胜"写在 `adoptProgram` 的注释里。等 autodiff 的 `retarget` 裁下来、真能出现同一个 id 的两个版本时，再补能区分的读数。

另一处要改的是你自己点出来的：`adoptProgram` 在 `expandDefinition`、`Engine::graphOf`、`functionGraph` 三处各写一次，漏一处就是静默错版本，这违反"同一类事一条规则"。收成一个入口："为拼接取一个函数值的图"，它负责收程序，三处都调它，不再各自记着。归第 4 路，排在优先级一之后。
