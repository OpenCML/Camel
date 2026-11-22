# 13 条 `TILES_OUTLINED` 红：期望 10，而机制给出 12 或 5，且树里已经自相矛盾

日期：2026-11-19 ｜ 来自 `lane-wide`（`30e58c866`）｜ 对象：`gpu.tiles_regions` 的 13 条红 ｜ 状态：**期望值保留不动，待作者裁**

这一页取代同目录 `2026-11-19-the-thirteen-expect-ten-and-the-mechanism-says-twelve.cn.md` 的第 ① 节：
那一页据一次 `std::jgir` 取证说「gpu 遍 12 张、host 遍 5 张」，方向对，但它把 13 条当成**一个**形状。
今天把 13 条真跑起来，它们**不是一个形状**，而且**树里已经对同一遍同一用例记了两个数**。

（代码按符号引用：`模块/文件#符号`。本页所有读数在 `30e58c866` 上量得。）

## 事实

### 1. 13 条红按 `got` 分两组：12 与 5，不是一组

`outlined_execution.{nvm,fvm,jit,prl}`（4 条）实际 **12**：

```
expect_stdout_exact differs: first difference at offset 16 (expected 50 chars, got 50 chars):
expected "TILES_OUTLINED 10\n364.0\n84.0\n42.0 364.0\n21.0\n84.0\n",
got      "TILES_OUTLINED 12\n364.0\n84.0\n42.0 364.0\n21.0\n84.0\n"
```

`outlined_resident_execution.{nvm,fvm,jit,prl}`、`preferred_resident_execution.{nvm,fvm,jit,prl}`（8 条）实际 **5**：

```
expect_stdout_exact differs: first difference at offset 15 (expected 50 chars, got 49 chars):
expected "TILES_OUTLINED 10\n364.0\n84.0\n42.0 364.0\n21.0\n84.0\n",
got      "TILES_OUTLINED 5\n364.0\n84.0\n42.0 364.0\n21.0\n84.0\n"
```

`resident_graph_transfers`（1 条，`test/cases/modules/tiles/resident_graph.py` 的 `prefix` 断言）实际 **5**：

```
assert prefix == "TILES_OUTLINED 10", prefix
AssertionError: TILES_OUTLINED 5
```

读数怎么来的：

```
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan gpu/tiles_regions -Jobs 8 -NoBuild
→ [SUMMARY] total: 30, pass: 17, fail: 13
```

**这条读数是抖的**：主机八张卡被租户占着时，计划头的 `cuda_free_device = true` 让整层
`total: 30, pass: 0, fail: 0, skip: 30`（同一天里两种读数都撞到过）。要拿这一层读数必须撞上空卡窗口，
**而 `skip` 不是 `pass`**（`internal/tools/gpu-plans.exceptions.txt` 里「A SKIP IS NOT A PASS」那一段）。

### 2. `modules/tiles/src/regions.cpp#partitionRegions` 不决定这个数

它只做一件事：把 `analyzeRegions` 标成 `eligible` 的节点按「同 control region × 商图无环」尽量合并。
它**没有自己的数**——给它 4 个 eligible 它就切 4 张，给它 12 个它就切 12 张。
所以「为什么不是 10」问不到它身上。

### 3. 分岔在 `modules/tiles/src/region_outline.cpp#outlineRegions` 里的 `analysisScopes`

`outlineRegions` 用 `GpuScopes analysisScopes(*program, outlinesForDevice)` 造一份分析用的作用域表，
再把它交给 `analyzeRegions`；后者用 `scopes->covers(graph, id)` 决定每个节点算不算设备节点
（`modules/tiles/src/graph_regions.cpp#analyzeRegions`），而这只决定 `eligible` ⇒ 决定张数。

而 `outlinesForDevice = true` **全仓只有一个遍设**：`tiles::test_outline_gpu`
（`modules/tiles/src/region_test_pass.cpp#registerRegionTestPasses`）。另两个遍
（`tiles::test_outline_resident`、`tiles::test_outline_preferred`）都不设它，
于是只看程序自己写的 `#gpu` 块，而 `test/cases/modules/tiles/region_execution.cml` **一个 `#gpu` 都没有**。

```
export CAMEL_HOME=$PWD/out/latest
export LD_LIBRARY_PATH=<clang-root>/lib:$CAMEL_HOME/libs:$CAMEL_HOME/lib
./out/latest/bin/camel test/cases/modules/tiles/region_execution.cml tiles::test_outline_gpu        std::jgir | head -1 → TILES_OUTLINED 12
./out/latest/bin/camel test/cases/modules/tiles/region_execution.cml tiles::test_outline_resident   std::jgir | head -1 → TILES_OUTLINED 5
./out/latest/bin/camel test/cases/modules/tiles/region_execution.cml tiles::test_outline_preferred  std::jgir | head -1 → TILES_OUTLINED 5
```

`std::jgir` 收尾是为了**打印完就返回空图、不执行**，所以这条读数**不需要空闲卡**——它要的是
「这台机器有卡」，不是「卡空着」。12 的构成是：5 张算术区域 + 6 张落在 `println` 副作用边界上的
单节点 `tensor:sum` + 1 张 `{add, add}`。

**⇒ 10 不属于这两种配置中的任何一种。**

### 4. 卡的可见性不参与这个数

同上三条命令加 `CUDA_VISIBLE_DEVICES=-1` 复跑，输出**逐字不变**（12 / 5 / 5）。
`modules/tiles/src/capability.cpp#deviceInUse` 只喂给
`modules/tiles/src/graph_regions.cpp#gpuRefusal` 的 `device_element_kernel_missing` 那一条拒绝，
与本用例的 f32 无关。⇒ 这个数不是「设备有没有跑起来」的读数，
**也不会因为今晚卡空出来就变成 10**。

### 5. 决定性旁证：树里对同一遍、同一用例记了两个数

`test/plans/feat/modules/tiles.plan.toml` 的 `preferred_resident_execution` 条目是**同一遍**
（`tiles::test_outline_preferred`）、**同一用例**（`region_execution.cml`），记的是 **5**，
而它今天**是绿的**：

```
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan feat/modules/tiles -Jobs 8 -NoBuild
→ [PASS] preferred_resident_execution.{nvm,fvm,jit,prl}
→ [SUMMARY] total: 109, pass: 103, fail: 0, skip: 6
```

而 `test/plans/gpu/tiles_regions.plan.toml` 的 `outlined_resident_execution` 与
`preferred_resident_execution` 两个条目记的是 **10**，红。
⇒ **同一个二进制、同一个 pass、同一个 case：记 5 的绿，记 10 的红。**
这不是「机制变了」，这是**两处记录互相矛盾**，而其中一处每天在跑、每天是绿的。

### 6. 例外表 13 行记的原因，今天一条都不成立

```
[camel-remote] gpu-plans: 15 red, 15 recorded, 0 new, 1 recorded for another reason, 0 recorded name(s) no longer red
[camel-remote] gpu-plans: RED FOR ANOTHER REASON: resident_graph_transfers was recorded for '0x040001B0',
                          red now for 'TILES-OUTLINED-5'
[camel-remote] gpu-plans: the failure printed neither a diagnostic code nor an assertion, so the recorded reason
                          could not be checked: outlined_execution.{nvm,fvm,jit,prl},
                          outlined_resident_execution.{nvm,fvm,jit,prl}, preferred_resident_execution.{nvm,fvm,jit,prl}
```

`internal/tools/gpu-plans.exceptions.txt` 里那 13 行 `0x040001B0`（`PassEffectBeyondType`）
**今天没有一条是红因**：今天的 13 条全是计数不符，一个诊断码都没打。
那一列记的是 2026-11-18 重读时的失败，之后已经换掉了。

## 问题

**`gpu/tiles_regions` 记的 10，是陈旧记录，还是机制本该给出 10 而实现退化了？**

- **若前者** ⇒ 期望应为 **5**（9 条可转绿：8 条 `...resident_execution.*` + `resident_graph_transfers`）。
  那么 `outlined_execution` 那 4 条的 **12** 要不要另立一条期望，还是「设备遍不该把 6 个
  `println` 归约各切一刀」这个设计问题先答？
- **若后者** ⇒ 退化在哪一处？事实 3 与事实 5 说明它不在 `partitionRegions`、不在 `analyzeRegions`
  的切分规则里（那套规则一个字没改），也不在卡的可见性里（事实 4）。

## 默认建议（按此执行，除非裁到）

**把 `gpu/tiles_regions.plan.toml` 那 9 条期望由 `10` 改为 `5`**（证据：`feat` 计划同遍同用例绿着 5，
事实 5）；**`outlined_execution` 那 4 条先不改**，它要的是「设备遍该不该把 6 个 `println` 归约各切一刀」
这个设计问题的答案，不是一条记录的修正；**例外表 13 行先不动**（等期望改完再一并收——改期望会让它们
变成「name no longer red」，那时删才是对的）。

## 附：为什么这一轮没有顺手改

1. **改 `10`→`5` 只救 9 条**，`outlined_execution` 那 4 条会变成「5 vs 12」继续红，
   而它问的是设计而不是记录。
2. **例外表 13 行今天已全部不是红因**，改它要动棘轮（`internal/tools/gpu-plans.exceptions.txt`
   里那两条编辑规矩）：一条名字只有在它记的原因就是今天的原因时，才算「已知红」。
3. **这一层是 report-only**，两次都 `exit 0`，不挡任何事 ⇒ 这件事可以等作者裁；
   而随手改一条记录会把「为什么是 5」这个还没人回答的问题埋掉。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

建议答：10 是旧记录。`feat` 计划里同遍、同用例记 5 而且每天绿，两处记录自相矛盾时以它为准。具体：9 条期望改成 5；`outlined_execution` 4 条把 12 钉成当前机制的输出，写明构成：5 张算术区域 + 6 个 `println` 归约 + 1 张 `{add, add}`；"设备遍要不要合并被 host 打印隔开的归约"是 Tiles 的优化设计，在 Tiles 设计里定，不靠用例期望值来回答；例外表 13 行等期望改完，按"原因已不成立"删除。develop 上仍是 10：`test/plans/gpu/tiles_regions.plan.toml:64/74/106`、`resident_graph.py:22`。
