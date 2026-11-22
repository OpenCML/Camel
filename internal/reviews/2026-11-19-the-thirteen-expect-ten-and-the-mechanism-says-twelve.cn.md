# 那 13 条期望的 `TILES_OUTLINED 10`，模型被实测推翻了

日期：2026-11-19 ｜ 来自 `lane-wide` ｜ 对象：`gpu.tiles_regions` 的 13 条红 ｜ 状态：**期望值保留不动，待作者裁**

## ① 被推翻的模型

原先的模型是「10 = 5 个逻辑区域各两份（host 副本 + device 副本）」。实测不是：

```
host 遍 5 张：{add,multiply} / {add} / {add(两入)} / {tuple: add,multiply} / {sum_axis}
gpu  遍 12 张 = 上面那 5 张 + 6 张「单节点 tensor:sum」+ 1 张「{add, add}」
```

那 6 张 `tensor:sum` 正是六个 `println(sum(...))` 的归约：它们的值要回到 host 给 `println`，
于是**每个都在 host 副作用边界上单独成了一张设备区域**；第 12 张是两个 `add`。
⇒ 这一套实现产出的是「在 host 副作用边界上被切碎」，**不是「每个区域两份」**。
（取证：`tiles::test_outline_gpu std::jgir`，按图名里的 `tiles.region/N` 分组，成员表即上。）

## ② 要问作者的一件事

**那 13 条记的 10，是从哪套机制记下来的？**

- 若「设备段 + host 副本」是**另一套**机制（按区域复制，而不是按副作用边界切），那 **10 是对的**，
  而**今天这套实现从来不是它** —— 缺的是那套复制，不是计数。
- 若那套机制已被取代、10 是旧记录，**那 10 本身要改** —— 而改一条记录的期望值要作者点头，
  不是执行者能顺手做的事（`internal/tools/gpu-plans.exceptions.txt` 的正文也把这一族记成
  「A FIX OWED, NOT A PERMISSION」）。

## ③ 默认（已写死）

**`10` 保留不动**；**这一族继续红**。这一笔提交（`d27b55d94`）只让 `tiles::test_outline_gpu`
第一次说清「我是设备遍」—— 判别式从 `run != nullptr` 换成遍自己的目标 `outlinesForDevice`，
其余五个遍的传参一个没动。**它没有修好这 13 条**，提交信息里就是这么写的。

## ④ 总体证据

- `feat/modules/tiles`：`total 109, pass 103, fail 0, skip 6`（相邻两次读数一致，skip 未动）。
- 计数：gpu 遍 **12**、host 遍 **5**；`nvm` 与 `fvm` 逐字一致（不是某个后端的读数）。
- 12 张的成员表见 ①；`analyzeRegions` 的切分规则一个字没改，期望值一个字没改。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

被 the-thirteen-expect-ten-and-the-mechanism-says-twelve-or-five 页取代，同页处理。建议答：10 是旧记录。`feat` 计划里同遍、同用例记 5 而且每天绿，两处记录自相矛盾时以它为准。具体：9 条期望改成 5；`outlined_execution` 4 条把 12 钉成当前机制的输出，写明构成：5 张算术区域 + 6 个 `println` 归约 + 1 张 `{add, add}`；"设备遍要不要合并被 host 打印隔开的归约"是 Tiles 的优化设计，在 Tiles 设计里定，不靠用例期望值来回答；例外表 13 行等期望改完，按"原因已不成立"删除。develop 上仍是 10：`test/plans/gpu/tiles_regions.plan.toml:64/74/106`、`resident_graph.py:22`。
