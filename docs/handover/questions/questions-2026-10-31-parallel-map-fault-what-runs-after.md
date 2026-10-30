# 一问：并行 `map` 在一处故障之后，低于它的下标还执行吗？

> 状态：等作者裁决。
> 相关：`docs/design-principles.cn.md` 3.6（`L198`）；`src/passes/sched/taskflow/taskflow.cpp:2576-2811`；`docs/handover/reports/code-review-backends.md:37`；用例 `test/plans/feat/consistency/attempt.plan.toml:207`。

## 现象

3.6 的原文（`docs/design-principles.cn.md:198`）：

> 并行的 `map` 中一个元素出了故障：已经开始的元素跑完，还没开始的不再开始。多个元素同时出故障时报下标最小的那个。

实现让"还没开始的不再开始"只管**下标更大**的元素（`taskflow.cpp:2811`）：

```cpp
if (lowest.load(std::memory_order_acquire) < index) [[unlikely]] {
    return;
}
```

`lowest` 是已知的最小故障下标。所以**下标更小、还没开始的元素，在故障已知之后仍然会开始**；这一点代码自己写在注释里（`:2795`）：

> "an element before it still runs, as it would have run first on a back end that takes the elements in order. The fault reported is therefore the one the linear back ends report."

同一段注释在 `:2797` 留着这个未决标记：

```cpp
// DESIGN-QUESTION(parallel-fault-lower-elements): the alternative is to start nothing at all
// after the first fault met, and then which fault is reported differs from run to run.
```

这是今天唯一记着这件事的地方：标记在代码里，不在原则里（`code-review-backends.md:37` 也点了它）。

## 两个读法各自要付什么

- **按 3.6 字面**：故障一出现，还没开始的一律不启动，包括下标更小的。那么"报下标最小的故障"就不可判：更小下标的元素可能还没开始，它会不会出故障没人知道；报出来的故障随哪条线程先撞上而变（`:2797` 那句 "differs from run to run" 就是代价）。
- **按今天的实现**：故障已知后只挡更大的下标，更小的仍启动，报的故障稳定（顺序后端报同一个），但**执行的元素数**比字面规则多：更小下标、故障之后才开始的元素也会跑，它们的效应（`println`、写 `var`）可见。跨后端一致性用例今天只比"报的故障值"（`attempt.plan.toml:207`，`attempt.lowest_index`），不比执行了哪些元素。

3.6 已经说"元素函数里的效应在并行 `map` 下顺序不作保证，由使用者负责"，所以多跑几个**不违反效应那条**；争的是 3.6 前半句"还没开始的不再开始"——它按字面就挡掉了今天这个做法。

## 为什么不能自己定

3.6 一句话里同时写了"执行到哪"和"报哪个"；两条读法各保一句、各丢一句。选哪句，决定 3.6 要不要改，也决定"同一输入同一输出"这条闸门在故障路径上还算不算数（`attempt.lowest_index` 的断言是"每一次运行、四个后端都报同一个值"）。

## 选项

- **① 保今天的实现，改 3.6 的措辞**：写"已经开始的元素跑完；下标小于已知最小故障、还没开始的元素仍会开始；下标更大的不再开始；报最小的下标"。
  代价：3.6 改一句；补一条用例钉住"更小下标的元素仍执行"；跨后端比较"报的故障值 + 允许范围内的执行数"。**验收**：`attempt.lowest_index` 仍四十次同值，新增一条部分故障的用例四个后端同判。
- **② 按字面执行，接受故障报告随调度**：故障一出现就什么都不再启动；报"实际撞上的最小下标"。
  代价：3.6 不改；要在原则里给"同一输入同一输出"开一个故障路径的例外，或者接受同一程序两次运行报不同下标。**验收**：同一程序连跑二十次，报的故障可以不同——这条要作者签字。
- **③ 都启动，全部跑完再报最小下标**：故障不取消任何元素。
  代价：故障路径等于不短路，并行度最差；但报的故障与执行数都确定。3.6 前半句改成"已经开始的、以及还没开始的，都跑完"。**验收**：执行数 = 元素总数，四个后端一致。

**默认建议：①**。今天的做法不是随手写的：它保住了"报最小的下标"和跨后端一致，代价只是更小下标的多跑几个——而 3.6 已经允许效应顺序不作保证。改 3.6 的措辞比改实现便宜，也把今天的选择写成规则，下一个人不用再从注释里猜。

**要作者答的一句**：`map` 在一处故障之后，下标更小、还没开始的元素是否仍执行——按今天的实现（①，改 3.6 的措辞），按字面停掉（②，接受报告随调度），还是全部跑完（③）？

在这一页裁定之前，`taskflow.cpp` 按①做；原则按字面写的是②的读法。两者不一致这件事，今天只有 `:2797` 那条 `DESIGN-QUESTION` 记着。
