# 暂停中的分支撞上「落后 100」这条闸门，闸门该不该对它们生效

## 现在的读数

```text
$ git -C D:\Projects\Camel rev-list --count --no-merges feat/jvp-closure-captures..origin/develop
103
$ git -C D:\Projects\Camel rev-list --count --no-merges lane/jvp-closure..origin/develop
81
```

```text
U=3  maxBehind=103
  feat/jvp-closure-captures  ahead=15  behind=103
  lane/driver-fix            ahead=1   behind=7
  lane/jvp-closure           ahead=18  behind=81
```

## 冲突在哪

`SKILL §0` 的闸门（2026-11-14 用户定）写的是：

> `U>20` 或 `maxBehind>100` ⇒ 不是「清理分支」，而是立刻停派新分支、转入审计合并。

而这两条分支**被作者命令暂停**：

> 暂停第二序（`jvp-closure`）。

所以闸门的处置「逐条 `git merge origin/develop` 进落后分支、解冲突、小跑、合、推」在这个情形下
是**要求我合并两条被明令暂停的分支**。两条命令互相否定。

另一个事实：这两条分支**没有人动它们**。`behind` 每过一小时就涨，
所以这个数会自己越过任何阈值，与「有没有人处理」无关。也就是说，
闸门量的是「落后多少」，而不是「落后是否被处理」。

## 默认建议（按此当轮开工）

**闸门对「暂停中的分支」不生效，但要把它量到另一个数上：**

1. `maxBehind` 只统计**未暂停**的分支。暂停的分支单列一行 `paused behind=…`，
   不参与阈值判断、也不触发「转入审计合并」。
2. 暂停分支另外记一个数：**它落后到多少时，恢复的成本会高于重做**。
   那个数到了一律报给作者，由他决定恢复还是丢弃，不由车道动手。
3. 若作者不同意区分，备选是**取消暂停**，让这两条分支走闸门原本的流程
   （先 `merge origin/develop`，解冲突，小跑，合或写出「哪一笔被什么取代」）。

**我按第 1、2 条开工**：本轮不派新分支（这一条与闸门一致），
但不去合并那两条暂停分支；恢复与否等作者裁定。