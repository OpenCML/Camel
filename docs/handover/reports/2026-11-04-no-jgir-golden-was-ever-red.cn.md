# 那条「重录 `std::jgir` 的 golden」的待办是虚构的——没有任何一份 golden 红过

**这份报告的存在理由**：一条待办被传下来、被执行、然后被忘掉，比一条待办被证明不存在更贵。今天这一笔付的是前者的账：有人已经准备用 `--update-golden` 去「修」一份**从来没有不匹配过**的 golden，而重录是唯一一种**必然通过**的动作——它会把「没有差异」变成一次不可复核的写入。

## 一、待办原话与它的出身

> 重建 `std::jgir` 的 golden，并在提交正文里写清「变的不是数」。

它唯一的出处是 `docs/handover/questions/2026-11-04-does-the-library-graphs-node-numbering-drift-with-a-laws-declaration.cn.md` 第 3 节「默认（照此做）」的第 1 条：「重建基线，并在提交正文写清『这不是数值变化，是四条律各少一个参数端口带来的节点下标平移』」。

**为它背书的读数：没有。** 那一页引的两个数族（`"origin": 37871 → 37875`、`"line": 868 → 872`，节点数两侧都是 120）来自**一次手工 jgir dump 对比**，不是任何一次计划运行；「节点数相等」也只是那次手工对比里的计数。也就是说：这条待办从一页**问句**的默认建议，直接变成了施工指令，中间没有任何一次红色计划为它背书。

## 二、证伪的三条

1. **B3 两笔没有碰过任何 golden 文件。** `git show --stat bb432e8ae` 与 `821f30c83` 的文件清单里没有一个 `golden/` 路径；`bb432e8ae` 在 `test/` 下只动过 `test/plans/feat/compile/transforms.plan.toml`（文字与 pin），`821f30c83` 一条 `test/` 路径都没有。**一次改动不可能让一个它没写过的文件变红**（除非是行为变化——那正是需要报告而不是重录的东西）。
2. **全树只有两份 jgir golden。** `git grep -l '"format": "camel.gir"'` 只命中 `test/plans/feat/opt/golden/algebra_rules.divergence.cpp.jgir`（724 行）与 `test/plans/feat/opt/golden/camel_passes.fold_fault.jgir`（263 行）。`std::jgir` 每次都印 `"format": "camel.gir"`，所以任何从它录下来的 golden 都逃不过这条 grep——**「有一份」这个前提可数的候选只有这两个**。
3. **这两份在 `ca464bca2` 上本来就 PASS**（`algebra_rules.divergence.by_cpp`、`camel_passes.fold_fault.by_cpp`）。**这一条是逻辑证明，不是测量结果**：PASS 的定义就是「规范化后与实际输出逐字节相等」，所以两者的差异**恒等于 0**，不存在「数值类」与「非数值类」之分——分类表五格全零，因为根本没有待分类的行。

## 三、B3 在场时的对照

在**回退之前**的树 `ac4f95f1f`（`bb432e8ae`、`821f30c83` 都在里面）跑同一组四张计划，读数与 `ca464bca2` 逐格相同：**`fail = 0`**。连 B3 在树里的时候，这两份 golden 也没有红过一格。所以「B3 之后有一份 golden 需要重录」这件事，**在 B3 当时就不成立**，不是回退之后才不成立。

| 树 | HEAD | 计划 | `[SUMMARY]` | golden |
| --- | --- | --- | --- | --- |
| develop | `ca464bca2` | 4 张（`opt/{algebra_rules,camel_passes}`、`trans/jgir`、`compile/transforms`） | total 243, pass 230, **fail 0**, known failures 13 | 两份全 PASS |
| 回退前 | `ac4f95f1f`（B3 在树里） | 同上 4 张 | total 243, pass 230, **fail 0**, known failures 13 | 两份全 PASS |
| develop | `ca464bca2` | 其余 12 张调用 `std::jgir` 的计划 | total 766, pass 766, **fail 0** | — |

（`internal/tools/camel-remote.ps1`，`-Tier fast -Jobs 8`，killed 0；调用 `std::jgir` 的计划共 16 张，1009 条，`fail = 0`。）

## 四、判词

**一条待办若没有任何读数为它背书，它就不是欠账，是传闻。**

## 五、给下一个要重录 golden 的人

**先问一句：它在当前树上是不是已经 PASS？** PASS 就意味着**没有差异可解释**，重录只会把「没有差异」变成一次不可复核的写入——而写入之后，再没有东西能区分「本来就是零差异」和「有人把差异抹平了」。重录只在一种情况下是对的：差异已经逐条分类完、且没有一条属于「数值」，那时重录是**记录一个已解释的结果**；若一条差异都拿不出来，重录就只是在**制造一个不会有第二批读者的事实**。