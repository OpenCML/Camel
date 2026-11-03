# P0-2 不变量侧：两个推导判据都替代不了那个标记

**结论：这条不变量今天不能只在检查侧换判据。** 换了两次，两次都把树打红到半成品以外：手写的 kind+算子表判据 **5222 红**、共享分类判据 **3224 红**（基线 14）。缺的不是谓词的写法，是**「这个节点占据一个链位」这个事实在图里只存在于 `kGCNodeFlagSyncStatement` 这一处**。

## 一、不变量在哪、今天用什么回答（改前原文）

`src/runtime/graph_edges.cpp#EdgeChecker::checkSyncEdges`（`:268`，由 `:100 run()` 调用）：

```cpp
if (!isWaitingCarrierOrGate(user)) {          // :296
    bad(std::format("ends on a node no `sync` block wrote (kind {}, flags {}, data type '{}')", ...));
    continue;
}
if (!isWaitingCarrier(pred)) {                // :304
    bad(std::format("starts at a node no `sync` block wrote (kind {}, flags {}, data type '{}')", ...));
    continue;
}
```

`isWaitingCarrier(const GCNode*)` = `(node->flags & kGCNodeFlagSyncStatement) != 0`（`:243`），标记由 `src/compile/gir/builder_helpers.h:661 markSyncStatement` 在建图时打上。**只读标记，不读边、不读算子表、不读区域。**

## 二、两次改判据的读数（同一棵树、同一条命令，只差这一个文件）

树 `D:\Projects\Camel-wt\m4census`，起点 `origin/develop` = `1e6477a6f`（不是最新的 `f385b42aa`，本页数字属于那个提交）。
命令：`camel-remote.ps1 -Path <该树> -Plan all -Tier fast -Jobs 8`。

| 判据 | total | pass | fail | skip | known | 红名单 |
|---|---|---|---|---|---|---|
| 标记（基线） | 8535 | 8501 | **14** | 10 | 10 | — |
| 甲：手写 kind + 算子表 | 8535 | 3293 | **5222** | 10 | 10 | 新增 5096，修好 0 |
| 乙：共享 `nodeIsFreelySchedulable` | 8535 | 5291 | **3224** | 10 | 10 | 新增 3193，修好 0 |

基线那 14 红（`grad_array_*` 6、`transpose_loop_escape.*` 5、`grad_array_no_int.fvm`、`grad_of_an_array_parameter.nvm`、`registry.docs_complete`）两次都还在，一个没变绿——**它们与本次改动无关，是 `1e6477a6f` 起手就有的。**

**一个族的两端最能说明问题**：`feat/linear` 基线 `873 / pass 871 / fail 0`，判据甲之后 `873 / pass 166 / fail 705`；`feat/para` 基线 `63 / 63 / 0`，之后 `63 / 8 / 55`。**谓词写窄能让两个全绿的族几乎全红，而全量表只说「红多了」。**

## 三、两次为什么都错（都是读图读出来的，不是推测）

**甲漏了 `Func`。** 宿主上 `camel test/cases/linear/nvm/var.cml std::gir` 的原文：

```
N09 [:io/println, OPER] --SYNC--> N0B [label="update", type=FUNC, value_type=int] --SYNC--> N0C [:io/println, OPER] --SYNC--> N0F [GATE]
```

`FUNC` **不是 lambda，是「经由函数值的调用点」**（`update(c)`、`choose(...)` 都是）。它是语句，链必须穿过它。甲只认 `Brch/Loop/Call` + 不纯 `Oper` ⇒ 4994 条（4268 end + 726 start）是 FUNC 被误拒。**文件里当时有两个近重复的 `isChainStatement*`，其中一个没有调用者——这条判据从来没被定下来，就是这个样子。**

**乙用了错的分类。** `include/camel/runtime/graph.h:195 #nodeIsFreelySchedulable` 回答的是「遍能不能随意挪动它」，`isChainStatement` 要回答的是「它承不承担一次等待」——**两个不同的问题**。证据：乙的残余按 kind 分组是 `Join`(7) 3732 end + 1009 start、纯 `Oper`(11) 2865 end + 2113 start、`Brch`(6) 763 start。而 `edges.cn.md` 3.4 明说「含效应或可能出故障的分支是链上的一条语句」、「链尾连到块的值」——`Brch` 与块的值**本来就该在链上**，共享分类却说它们自由。**同一堵墙两个名字比没有更坏，这次是同一件事两套分类。**

## 四、缺的是哪个事实，要改哪一层

**缺的事实：一个节点是不是占据一个链位。** 图上没有任何字段说它，除了标记：

1. **块的值**：`GCRegion::result` 只对有自己区域的块成立（`src/compile/gir/builder_control.cpp:2707/3544/3648` 打）；图的 body 没有区域，只能退回 `returnNodeRef()`，而那只对 `main` 的退出值成立。`sync { … }` 的块值是 `:op/add_l` 这类节点时，**图上一个字段都没有**。
2. **链穿过值**：甲/乙都测出大量 `starts at :op/…`（乙 2113 条）。按 `edges.cn.md` 5.3/5.7，链尾到不归本块独有的值要经 `GATE`，**而建图今天插的是标记，不是 `GATE`**。
3. **纯节点在链上**：算子表判「纯、无效应、不出故障」（`:op/add_l` 601 start / 537 end、`:op/mul_l`、`:op/eq_l`、`:struct/len_arr` …），建图仍然连它们。

**要改的是建图层**：`src/compile/gir/builder_helpers.h#writeSyncLink` / `src/runtime/draft.cpp#setSyncInputs` / `src/runtime/draft_inline.cpp` 的内联拼接——要么不再把非语句节点串进链（链尾直接接后一条语句），要么让**每个链尾都落到 `GATE`**，让「链在这里结束」由节点而不是标记表达。**检查侧在此之前无论怎么写谓词都会红 3224 以上。**

**撤净**：两份补丁在 `D:\Temp\p02-invariant-5222.patch`（甲）与 `D:\Temp\p02-invariant-v2.patch`（乙），树上不留代码改动，本页是这条车道唯一的提交内容。`git status --porcelain` 空，分支 `lane-p02-invariant`。