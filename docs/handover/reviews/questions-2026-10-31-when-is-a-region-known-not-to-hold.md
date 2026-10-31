# 一块 region 什么时候算"不成立"？

> 状态：**已裁**（2026-10-31，`fsh-dev = 874af53ff`）——「不成立」包含「编不出来」，判据不变；`openPending`/`assignId` 已按裁定删除。留档于此。
> 相关：`docs/handover/questions/questions-2026-10-31-gir-handle-names-a-transaction-or-a-draft.md`、
> `modules/tiles/src/region_outline.cpp`

## 一句话

**region 的"不成立"能不能不看编译结果来判？**（例如只按 `regionInScope` + 区域自身的静态事实）

## 今天的次序（`modules/tiles/src/region_outline.cpp`）

```
388  auto extracted = extractRegion(*graph, region);
390  extracted.graph->setId(context.graphIds().allocate());   // 命名（分配 id）
391  auto *callee = extracted.graph->encode(name, name);       // 编码
392  auto *fn     = ::Function::create(... autoSpace());       // 进 autoSpace
401  (void)compileTypes(fn, context.facts());                  // 编译（判定所需的输入在这里才存在）
402  } catch (const UnsupportedComputation &e) {
407      if (!regionInScope(*graph, region)) continue;         // 丢弃的决定
```

**"丢弃"今天的定义是**：这块 region **编不出来**（`compileTypes` 抛 `UnsupportedComputation`）**且**不在 `#gpu` 作用域内（`regionInScope` 为假）。**两者缺一不丢**：作用域内的 region 编不出来要报 `GpuScopeUnsupported`（`:419-428`），作用域外的编不出来才跳过。

## 为什么这决定 `openPending` 有没有位置

`GraphTransaction::openPending()` / `assignId()`（`166ccbfcd`）的意义是**"先造、后命名"**：块被判定成立才分配 id，因此**分配时刻不变、图的编号顺序不变、黄金文件一字不改**。

但 `region_outline.cpp` 里**每一次丢弃的决定都发生在编译之后**，而编译要 `fn`、`fn` 要 `encode`、`encode` 要 id。**所以在这个模块里，"不成立"今天的判据本身就依赖命名之后才存在的东西**——`openPending` 一次都没有调用者，不是忘了用，是**没有位置可放**。

## 两个分支的后果

* **能（不看编译结果也能判"不成立"）**：我立刻按"先判定后命名"改，`openPending` 就有位置，那条资源账用例（`before / during / after` 三个数，`during ≠ before` 且 `after == before`）当场能写。
* **不能（"不成立"必须包含"编不出来"）**：`openPending` 在 `tiles` 里**没有位置**，那条资源账用例**应当撤销，而不是硬写一条恒真的空话**；`openPending` 留给真正"先造后命名"未来的调用者（如果有）。

## 附：这件事的由来

作者 2026-10-31 的裁定是"加一个待命名的入口（`openPending`），`assignId` 仍在 attach 时调用；分配时刻不变，所以图的编号顺序不变，黄金文件一字不改"，并要求一条用例："抽出一块 region、判定它不成立、丢弃它，前后 GC 空间的读数相同"。

那条用例**今天写不出来**，卡在本题上：**判定不成立这件事，今天需要命名之后才能做的编译**。