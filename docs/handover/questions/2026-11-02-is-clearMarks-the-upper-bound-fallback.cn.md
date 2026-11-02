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

## 7. 实验做不成的那一步：**根集注册表里根本没有类型可传**

按要求，下一步是把根种子带上"注册时该有的类型"，再跑 `json.gc.forced_major.*` 一族。读注册表后这一步**判死**：

```cpp
// include/camel/core/mm/gen.h:581
struct ObjectRootSetEntry {
    const void *owner = nullptr;
    std::string name;
    std::vector<rtdata::Object *> *set = nullptr;
};
```

**没有 type 字段。** 所以 `markPhase()` / `clearMarks()` 给根种子传 `nullptr` **不是疏忽，是注册时就丢掉了**
—— `registerObjectRootSet`（声明 `gen.h:335`、定义 `gen.cpp:157`）收的就是 owner/name/set 三样，
主调用点是 `src/core/context/context.cpp:432`（注销在 `:320`）。

结论三句：

1. **候选 ① 的缺口在代码里是真的**（`Tuple::updateRefs` 在 `type == nullptr` 时一个槽都不走），
   但**补它不是一行**：要么给 `ObjectRootSetEntry` 加类型、并让每个注册点交出来，要么改 `Tuple::updateRefs`
   在没有类型时用值的运行时形状走槽（不安全：分不清哪个槽是引用）。**这属于"另一条要修的东西"，不是这一笔能补的。**
2. 因此"38 → 0 还是不动"这个 A/B/C **我没有运行**：实验的前提（能传类型）在本树不成立，
   跑它只会得到"还是 38 条"，那不是判据，是同一件事再说一遍。
3. **下一步的读物很具体**：`registerObjectRootSet` 的每个调用点**有没有类型可交**。有的（例如上下文持有
   一份带类型的根表）就先补它；没有的，这一行整空间扫就要留着 —— 它在替"根集不知道类型"这件事兜底。

**已推进到：判死（实验前提不成立）+ 定位到注册表。**
## 8. 三栏数完了：**A 栏 0 / 2** —— 加 type 字段救不了这条红

`git grep -n 'registerObjectRootSet(' -- src include modules` 的**真实调用点只有两个**（其余是声明、定义、注销）：

| 调用点 | 注册的 `set` 是什么 | 那一层手上有没有类型 |
| --- | --- | --- |
| `src/core/context/context.cpp:432` `registerObjectRootSet(ctx.get(), "runtimeGraphRoots", &ctx->runtimeGraphMgr_->gcRoots())` | `runtimeGraphMgr_->gcRoots()`，元素是 **`Program *`**（`src/runtime/program.cpp:186` 与 `:348` 塞入，`:345`/`:462` 清空） | **C 栏**：集合是**同质**的图载体，不需要数据类型 |
| `src/passes/test/contexts.cpp:81` `registerObjectRootSet(&ownRoots, kOwnRootSetName, &ownRoots)` | 测试自己的 `ownRoots`，只用来数条目数（`countRootSets`） | **C 栏**：测试探针，与类型无关 |

- **A 栏（有类型可交）：0 个** —— 没有任何调用点手上握着一个可以交出去的 `type::Type *`。
- **B 栏（类型在更上层被丢掉）：0 个** —— 不存在"上层有类型、注册时丢了"的情形；注册进去的本来就是**图载体**，
  它们（`Program` / `GCGraph`）在 `updateRefs` 里不需要数据层类型，跟 `Function::updateRefs` 同形。
- **C 栏（同质 / 无类型需求）：2 个，占全部调用点。**

**结论（这条线的终点）**：**给 `ObjectRootSetEntry` 加 type 字段也救不了那 38 条红** —— 没有调用点能交出类型，
而根集里装的是同质图载体，它们不靠类型也能追。所以：

> **整空间扫是当前唯一正确的做法**，它兜的不是"根集缺类型"，而是**根遍历这条路本身覆盖不到图空间里的某些 mark 位**
> （第 3 节实测：删掉就 38 条红，全部落在强制大回收一族）。要真删，得先回答"哪些 mark 位根遍历清不掉"，
> 而那已经不是注册表这一层的问题。

**这一线到此为止**（三页、四次读数、一个判死的实验、一张三栏表）。作者若要真删这一行，
读物是"`markObject` 追到的边 ⊆ 根遍历追到的边吗" —— 目前已知**不⊆**，实测为证。

**已推进到：终点（A 栏 0/2，加字段无用；整空间扫保留）。**
## 9. 差集（C 栏）：**markObject 只在 markPhase 里被调**，于是"根遍历走不到"的不是 markObject 的边

只读，不改代码。三件事：

- **A 栏（markObject 里的 relocate 点）**：`markObject` 自己只有一个 relocate 点 ——
  `current->updateRefs([...]push..., entry.type)`（`gen.cpp:1525`），它追的对象**全部来自"当前对象的槽"**，
  没有表、没有图空间的特殊通道。
- **B 栏（根遍历的种子）**：`objectRootSets_`（元素是 `Program *`）、`externalRootTracers_`、`rootHandles_` ——
  三条路，`markPhase` 用的就是它们（`gen.cpp:1440`、`:1460`、`:1463`），而 `markObject` 的**仅有两个调用点**
  也都在 `markPhase` 里（`:1446`、`:1456`）。
- **C 栏（差集）**：**"markObject 追到的边"不可能超出"markPhase 能到的边"** —— 两者就是同一次遍历。
  所以更强那句（"markObject 内部才追的边，根遍历走不到"）**不成立**；我上一轮那句"已知不 ⊆"**判错，撤回**。

**真正的差集在别处，而且有名字**：`minorTracedGraphs_`（`include/camel/core/mm/gen.h:631`，
`std::unordered_set<rtdata::Object *>`）。**图空间里的对象会在小回收（minor）里被打上 mark** ——
它们由**年轻代的引用追踪**标记，不经 `markPhase`：

- `gen.cpp:991` `if (minorTracedGraphs_.insert(rootObj).second) { ... }`（小回收从年轻根出发追图载体）；
- `gen.cpp:1367` `if (graphSpace().isAllocated(ref) && minorTracedGraphs_.insert(ref).second)`（年轻引用边里的图载体）；
- `gen.cpp:971` / `:1043` 两处 `minorTracedGraphs_.clear()` —— 说明这个集合本来就是"这轮小回收碰过的图"。

**所以那 38 条红的机制是**：小回收给图空间对象打了 mark，`clearMarks()` 的整空间扫把它们清掉；
换成"根遍历清位"后，**这些对象的 mark 位没有任何一条根路径能走到**（它们的参照者可能已经被年轻代回收掉了），
于是残留到下一次大回收，被 mark 逻辑当成仍然有效 —— 只在反复强制大回收/压力场景里暴露。

**下一笔可执行的那一句**：`clearMarks()` 里**遍历 `minorTracedGraphs_` 的成员逐个 `unmark()`，并清空该集合**
（再配根遍历 + 各空间的整表清位），然后才谈删整空间扫。这是用一个**精确集合**替换一个**粗集合**，
形状与"根遍历 + `objectRootSets_`"一致，**不是新增机制**。

**已推进到：差集定位（`minorTracedGraphs_`；上一轮"不 ⊆"那句话撤回）。**