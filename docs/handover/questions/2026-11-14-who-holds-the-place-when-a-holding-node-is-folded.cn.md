# 折掉一个持位节点之后，它的位置归谁？

**状态**：待裁（默认已按「交给后继」实现，见 §4）。这一页只问一件事：`std::opt::fold` 把一个
**在链上持位**的节点折成常量之后，那条链的次序应该落在谁身上。

## 1. 结论先写：`SYNC` 只表次序，所以「有 sync 用户」就是「在链上持位」，与 kind 无关

`test/cases/opt/generic/fold_cast.cml` 的前端图，`main` 的链：

```
N06 (tensor:reshape) --SYNC--> N08 (FUNC weights) --SYNC--> N09 (CAST) --SYNC--> N0A (FUNC three) --SYNC--> ...
```

同一个 `N09`（`weights() as f32[?, 3]`）的三组边：

```
N08 -> N09 [NORM] [CTRL] [SYNC]      // 值、次序，二者都是
N09 -> N0A [CTRL] [SYNC]             // 后继 three() 按次序等它
N09 -> N0C [NORM]                    // 它的值进 tensor:multiply
```

于是「**CAST** 的 sync 用户等的是值、**JOIN** 的 sync 用户等的是位置」这个区分**在图上不存在**：
CAST 同时是值和位置；`test/cases/opt/e9/rc00_seed30.cml` 里 `||` 的 JOIN 也一样（值走 `WITH` 进
`:str/format`，位置走 `SYNC` 给下一个 `println`）。值走 `NORM`、次序走 `SYNC`，所以能不能折一个
持位节点，与它是不是 CAST 无关，也不能拿 `syncUsers` 的条数当判据。

## 2. 缺陷：折掉之后那条等待被改写成「等一个常量」

`rc00_seed30.cml`，`std::opt::inline std::opt::fold` 之后，第一个 `:io/println` 的两条 SYNC 边都没了：

| | 折前 | 折后（修之前） |
|---|---|---|
| 进 | `N07 -> N0A [CTRL]` / `N07 -> N0A [SYNC]` | `N03 -> N06 [CTRL]` / `N02 -> N06 [CTRL]` |
| 出 | `N0A -> N0D [CTRL]` / `N0A -> N0D [SYNC]` | `N06 -> N0E [ORDER]` |

`N02`/`N03` 是折叠出来的 `true` 常量。等常量是空等待；`ORDER` 边不携带位置
（`docs/design/effects/edges.cn.md` 1.5、5.3）。于是这个 `println` 既不读自己的值、也不在链上，
`std::opt::dce` 报 `graph '__root__' has effect node 1 (':io/println') with neither a read of its
value nor a place in its chain`。

## 3. 两条候选，读数说选哪条

- **(a) 随节点一起消失**：语义上是删掉一次等待。等待只是换了对象，不该消失。
  这一条按「在 `replaceNode` 里补边」实现过，全量扫描退化（`same 1819`、`skipped +10`），已回退。
- **(b) 交给后继（拼接）**：折掉持位节点 `F`、`P --SYNC--> F --SYNC--> S` 时改为 `P --SYNC--> S`；
  `F` 是链头则 `S` 接任链头。次序与折前逐字一致，没有等待被删。

读数支持 (b)：`fold_cast.cml` 走完 `std::opt` 后 `main` 只剩 `N02(:io/println) --SYNC--> N04(GATE)`，
链条完好；`rc00_seed30` 之所以坏，是**拼接没发生**，而不是 CAST 不该折。

## 4. 默认（已实现，作为本页的默认建议）

**交给后继。** 依据：等待不该因为等的东西被折成常量而消失；且 `fold_cast` 的既有行为已经等价于
这个选择。实现落在 `fold` 本体（`src/passes/opt/generic/generic.cpp#placeLostStatements`，由
`foldDraft` 收尾调用），不改 `replaceNode` 的通用补边：

- 折掉 `F`：`P --SYNC--> F --SYNC--> S` 改为 `P --SYNC--> S`；
- `F` 是链头：`S` 接任链头，即「写在 `S` 之后的那条语句等 `S`」；
- 只处理三种之一（比如只处理中间那种）会漏掉 `rc00_seed30`——它恰好是链头。

修后同一段的逐字 GIR：

```
N06 -> N0E [CTRL] [ORDER] [SYNC]      // 第一个 println 把位置交给第二个
N0E -> N10 [CTRL] [SYNC]              // 第二个交给 GATE
```

## 5. 还需要作者一句话的地方

本页默认「位置跟着链走」。若作者认为某个持位节点的值一旦成为常量，它**作为语句**就该消失
（例如「`let x = <纯计算>` 折成常量后，那条语句本身不再存在」），那么 §4 的拼接对那一类是多余的，
需要一条区分「语句」与「只是持有值的位置」的判据——本页的读数给不出这条判据（§1）。

## 6. 复现

```
camel rc00_seed30.cml std::opt                 # 修前：PassInternalError，effect node 1 with neither …
camel rc00_seed30.cml std::opt::inline std::opt::fold std::opt::dce   # 最小组合（只有 dce 看得见）
camel test/cases/opt/generic/fold_cast.cml std::opt std::gir          # 对照：CAST 被折而链保住
```