# 每日审查：11-23 17:40 至 11-24 17:40 合进 develop 的代码

三个只读审查按前端、图视图与后端、工具与测试分头读，下面只列决策者逐条核实成立的。每条写在哪、错在哪、怎么修、怎么验收。

## 1. 过程痕迹：检查器看不见，于是到处都有

`scripts/check-process-traces.mjs` 只扫 `src/`，模式里没有派工编号（`D-1123-78`、`C-1123-17` 这种）、没有"待审"、没有车道代号（`lane-shapeext`）。所以它报绿的同时，这一天合进来的痕迹有：

- `src/`：`runtime/writings.h:157`、`stdlib/rewrite/module.cpp:1834`、`stdlib/test/include/camel/passes/test/cost.h:12` 写 `D-1123-78`；`std/exec/exec_plan.cpp:1086`、`std/prl/handoff.cpp:108,198` 写 `C-1123-17`；`random.cml:113`、`str.cml:122` 写 `D-1123-49`；`ops/operator_record.h:568,673`、`ops/operator_record.cpp:15`、`ops/operator_registry.cpp:39,45` 写"待审"和"this lane"。
- 测试：`test/plans/feat/compile/stream.plan.toml:1462,1466` 写派工编号；`test/plans/feat/modules/onnx.plan.toml:882,2135,3009` 的 `known_fail_owner` 写车道代号；`test/plans/**` 里六十个文件的 `desc` 与注释写"the ruling of 2026-11-xx""the author's ruling"；`test/cases/modules/tiles/` 五处写"the author's ruling of 2026-11-23"。
- 工具：`internal/tools/merge-lane.ps1`（派工编号、提交哈希、"iron rule 17"）、`scripts/check-clang-syntax.mjs`、`scripts/count-graph-readers.mjs`、`scripts/law-cotangent-ports.mjs`、`internal/tools/camel-cleanup.sh`、`scripts/check-doc-snippets.mjs` 的 `PENDING` 理由（车道名与两个派工编号，同一句贴了十几遍）。

修法：
- 检查器扫 `src/`、`scripts/`、`internal/tools/`、`test/`（计划的 `desc`、`known_fail_*`、注释，以及用例文件的注释）。
- 模式补上 `\b[DC]-\d{4}-\d+\b`、`待审`、`this lane`、`lane-[a-z0-9-]+`、`iron rule \d+`。
- 现有的一次清干净：保留技术理由，删掉编号、日期、角色、车道和哈希。理由写的是语言规则时，指向设计原则的条目，不指向裁定。

验收：检查器在整棵树上为 0，并且有一个用例证明它能抓到上面每种写法。

## 2. 节点角色自己算了一遍"是不是效应"

`src/kernel/runtime/node_roles.cpp:60-92` 手写了"算子自己的访问与函数操作数的子句取并"这条规则，判据是 `clause->pure() && spec.accesses.empty() && !spec.allocates && !spec.externalEffect`。同一条规则在 `effects.cpp` 的 `effectSummaryOfOperator` 和 `resources.cpp` 的 `Resolver::oper` 里已经各有一份，三份判据不一样，`sem::declaresOutside` 只有一份在用。这违反架构 2.3：事实只有一套规则算，别处只读。

修法：节点角色读事实层给这个节点的效应摘要，删掉这段手写的并。

验收：加一个用例，断言 `state:update` 这类节点在节点角色里是效应（即使它跑的函数是 `pure`），并断言一个只跑 `pure` 函数、自己什么都不碰的高阶算子不是效应。

## 3. 按 URI 文本分类形状的表还活着

`src/stdlib/std/exec/include/camel/execute/shaped_operators.h:81` 的 `shapeOfUri` 仍然按 `":op/cast"`、`":op/copy"` 等字符串比较来分类。它自己的注释说"must be gone"，但它还有两个调用者：`src/stdlib/onnx/exporter.cpp:3812` 和 `src/stdlib/test/oper_shape.cpp:66`。算子记录上已经登记了形状（`declareShape`），所以同一个事实有两处，没有任何东西保证它们一致。另外 `declareShape`、`stateShape`（包括登记形状冲突时抛错）和 `shapeOfOperator` 都没有测试。

修法：导出器和测试改为按 URI 在算子表里查记录、读记录上的形状；删掉 `shapeOfUri`；测试改成钉记录上的形状，并加一条"同一算子登记两种形状被拒"的用例。

验收：`git grep -n shapeOfUri -- src` 为 0。

## 4. 合并闸门里"说明了就允许删红用例"的口子太宽

`scripts/merge-readings.mjs`：
- `messageNames`（约 321 行）在合并说明里找用例名，或者只要找到计划名的子串就算"说明了"。计划名常是 `var`、`tags`、`stream` 这种短词，一句说明里出现 "variable" 就等于批准删掉 `var` 计划里所有的红用例。
- `removalExplained`（约 308 行）只要计划文件被动过，就认为是这次改动删了这条用例，不看这条用例本身是否从文件里消失。

这正是"把闸门变绿的最老办法"。

修法：
- 说明里必须出现完整的用例名，按词边界匹配，计划名不算数。
- 删除要有证据：这条用例的条目确实从计划文件的差异里消失了。

验收：加测试覆盖三种情况——说明只提到计划名、说明里有子串、计划文件被动过但用例还在——三种都判为未说明，报红。

## 5. 工具自己的测试没有任何计划在跑

`test/tools/reading-normal.test.mjs`、`internal/tools/merge-readings-superset.test.mjs`、`internal/tools/merge-lane-start.test.ps1`、`internal/tools/camel-cleanup.test.ps1`、`internal/tools/camel-remote-cleanup.test.ps1`，以及 `scripts/count-graph-readers.mjs --self-test`，都没有出现在 `test/plans/`、`scripts/gates.mjs` 或工作流里。`scripts/check-clang-syntax.mjs`（511 行，带"读不到改动就放宽到全量"的回落）没有任何测试。

修法：把它们放进一个工具计划，按路径选测规则挂上（改到对应工具时必跑）；给 `check-clang-syntax.mjs` 补一个自检，覆盖"依赖记录读得到时只查改到的单元"和"读不到时放宽并在读数里写明"。

## 6. 默认代价的用例没有测到代价

`test/plans/feat/mm/writings.plan.toml` 的 `writings.extract_default_cost` 只断言输出里有 `"15"`，而不管选了哪种写法，输出都是 15。`desc` 说"除法比乘法贵"，用例并没有测这一点。`relativeCost` 小于等于 0 被拒、同一算子声明两次代价被拒，这两条也都没测。

修法：
- 加一个两种写法代价不同的程序，用 `rewrite::extract` 打印选中的写法：默认代价选乘法那种，按节点数计代价选另一种。
- 两条拒绝各加一个用例。

## 7. 合并树的路径有两份

`internal/tools/merge-lane.ps1:92` 写死 `$MainCheckout = 'D:\Projects\Camel-wt\merge'`，而 `scripts/hooks/pre-push` 从 `camel-gate.local.json` 的 `mergeWorktree` 读同一件事。现在有两个合并位，这个写死的默认值对第二个合并位就是错的；两份不一致时，钩子会在跑完一次长合并之后才拒绝推送。

修法：`merge-lane.ps1` 从同一个配置读合并位的工作树（配置写成合并位列表），删掉写死的默认值；注释里的"iron rule 17"去掉。

## 8. `time_sensitive` 由两个解析器读

跑测工具按 TOML 结构校验这个字段，而 `internal/tools/camel-remote.ps1:1516-1519` 用正则在第一个 `[table]` 之前找 `time_sensitive = true`。TOML 合法、正则不认的写法，跑测工具那边通过，闸门这边却看不见，于是性能计划会在别的构建旁边测时间，而这个字段存在就是为了防止这种情况。

修法：闸门问跑测工具（同一个 TOML 解析器）这个计划是不是时间敏感的，不自己解析。令牌池那一步本来要改主机脚本，一起做。

## 9. 一处已裁、还没落地的期望

`test/plans/feat/compile/stream.plan.toml` 的 `stream.range_value_map_no_instance` 仍然钉 `expect_exit = 1`（`map` 作用在范围上报找不到函数）。今天的问题页已经答了 A：按设计原则"范围是值，不是数组"，`map` 作用在范围上给出结果数组，应当编译。这不算审查出的缺陷，但要有一条车道负责：改实现，并把这条期望改成 `expect_exit = 0`，合并说明里点名它。

同一族的还有 `random.cml`、`str.cml`：推导式的源先 `to_array` 再遍历。注释说要等 `operator for` 落地，而 `for` 遍历范围已经在 range-ri 里合进来了。核实推导式能不能直接遍历 `0 .. n`：能，就改回去；不能，注释写清技术原因（不写编号）。
