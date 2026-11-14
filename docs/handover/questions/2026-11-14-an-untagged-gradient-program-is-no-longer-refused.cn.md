# 一条不带 `#gpu` 的梯度程序不再被拒绝，而用例还在等那条拒绝

**判据一句话**：`grad_expand.untagged_graph_keeps_the_runtime_operator` 期望的 `exit 1` + `Device<f32>` 不再成立，这**不是**一条缺陷，而是作者 2026-10-24 晚裁定 `tiles-compile-resident-value-gradient` 第 1 条要求的结果（无作用域的程序 `tiles::compile` 一律不碰），而同一裁定第 3 条要求的用例改法**至今没有落**：这条入口今天的读数与裁定要的「与不带 `tiles::compile` 时逐字相同」逐字相同，却仍写着 `expect_exit = 1`。

状态：待裁（本笔只写这一页，**没有**改这条用例，也没有改任何代码）。树 `D:\Projects\Camel-wt\gradreject`，分支 `lane-gradreject`，基线 `c4c7d1bf9`（= `origin/develop`）。

## 要你裁的是什么

**这条入口按裁定第 3 条改成「与不带 `tiles::compile` 时逐字相同」（`expect_exit = 0` + 比对另一次运行的 stdout），还是先把裁定第 2 条（上传/下载的导数规则）做完再一起改？** 本车道按下面的默认停手，不改期望、不改代码。

## 读数（`-Plan test/plans/gpu/tiles.plan.toml -Jobs 8`，`c4c7d1bf9` 干净树）

```
[FAIL] grad_expand.untagged_graph_keeps_the_runtime_operator 74.85 ms
    [INFO] expected exit 1, got exit 0
    [INFO] output does not contain 'RuntimeDiag::RuntimeError'
    [INFO] output does not contain 'depends on a differentiated parameter'
    [INFO] output does not contain 'Device<f32>'
    [stdout] gradient [5.0, 7.0, 9.0]
    [stdout] wrong 0.0
    [stderr] (empty)
```

`wrong 0.0` 不是「错值」：它是这条用例自己的距离读数（`sum(expected - expected) + sum(abs(gradient - expected))`），`0.0` 表示梯度**恰好**等于 `p0` 的解析导数 `sum(x)` = `[5.0, 7.0, 9.0]`。

同一条程序、同一台主机，两条流水线各跑一次（临时探针，已删）：

| 流水线 | exit | stdout |
| --- | --- | --- |
| `tiles::compile std::nvm` | 0 | `gradient [5.0, 7.0, 9.0]` / `wrong 0.0` |
| `std::nvm` | 0 | `gradient [5.0, 7.0, 9.0]` / `wrong 0.0` |

两者逐字相同，`stderr` 都为空——**正是裁定第 1 条要求的那件事**。

## 为什么那条拒绝不触发

把它当「诊断还在、没人调用」去查是查不出来的：发出它的代码活着（`modules/autodiff/pullback.cpp#Backward::run` 里 `tainted_` 那一支，`failAt` 的句子是「a value of type '{}' depends on a differentiated parameter but has no gradient channel, and it is used as data here」），挂掉的是它**上游的前提**：那个「没有梯度通道的驻留值」在这条流水线里根本不会被造出来。

`--devices cpu ${case} tiles::compile std::jgir` 的转储（临时探针，已删）里，`tiles::compile` **一个区域都没切**，四个图逐节点都是原样：

```
__root__ :: Function(graph=main) => ()=>int, CALL=>int
main :: 0=>int, …, tensor:reshape=>f32[3], …, step=>f32[3], …, :op/add_f=>f32, …, GATE=>int
loss :: m=>M, .x=>f32[2,3], .p0=>f32[3], tensor:multiply=>f32[2,3], tensor:sum=>f32, :op/ftod=>float, GATE=>float
step :: Function(graph=loss)=><m: M>()=>float pure, autodiff:with_grad=><M>()=>(float,{p0:f32[3],x:f32[2,3]}), m=>M, CALL, .1, .p0=>f32[3], GATE=>f32[3]
```

没有 `Device<…>`、没有 `tiles:download`、没有 `tiles.region/N` 图，遍的报告行一行也没打印。原因在 `modules/tiles/src/graph_regions.cpp#eligibility`：`ordered && !device` 一律回 `control_edge`，而 `ordered` 现在读的是**控制边加 sync 链**（`graph_regions.cpp#inSyncChain`），`device` 是「节点是否被 `#gpu` 作用域覆盖」。`loss` 是 `sync` 函数，它的算式都在链上，又没有 `#gpu` 覆盖，于是全部留在主机。

所以运行期 `with_grad(loss)` 微分的是**主机图**，参数 `M` 的字段都有切空间（`f32[3]`、`f32[2,3]`），梯度沿普通张量算子走通，答案正确，不触发任何拒绝。

## 这条期望是怎么过期的

| 时点 | 事实 |
| --- | --- |
| 2026-10-24 晚 | 作者裁定 `tiles-compile-resident-value-gradient`：1) 无 `#gpu` 作用域的程序 `tiles::compile` 不碰；2) 驻留值本身要能求导，上传/下载是恒等，规则就是原样搬回去，「驻留值没有梯度通道」这条拒绝**不能留**；3) 这条入口「要改成：输出与不带 `tiles::compile` 时逐字相同」。见 `docs/handover/reviews/questions-2026-10-24-evening.md` |
| 2026-10-25 `a0f0701b2` | 入口写下，那时期望的是 `Device<Tensor<float32, [2, 3]>>`——驻留的**参数**；当时确实能读到拒绝 |
| 2026-10-26 `8adf778ef` | 按读数改成 `Device<f32>`：提交说明写「`tiles::compile` 切了 `loss`，区域交回的是 f32 标量 `Device<f32>`；同一份转储把参数印成 `(Device<f32[2, 3]>, Device<f32[3]>)`」——那是真实读数，`loss` 当时被切了 |
| 2026-10-28 `6be4596fc` | 「a statement of a sync block is ordered by its chain」：`eligibility` 的 `ordered` 加上 sync 链。无作用域的 `sync` 体从此不再被切，`loss` 留在主机。**这一笔把裁定的第 1 条落了下来，却没有连带落第 3 条**，入口就红了 |

也就是：**这条例外自 `6be4596fc`（2026-10-28）起存在**，不是「一直如此」，也不是最近合并引进的；`8adf778ef` 的读数是它在 2026-10-26 还读得到真实拒绝的证据。

## 三条路

- **甲（默认）按裁定补第 3 条**：把这条入口改成 `expect_exit = 0`，并用这个计划原本就有的 `compare_to` / `compare_fields = ["stdout_normalized"]` 对到「同一程序不带 `tiles::compile`」的那次运行上，把「此路不碰它的程序」钉住。它不会掩盖任何缺陷：同计划里 `grad_expand.untagged_graph_unchanged` 已经把「图没被改写」这一半钉住了。代价：一处期望字面量改动，一次闸门。
- **乙 连第 2 条一起做**：给 `tiles:upload`（其实现在写的是 `tiles:retain_preferred`）/`tiles:run_preferred_resident`/`tiles:download` 登记导数规则，让驻留值带上梯度通道，再按甲改入口。这是裁定原话「这一条和第 1 条一起修」。代价：动的是算子表的可微性标记与求导语义（`Device<T>` 的切空间），本车道按规矩不动。
- **丙 把切分恢复回去**：让无作用域的 `sync` 体也进区域。这直接违反裁定第 1 条，**不建议**；而且 `6be4596fc` 修的是「块里的语句被切出去就不再是块的语句」这一条真实缺陷。

## 默认建议（按它开工，不等回复）

**甲；本车道为止。** 这一页就是本轮的全部动作：不改期望、不改代码、探针已删（`git status --short` 干净）。

下一笔（由愿意动期望的车道做）：把 `test/plans/gpu/tiles.plan.toml` 的这条入口改成

```toml
kind = "behavior"
args = ["--devices", "cpu", "${case}", "tiles::compile", "std::nvm"]
expect_exit = 0
compare_to = "grad_expand.untagged_graph_unchanged_by_the_runtime"   # 另加一条只跑 std::nvm 的孪生入口
compare_fields = ["stdout_normalized"]
```

并在计划里补那条不带 `tiles::compile` 的孪生入口；乙另开一笔，落裁定第 2 条时把「驻留值没有梯度通道」这条拒绝按裁定删掉。

## 判据

1. 这条入口**不许**再用 `expect_exit = 1` 表达「驻留值没有梯度通道」——裁定第 2 条已判那条拒绝不能留，把它钉成期望就是把一条要删的拒绝固化成契约。
2. 也不许给这条入口挂 `known_fail`：程序今天的答案是**对的**（`[5.0, 7.0, 9.0]`，`wrong 0.0`），没有缺陷可登记。
3. 「`tiles::compile` 不碰无 `#gpu` 作用域的程序」要有独立读数：`compare_to` 两次运行的 stdout 逐字相同，而不是把两条输出各写一份字面量。
4. 第 2 条落地之前，任何 `#gpu` 作用域里的训练程序仍会撞上这条拒绝；那时的读数是「这条拒绝该消失」，不是「这条用例该改成 1」。