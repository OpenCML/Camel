# A refused access no clause can name: the ruling stands, the corpus cannot take it yet

日期：2026-11-20 ｜ 来自审计合并车道 `audit/merge-1`（工作树 `audit1`）｜ 对象：`beyondCoverage` 对 class-Unknown 的判定 ｜ 状态：**默认按甲开工，待作者确认**

本页问的是同一件事的两个时刻：**「说不出资源的访问同样拒绝」这条裁定（负责人 2026-11-20）什么时候启用。**
裁定本身没人反对；它现在不能进 `develop`，因为量出来的代价是**把已经绿的程序变红**，而红的理由正是这条拒绝要说的那件事。

## ① 读数：一条提交、二十处新红，报错是同一句

审计 `item5-red-925c38791` 时，它的第一笔（`cbf9862ea`，`src/runtime/resources.cpp` 与 `include/camel/runtime/resources.h`）
被 cherry-pick 到 `59fd00d4b` 上，作为 `audit/merge-1` 的第一笔 `9cd366c55`。同一台主机、同一天、同一 `-Jobs 8`：

| 树 | fast 层 | gpu 计划 |
|---|---|---|
| `origin/develop` = `59fd00d4b`（原样） | `total 9825 / pass 9757 / fail 20 / skip 11` | `13 red / 13 recorded / **0 new**` |
| 五笔（含这条拒绝）= `b026c2dab` | `total 4974 / pass 4917 / fail 38 / skip 10 / known 9` | `33 red / 13 recorded / **20 new**` |
| 四笔（**去掉**这条拒绝）= `b7569d86b` | `total 9825 / pass 9756 / fail 21 / skip 11` | `13 red / 13 recorded / **0 new**` |

两个 fast 的 `total` 不同口径（一个是 `--changed` 的子集、一个是 `smoke,feat` 的全层），**不可相减**；
`gpu **0 new** vs **20 new**` 是同一口径，可以直接比。四笔那一行与基线只差 **1** 条（`exemplars/08_structured_control_flow`，
原因是 `onnx:export_model` 现在真的声明了 `var fs`，期望的 dump 必须跟着改，已在该枝上重录）。

二十条新红的报错是同一句，取自 `gpu.tiles`、`gpu.tiles_regions`、`gpu.profile`、`gpu.reading`：

```
[Error]: Pass 'std::macro' returned a program whose call site reaches past the type it declares:
node 1 of graph '__root__' (a call) declares uses var stdout, var tiles::driver;
the graph it runs touches any unknown; no clause can n...
```

即：**程序里只要有一个调用进到「还没声明资源」的算子，整棵 `__root__` 就被拒。** 这不是误报，正是裁定要的语义；
代价是语料里还有那类算子。`f73afeed3` 自己已经把三个点过名：`tiles:retain_host`、`tiles:run_host_resident`、
`tiles:run_preferred_resident`（加上已经由 `5155995e2` 在 `develop` 上落地的 `tiles:regions`）。

**而那三个之所以没声明，理由是过期的**：`e575b49f0` 写下的「运行时 resolver 不读 `functionOperand` 这个字段」，
在 `f0051edd2`（`Read a function operand's clause in the runtime resource footprint`，**已在 `origin/develop` 里**）之后不成立；
`internal/questions/2026-11-20-the-higher-order-footprint-has-a-place-and-a-reader.cn.md` ① 已把这一点核过，
`map` / `filter` / `foreach` 一族早就在走 `src/runtime/resources.cpp` 里那条路。

## ② 问作者的一件事：这条拒绝是先只计数，还是等声明补齐再拒？

（本页只问这一件；`audit/merge-1` 已经把其余四笔测成「与基线只差一条必须跟着改的期望」，这条留在枝上不删。）

**甲（默认，按它开工）**：拒绝留在枝上，`develop` 先不启用；下一件事是**把会进函数体的算子声明补齐**
（`tiles` 那三个走 `functionOperand`，另按 `camel inspect --ops` 逐个查还有谁没声明），
补齐当天这条拒绝随那一笔一起进主干，`--verify-ops` 与 gpu 语料同时绿。
代价：裁定落地晚一轮；期间 `beyondCoverage` 仍把 class-Unknown 计数后放过，也就是今天的行为。

**乙**：现在就把这条拒绝合进 `develop`，把二十条新红写进 `internal/tools/gpu-plans.exceptions.txt`。
代价：那二十条**今天是绿的**，写进 exceptions 等于用「已知红」盖住「刚被这个改动变成红」；
而且 exceptions 是给「主机原因」用的（`docs/test-plans.md`「CUDA visibility」），不是给代码用的 —— 下一个读它的人会读错。

**丙**：把拒绝降级成只计数、只在 debug/test 构建里拒（发行构建放过）。
代价：2026-11-20 的裁定退成建议值，ECOOP 提案的 R1/R2 不再由发行构建强制；
而且这与「定理没人读过就只是猜测」那条（`cbf9862ea` 引用的同一条设计原则）方向相反。

⇒ 除非作者点乙或丙，**下一轮按甲做**：先补声明，再启用拒绝。

## ③ 本页没有说的事

- 没有动 `item5-red-925c38791` 与 `lane-unknown-resource` 两条枝（都没删）：两枝的内容是同一批六笔，
  其中 (a) 两笔、(c) 一笔由 `5155995e2` 取代，剩下五笔即 `audit/merge-1` 上那五笔。
- 没有改 `stdlib/signatures/` 下的任何文件：`gemm_library_threads`、`retain_preferred`、`export_model`
  三个算子的 `uses` 由 `--verify-ops` 双向核对，它们的签名文件本来就不写 `uses`（`declared === null` 时
  该检查提前返回），所以四笔那一轮的 `--verify-ops` 读数与基线一致。

## 默认建议（写死）

**默认建议**：按页首的 **甲** 开工 —— 拒绝留在 `audit/merge-1` 枝上、`develop` 先不启用；下一件事是
**把会进函数体的算子声明补齐**：先把 `tiles:retain_host`、`tiles:run_host_resident`、
`tiles:run_preferred_resident` 三个按 `functionOperand` 声明起来（`e575b49f0` 那条「resolver 不读
`functionOperand`」的理由已被 `f0051edd2` 作废，`map`/`filter`/`foreach` 一族早就在走那条路），
再按 `camel inspect --ops` 逐个查还差谁。
**判据**：补齐当天这条拒绝随同一笔进主干，gpu 计划从今天的 **`20 new` 回到 `0 new`**、
`--verify-ops` 与基线一致。**不许**把那二十条写进 `internal/tools/gpu-plans.exceptions.txt`
（那张表是给主机原因用的，用它盖「刚被改动变成红」的条目会让下一个读它的人读错）；
也**不许**走丙（降级成只计数、只有 debug/test 拒绝行构建才拒）——那是把 2026-11-20 的裁定退成建议值。
