# 片 4 的同一笔清单：`:op/keep` 删掉，保活交给 `GATE`

**依据**：负责人 2026-11-02 19:30 的裁定——`keep(x, v)` 就是值 `v`、同步前驱 `x` 的 `GATE`，**另设一个算子是第二套机制，违反原则 2.3**；`docs/design/effects/edges.cn.md` 5.3（`GATE` = 一个数据端口 + 一个同步前驱）；原则 3.1（节点执行当且仅当被用到）、3.4（同步块不向内继承）。

**这一笔不许分两次落**：产生侧换掉、算子表还留着，就是"两套并存"。清单里每一行都在同一笔里。

## 1. 要删的 8 处，各自由谁承接

| # | 位置 | 删掉什么 | 谁承接 |
|---|---|---|---|
| 1 | `include/camel/core/operator_table.h:153` | `inline constexpr OperId Keep = 22;` | 无：`GATE` 是节点种类（`GCNodeKind::Gate`），不是算子，不需要算子里程碑 |
| 2 | `include/camel/core/operator_table.h:127-151` | `:op/keep` 的契约注释（"the value `v`, with `x` as an input whose value no result takes"…） | 契约移到 `edges.cn.md` 5.3：`GATE` 的一个数据端口读值、一个同步前驱等链 |
| 3 | `include/camel/core/operator_table.h:187` | `kWellKnownOperatorUris` 里的 `":op/keep"` 一项 | 表尾回到 22 项；`kWellKnownCount` 从 23 改回 22，id 22 空出（**不给新算子复用**，见第 6 条） |
| 4 | `src/builtin/executors/builtin.cpp:170` | `{"op/keep", __keep__},` 的登记 | 无：`GATE` 由执行器直接实现（`GCNodeKind::Gate` 的分支），不经算子表 |
| 5 | `src/builtin/executors/builtin.cpp:643` | `key != "op/keep"` 这个纯性豁免 | 无：它是为 `op/` 前缀的自动分类留的洞，算子不在，洞也不在 |
| 6 | `src/builtin/operators/ops.h:19-33` | `__keep__` 的声明与注释 | 无 |
| 7 | `src/passes/test/semantics.cpp:151-157` | `printTraits(":op/keep")` 那一段 | 判据改由第 3 节的 `keepshape`/`keepnode` 新读数承担（见下） |
| 8 | `test/plans/feat/modules/operator_semantics.plan.toml:33-34` | 两行 `SEMANTICS traits :op/keep …` | 删；该计划其余行不动 |

## 2. 要改的 4 处产生侧：都改成"数据口 `v` + 同步前驱 `x` 的 `GATE`"

| # | 位置 | 现在 | 改成 |
|---|---|---|---|
| 1 | `src/passes/opt/generic/rewrite.h:180-218` | `keepThroughValue` 的契约："routes the node through `:op/keep(x, v)`…" | 契约原文改为：**"把 `x` 挂成 `GATE` 的同步前驱、把 `v` 接进它唯一的数据端口"**——`GATE` 的等待是同步前驱（5.3），'被用到'因此由同步边传递（`edges.cn.md` 1.5），这就是 3.1 要的那条 |
| 2 | `src/passes/opt/generic/rewrite.cpp:292-302` | `draft.addOperNode(header->dataType, nullptr, kWellKnownOperatorUris[oper::Keep])` + `setNormInputs(node, {source, passed})` | `draft.addGateNode(header->dataType)`；`setNormInputs(node, {passed})`（**只有一个数据端口**，读 `v`）；`draft.setSyncPredicate(node, source)`（同步前驱是 `x`）；`setNodeOrigin` 保持原样 |
| 3 | `src/passes/opt/generic/generic.cpp:731` | 注释："What takes the branch's place in the value is `:op/keep(x, v)`" | 注释改说 `GATE`：值读 `v`，同步前驱是 `x` |
| 4 | `src/passes/opt/generic/generic.cpp:1498` | 注释："what stands in its place is `:op/keep(x, v)` carrying the…" | 同上 |

**插入处的链位**：`GATE` 建在 `removed` 所在的区域（`draft.setInsertRegion(region)` 已有），同步前驱指向 `x`，因此 `checkSyncEdges` 的"同一块、相邻"校验与今天 `writeSyncStatement` 的口径一致；**不得**再写控制边（`writeControlLink` 是空的了，这正是片 3b 之后的状态）。

## 3. 三个测试装置：读数从"数 `:op/keep` 节点"改成数什么

**这一节是这次删算子最要紧的部分**：删掉一个算子，每个读它的地方都要有新的判据，否则就是"删了就没有答案"。

| 装置 | 今天读什么 | 改成读什么 |
|---|---|---|
| `src/passes/test/keepnode.cpp:41,72-110` | `using camel::core::oper::Keep;`，数 `OperId == Keep` 的节点，读 `keep=1 reads_the_divisor=1` | 数**`GCNodeKind::Gate` 且 `syncInputsOf(gate)` 非空**的节点：`gate=1`；"被保活的那个节点"= `syncInputsOf(gate).front()`；`reads_the_divisor` 的判据不变（那个节点是否读除数）。输出改 `KEEPNODE gate=1 reads_the_divisor=1` |
| `src/passes/test/keepshape.cpp:82-100,120-140` | `operUriOf(user) == ":op/keep"`，输出 `uri=:op/keep` | 读 `draft.header(user)->kind == Gate` 且 `syncInputsOf(user)` 非空：输出 `gate=1 sync_pred=1`（取代 `uri=:op/keep`）；`data_user`/`standing` 两列照旧（它们本来读的就是数据边与链位，与算子无关） |
| `src/passes/test/semantics.cpp:151-157` | `printTraits(":op/keep")`：读"那个不许被改的算子的 traits" | **删掉整段**。它守的性质是"保活机制不许被某个遍当成纯计算"——`GATE` 不是算子、没有 traits 可读；同一条性质现在由 `graph.cpp:561` 的提交断言（**一个既无控制入边又无同步前驱的 `GATE` 被拒**）与 `test::keepshape` 的 `standing=alive` 承担，判据不丢 |

## 4. 期望值与人口映射（逐行原文）

```
test/plans/feat/opt/effects_kept.plan.toml:172
  expect_stdout_exact = "KEEPSHAPE keep=1 uri=:op/keep data_user=1 sync_statement=0 wait_edge=0 waited_on=0 standing=alive\n1\n"
test/plans/feat/opt/effects_kept.plan.toml:187
  expect_stdout_contains = ["KEEPSHAPE keep=1 uri=:op/keep", "\"label\": \":op/keep\"", "\"exit\": \"g2n5\""]
test/plans/feat/opt/effects_kept.plan.toml:213
  expect_stdout_exact = "KEEPSHAPE keep=1 uri=:op/keep data_user=1 sync_statement=0 wait_edge=0 waited_on=0 standing=alive\nKEEPSPOTS keeps=1 in_chain=0 out_of_chain=1 waited=0\n1\n"
test/plans/feat/opt/effects_kept.plan.toml:258
  expect_stdout_contains = ["KEEPSHAPE keep=1 uri=:op/keep", "N04 [label=\":op/keep\", type=\"OPER\"", "N04 -> O02 [label=\"0\", type=\"EXIT\"]"]
test/plans/feat/opt/effects_kept.plan.toml:269
  expect_stdout_contains = ["KEEPSHAPE keep=1 uri=:op/keep", "label=\":op/\\nkeep\", type=\"OPER\"", "N04 -> O02 [label=\"0\", type=\"EXIT\", style=solid]"]
test/plans/feat/opt/keep_node.plan.toml:47
  expect_stdout_contains = [":op/keep", "KEEPNODE keep=1 reads_the_divisor=1 printed=1"]
test/plans/feat/opt/keep_node.plan.toml:55
  expect_stdout_contains = ["KEEPNODE keep=1 reads_the_divisor=1 printed=1"]
test/plans/feat/opt/keep_node.plan.toml:70
  expect_stdout_exact = "KEEPNODE_PURE keep_pure=0 keep_shared=0\nside\n2\n"
test/plans/feat/modules/operator_semantics.plan.toml:33-34
  line('SEMANTICS traits :op/keep pure=0 elementwise=0 stateOnly=0 mayFault=1 callsGraphs=0 readsGraphs=0')  ×2
test/cases/opt/pass_written/check.mjs:53
  uri: ':op/keep',   （人口映射的一格）
```

改法：`uri=:op/keep` → `gate=1 sync_pred=1`；`label=":op/keep", type="OPER"` → `label="GATE", type="GATE"` 且多一条 `x -> GATE [type="SYNC"]`；`keep=1` → `gate=1`；`KEEPNODE_PURE keep_pure=0 keep_shared=0` → `KEEPNODE_PURE gate_pure=0 gate_shared=0`；`operator_semantics` 的两行删；`check.mjs:53` 的 `uri: ':op/keep'` 一格删（人口表变少一格，`pass_written` 的"人口只许长"规则随之按新表读）。

**顺带**：`test/plans/feat/opt/effects_kept.plan.toml:154-171` 的注释块（"The one shape `:op/keep` exists for…"）与 `:157` 的一句"replaces the operator"同笔改掉，否则又留下一段讲已删算子的注释。

## 5. 验收（这一笔落地的判据）

1. `git grep -n ':op/keep' -- src include modules test` → **0 命中**；`git grep -n 'oper::Keep\|__keep__'` → **0 命中**。
2. **四后端读数**：`let a = println(1); return if 1 > 2 then a else 0` 在 `std::nvm`/`std::fvm`/`std::jit`/`std::tf` 下**都打印 `1`**，开关 `std::opt` 一致（负责人 19:30 给的读数）。
3. `std::cml` 往返：结果打印成带 `sync` 块的源码，往返等价。
4. 全量 `[SUMMARY] fail 0`、`headers --check`、`static_GUARD` 0。
5. **id 22 空出不复用**：`kWellKnownCount` 回到 22 之后，`OperId 22` 不分配给任何新算子——复用会让"删掉"变成"换了名字"。

## 5.1 这一笔**不动** `graph.cpp:561`（原第五节把它并进来的写法作废）

`src/runtime/graph.cpp:561` 的 `facts.hasWaitEdge = !graph->ctrlInputsOf(ref).empty();` 问的是**一个 `GATE` 有没有等待**，不是"效应有没有凭据"——两件事。片 4 把 `:op/keep` 换成 `GATE` 之后，`GATE` 的等待是同步前驱（5.3），控制入边那一项在**这一段**里可以继续存在，直到"谁还在写 `GATE` 的控制入边"这件事单独清完（那是 `coerceArmExit` 一族，属于控制边删除的另一个分片）。**所以片 4 不碰它**，只在清单里记一句：等 `GATE` 的控制入边写点全部迁到同步前驱之后，这一项才是删的对象。

## 6. 这一笔不做的事

- 不动 `writeControlLink` / `appendControlEdge`（那是片 3b 的读写侧）。
- 不动 `EffectLivenessFacts` 的字段集合（`verify` 的 P0-4 要收窄它，两边不各自改同一个模型）。
- 不为 `GATE` 新增算子身份（5.3 说它是**节点**，加算子就是把删掉的那套再建一遍）。