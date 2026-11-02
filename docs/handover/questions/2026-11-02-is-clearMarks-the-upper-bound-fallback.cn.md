# 这一处是不是"收集器『按上界扫』的兜底"：`clearMarks()` 每次回收都把整个图空间扫一遍清 mark 位

**状态**：待裁。**我找到了、判它是兜底、但不删**（删它是 GC 语义改动，见第 3 节）。

## 1. 找到的那一处（原文）

`src/core/mm/gen.cpp`，`GenerationalAllocatorWithGC::clearMarks()` —— `markPhase()` 的**第一句**就是它
（`src/core/mm/gen.cpp:1437`）。原文与它的理由注释：

```cpp
void GenerationalAllocatorWithGC::clearMarks() {
    // Young generation marks matter when the copying collector is enabled. Clearing them keeps
    // a later major collection from skipping an already-marked survivor and missing its edges.
    birthSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    havenSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    cacheSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

    elderGenSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    largeObjSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

    // Graph carriers are also traced through Function values (Function::updateRefs traces the
    // runtime graph so runtime-created graphs keep their static constants alive). Those graphs
    // are not necessarily in a registered root set, so unmark the whole graph space instead of only
    // the registered roots, or a later collection would skip them and miss their edges.
    graphSpace().iterateAllocated([](ObjectHeader *header) { header->unmark(); });

    for (const ObjectRootSetEntry &entry : objectRootSets_) {
        ...
        ObjectHeader *header = headerOf(root);
        if (!inYoungGenSpace(header) && !inElderGenSpace(header) && !inLargeObjSpace(header)) {
            header->unmark();
        }
    }
}
```

**"按上界扫"就是 `iterateAllocated` 的写法本身**（`include/camel/core/mm/alloc/bump_ptr.h:223`）：

```cpp
std::byte *current     = begin_;
std::byte *topSnapshot = top_.load(std::memory_order_relaxed);
while (current < topSnapshot) { ...; visitor(header); current += obj_size; }
```

—— 从起点走到**已分配的上界**，而不是走某个精确集合。

## 2. 判它是不是"兜底"：是

- **有更精确的集合，而且就在同一个函数里**：`objectRootSets_` 那段循环就是精确版（只清注册根里的、
  且不在年轻/老/大对象空间的）。**图空间这一路不用它，改用整表扫。**
- **它不在 `#if`、也不在异常路径**：`markPhase()` 每次标记前第一句就调 `clearMarks()`，**每次回收都跑**。
- **理由注释自己承认精确集合不够**：`Those graphs are not necessarily in a registered root set` ——
  经 `Function::updateRefs` 追到的图不在注册根集里，于是退到整表扫兜住。

判词：**这是"按上界扫"的兜底，而且是靠一段注释维持的兜底。**

## 3. 为什么我不删（作者要"同一笔删掉"，这一处不行）

删了它，`clearMarks()` 只剩根集那一段，**上一轮标过的 mark 位会留到下一轮**，注释把后果写得很具体：
`a later collection would skip them and miss their edges`。要让这句话不再成立，得先做完一件正事：
**把经 `Function::updateRefs` 追到的图登记为根**（或改成 sweep 时清位、或让标记自带世代号）。
**这是 GC 语义改动，不是删死代码** —— 我不在一笔删码里改收集器的语义。

## 4. 问（一页一问）

**这一处是不是您说的"收集器『按上界扫』的兜底"？**

- 若是：请裁 —— 先把 `Function::updateRefs` 追到的图补进根集（这样这段整表扫可以删），还是先留着；
- 若不是：请点名**符号**。三轮词表见第 5 节，除这一处外没有第二个"整表扫"。

## 5. 找过的词（三轮合并，可复查）

1. `upper`（`src/runtime src/compile`）→ 6 处：`std::uppercase` 的流格式、`builder.h` 一句
   "写入集只是上界"的注释、`literal.cpp` 的十进制位循环；**无收集器扫描**。
2. `fallback|safety|in case|conservative|upper|scan`（`src/core/mm`）→ 唯一候选
   `free_list.h` 的 `Step 2: scan the entire memory region`（分配器 `verify`，非兜底）。
3. `sweep`（`src include` 文件清单）→ `src/core/mm/{gen,block,mm}.cpp`、`gc_debug.cpp`、`profiler.cpp`。
4. `nodeCount|nodes_.size()|objectCount|allocCount|heapCount`（`src/core/mm src/runtime`）→
   `src/runtime/{draft,draft_clone,core_facts,facts,effects,definitions,branch_arms}.cpp` 的**图遍历**与
   `profiler.cpp` 的统计；**收集器本体没有 `nodeCount` 循环 —— 它按 space 走，不按节点表走**。
5. `mark|sweep|collect|compact|trace|scan(`（`src/core/mm/{gen,mm,block}.cpp`）→
   `markPhase`/`clearMarks`/`sweepOldGen`/`sweepLargeObjects`/`scanRelocatedObjects`；
   **只有 `clearMarks` 是"整表扫"**（`scanRelocatedObjects` 是 Cheney 扫描，扫的是新空间，属正常设计）。

**已推进到：找到并判定**（一处：`clearMarks()` 的图空间整表清位；判为兜底；**未删**，等裁）。
## 6. 实测（本轮补上）：按您原话删了，38 条红

| 步骤 | `[SUMMARY]` |
| --- | --- |
| 基线（删码笔之后） | `total: 8514, pass: 8450, fail: 44, skip: 10, known failures: 10` |
| 先补"按根走"的清位、整空间扫留着 | `total: 8514, pass: 8450, fail: 44, skip: 10, known failures: 10`（逐项相同） |
| 删掉整空间扫 | `total: 8514, pass: 8412, fail: 82, skip: 10, known failures: 10` |

新红的 38 条**全部落在强制大回收/回收压力这一族**（`json.gc.forced_major.*`、`attempt.collector.stress.*`、
`recursive_struct.array_gc_*`、`concurrent_alloc.prl_gc_stress`、`map_chunks.prl_gc_stress`、
`shapes.prl_gc_stress_major` …），**普通运行一条没红**。

**所以这一行不是兜底，是承重的。按您原话"同一笔删掉"我们做了，结果是 38 条红；
我们**不删**，并把"为什么它承重"继续读到能说清为止 —— 这是与您原话的第二处偏离，请您裁。**
