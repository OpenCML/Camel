# 效应活性判定的填充点：六处位置、五份字段集

- 状态：**数表**。只记位置与数：不含决定，不含提议，不改任何判定。
- 基于：`origin/develop` 的 `76297223f`，加本分支删掉一个字段的那一笔（`effect_liveness.h` 因此少了 3 行，该文件的行号以当前树为准）。
- 对象：`EffectLivenessFacts`（`include/camel/runtime/effect_liveness.h`，5 个字段）被哪些地方填、每个地方填哪几个、从哪一侧（输入 / 用户）读。
- 写法：引用写 `文件#符号`（仓库的引用检查只认符号），行号写在括号里，作描述用。提交列是该行字符最近一次被改动的提交（按行 blame），只写 sha。
- 一句话：**五份拷贝的第一件事不是合并，是列表——列表里的"方向"也是表的一部分。**
- 一句判据：**不被扫描的页等于没写——数表要放在会被读的那条路上。**
- 一句判据：**永远 false 的字段不是保守，是没人写的一份名单——它比写错更难发现，因为它从不报错。**

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