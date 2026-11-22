# 一个拒绝被拿掉之后，它挡住的缺陷就只剩读数了

## 问什么

今晚同一个形状出现了两次：一处**拒绝**（诊断、封口检查、`Refused*` 分支）被"修好"或放宽，
它本来挡住的那个缺陷没有一起修，于是错从"被明确拒绝"退化成"安静地算错"或"在别处炸出来"。
要定的是工程规矩，不是某一笔：**放宽一处拒绝之前，要不要先证明被它挡住的东西是对的？**

## 两个实例（都是今晚）

| 实例 | 拿掉的拒绝 | 之后露出来的是什么 | 发现代价 |
| --- | --- | --- | --- |
| `db741501d` | "一条调用没有转置"从拒绝变成可转置 | 跨调用 `grad` 静默答 `0.0` | 八小时后 |
| `e4aa634e8` | 同步边的封口判据从派生的 `isChainStatement` 换回 `kGCNodeFlagSyncStatement` 标记 | 26 条 `std::defunctionalize` 的 seal 拒绝消失，同一批改写造出的图在 `std::opt` 里撞上 branch-arm 不变量（`benchmarks/mlp/model.cml`，`std::macro std::opt std::nvm`） | 四十分钟，且差点被当成"旧红" |

两个实例的形状是同一个：**拒绝是对的，被挡住的图是错的；修掉拒绝只是把错挪到看不见的地方。**
第二例还多一层：`e4aa634e8` 本身是对的（它恢复的就是 `origin/develop` 的判断，
把它 cherry-pick 到 `34b659107` 上三方合并的结果与该树逐字节相同，见下），
错在被它放出来的那批改写——这正是"隐藏"比"制造"更危险的地方：**造墙的那一笔与拆墙的那一笔不是同一笔。**

## 读数（`feat/modules/autodiff`，`-Tier fast -Jobs 4`，树 `D:\Projects\Camel-wt\arminv`）

| 提交 | `[SUMMARY]` | `benchmark_gradients_vs_torch` |
| --- | --- | --- |
| `34b659107`（实时 `origin/develop`） | total 263, pass 263, fail 0 | `[PASS]` 909.83 s cpu / 76.46 s wall |
| `afcdc6795`（`integ2/rules-batch2` 第一父，可疑合并之前） | total 277, pass 277, fail 0 | `[PASS]` 911.34 s cpu / 74.99 s wall |
| `ab9b5bf1a`（合 `integ3/backlog-b`） | total 277, pass 265, fail 12 | `[FAIL]` 6.83 s cpu / 2.74 s wall，stderr 是 branch-arm 那句 |
| `52470e9c6`（`e4aa634e8` 的父） | total 277, pass 251, fail 26 | `[FAIL]`，stderr 是 `std::defunctionalize` 的 seal 拒绝 |
| `e4aa634e8` | total 277, pass 272, fail 5 | `[FAIL]`，stderr 是 branch-arm 那句 |

`e4aa634e8` 那一笔只有 `src/runtime/graph_edges.cpp`（9+/21−）。把它 `cherry-pick -n` 到
`34b659107` 上，工作树相对 HEAD 无改动——**它在 develop 上是一个空改动**，所以它不是造图的那一笔；
造图的那批（`3bdd5ee47`、`976890ced`、`4aa3f8c71`、`039be853d`、`1e1c1dbdb`、`8c48dbe37`）在
`2d5168a65` 那个合并里进树、随 `ab9b5bf1a` 进本批，`git merge-base --is-ancestor` 逐个查过，**都不在 `34b659107` 里**。

## 三条路

- **甲（默认）先证明、再放宽**：任何放宽/删掉拒绝的提交，必须带一条读数证明"被它挡住的东西是对的"
  （本页这一例：放宽后 `benchmarks/mlp/model.cml` 仍绿）。代价：每次放宽多一次闸门，约 2 分钟。
- **乙 放宽与造图同批**：允许一起合（今晚实际的做法）。代价是红了以后无法区分"造图错"还是"放宽错"，
  本页这一例花了一小时才把两层分开，而且差一步被记成"合之前就在批里的墙"。
- **丙 不放宽、只修造图**：先修改写，再放宽。代价：`ab9b5bf1a` 那 26 条 seal 红会一直挂着，
  在改写好之前收不了批。

## 默认建议（按它开工，不等回复）

甲 + 丙 的组合：**先按丙把改写的图修到 seal 不需要放宽，再按甲要求每一次放宽都附读数。**
本次的 `benchmark_gradients_vs_torch` 是编译器内部错误落在一条真实可跑的基准程序上，
**不许挂 `known_fail`**——标记等于把一条可达的编译器内部错误登记成正常。