# 保留清单丢了两条分支，以及 reflog 找到的东西

本页记 2026-11-05 夜间对「永久保留、不得删除」那六条分支的核对结果。结论先说：**六条里有两条当时不在**，一条能靠 tag 恢复，另一条连 tag 都没有，只能从 reflog 里捞回来。

## 核对方法

```
git for-each-ref refs/heads --format='%(refname:short)'
```

逐条比对保留清单的六个名字。**用 `rev-parse` 取 tip 而不是只看名字存在**：名字在但指向空不算在。

## 结果

| 分支 | 当时状态 | 恢复方式 | tip |
|---|---|---|---|
| `hold/fsh-dev` | 在 | 未动 | `83d000e60` |
| `parked-shapeext` | **不在** | 从 `archive/parked-shapeext` 重建 | `684364eab` |
| `parked-errwrite` | 在 | 未动 | `c53ef5820` |
| `lane-lifcall` | 在 | 未动 | `4f001bc19` |
| `statetypes-joint` | **不在** | **从 reflog 重建（无 tag）** | `248037260` |
| `parked-arrpat` | 在 | 未动 | `903571b81` |

## `statetypes-joint` 这一条最值得记

它不只在本地分支表里没有，`git tag -l '*statetypes*'`、`git branch -a`、`ls-remote --heads origin` 三处都没有。**唯一还在的地方是工作树的 reflog：**

```
git reflog --all | Select-String statetypes
-> worktrees/statetypes-joint/HEAD@{2026-11-05}: commit: tensor: the axis reductions state their extents …
```

按那个 tip 恢复之后，它带着一笔**没进 develop 的实质提交**（5 文件，+143/−60）：

```
modules/tensor/module.cpp
modules/tensor/ops/reduction_ops.cpp
modules/tensor/ops/registry.cpp
stdlib/signatures/tensor.cml
stdlib/tensor.cml
```

**它比 `parked-shapeext` 严重，因为 `parked-shapeext` 有 `archive/parked-shapeext` 钉着同一个 SHA，而这条什么都没有。** 删除当时若再晚一次 `git gc --prune`，它就不可恢复了。

## 顺带查出来的事

用同一份 reflog 枚举所有出现过的工作树名，与现存分支、与 `archive/*` tag 各比对一次，得到「消失且无 tag」四个：`arrpat`、`libcompose`、`packeta`、`rollup`。逐个查之后四个都只是**工作树目录名**，真正的分支名带 `lane-` 前缀且在树上；`rollup` 是闸门在 Linux 主机上的构建树，不是分支。**没有第五个真丢失的。**

## 而 `lane-arrpat` 不是「判定作废」

清理时曾把 `lane-arrpat` 归入「C 判定作废、指不出取代笔」并保留。**保留是对的，但理由不是「证据不足」。** 它在 `a7f0adcf4` 上有两笔：

```
a7f0adcf4 ElementOf, WithElement and RowsOf are written in the library, not as C++ primitives
0597a7f11 match type takes an array type apart, and ShapeOf states the rank
```

**这正是清理车道从 `arrpat-logs` 里救回的那页所描述的裁定**（`ShapeOf<T>` 写成 `match type`、包长度用值层拆包、三个原语从 C++ 移进库）。也就是说：**裁定已经实现成代码了，缺的只是把它合进 `develop`；而那页文档之所以「在 docs 里查不到」，是因为它描述的工作还没落地。**

**所以 `lane-arrpat` 属于「可交付」，且是排序最靠前的一条**——它实现的是作者已经裁定的东西，`docs/design-principles.cn.md` 那段（同样从 `arrpat-logs` 救回）也是同一条裁定的规范表述。两边一起进，规范与实现才不脱节。

## 两条纪律

1. **删任何分支之前先 `git tag archive/<name> <sha>`**。这次的教训不是「别删」，是「tag 只在`-D`之前打才有意义」——`statetypes-joint` 就是漏了这一步。
2. **`git worktree remove` 不等于分支没了，但分支没了会跟着工作树名的 reflog 一起消失。** reflog 不是备份，它会被 `gc` 清掉。**保留清单上的名字，每次清理都要用 `rev-parse` 核一遍 tip，不能只核名字。**