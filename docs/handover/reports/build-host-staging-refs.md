# 中转仓库的引用：`[SUMMARY]` 不见了就看这里

- **在哪**：主机 `88zjwei` 的 `~/camel-work/Camel.git`。每次运行把当前分支推成 `refs/heads/lane-<树名>`，主机 just-in-time fetch 后用的是自己的本地分支；它自己的 `develop` 是陈旧的（`abbc9c229`），所以判据要先推一个哨兵 `refs/camel-tmp/develop` 指向真正的主干 tip。
- **判据**：那里的引用只有两种价值——要么指向已落在主干里的东西（可删，物体由主干可达），要么指向某条车道还没交的活（不可删）。一条命令：`git merge-base --is-ancestor <sha> refs/camel-tmp/develop`。
- **现在谁删**：`lane-<树名>` 由 `camel-remote.ps1` 在 `finally` 里删（正常结束、异常、Ctrl-C 都算）；推的时候带 `--force`，因为那条 ref 本来就是**本次运行的草稿**，不是长期引用——不快进曾让运行停在主机之前，于是闸门跑完没有 `[SUMMARY]`。
- **那次清理**：`refs/heads` 973 → 216，删掉的 757 条全落在判据的第一类里（`lane-*` 73、`handover/*`+`probe*`+`tmp*` 673+11）。留下的 76 条是没交付的活，131 条单名草稿（`scratch`/`bisect`/`rehearsal`/`push-s1-1`/`m-lane-*`）来历不明——**两者都不动，谁要动先问**。
- **为什么幂等**：别的车道也在同一台主机上跑，清理期间 ref 有进有出，所以这套扫法必须随时可重跑；幂等也正是「删的只会是已落地那一类」的保证。