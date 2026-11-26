# 元层一行：`fsh-dev` 的 `camel-remote.ps1:2999` 让整批无法合入

## 事实

`origin/fsh-dev` 的 `internal/tools/camel-remote.ps1` 第 2999 行是一行注释，写在双引号 here-string 里：

```
# How long the pool's taker waits for a token before the run says `noslot` (-SlotWaitSeconds).
```

here-string 里 `` `n `` 是 PowerShell 的换行转义，所以生成的主机脚本变成两行：

```
# How long the pool's taker waits for a token before the run says
oslot (-SlotWaitSeconds).
```

主机侧 bash 在 `run.sh:552` 读到以 `oslot` 开头的裸行：

```
syntax error near unexpected token `-SlotWaitSeconds'
```

于是 `[camel-remote] … exit 2 (transfer 3.3 s, build 0.0 s)`：脚本没起来，不是编译错。**任何含这一行的树都编不了、跑不了，包括 `fsh-dev` 自己的树**，所以 `fsh-dev` 交回时引用的读数不可能出自它当前这一版。

## 需要的改动（一行，元层）

```
- # How long the pool's taker waits for a token before the run says `noslot` (-SlotWaitSeconds).
+ # How long the pool's taker waits for a token before the run says noslot (-SlotWaitSeconds).
```

第 3001 行的 `(`-FromThisRun`)` 同样丢反引号，但落在注释里不断行，可一并去掉。建议顺手全扫一遍这个文件里带反引号的注释：`` Select-String -Path internal/tools/camel-remote.ps1 -Pattern '`n|`t' ``。

## 卡住了什么

- `fsh-dev` 整枝合（7 笔：工作树池 `f4442f6f3a`、槽位换令牌 `db327d65e8`/`4ca6edba9a`、`83a6a088ed`、`f1b9c1a48f` 与两个 merge）—— 决策者的原则与施工实现还没进 integration；
- `new-worktree.ps1 -Release`/`-Pool` 只存在于 `fsh-dev`，所以已合车道的两棵树（`ref-boxes`、`parked-rebase`）无法释放回池；
- `camel-host-test-{1,2}.lock` 那套旧测试槽还在用，与令牌池并存（过渡期）。

## 我的默认做法（不等批复，按此走）

1. **不代写这一行**：元层由决策者做，这一行的作者是他那边；我这边只把补丁文本备好，落地即合。
2. 合并位在**一次性验证树**里临时打上这一行（不提交、不推送、用完丢弃），只为证明 `fsh-dev` 其余部分能合、能编 —— 这样修复一到手，合入是一步。
3. 在这条修好之前，任何以 `fsh-dev` 树跑出的读数一律按“不可用”处理（`build 0.0 s` 是脚本没起来，不是编过）。