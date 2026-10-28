# 保存失败以退出码 0 收场：ckpt 用例在 251 行就中止

基线 `origin/develop` `3ae196571`。以下读数都是远端主机上跑 `test/plans/feat/modules/checkpoints.plan.toml`（`camel-remote.ps1 -Plan … -Jobs 8`）的整行输出；本页没有在本机构建，也没有在本机跑过计划。

## 我量到的

**(1) 这个提交单独就红，两边的另一半都不是原因。**

| 树 | 是什么 | 读数 |
|---|---|---|
| `6030c30fc` | `fe2379c53` 的父提交 | `[PASS] native_state_checkpoints 3.69 s`，`total: 4, pass: 4, fail: 0` |
| `fe2379c53` | ckpt 声明化那一笔 | `[FAIL] native_state_checkpoints 6.41 s`，`checkpoint_verify.mjs:251:26`，`actual: 0, expected: 1`，`total: 4, pass: 3, fail: 1` |
| `4e31a97c1` | 我们的同步边批合入 develop 的那一点，用例输入还是 `fe2379c53` 之前的版本 | `[PASS] native_state_checkpoints 5.22 s`，`total: 4, pass: 4, fail: 0` |
| `3ae196571` | 当前 develop，两侧都在 | 红，同一行 `checkpoint_verify.mjs:251:26`，`[stdout] (empty)` |

`fe2379c53` 上我们的同步边批并不存在（`23a171931` 不是它的祖先），那条红与我们的批无关；`fe2379c53` 与 `fe2379c53^` 只差这一笔，而父提交全绿 ⇒ **是这个提交自己翻的红**。

**(2) 现象一句话。** `test/cases/modules/nn/checkpoint_roundtrip.cml` 的 `mode=7`（在不可写的父目录下整体替换既有 checkpoint）**退出 0 且没有任何输出**；用例要求**退出 1，且 stdout+stderr 匹配 `/FileSystemError|cannot create the checkpoint temporary directory/`**（`test/cases/modules/nn/checkpoint_verify.mjs:255`）。断言消息就是 camel 的 stdout+stderr（`checkpoint_verify.mjs:85`），所以栈顶没有文字，只剩 `code: 'ERR_ASSERTION', actual: 0, expected: 1, operator: 'strictEqual'`。

**(3) 我推测的因果（推测，不是定论）。** 这个提交把 `save_tree` 的落盘失败改成"返回 fs 的错误值"（提交信息原话：`save_tree's file-system failure is an error value of fs's kinds`）。保存失败于是不再以 fault 收场：`mode=7` 里 `let _ = (advance(initial(), 5))->save_tree<dir, 'dataset-v1'>` 之后照常往下走，进程以 0 退出；而 `:251-259` 仍按"报 fault、退出 1、旧 checkpoint 不变"写。

**(4) 一个连带影响。** 用例在 `:251` 的 `invoke` 里就中止了，`:260-265` 那两条**根本没跑到**："a failed save changed the old checkpoint" 与 "a failed save left a temporary or previous directory behind"（`.pending-` / `.ckpt-previous`）。也就是说，**现在没有人知道这两个不变式在新语义下是否还成立**。

## 要问的一句

**"保存失败不再以 fault 收场"是有意的语义改变（那用例与 `:251-259` 该按新语义改），还是漏了把失败路径接回 fault（那代码该补）？**

## 没量到的

- 没量 `checkpoint_verify.mjs:260-265` 那两条不变式在现语义下是否仍成立：用例在 `:251` 就中止，那两行没有执行到。
- 没扫 `save_tree` 改成返回错误值之后，其它调用点是否也指望它以 fault 收场：本页只量了这一条用例。

本页只报测量与一句询问：不改代码、用例、计划或生成物。