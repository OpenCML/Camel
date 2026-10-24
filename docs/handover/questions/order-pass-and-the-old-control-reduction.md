# `std::opt::ctrl` 改名 `std::opt::order`：新遍今天没有活干，删掉控制边约简整层变红

状态：待拍板（车道 `handover/h394-order-edges`，2026-10-24）
相关：`docs/design/effects/edges.cn.md:11`、`:49`（第 5 节第 4 条）、`:51`（4.6）、
`docs/technical/18_sync_control_semantics.md`、`docs/technical/45_graph_module.md:74,85`

## 问题

负责人裁决的第 1 项是「`std::opt::ctrl` 改名 `std::opt::order`，只做顺序边的传递约简
（A→B→C 已在时删去 A→C），别名、注册、参考文档里所有旧名一起改，旧名退役」。
两条实测说明它今天**不能单独落地**：新遍没有输入，旧遍的活没有别的遍接手。

## 实测一：顺序边今天没有任何生产者

```
$ grep -rn appendOrderDependency src/ --include=*.cpp
src/runtime/draft.cpp        （定义，以及本车道 item 3 新加的两处调用）
src/passes/test/edges.cpp    （测试遍）
```

`LinkType::Order` 在 `include/camel/compile/gir/types.h:24-35` 已经定义，
`src/compile/gir/builder_support.h:47-61` 也把它映射成 `DraftEdgeKind::Order`，
但**建图器不连它**：`docs/design/effects/edges.cn.md:11` 给顺序边列了三个来源
（同一资源上的效应之间、同一 `var` 的写与写之间、内联展开的同步链），
今天这三处产生的都是控制边（`LinkType::Ctrl`）。所以 `std::opt::order` 对今天
用户能写的任何程序都是空转——它可以实现、可以用 `test::edges_order_*`
这套测试遍驱动，但它不改变任何真实程序，而同时被删掉的控制边约简仍然有活干。

## 实测二：删掉控制边约简，`opt.control_data.graph` 变红

同一窗口、同一构建、同一命令（`test/plans/feat/opt/control_data.plan.toml` 的
`control_data.graph`，断言 `main` 的 `controls.length <= 10`）：

```
$ camel test/cases/opt/generic/control_data_paths.cml std::macro std::opt std::jgir
main controls=8  nodes=24
$ camel test/cases/opt/generic/control_data_paths.cml \
        std::macro std::opt::devirtualize std::inline std::opt::fold std::opt::cse std::opt::dce std::jgir
main controls=12 nodes=29
```

第二条正是"默认 `std::opt` 的内容，但不做最后那次控制边约简"。
`docs/design/effects/edges.cn.md:49` 给的依据是"纯计算上不再有任何非数据边，
所以这个遍的工作量大幅缩小"——那个前提由第 2 项提供，而第 2 项现在也落不了
（见 `pure-computation-off-the-sync-chain.md`：整层从 `fail: 0` 变 `fail: 88`）。

## 选项与代价

**A. 照 `edges.cn.md:49` 落地：`std::opt::order` 只做顺序边传递约简，控制边约简删除。**
代价：必须等两个前提——(1) 顺序边先有生产者（建图器把效应之间、同一 `var` 的写与写之间的
控制边改成顺序边/同步边），(2) 第 2 项落地让控制边计数自己降到阈值以下。
在两者到位前落地，`opt.control_data.graph` 直接红，而新遍空转。

**B. 分批退役：`std::opt::ctrl` 改名 `std::opt::order` 并实现顺序边约简；控制边约简原样保留，
用一个诚实的新名（如 `std::opt::control`）继续由 `simplifyGraph` 和默认管线调用，注明待删。**
代价：整层可以保持绿，`std::opt::ctrl` 这个名字退掉；但设计里没有这个中间名，是过渡态，
而且新遍仍然空转（实测一）。属于"新旧两套边并存"的过渡，需要一句写在文档里的期限。

**C. item 1 与 item 2 一起留给 H242 整批。**
代价 0 风险；本轮不交付改名、不交付新遍，只交付 item 3（删节点/替换时顺序边与同步边的转交）
和 `std::jgir` 画顺序边与同步边（已合入 `3118694a0`）。

## 建议

建议 **C**：一个对今天所有程序都空转的遍，加上一个必须靠别的遍接手才不红的删除，
合起来不是"一条车道一个目的"能收口的改动；把它和"效应之间的边从控制边换成顺序边/同步边"
以及第 2 项放在同一批里，落地当天就能看到 `std::opt::order` 真的在删东西。

## 依据

- `docs/design/effects/edges.cn.md:11`（顺序边的三个来源）、`:49`（第 5 节第 4 条）、
  `:51`（4.6：任何纯节点上没有顺序边；顺序边两端都是效应）。
- `docs/technical/18_sync_control_semantics.md:261,263,271,386`、
  `docs/technical/45_graph_module.md:74,85`：今天 `std::opt::ctrl` 的语义与预算（250000）。
- 旧名今天出现的位置（改名时要一起动的清单）：
  `src/execute/pass.cpp:561`（`registerCoreKernel("opt::ctrl", ...)`）、
  `src/passes/opt/generic/generic.cpp:903,925`（`ControlReductionPass` 的标签与
  `simplifyGraph` 的收尾约简）、`stdlib/std.cml:309-310`（导出声明）、
  `include/camel/passes/opt/generic.h`、`test/plans/feat/opt/control_data.plan.toml:52,144,156`、
  `docs/cli.cn.md:178`、`docs/cli.md:268`、`docs/technical/16_inline_rewrite_algorithm.md:53`、
  `docs/technical/18_sync_control_semantics.md`、`docs/technical/45_graph_module.md`、
  `src/compile/gir/builder_helpers.h:436`。
- 原始日志：`D:\Projects\Camel-wt\notes\h394-baseline.txt`（改前整层 `fail: 0`）。