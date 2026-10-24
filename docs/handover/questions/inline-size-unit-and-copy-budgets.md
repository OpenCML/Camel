# 内联大小改按"计算节点"计之后，另外三个节点数是否也跟着换单位

决策者 2026-10-24 傍晚的裁定（[questions-2026-10-23.md](../reviews/questions-2026-10-23.md)
§`block-model-superseded`，2026-10-24 傍晚一批，第 567 行）说：**"函数多小"量的是被内联的那个函数
自己的计算节点**（`OPER`、调用、结构节点），不数常量、形参、`GATE` 以及内联拼接产生的记账节点；
消息措辞随之改为"被内联的函数有 N 个计算节点"。

车道 `handover/h434-inline-size` 已按此落地（阈值仍是 8）：

- `InlineRewriteConfig::smallSubgraphMaxComputeNodes`（`include/camel/passes/opt/config.h:41`，由
  `smallSubgraphMaxNonDataPortNodes` 改名）由 `inlinedComputeNodeCount`
  （`src/passes/opt/inline/engine.cpp:92`）回答：按节点种类排掉 `DATA`/`PORT`/`GATE`/`SYNC`/`DEAD`，
  再排掉落在内联 region（`GCRegionKind::Inline`）里的节点——后半条是"不随拼接方式变"的必要条件，
  因为被拼进来的辅助函数自己的 `OPER` 落在被内联者的 region 里。
- `ReachesRecursion` 的消息改成 "the inlined function has N compute nodes (at most 8; a constant,
  a parameter, a gate and what an inline splice adds are not counted)"
  （`modules/onnx/passes/export.cpp:540`）。

## 没定住的一点

同一份 `InlineReport` 里还有三个"节点数"，它们服务的是**拷贝成本**，不是"函数多小"：
`InlineDeclined::calleeNodes`（内联会复制的节点 = 除 `DATA`/`PORT` 以外的全部）用于
`GrowthBudget`、`OwnerSize`、`Duplication` 三条消息和三处判定，`duplicationBudgetNodes = 256`
（`include/camel/passes/opt/config.h:44`）也是按它算 `(callSites - 1) x 复制节点数`。
裁定只说了"函数多小"和那一条消息，没有说这几个数是否也应改成计算节点。

我的处理（默认 (a)）：**"函数多小"换单位，预算不换**——预算是复制代价，常量与形参本来就不占复制
成本，`Duplication` 的消息继续报"too large (N nodes) to be copied to each"（N 为复制节点数）。

## 选项

- **(a) 保持现状（推荐）**：一处单位是"函数多小"（计算节点），一处是"复制成本"（节点数）。
  代价：同一份报告里有两种单位，读者要看清是哪一条消息；好处：三个预算的语义与数值都不动，
  没有 golden 重录。
- **(b) 全部换成计算节点**：四条消息、`duplicationBudgetNodes` 与增长/所有者预算一起改。
  代价：`duplicationBudgetNodes = 256` 的语义随之改变（常量与形参不再计入复制成本，同样大小的
  程序会复制得更多），且要重录 onnx 与 exemplars 的 golden；好处：报告里只有一种数字口径。

## 建议

**(a)**。理由：裁定的措辞是"消息措辞随之改为……"针对"内联大小"那一条；把复制预算也换成计算节点
会让"允许复制多少"取决于一种与复制量无关的量，是另一件事，应当单独立规则。

## 出处

- `docs/handover/reviews/questions-2026-10-23.md` §`block-model-superseded`（裁定原文，第 567 行）
- 车道 `handover/h434-inline-size` 的红前实测：同一个 `main`，`std::opt` 报 11、`std::opt::fold
  std::opt::devirtualize std::opt::inline` 报 15（差的是拼进来的 2 个 `OPER` 与 3 个形参 `GATE`）；
  改后两条流水线都报 9。