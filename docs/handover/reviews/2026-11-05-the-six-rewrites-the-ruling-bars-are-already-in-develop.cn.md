# 这六笔改写裁定说不进 develop，但它们已经在里面了

## 问题

裁定 `79b276925`（「拿掉一处拒绝之前」）第 4 条说：

> 那批改写（`3bdd5ee47`、`976890ced`、`4aa3f8c71`、`039be853d`、`1e1c1dbdb`、`8c48dbe37`）在修好之前不进 develop，退回 B 类。

对 `origin/develop` 逐个做 `git merge-base --is-ancestor`，六条全部返回 0，即**六条都是 `develop` 的祖先**：

```
3bdd5ee47 runtime: splice a chain's place in one operation, with the GATE rule inside it
976890ced opt: hand a branch's chain place to a statement, not to the arm's value
4aa3f8c71 graph: a chain place is handed to a statement, and the two draft writers say so themselves
039be853d graph: the chain question is a member of the draft, so the ledger keeps its place
1e1c1dbdb draft: a chain place is taken because the node is a statement, not because a mark said so
8c48dbe37 inline: a place fork is resolved between statements, and a statement is the node's own answer
```

所以「在修好之前不进 develop」这条按字面无法执行：它们不是待合的候选，是已经落地的东西。这不是合并位的操作失误——它们进去的时候裁定还没下。

## 它们动过的文件只有四个

```
include/camel/runtime/draft.h
src/passes/opt/generic/generic.cpp
src/runtime/draft.cpp
src/runtime/draft_inline.cpp
```

未合分支里碰这四个文件的现在有 7 条，已按第 4 条的理由（同处一份文件，合进来时若那批还有错，会把错误埋在两笔的交界处、二分找不出来）从可交付批里摘出来，归 B 类待查：`lane-chainplace`、`lane-eff-rule`、`lane-fold2`、`lane-gs6`、`lane-marks`、`lane-tidy`、`probe-mark-placefork`。

## 我方默认建议（按它往下做，不等回复）

**不回滚那六笔。** 理由三条：

1. **它们已经在 `develop` 上跑了很久**，回滚等于一次性推翻六笔连锁改写，代价与风险都远大于「带着它们继续修」，而且回滚本身会把 B 类那 7 条一起作废——那 7 条可能正是修好它们的路。
2. **裁定第 2 条给了更省的路径**：错要修在造出错图的那一处。那六笔是「造出错图的那一处」的候选，回滚等于不去修那一处而改去删掉它。
3. **裁定第 3 条是真正可执行的那一条**，而且它不依赖前面两条：**编译器内部错误落在一条真跑得动的程序上，不许标 `known_fail`，它就是红的。** 按这一条，那批改写的后果会被红色逼出来，而不是被账页盖住。

**具体怎么办**：六笔留在 `develop`；那 7 条分支算 B 类（需返工）；**凡是这六笔引进来的「内部错误落在真程序上」，一律不许进 `known_fail`，按红的排返工。** 等那批改写被修好（或证明本来就对），第 4 条自然失效。

## 若这个默认不对

最可能的另一种读法是：**「不进 develop」指的是「不要再往里加依赖它们的改动」**，那我方的做法（摘出 7 条）正好一致，只是不需要回滚。**若你要的是真的回滚那六笔，请在第 4 条后面写明「已合的也要撤」**——那样我方需要另开一条回滚分支，并接受 B 类从 7 条涨到 7 条加六笔的连带面。
