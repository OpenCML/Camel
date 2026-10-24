# BLOCK 已被设计线取消：本分支的代码实现的是一个被替换掉的设计

这是一处「文档压过代码」的冲突记录（AGENTS.md：设计原则优先；与代码冲突时记为缺陷或待办）。
不是给作者的提问，是给下一条施工车道的交接。

## 事实

- **本分支（`handover/h408-splice-rest`）实现的模型**：一条调用语句被内联时，把那个调用节点改成
  `GCNodeKind::Block`，它拥有函数体的区域，它的值就是该区域的返回值；读值、等值、建图都「读穿」它
  （`include/camel/runtime/graph.h#forwardsValue`）。本车道烧掉的 11 项红、以及按它重定的图钉
  （`algebra.plan.toml` 的 `integrated.graph.*` 与 `order.graph`、`gate_value.plan.toml` 的
  `other_wait.graph`、debugger 的节点断点）都以这条规则为前提。
- **develop（`34fa7c517`）取消了它**：`docs/design/effects/edges.cn.md` 5.7「内联与同步链：**不需要
  `BLOCK`**」（决策者 2026-10-24：函数体直接拼进调用者，从前一条语句向体内每个效应节点与可能出故障的节点
  连顺序边，再把它们连向后一条语句）。同一批裁定还删掉 `SYNC` 节点、把 `GATE` 变成没有控制输入的纯传值
  节点、把 `std::opt::ctrl` 改名 `std::opt::order`。删掉 BLOCK 的提交 `79d66e372` 同时删除了本分支注释
  与旧图钉引用的提问文件 `docs/handover/questions/block-node-on-the-sync-chain.md`。
- **代码还没跟上**：`origin/develop` 的 `include/camel/runtime/graph.h` 仍有 `GCNodeKind::Sync` 与
  sync/order 边数组，没有 `Block`；合并树（本分支 `829cf6314` = develop + 本分支的 `Block = 16`）两者
  都有。也就是「判词已下、施工未开始」。

## 因此

1. 本分支按 BLOCK 落地的读穿规则、`std::opt::dce` 的「没人读也没人等的 BLOCK」规则、以及上述图钉，
   **只在当前代码上成立**。有人按 5.7 实现「不加 BLOCK」时，这些地方要一起改：
   `runtime::forwardsValue` 的 `Block ||` 分支、`src/passes/opt/generic/generic.cpp` 的
   `isDeadBlock`/`blockRegionsMayGo`、`src/passes/opt/generic/screen.cpp` 的 `isFilledValue` 与
   `mayRemoveDead` 的 `Block` 分支，以及三处图钉。
2. 本车道**不实现新模型**：超出「烧红」的范围，且需要一次独立的施工车道（`SYNC` 的删除、
   `GATE` 去掉控制输入、`std::opt::ctrl` 改名都会动到全层的图钉）。合并树上 fast 层的红与 BLOCK 无关。
3. 本分支注释里对「`edges.cn.md` 5.7」的引用**已被 develop 改写成相反的判词**。本车道只把自己新写的
   注释与用例描述改指本文件（h400 那一批既有注释不在本车道范围内，未改）。

## 来源

- 判词：`docs/design/effects/edges.cn.md` 5.7（决策者 2026-10-24）、提交 `79d66e372
  docs: nine node kinds, BIND as context binding and closures, one branch node, copies left to memory
  planning, and no BLOCK`。
- 并入的合并提交：`829cf6314`；冲突文件 `include/camel/runtime/draft.h`（两边各加了一批声明，按内容两边都留）。
- 代码：`include/camel/runtime/graph.h`（`GCNodeKind::Sync`、`GCNodeKind::Block`、`forwardsValue`）、
  `src/passes/opt/generic/generic.cpp`、`src/passes/opt/generic/screen.cpp`。