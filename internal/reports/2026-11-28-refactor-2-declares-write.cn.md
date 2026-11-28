# `D-1128-180 ②` 的材料：`declaresWrite` 是"这次访问是写"的唯一说法

**这一页是读数与判据，不是补丁**（`lane/c4-min-verify@400ff80aa5`，base `27aea1ba45`）。它交给
`D-1128-180` 第②件（"效应事实只有一份"）的第一笔，供 `lane-arrangeable` 取用；本枝不改方向、
也没往别人的枝上写东西。

## 一、这一笔说了什么

一句话：**"这次访问写不写"只在一个地方回答（`Record::declaresWrite`），"这个资源会不会被写"由表扫一次
声明回答（`OperatorTable::mayBeWritten`），"一个算子的足迹是否落在被写的资源上"读这两者
（`Record::accessesWritableResource`）—— 而 `pure` 不参与这三问中的任何一问。**

| 名字 | 在哪 | 它回答什么 |
|---|---|---|
| `Record::declaresWrite(const sem::Resource *of)` | `src/kernel/core/operator_table.cpp:1263` | 这条 clause 的某次访问**是不是写**（`of` 非空时只问那个资源）；实现只读 `uses.value.accesses` + `sem::writes(mode)` |
| `OperatorTable::mayBeWritten(const sem::Resource &)` | `src/kernel/core/operator_table.cpp:146` | **有没有人声明写这个资源**：一次扫描全表声明，逐个问 `declaresWrite(&resource)`；不看资源 class |
| `Record::accessesWritableResource()` | `src/kernel/core/operator_table.cpp:1275` | 这个算子的访问里**有没有一次落在被写的资源上**（第五项）—— 逐个问 `mayBeWritten(access.resource)`，**不问 `pure`** |
| `sem::writes(AccessMode)` | `src/kernel/include/camel/core/resource_semantics.h:124` | 最底层那句：`mode != Read` 就是写 |

为什么 class 不进判定：`Handle` 一个 class 同时盖住"会被写的草稿"与"只读的句柄"，所以只有
"谁声明写过它"能答（`mayBeWritten` 的注释原文如此）。这一点与 `D-1128-145`（取键）无关。

## 二、`pure` 的承诺被修正为"clause 说明它不写"

`pure` 原先读的是 `uses.value.none()`，那是"一次访问都没有"：**只读一个没人写的资源**（图、拼写）
的算子因此被判成不纯，它声明的读也永远进不了足迹。承诺的本义是"这条 clause 不说写"，所以现在问
`declaresWrite`。另外三种"不命名访问也算写"的情形（外部效应、函数值带来的访问、算子分配的
资源）照旧让它不成立 —— 这是承诺一直以来的意思，不是新口径。

## 三、"枝上没有第二个说法"的证据（`git grep` 原文，`400ff80aa5` 上跑的）

```
$ git grep -n 'declaresWrite' -- src
src/kernel/core/operator_table.cpp:149:    // `Record::declaresWrite`, asked here for one resource -- it is not spelled a second time.
src/kernel/core/operator_table.cpp:156:        if (record != nullptr && record->declaresWrite(&resource)) {
src/kernel/core/operator_table.cpp:1263:bool Record::declaresWrite(const camel::core::sem::Resource *of) const {
src/kernel/ops/include/camel/ops/operator_record.h:489:               !uses.value.allocates && !declaresWrite();
src/kernel/ops/include/camel/ops/operator_record.h:497:    bool declaresWrite(const sem::Resource *of = nullptr) const;

$ git grep -n 'mayBeWritten' -- src
src/kernel/core/operator_table.cpp:146:bool OperatorTable::mayBeWritten(const sem::Resource &resource) const {
src/kernel/core/operator_table.cpp:1260:// is written is the table's -- `OperatorTable::mayBeWritten`. `pure` (the promise) is not read
src/kernel/core/operator_table.cpp:1281:        if (table.mayBeWritten(access.resource)) {
src/kernel/include/camel/core/operator_table.h:463:    bool mayBeWritten(const sem::Resource &resource) const;
src/kernel/ops/include/camel/ops/operator_record.h:495:    /// write. The only place the question is answered -- `OperatorTable::mayBeWritten` asks this
src/kernel/ops/include/camel/ops/operator_record.h:501:    /// `mayBeWritten`, so a read of a resource nothing writes answers false and the class of a

$ git grep -n 'accessesWritableResource' -- src
src/kernel/core/operator_table.cpp:1275:bool Record::accessesWritableResource() const {
src/kernel/ops/include/camel/ops/operator_record.h:504:    bool accessesWritableResource() const;
```

读法：`declaresWrite` 只有一处**实现**（`operator_table.cpp:1263`），四个读者（表的扫描 156、
`pure` 的 489、足迹 1275 那条链、以及 149 行注释说"不再拼第二遍"）；`mayBeWritten` 只有一处
实现，只有足迹（1281）读它；`accessesWritableResource` 只有一处实现。**这三问各只有一个实现、
一个名字**，没有第二份同样的规则散在别处。

这一笔还顺带把三处"自己拼规则"的读者改成读一处（同一个提交）：
`src/kernel/execute/pass.cpp:1732` 的 `access.mode != Read` → `sem::writes(access.mode)`；
`src/kernel/runtime/resources.cpp` 里给消息命名"读/写"的那处同理。

## 四、读数（在新尖端上：`lane/c4-min-verify@400ff80aa5`，base `27aea1ba45`）

判据 = 这一笔改到的层：`feat/modules` + `feat/compile` + `opt/generic`，`-Tier fast`，`BUILD_EXIT=0`：

```
[SUMMARY] total: 4955, pass: 4910, fail: 34, skip: 4, known failures: 7
```

**34/34 在 `internal/reports/2026-11-28-promotion-79-reds.tsv` 上，新增红 0**（脚本逐名核：
`new (not in 79): 0`）。逐名：
`sync.pure_call_has_no_edge_to_the_statement_beside_it`、`passes_never_run_json`、
`fold_cast.failing_fails_at_run_time`、`stream.interval_loop_not_materialised`、
`op_stats.table.nvm`、`op_stats.table.fvm`、`export_unsupported_op`、`export_impure_op`、
`pass_export_refusal_located.request_in_arm_json`、`as_refusal.constant_fails.pass`、
`as_refusal.constant_fails.operator`、`slice_runtime`、`si_as_fails`、`si_as_fails_fvm`、
`si_as_fails_jit`、`si_as_fails_prl`、`fusion_group.profile.none`、
`npy_generic_wrapper_mismatch`、`rename.env_graph_identity`、`rename.process_graph_identity`、
`types_and_node_kind`、`node_kinds_match_the_graph`、`outline_delivers_the_graph_it_makes`、
`outline_delivers_a_graph_the_bytecode_vm_can_compile`、`region_roots.writing_freedom_of_two_writes`、
`stated_mean_axis_answered_by_the_engine.nvm`、`transpose_runs_a_loop_rounds_backwards.nvm`、
`transpose_loop_escape.names_the_value_one_read.nvm`、
`transpose_loop_escape.one_value_read_twice_is_one_capture.nvm`、
`transpose_loop_escape.two_values_of_two_kinds.nvm`、
`transpose_loop_escape.two_states_before_the_escape.nvm`、
`transpose_loop_escape.second_state_of_the_same_loop.nvm`、
`transpose_loop_escape.int_is_a_zero_tangent_space.nvm`、`cml_var_holding_a_lambda`。

同一判据贴在 `140111bb5b` 上那次是**逐名相同的 34 条** ⇒ 这一层既没让这枝条目变红，也没让旧红变绿。

## 五、给 ② 的两条边界（这一笔没做的事）

1. **没有新增声明**：`pure` / `uses` 两张表还是那两张，`mayBeWritten` 是从已有声明**扫出来的**，
   不是第三张表（`c4-round-source` 上被决策者驳回的 `declareUnfoldable` 就是教训：那是第二份真相）。
2. **没有改摆布判定本身**：这一笔只让"读数"各处只有一个实现；第五项落到节点上怎么用（先算/合并
   要不要问它）是 `lane-arrangeable` 那边的事，本枝不碰。