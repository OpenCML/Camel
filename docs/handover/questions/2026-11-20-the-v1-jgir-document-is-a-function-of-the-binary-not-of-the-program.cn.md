# v1 的 `std::jgir` 是二进制的函数，不是程序的函数

**日期**：2026-11-20
**提出者**：车道 R2（`lane-r2-capture-order`，基线 `origin/develop` = `6cb6fa721`，工作树 `D:\Projects\Camel-wt\r2order`）
**状态**：**未修，未定位到具体容器**。成因已缩到「v1 打印、v2 不打印的某个字段」，读到本节第 3 条为止；
按裁定（到 R2 这一轮结束仍未定位就记为缺陷条目）停在这里，不再挖。
**触发**：R2 的判据②阴性臂（反转驻留序）顺手撞出来的，不是任何既有条目。

## 1. 问题（一句）

同一份源码、行为完全等价的三份 Release 构建，给出**三份不同的 v1 `std::jgir` 文档**，
而同一份程序在这三份构建里的 `std::jgir canonical=true`（v2）文档**逐字节相同**：
v1 里有什么东西随二进制的内存布局变？

## 2. 读数（同机连续三次构建，`src/compile/gir/builder_helpers.h` 各加一行谁都不读的垫片）

同一支自检 `scripts/check-capture-order.mjs` 的程序（两个 `var`、两个闭包、一支写其中一个的 `if`），
`camel <file> std::jgir` 与 `camel <file> std::jgir canonical=true` 各取 sha256：

| 构建 | v1 `std::jgir` | v2 `canonical=true` |
| --- | --- | --- |
| `6af5a832f` 干净树 | `6207e80efc3a6ea018a35bc11ddadaf89017df701eeb56f32a81717b327d987a`（28366 B） | `045a32b96d8652fb01faa69889f6dfd7c58bec2510f93a86067012130b077b74`（17490 B） |
| ＋反序钩子（env 未设，走不到） | `62447f2718c782e1b0cfa29aefe0a2b61232fff9b40b80a6e0daddb6163348e5`（28328 B） | 同上 |
| ＋一行 `inline const char *const` 垫片 | `12562fbcabadd9380046308edf69cf51bf67924c00d2f4a7b97f8ed73a207403`（28347 B） | 同上 |

三份构建的 `src` 语义相同（垫片无人读），所以 v1 的差别不是源码的差别。
**复现命令**（本机禁止构建，全部在构建主机上取）：

```
# 1. 干净树取一份
<worktree>\internal\tools\camel-remote.ps1 -Path <worktree> -Plan test/plans/feat/compile/capture_order.plan.toml
# 2. 在 src/compile/gir/builder_helpers.h 里加一行 inline const char *const kPad = "...";（无人读）
# 3. 同一条命令再取一份
# 4. 比 out/latest/bin/camel <file> std::jgir 的 sha256
```

## 3. 已经排除的（都是读数，不是推断）

1. **不是 R2 改的那五个遍历点。**把五个待排容器的到达序整体反转（`CAMEL_WALK_ORDER=reverse`，
   钩子不提交、用完即撤），同一二进制同一 pass 两个方向给出同一个 sha256：
   `62447f2718c782e1b0cfa29aefe0a2b61232fff9b40b80a6e0daddb6163348e5`（28328 B）两次。
2. **不是捕获普查。**`scripts/check-capture-census.mjs`（`scripts/check-capture-census.mjs:409` 读 v1）
   在两份不同布局的二进制上给出**完全相同**的读数：`capture_ports=177`（111 + 66 + 0）、
   `var_new=216`、`cell_typed_nodes=1195`、`capture_ports_value=2543`、
   `list_sha=c9c799c711c5628012058f15e6fa7d85c6cf424d4bc3359c4c16c7b6553f761b`、`ok=2796 refused=1163`。
   它数端口和 `:var/new`，不读节点序号，所以它是稳的——**稳不等于不敏感，它是构造上不碰这一层**。
3. **不是 v1 与 v2 用了两套编号。**`src/passes/trans/json/gir.cpp:181` 起，v1 用的就是
   `CanonicalProgram`（`canonical.order(*g)` / `canonical.numbers(*g)`），与 v2 同一个求解器。
   所以差别在「v1 打印而 v2 不打印」的字段里，不在编号本身。

## 4. 还没查的下一个点（谁接手从这里开始）

v1 比 v2 多打印的东西里，**按地址定序**的候选只剩：`collectReachableGraphs` 给出的图序（它定
`g0/g1/...` 的名字）、`decl` 编号、`types` 列表的首次出现序。其中图序最可疑：图名一换，整份文档里
所有 `gNnM` 都跟着动，而 v2 的图名另有来源。确认方法同上（垫片重编 + 比 v1），
在 `collectReachableGraphs` 的返回值上打一行 `scope`/`sort` 即可分辨。

## 5. 要作者裁的一件事

修的方向有两条，代价不同：
- **让 v1 打印的号与 v2 同源**（v1 改成读 v2 的文档模型）⇒ 每个既有 v1 读者的字节都会变，
  `internal/tools/camel-remote.ps1:535`（从 `std::jgir` 里读按引用绑定的捕获端口）与
  `scripts/check-capture-census.mjs:409` 都要跟着看；
- **保留 v1 的形状，只给它一个结构性的并列断法** ⇒ 字节只在该并列时才变，改动面小，
  但「v1 与 v2 各有一套编号」这件事就永远留着。

R2 的改动不依赖这一条的结论：R2 那支自检比的是 v2，v2 是稳的。