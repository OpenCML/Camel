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

## 第一层量出来的结构

对那五十二条逐条做 `git merge-tree --write-tree origin/develop <分支>`，**直接问「合进来树会不会变」**，而不是比较补丁、比较文件集、或者比较 blob 相等：

```
与 develop 无任何差异（merge-tree 结果树 = develop 的树）   1 条  lane-knownau
合进来会改变树、且无冲突                                    15 条
合进来与 develop 冲突                                       36 条  （含 release，属保留分支，不计）
```

**这个判据是今晚唯一一个没被误导过的**，因为它根本不做比较：它做一次真的合并，然后看结果树。`git cherry` 比的是补丁等价，`git diff --name-only` 比的是文件集，两者都答不了「这条分支有没有带来 `develop` 没有的东西」。

**而它给出一个与预期相反的结果**：**没有一条是「内容已在 `develop`」的空壳分支。** 五十二条里除 `lane-knownau` 之外，**每一条合进来都会改变 `develop` 的树**──所以「未合分支」这个说法在数量上是准确的，不是账页夸大的。

### `lane-knownau` 不能关，而三条判据都说它能关

| 判据 | 读数 |
|---|---|
| `git rev-list --count origin/develop..lane-knownau` | 1 |
| `git diff --name-only origin/develop...lane-knownau` | 0 个文件 |
| `git cherry origin/develop lane-knownau` | 0 行（连 `-` 都没有） |
| `git merge-tree --write-tree` 的结果树 | 与 `origin/develop` 的树完全相同 |

四条都指向「它不带来任何东西」。**但 `git log -1 --format='%p'` 给出两个父提交 `7736cbf3c 52e969f83`，而它自己的说明是 `test merge: measure the pins lane-oneword moves (not for push)`。**

**它是一条双亲合并提交，并且自报 `not for push`。** 清理协议里那条「`cherry` 无输出但 `ahead=1` 的是 merge 提交型，一律只列不删」正是为这件事写的。

**两条判据问的不是一个问题**：四条读数问的是「它带来什么净内容」，协议问的是「这个提交是什么东西」。**在 `ahead=1` 这个数上，两者完全一样，而处置相反。** 所以本页把它归入探针保留，理由写「test merge、自报 not for push」，不予关闭。

## 两条纪律（都付过代价）

1. **删任何分支前先 `git tag archive/<name> <sha>`。** 今晚 `statetypes-joint` 漏了这一步，唯一残存的指针是工作树 reflog，再晚一次 `gc --prune` 就不可恢复。
2. **保留清单要用 `rev-parse` 核 tip，不能只核名字。** 名字存在但解析不出东西，与名字不存在在 `branch --list` 里看起来一样。