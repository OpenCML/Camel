# 效应活性判定的填充点：六处位置、五份字段集

- 状态：**数表**。只记位置与数：不含决定，不含提议，不改任何判定。
- 基于：`origin/develop` 的 `76297223f`，加本分支删掉一个字段的那一笔（`effect_liveness.h` 因此少了 3 行，该文件的行号以当前树为准）。
- 对象：`EffectLivenessFacts`（`include/camel/runtime/effect_liveness.h`，5 个字段）被哪些地方填、每个地方填哪几个、从哪一侧（输入 / 用户）读。
- 写法：引用写 `文件#符号`（仓库的引用检查只认符号），行号写在括号里，作描述用。提交列是该行字符最近一次被改动的提交（按行 blame），只写 sha。
- 一句话：**五份拷贝的第一件事不是合并，是列表——列表里的"方向"也是表的一部分。**
- 一句判据：**不被扫描的页等于没写——数表要放在会被读的那条路上。**
- 一句判据：**永远 false 的字段不是保守，是没人写的一份名单——它比写错更难发现，因为它从不报错。**
- 一句判据：**零读点的代码不一定错——要问的是它描述的规则，今天还有没有别的东西在替它说。**
- 一句判据：**重起一条线的时候，第一笔不是修出来的数，是一条能立刻被判红的不变量。**

---

## 1. 六处（每处都带 文件 + 行）

| # | 位置（文件#符号，第 N 行） | 提交 | 场合 | 填的字段（个数） | 读的方向 |
|---|---|---|---|---|---|
| 1 | `src/runtime/graph.cpp#validateRuntimeGraphPayloadImpl`（第 583–584 行） | `23b2e69e3` | GATE 的"没有等待"断言 | `hasWaitEdge`、`isSyncStatement`（2） | 输入：`ctrlInputsOf`、`syncPredsOf`（图侧） |
| 2 | `src/passes/opt/generic/gates.cpp#bypassValueGate`（第 32–33 行） | `222cf318c`（32 行）、`21f2f3be8`（33 行） | 门节点的旁路判定 | `hasWaitEdge`、`isSyncStatement`（2） | 输入：`ctrlInputsOf`、`syncInputsOf`（草稿侧）；用户集在 44、45、67、78 行另读，不进 facts |
| 3 | `src/execute/fusion_group.cpp#isPrivateTo`（第 92–94 行） | `a882b3aaf` | 融合组的成员判定 | `hasDataUser`、`isWaitedOn`、`isSyncStatement`（3） | 用户：`withOutputsOf`（92 行）、`ctrlOutputsOf`（93 行）；输入：`syncPredsOf`（94 行）；另手扫 `syncEdges`（95–102 行）；norm 用户在 82–85 行另读 |
| 4 | `src/runtime/draft_inline.cpp#inlineCallableInDraft`（第 1754–1755 行） | `f9e2ecc63` | 内联拼接：一次调用的等待 | `hasWaitEdge`、`isSyncStatement`（2） | 输入：`controlFrontier`（1739–1748 行，`include/camel/runtime/control.h#controlFrontier`）、`syncInputsOf`；用户集在 1765–1769 行另读 |
| 5 | `src/runtime/writings.cpp#extract`（第 1147–1148 行） | `38b5880fa` | 写作折叠：成员迁移 | `hasWaitEdge`、`isSyncStatement`（2） | 输入：`ctrlInputsOf`、`syncInputsOf`（草稿侧） |
| 6 | `src/runtime/graph.cpp#isUnanchoredPure`（第 1300–1319 行） | `61f5b677b` | 无锚纯节点的判定 | 不建 facts（0）；直接读 6 组 | 输入 3 组：`ctrlInputsOf`、`orderInputsOf`、`syncInputsOf`；用户 3 组：`ctrlUsersOf`、`orderUsersOf`、`syncUsersOf`（1316–1318 行） |

第 6 行不是填充点：它读同一批边，但不经过结构体。

## 2. 五份字段集：2 / 2 / 3 / 2 / 2

五份共 **11 次赋值**：

| 字段 | 赋值处数 | 位置（行） |
|---|---|---|
| `hasWaitEdge` | 4 | 处 1、2、4、5 |
| `isSyncStatement` | 5 | 处 1、2、3、4、5 |
| `hasDataUser` | 1 | 处 3（92 行） |
| `isWaitedOn` | 1 | 处 3（93 行） |
| `isWriterWithLaterRead` | 0 | 赋值 0，读点 1（`src/runtime/effects.cpp#effectStandingOf`，第 120 行；该函数调用者 0） |
| ~~`isCallThroughValue`~~ | 已删 | `06a579a50`（删 4 行：3 行注释 + 1 行声明；读点 0、赋值 0） |

两个名字查过全树：

- `isCallThroughValue`：读点 **0**、赋值 **0** —— 已删（`06a579a50`），字段连同它的 3 行注释。
- `isWriterWithLaterRead`：赋值 **0**、读点 **1** —— 留在原处。那一个读点在 `effectStandingOf`（调用者 0）的条件里，删掉它等于把这个函数对 3.1 第三条凭据（写者表）的唯一表述一起删掉：该字段是代码里唯一说出这件事的地方，删它是对答案的决定，不是删死码。它的注释现在写明"没有调用者填它"（`include/camel/runtime/effect_liveness.h#EffectLivenessFacts`，第 42–45 行）。

## 3. `hasWaitEdge` 的三种写法

| 写法 | 处（行） |
|---|---|
| 只看控制输入：`!ctrlInputsOf(x).empty()` | 1（583 行）、5（1147 行） |
| 控制输入或同步输入：`!ctrl ∨ !sync` | 2（32 行） |
| 控制∪同步前驱的 frontier 非空：`!controlFrontier(ctrl ∪ sync).empty()` | 4（1748 行 → 1754 行） |

三种写法数的是三种不同的东西，各自的上下文写在各自的位置上。

## 4. 两个方向

11 次赋值按方向分：**输入侧 9 次，用户侧 2 次**。

| 方向 | 次数 | 字段 |
|---|---|---|
| 输入（`*InputsOf` / `*PredsOf`） | 9 | `hasWaitEdge` ×4、`isSyncStatement` ×5（处 3 的 `isSyncStatement` 读 `syncPredsOf`，也是输入侧） |
| 用户（`*OutputsOf` / `*UsersOf`） | 2 | `hasDataUser`、`isWaitedOn`（都在处 3） |

事实之外的另读（不进 facts，但同一个问题在同一个函数里读）：

| 场合 | 另读用户集（行） |
|---|---|
| 处 2 `bypassValueGate` | 44、45、67、78 |
| 处 3 `isPrivateTo` | 82（norm 用户） |
| 处 4 `inlineCallableInDraft` | 1765–1769 |

## 5. 判定本身与调用者

| 项 | 位置（文件#符号，第 N 行） | 提交 | 调用者 |
|---|---|---|---|
| `EffectLivenessFacts`（5 个字段） | 声明 `include/camel/runtime/effect_liveness.h#EffectLivenessFacts`（第 35–59 行） | `06a579a50`（最晚；同段还有 `30fa22344`、`821185288`、`a882b3aaf`、`21f2f3be8`） | — |
| `effectStandingOf` | 定义 `src/runtime/effects.cpp#effectStandingOf`（第 117–129 行）；声明 `include/camel/runtime/effect_liveness.h#effectStandingOf`（第 68 行） | `821185288`、`30fa22344` | **0**（全树只有声明、定义，和 `src/passes/opt/generic/gates.cpp#bypassValueGate` 的一处注释引用，第 27 行） |
| `keepsEffectAlive` | 定义 `src/runtime/effects.cpp#keepsEffectAlive`（第 131–133 行）；声明 `include/camel/runtime/effect_liveness.h#keepsEffectAlive`（第 71 行） | `30fa22344` | **0** |

## 6. 两侧的名字（读同一批边）

| 边 | 草稿侧 | 图侧 |
|---|---|---|
| 数据（普通） | `normUsersOf` / `normInputsOf` | `normOutputsOf` / `normInputsOf` / `normUsersOf` |
| 数据（上下文） | `withUsersOf` / `withInputsOf` | `withOutputsOf` / `withInputsOf` / `withUsersOf` |
| 控制 | `ctrlUsersOf` / `ctrlInputsOf` | `ctrlOutputsOf` / `ctrlInputsOf` / `ctrlUsersOf` |
| 同步 | `syncUsersOf` / `syncInputsOf` | `syncUsersOf`（用户）/ `syncPredsOf`（前驱，边记录） |
| 顺序 | `orderUsersOf` / `orderInputsOf` | `orderUsersOf`（用户）/ `orderPredsOf`（前驱，边记录） |

图侧的 `syncUsersOf`、`orderUsersOf` 是本轮补的（`src/runtime/graph.h`、`src/runtime/graph.cpp`）；`isUnanchoredPure` 那 6 组因此两侧都读得到。前驱侧两侧的形状不同：草稿侧返回节点号，图侧返回边记录（取 `.pred`）。

## 7. 行号的用法

行号取自上一节那棵树；改动之后按符号重新定位，不按行号。符号都在表里。

## 8. 锁住这张表的用例

`test/plans/feat/opt/edges.plan.toml`，用例 `the_five_user_sets_read_the_same_on_both_sides_of_the_freeze`，pass `test::edge_users`：同一个节点上五组用户逐项比，打印五组计数与不一致的节点数。五轮 A/B（逐一把图侧一个访问器指到另一组）只让这一例变红，其余 14 例绿；其中三组的五个计数与真值逐字相同，只有集合比较能抓住。

## 9. 那个洞的读数（`isWriterWithLaterRead` 没人填）

最小程序：`test/cases/effects/writer_table_through_callee.cml`——`apply(f, v)` 里调 `f(v)`；`main` 里 `var counter = 0`，调用前印一次，把写 `counter` 的闭包交给 `apply`，调用后再印一次。

| 读数 | 值 |
|---|---|
| `std::nvm` 输出 | 两行：`0`、`41` |
| `std::fvm` 输出 | 两行：`0`、`41` |
| 两个 back end 是否一致 | 一致（用例 `callback.writer_table_through_callee` 用 `backends_agree` 锁住） |
| 程序里的 `Call` 节点数 | 2（`__root__#1` 调 `main`、`apply#2` 调 `f(v)`） |
| `effectStandingOf` 对这 2 个节点说什么 | 都是 `HasNoCredential`：`__root__#1:s1,d0,y0,w0`、`apply#2:s1,d0,y0,w0`（s = standing，d/y/w = 数据用户 / 同步 / 写者表字段，全 0） |
| 回调里写 `counter` 的节点 | `__g_49__#2`，同一次探针的逐节点输出里也是 `HasNoCredential`（`data=0 sync=0 waited=1`） |
| 读法的来源与去向 | 临时把 `effectStandingOf` 接到 `test::edge_users` 里跑一次，facts 按 pass 的填法从冻结图取（`isWriterWithLaterRead` 仍是 0）；读数后已撤净，`git status` 空 |

**结论**：行为对（两个 back end 都是 `0` / `41`，调用跑了、写发生了、调用之后的读拿到了新版本），判定错（这 2 个节点都说"没有凭据"）。因为它没有调用者，错不产生行为；一旦有人接上它去丢节点，这个形状就是第一个被丢的。所以它不是死码，是一个会咬人的洞——删掉 `isWriterWithLaterRead` 会把洞藏起来。调用为什么还是跑了（靠什么让 `f(v)` 执行）不在这张表里，这里只记"它跑了"和"判定说没凭据"这两件读数。

## 10. 重起的第一笔：一条遍边界不变量

守的是一句话：**一个活着的执行区域，它的结果节点必须在这次遍历的 `live` 集合里**。位置 `src/execute/graph_runtime_support.cpp#computeExecutionOrder`（第 184–202 行，`GUARD` 在第 194 行，`a67652d18`）：`live` 是从图的出口（`exitNodeRef`，没有则 `returnNodeRef`）沿 ctrl / norm / with 输入与 sync 前驱走出来的那批节点。

为什么先加它：在它之前，能红的东西只有"跑出来的数不对"；而"区域的结果没人取"这种改写错误不改数——一样活干完，然后扔掉，图上和数上都看不出来。`GUARD` 在 `NDEBUG` 下也在，所以它是当场判死的一句话，不是只在调试版里响的提醒。

A/B 读数（都跑 `test/plans/feat/compile/branch_exit.plan.toml`，10 例）：

| 方向 | 读数 |
|---|---|
| 还原 | `total: 10, pass: 10, fail: 0` |
| 拿掉被守的那条分支（把 JOIN 的 `with` 输入从 `live` 的走法里去掉——臂的结果正是这样进图的） | `total: 10, pass: 1, fail: 9`，9 例全是同一句：`Camel Invariant Violation: Region 1 yields node 4 and its owner 7 is in the execution, but that node is not: the value the region makes is read by nothing the exit depends on.`（`used_arm_exit` 与 `unused_arm_exit` 各 4 个 back end，加 `unused_arm_exit.wno`） |

被守的分支一拿掉，红的不是数值，是这句话本身；还原即绿。这一笔只加这一条不变量，没动任何判定；`isWriterWithLaterRead` 的语义也没有动（它仍是那个洞的名字）。