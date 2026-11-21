# 派生链位还没接上的两处（第三处已修）

`refactor(runtime): derive a statement's chain place from the graph's structure`（`6b105528a`）换了链位的来源：一条语句在块里的位置不再由构建器打的标记给出，而是由图的形状读出来（`docs/design/effects/edges.cn.md` 2、3.1、4.6、5.3、5.7）。`test/cases/opt/generic/static_branches.cml` 的 `tick 1` 因此修好，`test/plans/feat/opt` 1185/1185 全绿。快速层在 `6ba46ba81` 之后还剩三处是这次改动带来的；其中 `new_values.peek_loop.graph` 已由 `c667b6b6f` 修好（下面第二节记的是修法与它定下的判据），另外两处都测到了根因，都还没有修：它们要动的东西互不相同，任何一个都不是"顺手改一行"。

读法：每处三句——**症状**（能复现的原话）、**量到的**（探针读数，含文件与行）、**下一步**（先动哪儿，以及为什么试过的两条路不通）。

---

## 一、`exemplars/05_effect_order`：剪掉"臂里没有代码"的分支，它带的效果就没人扛了

**症状**

```
$ camel exemplars/05_effect_order/program.cml std::nvm
start / visit 3 / visit 1 / visit 2 / total 60
$ camel exemplars/05_effect_order/program.cml std::macro std::opt std::nvm
start / visit 3 / total 60
```

`visit 1 / visit 2` 是回调里的 `println`，加 `std::opt` 就丢掉（`visit 3` 在 `c667b6b6f` 之后回来了，那之前是三个全丢）。最小复现 `D:\tmp\e05-min2.cml`（两个字面元素 `[3, 1]`）：`std::opt` 下 `start / visit 3 / total 40`，只丢第二次。

**量到的（2026-11-11，这一轮换了定位方法：先在整条 pass 上做 A/B，再读那一次剪枝的现场）**

1. **剪枝就是现场。** 在 `pruneBranch` 开头加一个 `return false` 的开关（`CAMEL_NO_PRUNE`，已回退）后，最小复现输出 `start / visit 3 / visit 1 / total 40`，示例输出 `start / visit 3 / visit 1 / visit 2 / total 60` —— 全对。整条 pass 里关掉别的都不影响，所以丢打印的是 `pruneBranch`，不是内联交接（上一版报告在这里的判断是错的，交接那段的读数仍然有效，但它不是原因）。
2. **只关"被取的臂没有代码"的那几次剪枝就够了。** `CAMEL_NO_PRUNE_EMPTY`（`regions[*arm].empty()` 时不剪，已回退）同样让两程序全对。也就是说，丢效果的是"`then 0` 这种臂里一个节点都没有"的剪枝，而这条臂的形状是：`regions[arm]` 为空、`syncInputsOf(brch)` 为空、`join` 没有任何用户、被删的臂里有 10 个节点（含两次 `Func`）。
3. **那次剪枝的 `lost` 非空但一条边都没写。** 末尾的"删除臂读到块外效果"修补算出 `outsideReads: 39 40 41 29`、`lost: 40 41`（两个 `GATE`），`targets: 18`，然后两条都走了 `skip … computed after` 分支 —— 守卫认为 `18` 已经在 `40` 之后算，于是不写顺序边。这条守卫在门被溶解之后是否还成立，就是下一步要问的问题。

**下一步**

现场在"`lost` 里是门、而门迟早被溶解"这个组合上。那条 `isComputedAfter` 守卫是在**门还在图里**的时候读的，而门恰恰是这一轮要消失的东西：读它的意义应当是把门当作已经不在，再问"目标是否还在源之后"。这比撬动剪枝本身小得多，也是唯一不损失优化的方向。

**试过并回退的三条路（都改的是"剪不剪"，代价相同，记在这里以免重走）**

`regions[*arm].empty()` 时不剪 → 两程序全对，但 `feat/opt` 的 `static_branches.simplify.rgir`、`recursion_unroll.simplify.rgir`、`shape_unroll.simplify.rgir` 三张图全红：它们钉的正是"递归展开末尾的 `then 0` 被剪掉"。加条件限窄也没用：

- `empty && syncInputsOf(brch).empty()`：示例全对，三张图照旧红。
- `empty && 分支本身在某个臂区域里`：三张图绿，示例输出退回 `start / visit 3 / total 60`（更差）。
- `empty && 被删的臂里有 carriesOrdering 的节点`：示例全对，三张图照旧红。

结论：这三张图说明"`then <值>` 的常量分支必须被剪"是优化器的既有契约，所以修法不能是"不剪"，只能是"剪了之后把顺序写对"。另外，`export_gated_gru_step`（第三节）在关掉这几次剪枝后仍然失败，两者不是同一处。

第四条试过并回退的路，走的是"把顺序写对"这一侧：那条 `skip` 守卫把两个方向都当成"不用写"（`isComputedAfter(target, source) || isComputedAfter(source, target)`），我把它收窄成只在"写下去会成环"的那个方向（`isComputedAfter(source, target)`）才跳过，让"目标已经在源之后"也照样补一条 CTRL。`feat/opt` 的 `generic` 计划 121/121 全绿，但示例和最小复现的输出一个字没变 —— 所以那几次跳过不是打印丢失的原因，写不写这条边都改变不了 `40`/`41` 之后会发生什么。

---

## 二、`new_values.peek_loop.graph`：已修（`c667b6b6f`）

**症状**（修之前）

```
[FAIL] new_values.peek_loop.graph   stdout does not contain ':var/peek'
```

`test/cases/consistency/new_values/peek_loop.cml` 的存在意义就是钉住"`len(out)` 的读留不住数组，于是每轮 `append` 原地做、循环是线性的"。当时图里是 `:var/get`，每轮 `append` 都拷贝，20000 轮的循环成了二次方。

**量到的**

`Builder::markNonRetainingReads`（`src/compile/gir/builder_diagnostics.cpp`）的条件 3 要求读的**每个直接用户**都在 `chainedNodes_` 里。`__g_49__` 里这个读的用户是 `:struct/len_arr`——纯运算，按 5.3 已经不是链上语句，于是判据失败，读保持 `:var/get`。探针读出 `read=15 -> keep`、`read=18 -> keep`，正是这一条。

**修法**

条件 3 想说的是"读的值在变量被再次写之前就用完了"，"用户是链上语句"只是它在前一个模型里的写法。`c667b6b6f` 把用户的读取从"读一遍直接用户"改成一次走查：

- 走过**纯的、结果不含引用的运算**（5.3 把它留在链外，所以它在不在链上不说明它什么时候跑），也走过 **`GATE`**（它只把值送进链，不观察值，而 `closeChainOver` 把门建在链的末端）；
- 这样到达的节点只能定顺序、不可能抓住数组，所以它延长条件 4 的"最后一次读"射程，却**不**让读退回 `:var/get`——这就是 `len(out)` 能留在 `:var/peek` 的原因，也是"晚到的 `println` 照样拦住 peek"的原因（`n0 = len(acc)` 被推迟到最后那个 `println` 时，写落在读与最后读者之间，条件 4 仍然拒绝）；
- 两件事**故意不变**：直接读数组的节点（`println(v)`、`var` 实参）照样让它退回 `:var/get`，正如设计里"传参、放进容器即 `:var/get`"那句；而按值复制的值（`int` 等）没有什么是抓得住的，它的读保持原样——`cse_subject`、`written_variable` 的契约因此一个字没动。

走过的弯路记在这里：先把条件 3 放宽成"值能到达的节点里有链上语句"，`peek_loop` 变成 `:var/peek`，但 `new_values.peek.*` 六个后端一起红——`len` 被推迟到最后一个 `println` 才跑，读到的是几次 `append` 之后的数组，期望 `1` 打出 `2`。教训是"不是能到达链，而是在下次写之前到达链"，而这一点靠的是条件 4 的 `lastUser`，不是条件 3 的放宽。

---

## 三、`export_gated_gru_step`：间接调用的被调者变成了程序的参数端口

**症状**

```
export_gated_gru_step.cml:53:26: [Error]: An error value reached the exit of 'main' without being handled:
  ExportUnsupported: in 'run.pb': an indirect call remains after simplification;
  its callee must be known at export time.
```

**量到的**

`Evaluator::evalIndirectCall`（`modules/onnx/exporter.cpp` 3161 行）在失败点被探针（已回退）读到：

```
indirect call ref=2 with=1 kind=0 form=0 closureKind=0
  with[0] ref=1 nodeKind=1
```

- `with` 不空，所以不是"被调者边丢了"；
- `with[0]` 的节点是 `Port`（`kind=1`），求值结果却是常量张量（`kind=0 form=0`，`Tensor`）；
- 该图只有一个端口（`ref=1` 就是它），调用节点是 `ref=2`。

即：间接调用的被调者操作数指向了程序的**参数端口**，而那个端口绑的是 `(m, x)` 这个元组，不是 `step` 这个函数值。上报位置说的是"after simplification"，所以先怀疑内联/拼接把 WITH 边重新指错了对象——和第一处是同一块代码的邻居。

**下一步**

把 `run.pb` 的图在导出时打出来（今天没有任何 pass 能打印这个程序：`std::gir` 到不了，需要一行 `-vv` 或者给程序构建器一个 `std::jgir` 口），看调用节点的 `WITH[0]` 到底是谁。应当是 `step` 的函数常量或其闭包端口。在拿到那张图之前，任何改动都只能靠猜。

---

## 附带：环境噪声与本轮已修的部分

- `stream.generators_shapes_consumed.{nvm,fvm,jit,prl}`、`match.complete.prl`、`capacity.growth_nvm_gc_stress`、`registry.*`（5 个）、`gate.conformance_matrix`、`transpose.call_of_a_body_closure.function_value_argument_is_not_read_as_the_callee.nvm` 在满载并发跑快速层时红过，单独跑或换一轮负载就绿——是并发噪声，不是这次改动带来的。
- 本轮已经随 `46785728e`、`6ba46ba81` 修好的：`entering.graph_*`、`partial_application.graph`、`generic_values.graph`、`comprehension.graph_*`、`tutorial.examples.13_graph_rgir`（六张图加一对 `SYNC`+`CTRL`，即 4.6 的"调用是语句"）、`rename.{env,process}_graph_identity` 与 `poison` 里那份拷贝（摘要重录）、`framed.loops.regions.{lowered,compiled.inline}`（`ticks` 的 helper 理由从 `the round contains OPER` 换成 `the round makes the state tuple anew every round`，两条都是真的，行为不变）。
- `c667b6b6f` 之后快速层是 8862 通过 / 20 红：其中 14 条是本机基线，4 条是上面那类并发噪声（`stream` 那个计划单独跑 147/147 全绿），剩下 2 条就是本文第一节和第三节。