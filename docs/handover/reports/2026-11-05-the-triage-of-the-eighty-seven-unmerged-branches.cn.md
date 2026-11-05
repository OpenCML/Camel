# 八十七条未合分支的分诊现状

本页取代 `2026-11-05-what-the-eighty-seven-unmerged-lanes-still-hold.md` 里的 A/B/C/D 计数，因为它写于 C 类清空、保留清单核对、裁定 `79b276925` 之前。**分类依据全部是当场测量，不是沿用旧表。**

## 总数与口径

```
unmerged = 87   （= 本地分支总数 104 − 已合入 origin/develop 的 17）
```

「已合入」的判据是 `git branch --merged origin/develop`，**不是 `git cherry`**。今天已经付过两次代价：`cherry` 的判据是补丁等价，`-d` 的判据是祖先可达，是两个不同的尺子；`lane-reverse`、`lane-typelaw`、`lane-zeroplan` 三条都是 `cherry` 全 `-` 而内容确实已在 `develop`。

## 七类

| 类 | 条数 | 依据 | 处置 |
|---|---|---|---|
| 永久保留 | **5 + `release`** | 保留清单六条；`release` 按作者 `49195ebe6`「leave `hold/fsh-dev` and `release` to the owner」 | 永不删、永不合并 |
| 探针 | **4** | `probe/*` 四条：`probe/bisect-forward`、`probe/prevconv-array-conversion`、`probe/revert-dry`、`probe/shuffle` | 保留，理由记录在案 |
| 受裁定第 4 条约束 | **7** | 与已经进 `develop` 的那六笔改写同处四个文件 | 归 B 类待查，不进任何批 |
| 纯文档，已编组 | **14** | 文件集两两不相交（`overlaps=0`，代码核过） | Batch 1 闸门在跑，Batch 2 已合，Batch 3 待核 |
| 本轮新造 | **5** | `lane-mtcheck`、`lane-arrpat`、`lane-r3export`、`lane-r1comments`、`pending-docs-defaults` | 各自按自身判据处置 |
| **未分类** | **52** | 见下 | **本轮未动，是剩余工作的全部** |

## 那五条保留分支

| 分支 | tip | 备注 |
|---|---|---|
| `hold/fsh-dev` | `83d000e60` | 作者的线，未动 |
| `parked-shapeext` | `684364eab` | **今晚发现不在，按 `archive/parked-shapeext` 重建** |
| `parked-errwrite` | `c53ef5820` | 未动 |
| `lane-lifcall` | `4f001bc19` | 未动 |
| `statetypes-joint` | `248037260` | **今晚发现不在且无 tag，按 reflog 重建；带 5 文件 +143/−60 未合并提交** |
| `parked-arrpat` | `903571b81` | 未动 |

**`release` 要加进这一组**：保留清单六条里没有它，但作者在 `49195ebe6` 里写了「leave `hold/fsh-dev` and `release` to the owner」。**「清单没列」不等于「可以删」——这正是今晚丢了两条分支的同一类失误：拿清单代替核对。**

## 那七条受裁定第 4 条约束的

`lane-chainplace`、`lane-eff-rule`、`lane-fold2`、`lane-gs6`、`lane-marks`、`lane-tidy`、`probe-mark-placefork`。

它们的共同点是碰了那六笔改写动过的四个文件（`include/camel/runtime/draft.h`、`src/passes/opt/generic/generic.cpp`、`src/runtime/draft.cpp`、`src/runtime/draft_inline.cpp`）。**摘出来的理由不是「它们写着 bad」，是「与已进 `develop` 的那批同处一份文件，若那批还有错，合进来会把错埋在两笔交界处，二分找不出来」。**

**而那六笔改写本身已经是 `develop` 的祖先**——裁定第 4 条按字面执行不了，已开问题页并写下默认（不回滚，但它们的后果不许再被 `known_fail` 兜住）。

## 那五十二条未分类的

按名字看大致是三组，**但这只是分组，不是判定**：返工续做（`lane-b2`、`lane-b3redo2`、`lane-b3redo3`、`lane-b5ext`、`lane-m5`、`lane-rc00`、`lane-rc04`、`lane-loopfix`、`lane-iffix`、`lane-edges`、`lane-edges-green`、`lane-defun4`）、工具与文档（`lane-bisect`、`lane-bisect-script`、`lane-tiergap`、`lane-docdrift`、`lane-c3-merged`、`lane-gaterepo`、`lane-garbage`、`lane-gcfreed`、`probe-b3full`、`probe-b3off`、`probe-joint`）、以及一批已合或已被取代但名字还在的（`merge-train-2`、`merge-train-20261104`）。

**每一条都要按「它有没有别处没有的内容」逐条量，判据与今晚清理 C 类时用的一样**：`git grep` 关键标识符（不是文件名）+ 逐行集合差（不用 `Compare-Object` 的箭头判方向）。**一条都不能凭名字判。**

## 两条纪律（都付过代价）

1. **删任何分支前先 `git tag archive/<name> <sha>`。** 今晚 `statetypes-joint` 漏了这一步，唯一残存的指针是工作树 reflog，再晚一次 `gc --prune` 就不可恢复。
2. **保留清单要用 `rev-parse` 核 tip，不能只核名字。** 名字存在但解析不出东西，与名字不存在在 `branch --list` 里看起来一样。