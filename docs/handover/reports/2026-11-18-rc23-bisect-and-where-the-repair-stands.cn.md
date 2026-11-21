# rc23 二分定位：引入的是哪一笔、为什么、修到哪一步

作者要求「下一次合并先报：**二分定位到哪几笔、各自引入了哪一类、修到哪一步**」。这是那一份。

## 一、基线：`develop` 上它本来是绿的

```
`develop` @ `33c0fb536`（未改动、树干净、同一入口 `-Plan ... -Jobs 8 -NoGpuWatch`）：
  `feat/opt` **1538 total / 1538 pass / 0 fail**
  `feat/opt/e9` 单跑 **212 / 212**
⇒ **`rc23_seed429_sync_arm` 在 `develop` 上四个后端全绿。**
⇒ 所以它是**这一批引入的回归**，不是「块切面不可达」，不需要裁定。
```

## 二、二分：定位到一笔，而它的「撤回」没有救回来

同一探针、同一程序、同一提交序列：

| 提交 | 读数 |
|---|---|
| `102c94b22` | **`P 5`** ✅ |
| **`e81642ed8`**「the region roots are back」 | **`Warn`** ❌ ← **第一笔红** |
| `b0b353e0e`（它的「撤回」） | `Warn` ❌ ← **没有救回来** |
| `4cc13edcb` / HEAD | `Warn` ❌ |

**⇒ 引入的是 `e81642ed8`；`b0b353e0e` 撤回了区域根，但没把这个形状恢复。**

## 三、为什么：`result already reaches it` 被当成了「不必建 GATE」

`layRegionWriteEdges` 的临时 trace，实测原文：

```
[trace] id=16 kind=10 -> statement=16 region=3 exit=18    ← `q(5)` 确实被收进了臂的写
[gate]  region=3 write=16 dropped: result already reaches it
[gate]  region=3 nothing to carry                          ← 于是没有 GATE
```

**⇒ 「出口已经够到写」被当成「不必建 GATE」的理由，而没有任何东西让那个出口被需要** ⇒ 冻结时整个 `match` 不可达（图里没有 BRCH/JOIN 与 `q` 的调用），打印那句 `Warn`。

**对照 `develop`**：那边的 `gateExitOf` **无条件**给臂建 GATE，并用同步边把 JOIN 排在其后，所以绿。

**⇒ 一句话**：**这一批把「无条件建 GATE」改成了「出口够到写就不建」，而「够到」并不意味着「被需要」。** 这正是作者那句「**归属与启动是 membership，不是 order**」的反面：`result already reaches it` 是一个**顺序**事实，被用来回答一个**是否需要**的问题。

## 四、修到哪一步

```
① 判据 ④ 已绿（`445fd6ea5`）：`same_graph` = true，nodes=15 edges=12，6568 字节、两侧 sha256 相同；
   两个反例仍 false。根因不是 sync 边，是出口 GATE 尾随的写按书写序占槽位 ⇒ 改成 tie group
   （与同函数 capture 表同构），文档 `slot` 印规范序号。**前端未动。**
② rc23 按作者返工令落地（`007f0550b`）：**用例不撤**，四个后端标 `known_fail`，写明
   缺陷（`a region's write is not joined to the region's exit when the graph is built`）、
   负责的那笔返工（`Builder::layRegionWriteEdges`）、撤销条件（删标记并打印 `P 5`），
   并把**今天那句 `Warn` 原文钉进 `known_fail_msg`** —— 修好那天这条标记自己变红。
③ 两条补法试过并**全部回退**（放宽 `exitInRegion`；按臂序号只换本臂的 `with` 槽），都没让它绿。
④ **未修好的原因已定位**：需要让出口「被需要」，而 `develop` 的做法是无条件建 GATE。
   这不是靠局部放宽能得到的，要动 `gateOf`/`layRegionWriteEdges` 的取舍条件本身。
```

## 五、读数（入口 `-Plan <目录> -Jobs 8 -NoGpuWatch`，tier fast、**不含 gpu-plans**）

| 计划 | `lane-rrread` | `develop` 同入口 |
|---|---|---|
| `feat/opt` | 1538 / 1534 pass / **0 fail** / 4 known | 1538 / 1538 |
| `feat/para` | 100 / 100 | — |
| `feat/tools` | 70 / 70 | — |
| `feat/compile` | 2627 / 2594 / **16 fail** / 17 known | 2626 / 2570 / **39 fail** / 17 known |

**逐名比较：新增 6、消失 29。**

```
新增 6（本批「give-back 改成计数」那笔带来，不在上面两笔里）：
  `ref.foreach_gives_back.{nvm,fvm,jit,prl}`、`ref.reduce_takes_its_initial.{fvm,jit}`
⇒ **其中 `ref.reduce_takes_its_initial` 上一只手在自己的树上量到四后端全绿，
  而那笔已合进 develop ⇒ 这 6 条是两笔合起来的交互回归。已单独派车道。**
消失 29：blocks×4、`sync.effect_call_stays_on_the_chain`、dropped_effects 族、`lambda_rest_as_a_value`、
  `unused_values.effect_body`、`region_roots.dead_loop`、`region_roots.branch_statement*`、
  `branch_statement_kinds*`、`branch_value_dropped`、`refwrite.dead_arm×4`
```

## 六、合入判据（我的默认，已按此停手）

```
① 那 6 条 `ref.*` 全绿 —— **否则不合**（新增红不进主干，即便净减 23）
② `feat/opt` 回到 1538 或 4 条 known 有作者的准许
③ `feat/compile` 逐名不新增红
⇒ 三条齐了再合。**净减不是合的理由，新增才是合的障碍。**
```