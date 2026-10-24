# 「函数多大」数不数内联留下的 BLOCK 与拼接口的 FILL

## 问题

`test/cases/modules/onnx/export_loop.cml` 的 `pass_export_loop` 在 `onnx::export` 处被拒：

```
Translation pass 'onnx::export_model' refused the graph: the entry graph still contains a call to
'main' that was not inlined ... inlined only while it is small (at most 8 nodes besides constants
and parameters; this one has 12)   (export_loop.cml:27:1)
```

阈值在 `src/passes/opt/inline/engine.cpp#isSmallRuntimeSubgraphForInline`（:65）：数「除 `Data` 与
`Port` 以外的节点」，超过 `smallSubgraphMaxNonDataPortNodes`（默认 8）即拒绝；同文件
`#inlinedNodeCount`（:88）用同一条度量，`onnx::export` 的消息里那个数就是它。

## 实测（宿主 88zjwei，本分支 `h408-splice-rest`）

`main` 在 `std::macro std::opt` 之后：

| | 节点数 | 非 Data/Port | 组成 |
|---|---|---|---|
| BLOCK 落地前（`h403-parsecheck-crash`，`b5bc264e3`） | 16 | **8** | OPER 5、DATA 8、GATE 1、FUNC 2 |
| 本分支 | 21 | **12** | BLOCK 3、OPER 5、DATA 9、GATE 1、FUNC 2、FILL 1 |

也就是说：**8 + 3 个 BLOCK + 1 个 FILL = 12**，8 不 > 8 ⇒ 当时内联并导出成功；12 > 8 ⇒ 今天拒绝。
差别全部来自 splice/inline 的边界节点，函数的运算一字未变。

三个 BLOCK 是三个内联语句的标记（`g1n0` 拥有 `main` 自己那层区域 `g1r1`，`g1n12` 拥有
`iterate$spec4$spec10` 的区域 `g1r2`，`g1n24` 拥有 `iterate` 的区域 `g1r3`）；那个 FILL
（`g1n14`，`(Tensor, Tensor)`）是把区域的结果拼成元组交出去的节点，读 `(null, null)` 模板、一个函数值和
`BLOCK g1n12`。

**注意：只把 BLOCK 从计数里去掉仍然不够**（12 − 3 = 9 > 8），所以这一项不是「BLOCK 算不算节点」一条就能
修好的：要修就得同时定 FILL 的归属，或者把阈值本身的意义重新说清楚。本车道因此**不动它**，报作者裁。

## 判词需要回答的

1. 「函数多大」数不数**边界节点**（`BLOCK`：它拥有的区域里的节点已经被数进去了；`FILL`：把区域结果拼成
   元组交出去的那个节点）？今天两者都数。
2. 若数：同一个函数在「BLOCK 落地前/后」是「小」与「不小」两个答案，`std::opt::inline` 的 `Small` 策略
   与 `onnx::export` 的「内联到小为止」因此对**所有含 BLOCK 的程序**都收紧了（不只是这一项）。
3. 消息里的措辞「at most 8 nodes besides constants and parameters」要不要跟着改（BLOCK/FILL 既不是常量
   也不是参数）。

## 选项与代价

- **(a) 阈值与消息都按「不数边界节点」重定**：`isSmallRuntimeSubgraphForInline` 跳过 `GCNodeKind::Block`
  与拼接口的 `FILL`，`inlinedNodeCount` 仍数真实拷贝的节点数（它回答的是「拷贝多大」，另一个问题）。
  代价：`Small` 策略对所有含 BLOCK 的函数放宽 ⇒ 全层 `.graph` 形状钉要重新测量并逐条核对（fast 层全量
  一次约 5 分钟 + 改若干图钉），且这是「谁算小」的语义判断。**只有 (a) 能修好这一项**（9 > 8）。
- **(b) 改用例**（把 `export_loop.cml` 缩到阈值以内）不取：用例写的是「小函数 + 循环 + 导出并成功」这条
  路径，缩它是为了绿而改用例。
- **(c) 保持红记档**：`pass_export_loop` 长期红，且 `onnx::export` 对「内联过若干语句」的程序直接拒绝
  （能力缺失，不只是测试红）。本车道现状即 (c)。

## 建议

请作者裁 (1)。若判 (a)，本车道可按一个提交落地并按全层重测给出新红集合；若不判，按 (c) 记档。

## 来源

- 现场：`test/plans/feat/modules/onnx.plan.toml#pass_export_loop`（:1581）、
  `#pass_export_refusal_located.main_reaches_loop`（:2383，钉住 17；该图的 `main` 没有 BLOCK，
  两项因此互为对照）。
- 代码：`src/passes/opt/inline/engine.cpp#isSmallRuntimeSubgraphForInline`（:65）、
  `#inlinedNodeCount`（:88）、`include/camel/passes/opt/config.h#InlineRewriteConfig`；
  `onnx::export` 的拒绝消息在 `src/passes/trans/onnx/`。
- 规格：`docs/design/effects/edges.cn.md` 5.7；`docs/handover/questions/block-node-on-the-sync-chain.md`
  及其裁定。