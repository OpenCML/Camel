# lane-b2 的 9 笔里，我方链条还差什么

**结论：整支不要。** 9 笔里 1 笔已在我方（`4211a0332`）、1 笔是 WIP（`b16981bfc`，其自述"下一轮从这里开始"）、
4 笔的**结果**我方今天已用别的手段拿到（§2 读数）、2 笔与我方机制**语义冲突**（§3）、1 笔纯测试。
值得搬的只有 2 笔：`c73d322c5`（测试，钉一道我实测到的闸门顺序）与 `270b67f09`（5 行注释，正是我方 ② 写的
with 段布局）。总代价约 35 行、0 处代码冲突。

**先纠一个前提。** 我方 ① 不在 `db741501d` 上：`git merge-base --is-ancestor 4c744f258 db741501d` 为假；
`element_at`/`extent_pair`/`zeros_of` 在 `db741501d`/`develop`/`lane-b2` 全 absent、在 `4c744f258`/`64f1af80a` 有，
而 ① 删掉的 `wantedValues` 反过来只在 `db741501d`/`develop`/`lane-b2` 上。`db741501d` 的父链是
e07ce4481（18:35 裁定的合并）→ 多条 lane 的合并，不含 d1aa176ee。所以"我方 ①② 都做完了"只对**三支并集**成立，
`lincall` 上是"trunk 无 ① + ②"。这一条决定了 §3 冲突的形状，也决定了下面的合并顺序。

## 1. 基线（只量不修，`-Tier fast -Jobs 4`，一次一个目标）

| 目标 | 树 / sha | 时刻（+08:00） | load 1m/5m/15m（前 → 后） | 读数 |
|---|---|---|---|---|
| `test/plans/feat/compile/transforms.plan.toml` | `D:\Projects\Camel-wt\b2triage` @ `db741501d`（clean） | 02:46:48 → 02:47:09 | 15.48/17.11/18.83 → 16.35/17.22/18.84 | total 105, pass 102, **fail 2**, known 1, exit 1 |
| `test/plans/feat/transforms/split.plan.toml` | 同上 | 02:47:20 | 16.35/17.22/18.84 → 16.30/17.18/18.81 | total 19, pass **19**, fail 0, exit 0 |

两条红的成因（run 的 stderr，不是我推的）：`transpose_loop_escape.two_values_of_two_kinds`
= `TRANSPOSE function=read_outer ports=2 promised=1 cause=needs-a-value-outside-the-closure`；
`transpose_loop_escape.second_state_of_the_same_loop` = `split` 的 `skip=forward`（"this loop is on the
chain the exit reads and neither half has a value for it"）。主机 32 核，runner 自报 `1m load > 8`，
**读数不与静默主机可比**（这一点写在这里，不写进结论）。XFAIL 是 `vjp_loop.rounds_are_undone_in_reverse`。

## 2. 判定表（每笔三问）

| 笔 | 它解决的问题（用代码说） | 我方今天有解吗 | 若缺，落成我方写法的代价 |
|---|---|---|---|
| `b66ab19ce` | `buildTransposeMember` 的 `capturesOf` 循环里给"闭包带不动"的参数加 `with` 端口（`appendWithPort`），让调用点**已有的** with 实参填它，不再 `skip=` | **结果已达成**（不是我搬的）：六例 array + `grad_of_a_division` 在我 tip 全绿；实测 `TRANSPOSE function=rulesets.jvp::index_into<float> type=(int, float[], float) => (float[],)`，无 capture | 不搬。它与 `transpose.cpp#buildTransposeMember` 同一个循环里我方的"capture = 追加 norm 端口"互斥（同写 `capturePortOf[at]`），见 §3-S1 |
| `2f5d3fc5d` | 需要的值从**继承的闭包**按类型唯一命中绑（`closureMembers`+`closurePorts` 扫描，`hits==1` 才绑，否则 `candidates=N` 拒绝） | **语义相反**：我方 `transpose.cpp#transposeSignature` 把 capture 写成转置类型的 norm 参数，调用点按位置填；`#forward` 的 `closurePortOf` 是**按节点身份**，从不按类型 | 约 25 行，与 capture 循环正面冲突（同一循环、同一 map）；且与 `split` 的调用形状冲突 |
| `b6544e092` | cut 半的闭包按 `residualsOf` 全表声明（`closureMembers` 取 `residual.type`），修"2 槽 vs 5 元组" | **问题不存在**：`split.cpp#addResidualCaptures` 就是每残差一个 closure 端口、按 `residualsOf` 序，`buildLinearClosure` 的 slots 同序，所以 cut 半的 `closureNodes()` 天然是那张表 | 约 20 行、收益 0；风险是 `residual.type` 为空时回退 `Float64()`，把数组残差静默变 float 槽（§3-S2） |
| `80bb6f752` | 带不动的 capture 从"加 with 端口/运行期 abort"改成**具名拒绝**，并给 `transpose has no transpose` 补原因 | **已达成，另一道闸门**：`transpose_operator.cpp#transposeOperator` 的 `promisedCount < memberCount` 守卫；实测探针 `div2(x,y)=x/y` → `skip=cotangent-needs-a-value uri=:op/div_d position=1`，exit 1，无 abort | 只差句子（约 12 行），价值低；句子在 `#transposeRefusalWord` 与 `#transposeKernel` |
| `0801dcd0c` | 探针**探自己的被调**（`TransposeState::probing` 链，环靠链终止）+ 问到不动点（4 轮），把"值在上一层"的请求带出来 | **没有**：我方是 `TransposeState::inProbe`（探针不探），`calleeNeeds` 不累加 | 约 35 行，落在 `#buildTransposeMember` 的探针块——正是 ① 要删的地方；且我方 ② 已把同一件事放到**调用点按身份取**（`transpose.cpp#transposePairCall` 的 `residualsOf` 循环），一套活两种写法 |
| `3c37b315c` | `state.calleeNeeds[f] = found` 改成只增不减（`index_into` 的 need 被它自己那次构建抹掉） | **机制活着**：我 tip 仍是赋值，且 `NeedsOut` 析构把空的 `wantedValues` 写回同一张 map，抹除路径一模一样 | 约 12 行；但 ① 落地后这条通道恒空，**不该单独搬**。它是否是我那两条红的成因**未验证**（见 §5） |
| `4211a0332` | 臂里 call 节点编号 6 而非 7（测试串） | **已在**：我方 `split.plan.toml` 就是 `node=6`，逐字相同 | — |
| `b16981bfc` | WIP：闭包 tuple 由**端口自己的类型**建，不由 `closureType()->types()`（六个 SIGSEGV 的来源；自带三处 window 打印） | 我方 `split.cpp` 不设闭包类型、`#buildTransposeMember` 的 `setClosureType` 仅在 `closureType()!=nullptr` 时执行，所以"一元组三种口"不成立 | **不合**（WIP）。顺带一条待测：`transpose.cpp#transposePairCall` 把 `cut.backward->closureType()`（我实测为 nullptr）交给 `buildLinearClosure`，靠 FILL 的 slots 跑通 —— 这不是缺陷，但值得一条独立读数 |
| `c73d322c5` | 未读的 **array** 参数先被 `autodiff: a zero tangent array needs an array with a tangent` 挡住，到不了 `skip=unread-parameter`；page 把"unreachable"改成"not seen" | **行为相同，实测**：探针 `g(x: float[], n: float) = n*2.0` 在我 tip 上逐字打这条（`.cml` 落 `D:\Temp\b2triage\probe/`） | 值得搬：1 个 case（约 17 行）+ 1 条 plan 项；page 属归档页，按 18:35 不改写 |
| `270b67f09`（盘点判"价值低"） | `src/runtime/graph.cpp#validateRuntimeGraphCalls` 上 5 行注释：Func 节点 `with` 段 = `withPorts()`，Call 节点 = `1 + withPorts()`（callee 在 slot 0） | 没有这段注释；`apply --check` 干净 | 值得搬，0 风险：它说的正是我方 ② 用 `setWithInputs(transposedCall, {operand})` 写的那个 Call 布局 |

## 3. 语义冲突单列（比"缺"危险：两套都在，会有一个静默盖掉另一个）

- **S1 成员需要的值从哪来**。我方 = 追加 **norm 参数**（`transposeSignature(...&captures)` + `#buildTransposeMember`
  的 `capturePortOf` + `#transposeOperator` 的计数守卫）；b2 = **闭包端口**（`2f5d3fc5d`/`b6544e092`）→ **with 端口**
  （`b66ab19ce`）。并存时谁赢：`#forward` 的查找顺序（`withPortOf` → `closurePortOf` → norm 端口 → `capturePortOf`）
  让读值走 b2 那条，而**类型里那个多出来的 norm 参数还在**，调用点按位置填一个体内再没人读的槽。静默点：同一张
  `capturePortOf`（key = 参数号）被两套写法写，后写的那个赢，先写的那个不改类型。
- **S2 cut 半的闭包按什么声明**。`closureNodes()`（我方）vs `residualsOf()`（b2）。b2 的写法在 `crossing.empty()`
  时才回退，replay 之后我方那条 `closureOf` 循环被**整段跳过**；`residual.type` 空时回退 `Float64()`，数组残差静默成 float。
- **S3 开形状数组参数**。我方 `#forward` 里 `shapeCarrierOf`（按类型给零载体）先返回，b2 的"问调用点要"永远轮不到；
  反过来若先落 b2，with 端口没人填 → 调用点 with-arity mismatch，正是 `3c37b315c` 自己量到的那次 SIGABRT。
- **S4 调用形状**。我方 `(known..., ct)` vs b2 `(ct)` + with 实参。这一条**已经半并存**在我方一棵树上：
  `TransposeResolver` 只承诺 `(ct)`，成员在 capture 时多出参数 —— 所以才有 `#transposeOperator` 那个守卫。
  动 b2 任一笔，就是在守卫两边改一边。

## 4. 建议（挑哪几笔，以及我方自己的合并顺序）

1. **lane-b2 整支不要**。它离 develop 是 39 文件 +1388/−319，其中代码只有 7 个；剩下 9 笔的净代码约 120 行、7 处 hunk。
   "2–3 天手解 11 个文件"这个估计对**剩余 9 笔**不成立（每笔每文件只有 1 段冲突），但对**该不该合**也不构成理由：
   数量不是问题，S1–S4 是。
2. **只搬 2 笔**：`c73d322c5`（测试，约 30 行，零代码冲突）+ `270b67f09`（5 行注释，`apply --check` 干净）。
3. **我方先合 loopres 进 lincall**（`64f1af80a` → `db741501d`），因为 ① 只在 loopres 那一支上。`git merge-tree
   --write-tree` 量到 **4 个冲突文件、每个 1 段**：`split.h` 8/8（`Residual::afterLoop` vs `Residual::linearHalf`，
   两边都留）、`transpose.cpp` 29/69（① 删的按位置取 + `wantedValues`，与 ② 不碰的同一段相邻）、
   `stdlib/rulesets/jvp.cml` 3/1、`stdlib/rulesets/transpose.cml` 3/1（两条 import 取并集）。**全是"两边都留/取并集"，
   不是取舍**，估半小时到一小时。`f5eaeb0fa` 是两边的共同祖先，所以 loopres 里的 ② 侧差别只到
   `transpose.cpp` 那一段。
4. `270b67f09` 之后再谈 `b16981bfc` 的 tuple 修 —— 不合，只借它的结论。

## 5. 没做的

没合、没 checkout / merge / cherry-pick lane-b2、没删树、没改任何源码；只跑上面两条基线（本机不构建）；
两条红的成因只到"机制在场"，**没有验证**（验证要改代码，越界）。不写问题页：① 是作者 18:35 已裁的，默认即 ①。
探针与日志在 `D:\Temp\b2triage\`（`diff-*.patch`、`probe/`、`transforms.log`、`split.log`），不入 git。