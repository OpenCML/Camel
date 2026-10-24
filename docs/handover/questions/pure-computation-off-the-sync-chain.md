# 纯计算离开同步链：红已复现，但它不是一条车道能单独落的

状态：待拍板（车道 `handover/h394-order-edges`，2026-10-24）
相关：`docs/design/effects/edges.cn.md:14`、`:48`、`:49`、`:51`、`:54`（第 5 节第 3/4/6/7 条）、
`docs/design-principles.cn.md` 3.4、`docs/handover/reviews/questions-2026-10-23.md`
（`rule-conditions-and-policy` 第 1 点、`h241-limits` 第 2、3 点）

## 问题

负责人裁决的第 2 项是「纯计算不再有同步语义（同步链只连带效应语句）」。
我按 `edges.cn.md:14` 的字面意思实现了一遍（纯算子既不进链、也不推进链尾），
红确实消掉了，但整层从 `fail: 0` 变成 `fail: 88`，其中包含**结果变化**和**丢失优化**，
不是更新期望就能收口的。请拍板：是按 A 照字面落地并把它当成 H242 整批的一部分，
还是按 B 把链边改成"按语句发"（`edges.cn.md:14` 相应改口径）。

## 红（同一窗口、当前构建 `3118694a0`，只回退待判改动）

探针 `tmp/h394probe/pure_sync.cml`（内容见本文件末尾）：

```
$ camel /tmp/h394_pure_sync.cml std::jgir
products non-data edges: 2 [('control', 'g2n2', 'g2n4'), ('control', 'g2n5', 'g2n6')]
chained  non-data edges: 2 [('control', 'g3n2', 'g3n4'), ('control', 'g3n7', 'g3n8')]
```

`products` 是 `let a = x * 3 + x * 5; return a`（块内无效应）：两条控制边一条连两个
乘积（`g2n2 -> g2n4`），一条把块的值挂到链尾。裁决说这两条都不该在——两个乘积是
同一条语句的子表达式，`questions-2026-10-23.md` 把它点名为"旧建图器一律生成控制边的产物"。

## 改法（补丁在 `D:\Projects\Camel-wt\notes\h394-item2-pure-off-chain.patch`）

`src/compile/gir/builder_operators.cpp:2817` 那段对**每个**算子节点都推进链尾并连一条
控制边。补丁加一个判断：`OperatorTable::isPure(uri)` 为真的算子不进链、不推进链尾、
不进 `chainedNodes_`；表里没声明为纯的（含未声明的）照旧。改完上面两张红消失，
我自己加的 `feat.linear.sync_block_edges` 两条断言转绿。

## 实测影响（整层 `smoke,test/plans`，tier fast，`-Jobs 8`）

```
改前： total 6000, pass 5985, fail 0,  skip 11, RUN_EXIT=0
改后： total 6003, pass 5900, fail 88, skip 11, RUN_EXIT=1
```

88 项按成因分组：

| 组 | 项数 | 代表 | 性质 |
|---|---|---|---|
| 故障被推迟到块的值 | 16 | `fault.after_output.*`、`attempt.effects.*`、`attempt.siblings.*`、`attempt.resources.isolated.*` | **行为变化**：`let r = 5 / z` 现在被块值的同步边钉在链尾，`println('not reached in work')` 会先打印再报错 |
| GPU 区域抽取的不变量 | 25 | `regions.*`、`compiled_region_tuple_and_embedded_axis.*`、`host_resident_region_execution.*`、`outlined_region_execution.*`、`native_region_invocation.*`、`preferred_resident_execution.*`、`public_region_graph` | **结构变化**：区域抽取写着"控制端点必须留在区域外"，纯节点没有控制边后这条不变量要重述 |
| 规则库/改写测试 | 12 | `camel_passes.cse.*`、`camel_passes.fold_fault.*`、`algebra_rules.*` | 需要逐条看是期望陈旧还是改写判据变了 |
| 未使用的纯计算不再运行 | 8 | `softmax_cross_entropy_labels_out_of_range.*`、`one_hot_label_out_of_range.*` | **行为变化**：用例靠"sync 把纯计算拉活"来触发越界诊断；现在值没人用就不跑，诊断消失、退出码变 0 |
| 控制边计数当度量 | 6 | `saturate.{default,safe,deterministic}.{nvm,fvm}` | `waits=1` 变 `waits=0`，与 `edges.cn.md:49` 的预测一致 |
| 费用启发式数控制等待 | 6 | `gemm_budget.cli_*`、`pools.cli_*` | 选路结果变了（`expected=4`，实测 `1`），退 2 |
| 图快照/golden | 11 | `gir.gir_snapshot`、`boundaries.agir`、`rename.hash_graph_identity`、`01_scalar_boundary` 等 | 机械更新 |
| `:var/peek` 失去安全论证 | 4 | `new_values.peek_loop.{fvm,jit,prl,graph}` | **丢优化**：`markNonRetainingReads` 的第 3 条要求"读的使用者在链上"，纯使用者离链后该条件恒假，`:var/peek` 不再生成 |

## 需要拍板的四点（都是裁决的推论，规范没有直接说）

1. **故障位置**：`sync` 块里纯计算出的故障，是否就该推到块的值那里（=块里所有效应之后）？
   `fault_after_output` 的用例注释写着"故障前四行都在标准输出、按顺序"，现在多出一行。
2. **没人用的纯计算不再报故障**：`let loss = softmax_cross_entropy(logits, [0, -1])` 现在
   静默通过。这与非 `sync` 代码今天的表现一致（那也是 `:var/peek` 之外的既有语义），
   但两个一致性用例正是靠它触发诊断。
3. **`:var/peek` 的前提**：它的安全论证依赖"读的使用者排在链上、因此排在后续写之前"。
   纯使用者离链后这个前提没了。`edges.cn.md:48` 说带同步边的 `GATE` 算含效应，但没写读的
   使用者离链后怎么办。这一条压在 H242a（`var` 读写改成"读是从最近一次写的数据边"）上。
4. **控制边计数的下游**：`saturate` 的 `waits`、GPU 区域抽取的端点规则、`gemm`/`pools` 的
   费用口径都把控制边当结构事实。它们要不要一起改口径、什么时候改。

## 选项与代价

**A. 照 `edges.cn.md:14` 字面落地（纯计算完全离链），修完这 88 项。**
代价不只是更新期望：`markNonRetainingReads` 的安全条件要重写（依赖 H242a 先落）、区域抽取的
不变量要重述、两个费用启发式要改口径，外加四个一致性用例的期望与注释、11 个 golden。
这是 H242 整批的量，不是一条车道一次提交。

**B. 链边改成"按语句发"：语句结束时才从上一个语句节点连一条到本语句节点。**
红里的 `control g2n2 -> g2n4`（两个乘积之间）同样消失，因为一条语句只发一条边；
而 `fault_after_output` 的故障顺序、`:var/peek`、费用启发式、区域抽取都不动，影响面预期很小
（要用同一套命令实测确认）。建图器已经有语句粒度的记账（`builder.h:2242`
`lastStatementNode_`："The last statement of the block visitExecNode finished most recently,
and its node"），实现是改 `builder_operators.cpp:2817` 那一处的触发时机。
冲突：与 `edges.cn.md:14` 的"纯计算……不在同步链上"直接冲突，那一行要改口径。
注意两张红证据只否掉了"每条控制边都生成"，没有否掉"语句在链上"。

**C. item 2 与 item 1 一起留给 H242 整批落地，本车道只交付能单独变绿的部分。**
代价 0 风险，但第 2 项裁决停在纸面。

## 建议

建议 **B**：它是"只去掉裁决点名为错的那条边"的最小改动，不需要先落 H242 的其余部分，
也不会拿 88 项（含结果变化与丢优化）去换一条边。若作者确认要的是 A，那么 A 必须作为 H242 的
整批集成来做，且 H242a（`var` 读写改成数据边）要排在它前面——`:var/peek` 的安全论证和
`allocs` 黄金都压在 H242a 上。本轮按 C 收口。

## 依据

- `docs/design/effects/edges.cn.md:14`：「**纯计算没有顺序边，也不在同步链上**：它执行与否
  完全由值是否被用到决定……同步链只连带效应的语句（设计原则 3.4，2026-10-24 改……）；
  唯一的例外是块的值接收链尾的同步边。」
- `docs/design/effects/edges.cn.md:49`：「纯计算上不再有任何非数据边，所以这个遍的工作量大幅缩小。」
- `docs/design/effects/edges.cn.md:51`（4.6）：同步边只连同一个同步块直接写着的相邻语句。
- `docs/handover/reviews/questions-2026-10-23.md`，`rule-conditions-and-policy` 第 1 点。
- 红与影响的原始日志：`D:\Projects\Camel-wt\notes\h394-after-item2.txt`（改后整层）、
  `D:\Projects\Camel-wt\notes\h394-baseline.txt`（改前整层）、
  `D:\Projects\Camel-wt\notes\h394-item2-pure-off-chain.patch`（改动本身）、
  `D:\Projects\Camel-wt\notes\h394\sync_block_pure.cml` 与
  `D:\Projects\Camel-wt\notes\h394\sync_block_edges.plan.toml`（断言这两张红的用例与计划，
  未随车道合入，因为只有 A 或 B 落地后它们才会绿）。

## 探针源码

```camel
module pure_sync

func products(x: int): int sync {
    let a = x * 3 + x * 5
    return a
}

func chained(x: int): int sync {
    let a = println(x * 3 + x * 5)
    println(a)
    return a
}

func main(): int {
    return products(2) + chained(2)
}
```