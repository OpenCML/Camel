# 删掉 `clearMarks()` 的整空间扫：38 条新红，注释那句"会被跳过、会漏边"被实测证实

**状态**：**已回退**。这是"删除 `GraphAllocator::iterateAllocated` 那一行"的实证记录，不是一次成功的删码。

## 1. 做了什么（两步，各自一跑全量 fast）

1. **先补一条"按根走"的清位**（`clearMarks()` 末尾）：用与 `markPhase()` 同一组根
   （`objectRootSets_`、`externalRootTracers_`、`rootHandles_`）做一次遍历，把**可达对象**的 mark 位清掉；
   mark 位兼作"已访问"标记，所以环不会死循环。**不登记任何东西、不钉住任何图** —— 可达性由 Function 值决定。
   此时**整空间扫还在**（冗余但行为中性）。
2. **然后删掉整空间扫**（`graphSpace().iterateAllocated(...)` 那五行注释 + 一行调用），只留根集那一段。

## 2. 读数（同一棵树、同一 wrapper、`-Plan all -Tier fast -Jobs 8`）

| 步骤 | `[SUMMARY]` |
| --- | --- |
| 删码笔之后（基线） | `total: 8514, pass: 8450, fail: 44, skip: 10, known failures: 10` |
| ① 加了根走清位、整空间扫仍在 | `total: 8514, pass: 8450, fail: 44, skip: 10, known failures: 10`（**逐项相同**） |
| ② 删掉整空间扫 | `total: 8514, **pass: 8412, fail: 82**, skip: 10, known failures: 10` |

**新增的 38 条红**（`[FAIL]` 名字逐条比）：`recursive_struct.array_gc_nvm` / `_fvm`、
`concurrent_alloc.prl_gc_stress`、`map_chunks.prl_gc_stress`、`shapes.prl_gc_stress_major`、
`json.gc.forced_major.{nvm,fvm,prl}`、`json.gc_macro.forced_major.{nvm,fvm}`、
`attempt.collector.stress.{nvm,fvm,jit,prl}`，以及同族的其余 `*_gc_stress` / `forced_major` 条目。

## 3. 判读

- 新红**全部落在"强制大回收/回收压力"这一族**（`forced_major`、`gc_stress`、`*_major`），
  **没有一条落在普通运行上**。这与注释说的失效方式一致：**一次大回收跳过"上一轮已经标过的幸存者"，
  于是不再走它的边，边另一头的对象被当成垃圾**；只在反复强制大回收时才暴露。
- 所以注释里 `a later collection would skip them and miss their edges` **不是推测，是实测**；
  那一行整空间扫是**承重的**，不是兜底多余物。
- **我那条"按根走"的清位没有替代它**：它只清**从根可达**的对象，实测覆盖率不如整空间扫。
  缺在哪一条路径上**我还没读出来**（候选：`updateRefs` 在根种子 `type == nullptr` 时走不全聚合体的槽、
  或某条边只在 `markObject` 内部被追踪而不在根遍历里）—— 这是下一步要读的东西，
  **在读到之前不再动这一行**。

## 4. 现状与下一步

`src/core/mm/gen.cpp` 已 `git checkout` 回退到删码笔的状态（工作树干净，`HEAD` 未变）。
下一步若要真删，得先把"哪条路径上的 mark 位没被根走清掉"读出来 —— 那就是 `clearMarks` 的整空间扫
唯一真正在做的事；在那之前，**这一行留着是对的**。

**已推进到：实证**（删即红 38 条、已回退；缺的路径待读）。
## 5. 这一步的唯一读数

**44 → 82**（`fail` 数），两次跑之间**只有一个变量**：删掉 `graphSpace().iterateAllocated(...)` 那一行
（以及它的五行注释）。`total` 8514、`pass` 从 8450 掉到 8412、`skip` 10、`known failures` 10 都没动，
所以这 38 条的差值就是那一行在做的事。
