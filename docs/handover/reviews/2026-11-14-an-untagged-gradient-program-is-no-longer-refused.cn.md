# 一条不带 `#gpu` 的梯度程序不再被拒绝，这条入口现在读的是「有没有这条遍都一样」

**判据一句话**：`grad_expand.untagged_graph_keeps_the_runtime_operator` 已按作者 2026-10-24 晚裁定 `tiles-compile-resident-value-gradient` 第 3 条落地——期望从「`exit 1` + `Device<f32>`」换成「与孪生入口（同一程序、只跑 `std::nvm`、不带 `tiles::compile`）逐字相同」，并带反证：把 `tiles::compile` 的「作用域覆盖」临时答成真，孪生比较立刻红。剩下**只有**裁定第 2 条（驻留值要能求导），本页留着它。

状态：第 3 条已落地（`test/plans/gpu/tiles.plan.toml`：孪生 `grad_expand.untagged_graph_runs_without_the_pass` + 差分 `…keeps_the_runtime_operator`）；第 1 条由 `6be4596fc`（2026-10-28）落地；第 2 条未落，按裁定另派车道。基线 `c4c7d1bf9`（= `origin/develop`），树 `D:\Projects\Camel-wt\gradreject`。

## 落了什么

| 入口 | 之前 | 现在 |
| --- | --- | --- |
| `grad_expand.untagged_graph_runs_without_the_pass`（新） | — | `kind = "behavior"`，`args = [… "${case}", "std::nvm"]`，`expect_exit = 0`，`expect_stdout_contains = ["gradient [5.0, 7.0, 9.0]", "wrong 0.0"]` |
| `grad_expand.untagged_graph_keeps_the_runtime_operator` | `kind = "negative"`，`expect_exit = 1`，`expect_output_contains = ["RuntimeDiag::RuntimeError", "depends on a differentiated parameter", "Device<f32>"]` | `kind = "differential"`，`expect_exit = 0`，`compare_to = "grad_expand.untagged_graph_runs_without_the_pass"`，`compare_fields = ["exit_code", "stdout_normalized"]`，`expect_output_not_contains` 三条保留 |

保留 `expect_output_not_contains`（`macro operator no pass has expanded`、`cannot run on the device`、`has no device program`）是有意的：孪生比较只说「两边一样」，这三条说的是「这条遍没有替程序回答问题」，是另一条独立的断言。`kind` 之所以要从 `negative` 改成 `differential`：`test/tools/run-tests.mjs#compareField` 只对 `kind === 'differential'` 求值 `compare_to`，`negative` 只是给读者看的标签（`docs/test-plans.md`），写在它上面是一条**不生效**的比较——原入口在 `a0f0701b2` 里正是这个形状。

## 反证（临时补丁，已撤回）

在 `modules/tiles/src/graph_regions.cpp#analyzeRegions` 里把作用域答案临时答成真——`const bool device = true || (scopes != nullptr && scopes->covers(graph, id));`——这就是 `6be4596fc` 之前的形状：无 `#gpu` 的 `sync` 体也会被切成区域。`-Plan test/plans/gpu/tiles.plan.toml -Jobs 8` 的读数：

```
[PASS] grad_expand.untagged_graph_unchanged                  71.69 ms
[PASS] grad_expand.untagged_graph_runs_without_the_pass      69.97 ms
[FAIL] grad_expand.untagged_graph_keeps_the_runtime_operator 73.92 ms
    [INFO] expected exit 0, got exit 1
    [INFO] differential field mismatch: exit_code
    [INFO] differential field mismatch: stdout_normalized
    [stdout] …grad_expand_plain.cml:29:18: [Error]: An error has occurred: autodiff: a value of type
             'Device<f32>' depends on a differentiated parameter but has no gradient ch…
    [stderr] (empty)
[SUMMARY] total: 104, pass: 101, fail: 3
```

也就是：**遍真的碰这个程序时，孪生比较的两个字段都红**，除两条 GPU 位精确性之外没有多出来的红。撤回补丁后同一条计划是 `[SUMMARY] total: 104, pass: 102, fail: 2`，两条 `grad_expand` 入口都 `[PASS]`。补丁撤回了，工作树里只剩计划文件与这一页。

## 这条程序为什么不该被拒绝

`--devices cpu ${case} tiles::compile std::jgir` 的转储里，`tiles::compile` **一个区域都没切**：`loss :: m=>M, .x=>f32[2,3], .p0=>f32[3], tensor:multiply=>f32[2,3], tensor:sum=>f32, :op/ftod=>float, GATE=>float`，没有 `Device<…>`、没有 `tiles:download`、没有 `tiles.region/N` 图，遍的报告行一行也没打印。原因在 `modules/tiles/src/graph_regions.cpp#eligibility`：`ordered && !device` 一律回 `control_edge`，`ordered` 读控制边加 sync 链（`#inSyncChain`），`device` 是「节点是否被 `#gpu` 作用域覆盖」。`loss` 是 `sync` 函数、算式都在链上、又没有 `#gpu` 覆盖，于是全留在主机；运行期 `with_grad(loss)` 微分的是主机图，参数 `M` 的两个字段都有切空间，梯度沿普通张量算子走通。

`wrong 0.0` 不是「错值」：它是这条用例自己的距离读数（`sum(expected - expected) + sum(abs(gradient - expected))`），`0.0` 表示梯度**恰好**等于 `p0` 的解析导数 `sum(x)` = `[5.0, 7.0, 9.0]`。加与不加 `tiles::compile`，两条流水线的 stdout 与 exit 码逐字相同。

## 这条期望是怎么过期的

| 时点 | 事实 |
| --- | --- |
| 2026-10-24 晚 | 作者裁定 `tiles-compile-resident-value-gradient`：1) 无 `#gpu` 作用域的程序 `tiles::compile` 不碰，输出应逐字相同；2) 驻留值本身要能求导，上传/下载是恒等，「驻留值没有梯度通道」这条拒绝**不能留**；3) 这条入口「要改成：输出与不带 `tiles::compile` 时逐字相同」。见 `docs/handover/reviews/questions-2026-10-24-evening.md` |
| 2026-10-25 `a0f0701b2` | 入口写下，期望的是驻留**参数** `Device<Tensor<float32, [2, 3]>>`；那时 `loss` 被切，拒绝真的响 |
| 2026-10-26 `8adf778ef` | 按读数改成 `Device<f32>`：提交说明写「区域交回 f32 标量 `Device<f32>`，同一转储把参数印成 `(Device<f32[2, 3]>, Device<f32[3]>)`」 |
| 2026-10-28 `6be4596fc` | 「a statement of a sync block is ordered by its chain」：`ordered` 加上 sync 链，无作用域的 `sync` 体不再被切。**这一笔落了裁定第 1 条，没连带落第 3 条**，入口从此红 |
| 2026-11-14 | 本笔落第 3 条 |

## 剩下的只有第 2 条

裁定第 2 条要的是：上传和下载在数值上是恒等，它们的导数规则就是把梯度原样搬回去；`#gpu` 作用域里训练、反向在设备上，是 H49 的验收要求，所以「驻留值没有梯度通道」这条拒绝不能留。它没落之前：

- `modules/autodiff/pullback.cpp#Backward::run` 里那条拒绝仍然会在**真的被编译进作用域**的梯度程序上响（反证那一段的 stdout 就是它，`Device<f32>` + 「depends on a differentiated parameter」）；
- 要落它得动 `Device<T>` 的切空间与 `tiles:retain_preferred` / `tiles:run_preferred_resident` / `tiles:download` 的导数规则，即算子表的可微性标记与求导语义。

## 默认建议（按它开工，不等回复）

**第 3 条已落地，本页剩下的请求只有一条**：按裁定第 2 条另派一笔，给 `Device<T>` 切空间与三个传输算子的导数规则，并在那一笔里把「驻留值没有梯度通道」这条拒绝删掉。落地时要补一条**作用域内**的梯度端点用例：反证那一段的临时形状（把作用域答案答成真）就是它该有的读数，只是那时应当跑出 `sum(x)` 而不是那条拒绝。在那之前不要动这两条入口：它们现在读的是裁定第 1 条要的那件事，且已经证明会红。

## 判据

1. 这两条入口**不许**再回到 `expect_exit = 1`：裁定第 2 条已判「驻留值没有梯度通道」这条拒绝要删，把它钉成期望就是把一条要删的拒绝固化成契约。
2. 也不许挂 `known_fail`：程序今天的答案是**对的**（`[5.0, 7.0, 9.0]`，`wrong 0.0`），没有缺陷可登记。
3. 「`tiles::compile` 不碰无 `#gpu` 作用域的程序」的读数是**孪生比较**，不是两份写死的输出文本；比较必须带上面那种反证，否则两条相同命令互验等于没测。
4. 第 2 条落地之后，反证那一段里那条拒绝应当**消失**而不是被绕开；那时再加作用域内的端点用例，并把这一页挪进 `reviews/`。
## 决策者答复（2026-11-14）

第 3 条的落法对：孪生比较加反证，判据第 1–3 条照写的做。

第 2 条（驻留值能求导）**不另派车道，推到收敛之后，随 GPU（H49/H50）一起做**（任务书第十一节第 1 部分：H17、GPU 推到收敛之后）。落地时注意两点：

1. **导数规则写进库的规则集，不写进 C++ 引擎。** 求导引擎（`modules/autodiff/pullback.cpp`）已冻结，不再加规则（第十一节第 5 路）。上传、下载、`retain_preferred` 这三个传输算子的前向与转置规则写在 `stdlib/rulesets/jvp.cml`、`transpose.cml`，`Device<T>` 的切空间按原则 5.4 的规则由类型给出。
2. **`pullback.cpp` 里"驻留值没有梯度通道"那条拒绝不单独去改**：它属于引擎，随引擎整块删除（库的 `vjp` 与引擎对齐之后）。在那之前，作用域内的梯度程序走库的 `grad`，就不会碰到它。

本页结案，移入 `reviews/`。
