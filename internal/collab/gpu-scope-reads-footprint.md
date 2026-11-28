# `#gpu` 作用域问"设备能不能跑"：一族七个读者，从承诺改读足迹

**枝**：`lane/gpu-scope-placement`，本笔 `1af66bc453`（父 `origin/integration` 的 `9966ef6393`）。**`pure` 的定义一个字没动。**

**基的说明**：本页第一节的读数是在**带着 ops 那笔**（`c8ae83150f`，"子句的承诺 = 不写"）的基上取的。本笔已
rebase 到 `origin/integration`，而这个基**还没有那笔**，那里的 `pure()` 仍是"一个访问都没有"，所以本笔在
`origin/integration` 上**行为等价**（`pure() && !external()` 与 `!external() && uses.value.none()` 同义）——
它的作用是**先把消费者与 `pure` 的那层借用拆掉**，让 `#gpu` 作用域这个问题在 ops 那笔落地时不必再看承诺的
意思。读数：`origin/integration` 上 `resident_cpu_block` 本来就是绿的（回归由 `c8ae83150f` 引入），本笔
带与不带都不改变它；`tiles` 109/103/0/6、`arrangeable_families` 7/7、`feat/modules` 甲/乙 ∅。
**判据**：`tiles.plan.toml` `109 / 103 pass / 0 fail / 6 skip`；`resident_cpu_block` PASS（输出含 `24.0`）；
`feat/modules` 对 `85101c529b`：甲 1（`resident_cpu_block`）、乙 0；`feat/linear/loops` 的
`loops.edges.{nvm,fvm,jit,prl}` 全 PASS。

## 1. 症状与病根（先纠正一条被否掉的诊断）

`resident_cpu_block`（两段设备区域之间夹一个 `#cpu` 块）在 `tiles::compile` 上 exit 1：
`GpuScopeUnsupported: '#gpu' scope 'step': operators ':var/get', ':var/get' cannot run on the device`。

**不是第五项 `noAccessToWrittenResource` 引起的**（这一条是实测否掉的）：把 `85101c529b` 整体退回
（只留 `c8ae83150f`）、重建、跑同一条计划 —— 仍然 `109/102/1/6`、红名与错误一字不差。所以
`keepsOrder` / 顺序边 / 设备区域钉点这条链在本格**没有被踩到**。

真因在 `c8ae83150f`：`Record::pure()` 从 `uses.value.none()`（"一个访问都没有"）改成了
"子句承诺不写"（`!declaresWrite()`，并保留 external/functionOperand/allocates 三项）。而
`tiles` 的 `touchesNothing(oper)` 一直**借 `pure()` 回答"设备能不能跑"**：

```
# 改前（承诺）
bool touchesNothing(core::OperId oper) {
    const camel::ops::Record *record = ...recordOf(oper);
    if (record == nullptr) { return false; }
    return record->pure() && !record->external();
}
```

`pure` 改意思之后，**只读资源的节点（`:var/get`）与只分配资源的节点（`:var/new`）都被答成"设备能跑"**，
于是作用域把它们当设备工作，答"没有设备实现"。

两次读数把这一条钉死：
- 用裁决给的窄读法 `!external() && !accessesWritableResource()`：红名从 `:var/get` **变成 `:var/new`**
  ⇒ `accessesWritableResource` 只覆盖"读到某个被写的资源"，覆盖不了**只读**（`:var/get`）与**分配**
  （`:var/new`）。
- 用足迹的"一个访问都没有"读法 `!external() && uses.value.none()`：全绿（`103/0`，`24.0`）。

## 2. 七个读者，改前读什么、改后读什么

| # | 位置 | 它问的问题 | 改前读的 | 改后读的 |
|---|---|---|---|---|
| 1 | `graph_regions.cpp#touchesNothing`（定义，`:332`） | 跑它碰没碰设备做不了的事 | `record->pure()`（承诺）＋`external()` | `!record->external() && record->uses.value.none()`（**足迹**） |
| 2 | `graph_regions.cpp#eligibility` `:166` | 这个节点能不能进设备段 | 经 ① 借承诺 | 经 ① 读足迹 |
| 3 | `graph_regions.cpp#deviceCapable` `:371` | 能力表 ∩ 碰不碰东西 | 经 ① 借承诺 | 经 ① 读足迹 |
| 4 | `graph_regions.cpp#gpuRefusal` `:384` | 形状/算子的设备程序有没有 | 经 ① 借承诺 | 经 ① 读足迹 |
| 5 | `census.cpp#operatorSurface` `:61` | 能力盘点里算 surface 还是 gap | 经 ① 借承诺 | 经 ① 读足迹 |
| 6 | `region_outline.cpp` `:586` | 宿主边界（留主机、不拒） | 经 ① 借承诺 | 经 ① 读足迹 |
| 7 | `region_outline.cpp#keepsOrder` `:155-175` | **顺序边留不留**（不是域归属） | `nodeIsFreelyArrangeable`（可摆性） | **不变** |

① 是唯一的口径，②–⑥ 全部经它；所以改一处、五处消费者同时换位，**没有第二份"设备能不能跑"的推导**。

## 3. 第 7 处为什么不改

`keepsOrder` 问的不是"落在哪个域"，是"编码器会不会保留这条顺序边"：`graph.cpp` 的编码器对**可自由摆放**的
节点丢顺序边，`keepsOrder` 只是把编码器的规则**事先算一遍**（文件内注释原话："a node that may be arranged
anywhere drops it (graph.cpp, the payload plan: core.arrangeable)"）。让它改读别的位，就会让 pass 与编码器
对"这条边还在不在"给出两个答案 —— 那是造第二份真相。域归属已经在 ①–⑥ 那一侧答完了；
`Arrangeable` 自己的两位（承诺 `pure` + 足迹 `noAccessToWrittenResource`）是
`internal/collab/pure-and-footprint-two-bits.md` 第 3 类读者的事，本格不动。

## 4. `pure()` 的读者清点（判据第 6 条）

`git grep -n 'pure()' -- src/stdlib/tiles src/kernel/include/camel/runtime src/kernel/runtime`：

| 位置 | 问的是承诺还是足迹 | 处理 |
|---|---|---|
| `src/stdlib/tiles/src/graph_regions.cpp:345` | 改前：承诺；改后：不再读 `pure()` | 改（本笔） |
| `src/stdlib/tiles/src/compiler.cpp:461` `regions.body.pure()` | **足迹**：`EffectSummary::pure()` = `!external && !writesVariables`，是图的效应摘要，不是算子的 `Record::pure()` | 不动（不是同一件事） |
| `src/kernel/include/camel/runtime/effects.h:40` `pure()` | 同上，`EffectSummary` 的方法定义 | 不动（不是同一件事） |

三个路径里只有第一处问过算子记录；另两处是效应摘要的同名方法（不同类、不同问题）。