# P1-14 判据 2 的搬迁图：`cutBranchGraph` 183 行 + `buildHalfBranch` 62 行

状态：**地图，未施工**（2026-11-02）。这一页不改一行行为，只把"哪一段搬到区域递归里的哪一步"钉住，下一轮照着做就是机械劳动。

**为什么需要地图**：`cutBranchGraph`（`split.cpp` 957–1139）是"两半边 × 两个臂 × 条件跨切 × Join 重写"一整块。它不是坏代码，它是**按图切**的产物：它一次握住了整张分支图，所以只能整块搬。目标是把它的每一段落到**一个区域**上——那时"逐节点"才成立，拒因才从图级降到节点级。

## 四栏

| 步 | 做什么 | 行号区间（`split.cpp`，`defb3527b` 时点） | 搬到哪（区域递归里的哪一步） |
|---|---|---|---|
| **S1** | 定"跨切的东西"：每个值参数 + 条件，做成 `cuts` 与 `cutTypes` | 962–991 | **递归之外**：一份 `cuts`，递归共享（见下"注 2"） |
| **S2** | 造切点元组、两半边签名（`splitHalvesWith`） | 992–998 | **递归之外**：签名是整张图的，不是区域的 |
| **S3** | 顶层 pair 的两个类型，取自 `arms.front()` 的 value/tangent | 999–1009 | **递归之外**，且**要改**：pair 的类型应由"臂区域的 yield 类型"决定，不该由第一个臂代表全体 |
| **S4** | `residualIndexOf` 表 | 1010–1013 | **递归之外**：残差编号是全图的 |
| **S5** | 建组、两个成员 reserve | 1014–1017 | **递归之外**：一个切一组成员 |
| **S6** | 前向半边：签名 / 值 ports / **残差先在 region 0 投影** / `buildHalfBranch` / 把 `(join, residualValue)` 打包成 exit | 1018–1080 | **半步进递归**：`buildHalfBranch` → 每臂一次 `cutArm(region, forward)`；ports 与 exit 打包留在外面 |
| **S7** | 反向半边：签名 / 残差 port / 种子 ports / 残差投影 / `buildHalfBranch` / exit = join | 1081–1120 | 同 S6，另一半 |
| **S8** | `group.encode()`、成员取回、读数行、交回 `built` | 1121–1139 | **完全留在外面**：读数与交回是整次切的，不是区域的 |

## 注 1：只依赖"一个区域"的段（直接落 `cutArm`）

- **S6/S7 里 `buildHalfBranch` 的两个臂循环**——已经是 `cutArm` 的形状（`defb3527b` 抽出来的），**这一步吸收时只需把它换掉**，不需要新语义。
- **臂内的体本身**：`Half::copyHalf` 已经在按节点走，且已经用 `copyArmBodies` 之外的路径进入区域（`draft.setInsertRegion(region)`）。

这两段是这张图里唯一"搬过去就完事"的部分；其余七段都有整图的依赖，**所以递归的层数由它们决定，不由 `cutArm` 决定**。

## 注 2：依赖"两个半边互相看"的段（递归之外，必须点名）

1. **S1 的 `cuts` 与 S4 的 `residualIndexOf`**：前向半边算出的值，反向半边从残差元组里取——**一个残差是两个半边的共同约定**，所以它不属于任何一层递归，属于"这一刀"。
2. **S6 的 exit 打包**（`(join, residualValue)` 的 `fillTuple`）：它把"臂的结果"和"残差元组"捆成前向半边的出口，**这一步同时读 join 和 cuts**。
3. **S7 的 `exit = join`**：反向半边的出口就是那个 join，**没有残差出口**——两半边的出口形状不对称，这是"整次切"的事实。
4. **S8 的 `built.residuals = cuts.size()` 与读数行**：对外契约。
5. **S3 的 `arms.front()`**：**它是这张图里唯一一处"用第一个臂代表全体臂"的假设**，也是吸收后最该被删的：pair 类型应该从区域的 yield 读。

**结论（也是给下一轮的边界）**：**递归只能有一层落在 `cutArm` 上，其余七段留在 `cutBranchGraph` 改成的那层"整次切"里**。把它们硬塞进递归，就会得到"每臂各自一份签名/一遍 encode"——那是另一个变换，不是这个。

## 注 3：条件跨切现在读的是什么

条件在 S1 被加进 `cuts`（`one.parameter = kNotAParameter`），然后：

- **前向半边**用 `forward.copyOf[condition]` 作为 `Brch` 的 norm input（`buildHalfBranch` 里 `setNormInputs(brch, conditionInput)`）；
- **反向半边**用 `backward.copyOf[condition]`——**它不是重算比较，是从残差元组里投影出来的那个值**（`Half::copyHalf` 走 `projectResidual`）。这正是设计 4.2 的"两半边按同一个值选同一臂"。

**所以"条件属于分支所在的区域、臂属于条件里面"这句在代码里的落点是**：条件在**分支所在区域**被算出来（S1/S6），然后作为残差跨过切；**臂是它里面的区域**（S6 的 `cutArm` 建 `GCRegionKind::Arm`，owner 是那个 `Brch`）。

**与 `guard`/`verify` 的边界**：S6/S7 里那两段"残差必须在 region 0 投影"的注释（1029–1032、1097–1098）紧邻的判据是 `runtime/branch_arms.cpp` 的**分支臂不变量**（一个节点只能读"它运行时一定运行"的节点）。吸收时**只要不改变投影的位置**，就不碰这条不变量；**一旦为了递归把投影挪进臂里，就会踩到 `guard`/`verify` 正在施工的那片地形**——所以：**投影位置不动，是本笔的红线**。

## 拒因：改前 → 目标措辞

- **改前**（今天，挂在**图**层，`buildSplitGraph` 的 `case Branch` 里）：
  > `this graph's exit is the Join of a branch (the arm the condition picks writes the pair), so the cut has to happen inside the arms, with the condition carried as a residual … but the arm is not *chosen* yet: the condition picks it, and reading that condition to pick is the next cut, not this one`
- **改后（目标，不是现状）**：拒因挂在**节点**层（`Half::makeCopy` 的判断），形如
  > `this node (kind=N, region=R) is read by the backward half, and the value it needs crosses from an arm of BRCH k that may not be taken when this half runs`
  
  即：**从"这个图不是分支"变成"这个节点、这一半，读不了它"**。改的是**挂点**（`buildSplitGraph` 的出口判断 → `Half::makeCopy`），不是措辞风格。

## 施工顺序（下一轮，机械）

1. `case Branch` 的 `return cutBranchGraph(...)` → 对每个臂区域调 `cutArm`（`cutBranchGraph` 里 S6/S7 的那两个调用点消失）；
2. `buildHalfBranch` 剩下的"建 Join / 建 Brch / 连线 / 重写 Join"改名 `wireBranchJoin`（或内联进 S6/S7），**不留只被调两次的中间层**；
3. 三行 `WALK`/`ARM` 读数逐字不变；`graph_draft_ledger` 不涨（新代码不直接碰 `GraphDraft`，`cutArm` 是模板的先例可循）；
4. 拒因换挂点（这一条可以单独一笔，先做 1–3）。