# 第②件第二刀：写集 —— 「这个值里有没有写者」要有自己的一份事实

2026-11-28 · `lane-arrangeable` · 承接 `internal/plans/2026-11-28-refactor-not-patch.cn.md` 第 2 件（`fsh-dev@68fcfc6a57`）
· 基线尖端 `origin/integration@6aaf4555da`（batch47）· 本轮**不改 `src/`**、未开枝、只取读数

**这一页是材料与判据，不是补丁。**它交的三样是计划页要的：**要删的清单（带行数）、替换后的形状、做比对的语料**；另外交了**改动顺序**与**改前基线读数**。枝 `lane/effect-facts-cut-2` 等合并位数到 <20 再开。

## 0 一句话

`core.arrangeable` 的第五项 `noAccessToWrittenResource` 今天问的是**整张算子表**（`mayBeWritten` 扫一遍所有算子的访问声明），所以同一个读节点，在"这个程序里没有任何人写"和"这个程序里有人写"两种程序里拿到**同一个答案**。第二刀 = 让"这个值里有没有写者"成为**按值的一份答案**，再让第五项去问它。

**结论先说：不能只删。**`mayBeWritten` 的整表扫不是一段多余代码，它是**答案的出处**。删它之前必须先有"按值的写集"，否则第五项没有地方改问。顺序固定为：

```
按值的写集（新）
   → accessesWritableResource 改问它（旧的全表扫退场）
       → mayBeWritten 无人调，才删
```

## 1 规格与第一刀留下的

**计划页第 2 件原句（`:25-31`）**——三句是我们这一刀的全部依据：

> 今天：同一件事有好几份答案。纯度表与访问声明是两份声明；`Record::pure` 与 `ResourceSignature::pure` 各算一份；"可能被写"扫的是整张算子表；fold 只问 `pure`；一度还想再加一张 `unfoldable` 表；规则条件和 GPU 放置也各问各的。
> 目标：一个算子只有一份声明……**每个程序值算一份事实：每个节点的足迹，加上这个值里的写集。**下面这些都是对这份事实的判定函数，各在一处定义：纯；可摆（**第五项问的是"读的键在这个值里有没有写者"**）；能否在设备上跑；遍能否先替它算出来；能否用在规则条件里。
> 删：纯度表；两个 `pure`；`mayBeWritten` 的整表扫描；各遍自带的纯度判断。

**第一刀（已落，`lane/c4-min-verify@400ff80aa5`，页 `internal/reports/2026-11-28-refactor-2-declares-write.cn.md`）留了什么**：

| 留下 | 出处 | 状态 |
|---|---|---|
| "这次访问是不是写"的唯一实现 | `src/kernel/core/operator_table.cpp:1263-1273`（`Record::declaresWrite`） | 已定，第二刀**不改** |
| `pure` 的承诺改成"clause 说明它不写" | `src/kernel/ops/include/camel/ops/operator_record.h:489` | 已定，第二刀**不改** |

材料页「裁」的原话是：**"第 1 刀只把两个 `pure()` 合成一处 `declaresNoWrite`，不动读者语义、不动 `mayBeWritten`、不动第五项。"**⇒ 留给第二刀的正好是这三件里剩下的两件：**`mayBeWritten` 的整表扫**与**第五项**；而 `readKeyIsWrittenInThisValue` 今天**不存在**（`git grep` 0 命中，`6aaf4555da`）。

## 2 键 / 戳 / 拒绝：三处出处（先判"能不能只删"）

**两套事实框架不要混**：效应 kind（`core.effects` 等）在 **runtime 框架**（`src/kernel/runtime/facts.h`）；可摆性 kind `core.arrangeable` 在 **kernel facts 框架**（`src/kernel/facts/facts.h`，注册在 `src/kernel/facts/facts.cpp:1634`）。第五项属后者。

| 问 | 答（`6aaf4555da`） |
|---|---|
| **在键里怎么进键** | `Key{kind, graph, node}` —— `src/kernel/facts/include/camel/facts/facts.h:599-605`；`KeyHash` `:606-608`；桶表 `entries_` `:629`；查找 `find` `:611`。⇒ **写集没有自己的键**：它不是一份事实，只是可摆性事实里的一个 bit（`arrangeable.h:74`），所以只能按"算子"回答，不能按"值"回答 |
| **在戳里怎么被消费** | `StoredFact.versions` / `.reads` / `.epoch` / `.target` —— `facts.h:456-459`；`VersionMark{identity, revision}` `:442-447`。可摆性 kind 的戳必须说清它读了谁：写集沿 `Data`/`Sync`/`Order`/`Wait` 的**上游闭包**走 ⇒ 只钉 anchor 的一个节点**不够**；`readsDeclarations` 必须为真（算子声明一变，答案就变） |
| **谁 reject** | `Store::checkType` `src/kernel/facts/facts.cpp:838`；`KindSpec` 校验抛 `std::invalid_argument` `:74`；未知 kind 抛 `std::out_of_range` `:397`；anchor 形状由 `KindSpec`/`anchorsIn` 决定（旧框架同一件事的写法：`src/kernel/runtime/core_facts.cpp:302-307` 的 `noBulkAnchors`） |

**由此判**：第五项不是"身份多余"，而是**一个全局答案占了按值答案的位置**。所以第二刀不是删除型改动，是**换出处**：先造一份按值的事实，再让三个读者改问它，最后删掉旧的扫描。

## 3 探针读数原文（"两个同答案" = 这一刀的复现证据）

两个程序只差一件事：`cut2_read_only.cml` 里**没有任何写**，`cut2_written.cml` 里**有 `state::set`**；两个都含同一个 `state::get` 读节点。

```cml
// cut2_read_only.cml                      // cut2_written.cml
import state                               import state
func main() sync {                         func main() sync {
    let r = state::ref(1)                      let r = state::ref(1)
    println(state::get(r))                     let t = state::get(r)
}                                              let w = state::set(r, 2)
                                               println(t + w)
                                           }
```

跑法（只读跑测，探针文件不提交、跑完删掉；`-AllowDirty` 只列这三条路径）：

```
camel-remote.ps1 -Path <tree> -Plan 'test/plans/probe/cut2.plan.toml' -Jobs 2 \
  -QueueOnTreeLock -AllowDirty 'test/cases/probe/cut2_read_only.cml,test/cases/probe/cut2_written.cml,test/plans/probe/cut2.plan.toml'
```

两条探针**故意红**（`verify_script` 抛出 census 原文），`[SUMMARY] total: 2, pass: 0, fail: 2`。原文（`test::facts_compare` 的 census 行，逐字）：

```
[FAIL] probe.cut2_read_only
  PROBE_READ_ONLY  answers_row node_kind=call oper=- nodes=1 free=0 kept=1
                 | answers_row node_kind=data oper=- nodes=2 free=2 kept=0
                 | answers_row node_kind=gate oper=- nodes=1 free=0 kept=1
                 | answers_row node_kind=oper oper=:io/println nodes=1 free=0 kept=1
                 | answers_row node_kind=oper oper=state:get nodes=1 free=0 kept=1     ← 只读，kept
                 | answers_row node_kind=oper oper=state:ref nodes=1 free=0 kept=1
  TOTAL kind=core.arrangeable answers graphs=2 nodes=7 free=2 kept=5 no_fact=0

[FAIL] probe.cut2_written
  PROBE_WRITTEN    answers_row node_kind=call oper=- nodes=1 free=0 kept=1
                 | answers_row node_kind=data oper=- nodes=3 free=3 kept=0
                 | answers_row node_kind=gate oper=- nodes=1 free=0 kept=1
                 | answers_row node_kind=oper oper=:io/println nodes=1 free=0 kept=1
                 | answers_row node_kind=oper oper=:op/add_l nodes=1 free=1 kept=0
                 | answers_row node_kind=oper oper=state:get nodes=1 free=0 kept=1     ← 有写，也 kept
                 | answers_row node_kind=oper oper=state:ref nodes=1 free=0 kept=1
                 | answers_row node_kind=oper oper=state:set nodes=1 free=0 kept=1
  TOTAL kind=core.arrangeable answers graphs=2 nodes=10 free=4 kept=6 no_fact=0
```

**读法**：同一个 `state:get` 节点，**一个程序里没有任何写、一个程序里有 `state::set`，答案都是 `kept=1`**。这就是"按表回答而不是按值回答"的形状——第一刀之前它是缺陷描述，现在是读数。

## 4 改动清单（六条，每条带"为什么只能这样"）

1. **加**：`readKeyIsWrittenInThisValue(value, key)` —— 一份按值的事实，只定义一处，与 `src/kernel/facts/include/camel/facts/arrangeable.h:60-80` 的五个项并列。
   *只能这样*：计划页原句是"每个程序值算一份事实……加上这个值里的写集"；扫表在结构上答不出"这个值"。
2. **搬**：写集的来源从"算子声明 + 全表扫"搬到**沿 `Data`/`Sync`/`Order`/`Wait` 的上游闭包**。
   *只能这样*：第五项问的是"读的键**在这个值里**有没有写者"（计划页 `:29`），"这个值里"就是可达闭包，不是全程序。
3. **改**：`Record::accessesWritableResource()`（`src/kernel/core/operator_table.cpp:1275-1290`）改问写集，或退成写集上的判定函数。
   *只能这样*：它今天是把 `mayBeWritten` 逐条 access 调一遍的包装（`:1285`），是"全局答案"进入第五项的那道门。
4. **改一处读者**：`src/kernel/facts/facts.cpp:1332`（`terms.noAccessToWrittenResource = !record->accessesWritableResource();`）改成问写集。
   *只能这样*：这是第五项唯一的落点，换了出处就必须在这里换。
5. **删**：`OperatorTable::mayBeWritten`（`operator_table.cpp:146-162`，17 行）与 `accessesWritableResource` 旧实现。
   *只能这样*：`accessesWritableResource`（`:1285`）是 `mayBeWritten` **唯一**读者；第 3 条改完它就无人调。**但必须排在 1–4 之后**，否则先红。
6. **改两段注释**：`operator_table.cpp:1256-1261`（"The footprint's two readings"）与 `src/kernel/facts/facts.cpp:1327-1331`（"this says no resource the clause reads is one a write reaches"）。
   *只能这样*：这两段写的是"足迹由表扫回答"，与第二刀后的形状相反；留着就是第二份说明。

## 5 不动的四样

1. `Record::declaresWrite`（`operator_table.cpp:1263`）——第一刀定的唯一实现。
2. `Arrangeable` 的其余四项（`arrangeable.h:61-68`：`pure`/`noExternalEffect`/`cannotFault`/`terminates`）。
3. `pure`（`operator_record.h:489`）——承诺那一位，第二刀不碰。
4. **第五项读者之外的调用点**：`Record::declaresWrite` 的四个读者、`sem::writes`（`resource_semantics.h:124`）都不动；只改第五项这一条链（`facts.cpp:1332` 与它的上游）。

## 6 改前基线读数（`origin/integration@6aaf4555da`，`-Jobs 2`，`BUILD_EXIT=0`，`TREE_STATE=clean`）

```
feat/tools/facts   [SUMMARY] total: 11, pass: 11, fail: 0            RUN_EXIT=0
feat/modules       [SUMMARY] total: 2100, pass: 2072, fail: 20, skip: 4, known failures: 4   RUN_EXIT=1
```

`feat/modules` 的 **20 条红逐名**（改后要逐名比，任何新增名都要报）：

```
op_stats.table.nvm                                   op_stats.table.fvm
export_unsupported_op                                export_impure_op
pass_export_refusal_located.request_in_arm_json      as_refusal.constant_fails.pass
as_refusal.constant_fails.operator                   slice_runtime
si_as_fails                                          si_as_fails_fvm
si_as_fails_jit                                      si_as_fails_prl
fusion_group.profile.none                            npy_generic_wrapper_mismatch
rename.env_graph_identity                            rename.process_graph_identity
types_and_node_kind                                  node_kinds_match_the_graph
outline_delivers_the_graph_it_makes                  outline_delivers_a_graph_the_bytecode_vm_can_compile
```

4 条 XFAIL：`second_order_refused_vjp`、`ort_loop_values_dynamic`、`conv2d_shaped.derived_input_states_the_input_extents.nvm`、`conv2d_shaped.kernel_builds_what_it_derived.nvm`。

> 其中 `types_and_node_kind` / `node_kinds_match_the_graph` 是本车道已交的 `lane/graph-kinds-renumber@26e255b943` 要按**性质**收掉的两条（`D-1128-208`），与第二刀无关；改后仍应只在合并位那边消失，**不在本刀的读数里转绿**。

## 7 判据（第二刀自己的，不是"某条红转绿"）

1. **探针从"两个同答案"变成"两个不同答案"**：`cut2_read_only` 的 `state:get` 必须 `free=1 kept=0`，`cut2_written` 的 `state:get` 必须 `kept=1`。这一条是这一刀的全部意义。
2. **新增红 0**：`feat/modules` 逐名对上面 20 条、`feat/compile` 与 `opt/generic` 同样逐名；任何新名逐个报。
3. **`feat/tools/facts` 11/11 不掉**（含 `facts.compare_arrangeable`、`facts.arrangeable_answers*`）。
4. **不动判据**：不许为了让探针变绿改断言；探针的期望是从计划页第 2 件的定义推出来的。

## 8 交接（给合并位与下一位）

- 枝名：`lane/effect-facts-cut-2`（**等合并位数到 <20 再开**，本轮未建）。
- 落点：`src/kernel/facts/`（写集与第五项）、`src/kernel/core/operator_table.cpp`（删扫表）。
- 语料：第 3 节两个探针（源文件逐字在上面，重建即可）+ 第 6 节两套基线 + `feat/compile`、`opt/generic`、`test/plans/feat/tools/resources.plan.toml`、`arrangeable_families 7/7`。
- 本轮的树：基线在 `compile-view`（`6aaf4555da`，只读，探针文件已删净）；页写在 `value-ports` 的**未提交**文件（枝未开，不能提交）。
## 9 三组改前基线（本轮补，尖端 `origin/integration@64cfb8c524`）

> §6 那两组是 `6aaf4555da` 上取的（当时尖）。集成在这半小时里前进过（`6aaf4555da` → `64cfb8c524`），所以**建枝那一刻三组要重取一次**——这是"新增红 0"的判据本身决定的：`0 not in` 才是判据，拿旧表比新尖等于把别人的红算到自己头上（今晚已经吃过一次）。

三组同一台机、同一条命令形状（`-Jobs 2`、`-QueueOnTreeLock`、`-AllowDirty` 只列本页文件），跑前跑后都 `git status --porcelain` 记录：跑前 `?? internal/reports/2026-11-28-refactor-2-cut-2-write-set.cn.md`（1 条，本页），跑后同一条，没多没少；`[TREE_STATE=clean]`。

| 组 | `[SUMMARY]` 原文 | `[provenance]` | exit |
|---|---|---|---|
| `feat/tools/facts` | `total: 11, pass: 11, fail: 0` | `tip 6aaf4555da…`（§6 那次） | 0 |
| `feat/modules` | `total: 2100, pass: 2072, fail: 20, skip: 4, known failures: 4` | `tip 6aaf4555da…`（§6 那次） | 1 |
| `feat/opt/generic` | `total: 121, pass: 120, fail: 1` | `tip 64cfb8c524…（dirty: 1 files）`，`binary built 2026-11-28T20:17:09+08:00`，`tree /home/zjwei/camel-work/value-ports` | 1 |
| `feat/compile` | 见 §9.2（随后补） | 同上尖端 | — |

`feat/opt/generic` 的唯一红：`fold_cast.failing_fails_at_run_time`（**既有红**，不是本刀产生的；`NOTE-c4-resolved` 里已记过它是既有红，用例与计划的行号不一致）。

### 9.1 那 20 条红里，哪几条是别人的

| 红 | 归属 | 合掉后应消失 |
|---|---|---|
| `types_and_node_kind`、`node_kinds_match_the_graph` | 本车道 `lane/graph-kinds-renumber@26e255b943`（合并队列） | 是 |
| `as_refusal.constant_fails.pass`、`as_refusal.constant_fails.operator` | onnx 那条（队列里） | 是 |
| 其余 16 条 | **未归属**——建枝时按名对**当次**基线，不做推断 | 以当次读数判 |

判据写死一句：**"新增红 0" = 改后红名集合与当次基线红名集合求差为空**；任何一条名字不在基线里，就逐个报，不按目录、不按数量。

### 9.2 `feat/compile` 基线

```
[SUMMARY] total: 2741, pass: 2725, fail: 13, known failures: 3        RUN_EXIT=1
[provenance] tip (dirty: 1 files) | binary built 2026-11-28T20:17:44+08:00 | tree /home/zjwei/camel-work/value-ports | tier fast | jobs 2
```

13 条红逐名：`sync.pure_call_has_no_edge_to_the_statement_beside_it`、`passes_never_run_json`、`stream.interval_loop_not_materialised`、`region_roots.writing_freedom_of_two_writes`、`stated_mean_axis_answered_by_the_engine.nvm`、`transpose_runs_a_loop_rounds_backwards.nvm`、`transpose_loop_escape.names_the_value_one_read.nvm`、`transpose_loop_escape.one_value_read_twice_is_one_capture.nvm`、`transpose_loop_escape.two_values_of_two_kinds.nvm`、`transpose_loop_escape.two_states_before_the_escape.nvm`、`transpose_loop_escape.second_state_of_the_same_loop.nvm`、`transpose_loop_escape.int_is_a_zero_tangent_space.nvm`、`cml_var_holding_a_lambda`。
3 条 XFAIL：`stated_mean_axis_gradient.nvm`、`stated_mean_axis_element_gradient.nvm`、`transforms.with_grad_from_the_library`。
> 这 13 条与 `internal/reports/2026-11-28-refactor-2-declares-write.cn.md` 第四节那 34 条的交集逐名一致（那页是 `feat/modules`+`feat/compile`+`opt/generic` 三层合起来的 34 条），说明**这一层的红是既有的、不是那第一刀带来的**。

## 10 逐行草稿（六条各一对；`file:line` 对的是 `64cfb8c524` 的现状）

**1 删旧的第五项出处** — `src/kernel/facts/facts.cpp:1332`
```diff
-        terms.noAccessToWrittenResource = !record->accessesWritableResource();
+        // The fifth term is asked where the value is, not of the operator: `termsOfOperNode`
+        // below. Nothing is filled here, because the operator alone cannot answer it.
```
*为什么只能这样*：这一段拿到的只有 `OperId`，看不见节点、更看不见这个值；答案必须挪到有 `view`/`ref` 的地方。

**2 在值处填第五项** — `src/kernel/facts/facts.cpp:1375`（`termsOfOperNode` 里，早于 conversion 的 early-return）
```diff
-    Arrangeable terms = termsOfOperator(oper);
+    Arrangeable terms = termsOfOperator(oper);
+    // 第五项：这个值（`ref` 的读闭包）里有没有写者。`log` 把闭包报给戳。
+    terms.noAccessToWrittenResource = !readKeyIsWrittenInThisValue(view, ref, log);
```
*为什么只能这样*：`termsOfOperNode` 是既有调用链上**唯一**同时握着 `view` 与 `ref` 又已经在往 `ReadLog` 记账的函数；戳是 `Dependence::Reported`（`facts.cpp:1639`），闭包不 `log` 就进不了戳。

**3 写集本身，定义一处** — 声明加在 `src/kernel/facts/include/camel/facts/arrangeable.h:84`（`arrangeableKind()` 之前），定义加在 `src/kernel/facts/facts.cpp`（`ArrangeWalk` 之前，与 `termsOfOperNode` 同区）
```diff
+/// Whether the value of `ref` -- the closure of what it reads -- has a write in it (design
+/// principles 3.4, the fifth term). One definition; the readers of the term ask this.
+bool readKeyIsWrittenInThisValue(
+    const graph::GraphView &view, graph::NodeRef ref, ReadLog &log);
```
```diff
+bool readKeyIsWrittenInThisValue(const graph::GraphView &view, graph::NodeRef ref, ReadLog &log) {
+    // Walk what `ref` reads (Data/Sync/Order/Wait), log every node and every operator the walk
+    // touches, and answer whether any of them is a write to a resource this value names.
+}
```
*为什么只能这样*：计划页第 2 件要求判定函数"各在一处定义"；这是第五项的那一处。

**4 旧读法退场：`accessesWritableResource`** — `src/kernel/core/operator_table.cpp:1275-1290` 删（整段），声明 `src/kernel/ops/include/camel/ops/operator_record.h:506` 删
```diff
-bool Record::accessesWritableResource() const {
-    ... 逐条 access 调 table.mayBeWritten(access.resource) ...
-}
```
*为什么只能这样*：它就是把全局答案接进第五项的那道门；第 1–3 条改成按值以后它没有读者。

**5 最后才删 `mayBeWritten`** — `src/kernel/core/operator_table.cpp:146-162` 删（17 行），声明 `src/kernel/include/camel/core/operator_table.h:463` 删
```diff
-bool OperatorTable::mayBeWritten(const sem::Resource &resource) const {
-    ... 扫全表声明的 17 行 ...
-}
```
*为什么只能这样*：`accessesWritableResource`（`:1285`）是它**唯一**读者；第 4 条落地后它才真的没人调。**顺序不能换**：先删它 = 先红。

**6 两段注释改向** — `src/kernel/facts/facts.cpp:1327-1331` 与 `src/kernel/core/operator_table.cpp:1256-1261`
```diff
-        // The fifth term is the footprint, not the promise: `pure` above says the clause declared
-        // no write, and this says no resource the clause reads is one a write reaches. ...
+        // The fifth term is the footprint, not the promise: `pure` above says the clause declared
+        // no write; the footprint says no resource *this value* reads is one a write reaches, and
+        // it is answered at the node (`termsOfOperNode`), from the value's own closure.
```
*为什么只能这样*：这两段描述的是"足迹由表扫回答"，与第二刀后的形状相反；留旧话就是第二份说明，下一个人会照着它把扫表加回来。

### 10.1 这一笔不动的四样

1. `Record::declaresWrite`（`src/kernel/core/operator_table.cpp:1263-1273`）——第一刀的唯一实现；
2. `Arrangeable` 的其余四项（`src/kernel/facts/include/camel/facts/arrangeable.h:61-68`）；
3. `pure` 与它的承诺检查（`src/kernel/ops/include/camel/ops/operator_record.h:489`、`src/compile/src/gct/builder.cpp:1218`）；
4. **第五项读者之外的调用点**：`declaresWrite` 的四个读者、`sem::writes`（`src/kernel/include/camel/core/resource_semantics.h:124`）、`core.arrangeable` 的其它读者（`graph.cpp:231`、`builder_helpers.h` 等）都不改。

## 11 提交计划（搬与删分开）与戳的专门验证

枝 `lane/effect-facts-cut-2` 的第一笔**不一次做完**，按固定序拆成两个提交，**搬与删不同一份提交**——读数才分得清哪一步让它变了：

| 提交 | 内容 | 预期读数 |
|---|---|---|
| ① **搬**（commit A） | §10 第 1–3 条 + 第 6 条注释：写集定义一处、第五项在 `termsOfOperNode` 里改问、`accessesWritableResource` 仍留着但不再被读 | 两个探针**已经不同答案**（`read_only` 的 `state:get` → `free=1`，`written` 仍 `kept=1`）；其余读数应与基线逐名相同 |
| ② **删**（commit B） | §10 第 4–5 条：`accessesWritableResource`、`mayBeWritten` 及其声明 | 读数与 A **逐名相同**（删的是无人调的东西；若 A 之后还有读者，B 会编译失败——这正是拆开的价值） |

**戳的专门验证（负责人点名）**：戳是 `Dependence::Reported`（`src/kernel/facts/facts.cpp:1639`），闭包不 `log` 就进不了戳 ⇒ 改完必须专门看一次：
```
camel inspect --fact core.arrangeable <case>
```
每行 `FACT` 的 `stamp`/`reads` 里必须出现写集走到的那些节点/算子；若 `reads` 里没有它们，说明写集"算出来了但没报"，戳会沉默失效——这比读数变红更危险（它不报错，只在不该有效的时候继续有效）。探针 `cut2_*` 与 `feat/tools/facts` 的 `facts.self_check`（它验"validator 用新算重算比存储值"）是这个验证的两个落点。

**三组基线重取**：建枝那一刻按 §9 的表**重取**（集成已到 `01a53b959d` 之后），红名集合以当次为准；判据仍是 `0 not in`。
