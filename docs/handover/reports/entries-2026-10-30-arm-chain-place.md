# 臂的链：效应被旁路，与它留在图上的节点

*2026-10-30 晚至夜，lane `lane-eff-rule`（`D:\Projects\Camel-wt\h-eff`）。这条诊断线到此为止，本页是交接：已经确证的、没有定位的、可以独立交付的，分开写。*

## 1. 现象与最小用例

`test/cases/consistency/if_without_else/older_effect_place.cml`（新增，已在树，**现在是红的**）：

```camel
func older(c: bool): int sync {
    if c then sync { println('older'); null }
    return 1
}
func main(): int sync {
    println('before')
    println(older(true), older(false))
    println('after')
    return 0
}
```

- `std::nvm`：`before / older / 1 1 / after` —— 正确。
- `std::opt`：**`older / before / 1 1 / after`** —— 红。

同一形状的更大用例是 `if_without_else.statement.{opt,simplify,inline_opt}` 与 `attempt.not_isolated.fault_in_argument.{nvm,fvm,jit,prl}`（后者少了本该先打的 `20\n`）。全闸里另有 21 条 `sync chain forks at node N` 属同一族（效应/语句在链上的位置），见第 5 节。

## 2. 已经确证的四件事

1. **`fold` 之后链是正确的。** 内联把 `older` 的体并进主图后，链是 `4381(before) → 4375(older 的 println) → 4389 → 4392(after)`。
2. **`cse` 的合并不是搬运者。** 在 `cseDraft` 里唯一那处「把 `id` 并进 `kept`」的 `replaceNode` 之前装了探针（`CAMEL_CSE_TRACE`），**整份程序零行输出**——`4375` 从未作为 `id` 经过合并。
3. **`4375` 属于 `older` 的臂区域图，不属于 `main`。** `members` 行给出：`main` 的图成员是 origin 簇 `4380–4393`，**其中没有 4375**；`4375` 在另一张 10 节点图里，成员是 `4371 4370 4374 4375 4376 4377`（`older` 体内）。
4. **主链被旁路。** `4389` 的 sync 前驱在 `fold` 后是 `4375`，在区域再提取之后变成 `4381`——链从「经由效应」变成「直接跨过它」。

**因此：节点没有消失，它只是掉了链位。** 节点数不变、无环、类型正确——所有结构校验都看不出来。这是这一格最值得记住的一句。

## 3. 没有定位的一处

**区域再提取把 `4375` 收回区域图的那一处写入**（`fold` 的内联之后、`cse` 的输出之前）。它该不该保链位，取决于「把节点放进区域图」时是否检查它在原链上的邻居。

`ROUND` 与 `members` 两种打点已经把阶段圈到这里，但**没有指到具体那一行写入**。再往下要第三条探针，今晚不投入。

判据（留给接手的人）：**区域图里带效应的成员，必须仍排在它原来的效应前驱之后、效应后继之前**；否则 `println('before')` 与 `println('older')` 这两条同一资源（stdout）上的效应顺序变了，**直接违反设计原则 3.3**（同一资源上的效应在每一个遍之后都保持写下的顺序），也违反红线 3 的意图。

「带效应」的判法，**全树唯一的那一种**（`modules/tiles/src/graph_regions.cpp:97`）：

```cpp
if (!table.isPure(oper) || table.hasExternalEffect(oper))
    return "effect";
```

守卫抄它，不许另写第二套。

## 4. 三条教训

1. **诊断打点挂在哪一层回调上，决定了它能看到什么。** 挂在「只对该遍改动过的对象」的回调上（`rewriteReachableGraphs` 的 draft 回调），就永远看不出「一直存在的东西」——我据此误判过一次「cse 新建了区域图」，被数据推翻。
2. **诊断的第一行要打在任何处理之前**（`ROUND <n> start graphs=/sizes=`）。否则看到的永远是「被处理过的东西」，不是「东西本身」。
3. **「局部合法、合成之后错」今晚出现了三次**：环（`rewrite` 里多处局部合并）、顺序（`fold → cse`）、区域（效应被旁路）。**这不是三个 bug，是一个模式：每次合并/提取都只看自己那一处是否合法，没有人检查「节点的链位与 region 归属在合并之后是否仍然成立」。**

## 5. 读数与分界

| 项 | 数 |
|---|---|
| 全闸（本车道旧树） | `total 7854, pass 7750, fail 94, skip 8, known 2` |
| 其中环境项 | `portability.std_includes`（`No module named 'camel'`），不计入代码账 |
| 环（`lies on a cycle`） | 0（已修，见第 6 节） |
| `expect_stdout differs` | 0 |
| 余下 93 条分类 | 21 链分叉（真失败）+ 7 顺序（真失败）+ 17 图快照 + 6 边计数 + 42 断言类 |

`skip` 随 GPU 占用变化（8 并发时 0 号卡被他人占用，探针如实报告不可用），**`fail` 不随环境变**。

`develop` 现头 `8913d9372`，其基线为 `total 7873 / fail 0`；**本车道的 93 是这条车道自己的红**，不是 develop 的。

## 6. 可独立交付的部分

| 项 | 状态 | 说明 |
|---|---|---|
| **环那笔**（`rewrite.cpp`：`draftHasCycle` + 改写后整体校验 + 成环丢弃会话） | **要捞，可独立交付** | 修掉 30 条 `PassInternalError: lies on a cycle of inputs`。判据：**判在规则层找不到环——环可以由多次局部合法改写拼成，所以校验放在改写之后、整图检查**。与臂链错位是**两条独立的线**。 |
| **四个诊断开关** | 可随页一并进树 | `CAMEL_CYCLE_TRACE`（`graph_edges.cpp`）、`CAMEL_ARM_TRACE`（builder 分支接合）、`CAMEL_ROUND_TRACE`（定点轮次：`start graphs=/sizes=`、每遍 `chain=`/`members=`）、`CAMEL_CSE_TRACE`（`cseDraft` 合并两端）。**全部默认关闭，零行为改变、零输出改变。** |
| **5 行红色用例** | 留在树上 | `if_without_else.older_effect_place`，计划项已加。修好那一笔记它转绿，是一笔的验收。 |
| 建图规则 4 笔、`graph_regions.cpp`、`gates.cpp`、`taskflow.cpp`、`draft_inline.cpp`（`waitsOn` 补 order）、`camel-env.mjs` 基建、`public_pass_graph.py` 断言改写、`entries-2026-10-30-pass-boundary-invariant.md` | 工作区，**未推** | 与 `fail 93` 绑在一起，闸门清零前不推。 |

## 7. 一次被否决的做法（别再试）

在 `builder_helpers.h` / `generic.cpp` 里加「判环」的守卫，**撤掉后 `fail` 从 94 涨到 104（多 11 条）**——恢复后回到 94（93 + 1 环境）。

| 状态 | SUMMARY |
|---|---|
| 撤掉三处守卫 | `total 7854, pass 7740, fail 104, skip 8` |
| 恢复三处守卫 | `total 7854, pass 7750, fail 94, skip 8` |

**它们不是「永不触发的检查」，不许再撤。**