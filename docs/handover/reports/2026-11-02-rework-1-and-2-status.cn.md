# 返工 1 与返工 2 在这棵树上的状态：四件都已落地，证据逐条

**2026-11-02，作者 23:20 分拣 58 页后点名的两项返工。** 我按"先量再动"核了一遍，结论是**四件在 `origin/develop` 并在本车道（`bc28c4f3d` 合并后）都已落地**，不需要再改代码。下面每一条都附可复查的命令与结果。

## 返工 1：两个环境变量开关

| 变量 | 在 `src include modules stdlib` 的命中 | 结论 |
|---|---|---|
| `CAMEL_RULESET_INDEX` | **0** | 已改成遍输出（`fdb4a9ae9`，作者在 `questions-2026-10-29.md:788` 已"收下"） |
| `CAMEL_TILES_TEST_REASONS` | **0** | 已清 |

复查命令：
```
git grep -n 'CAMEL_RULESET_INDEX\|CAMEL_TILES_TEST_REASONS' -- src include modules stdlib   → 0 命中
```

- **`docs/cli.md` 没有提到它们**（`git grep -n 'CAMEL_RULESET_INDEX\|CAMEL_TILES_TEST_REASONS' -- docs/cli.md` → 0），所以"同笔删"无事可做；
- `docs/` 里剩下的 7 处命中全在**历史记录**（`docs/handover/questions/README.md`、`today-state-one-number-a-row.md`、两份 reviews、`test/cases/compile/rulesets/rulesets_lookup.py` 里那行 `env.pop(...)` 是**测试脚本显式清掉它**，正是"已不需要它"的证据）。**历史记录不改写**，只在这里记一笔。

## 返工 2：`taskflow.cpp` 的 `DESIGN-QUESTION` 与 3.6

1. **`DESIGN-QUESTION` 那条已经不在了**：`git grep -n 'DESIGN-QUESTION' src/passes/sched/taskflow/taskflow.cpp` → **0 命中**；原位置（`taskflow.cpp:2790` 起）现在是一段陈述性的注释，逐字就是作者的裁定："elements that have started run to their end; of the elements that have not started, those above the smallest fault known do not start and those below it still do; the fault of the smallest index is reported."
2. **原则 3.6 的措辞就是作者给的那句**（`docs/design-principles.cn.md:199`）："已经开始的元素跑完；下标大于已知最小故障、还没开始的元素不再开始，下标更小的仍会开始；报下标最小的故障。"
3. **"部分故障、四后端同判"的用例已经在树上**，而且不止一条：
   - `test/plans/feat/consistency/attempt.plan.toml` 的 **`attempt.partial_fault`**：`desc` 原文 "One element of a parallel map faults at once while the elements below it are still queued (element 40 of 64…)… every element below it starts and runs to its end, whether or not it had started, the fault reported is element 40's, and **the four back ends agree** -- design principles 3.6 as ruled on 2026-10-31."；`case` = `test/cases/consistency/attempt/partial_fault.cml`，`kind = "behavior"`，带 `backends_agree`。
   - **`attempt.lowest_index`**（`lowest_index.cml`）：多元素同时故障、`some()` 是"5 以上故障、5 以下跑完"的部分故障，`desc` 写明"the same on every run and on the four back ends"。
   - **`fault.parallel_lowest_index`**（`faults.plan.toml:426`）：全部元素故障时的最小下标。

## 结论

**这一轮不动代码。** 若作者要的是"另一条**新的**部分故障用例"，请说明它要钉什么（例如"哪些已开始元素跑完"的**顺序**，今天 `attempt.partial_fault` 明确不钉：`desc` 里写着 "What became of the elements above 40 is not asserted: one that had started ends, and which had started is the scheduler's"）。**在那之前，新增一条只是重复。**

## 一处要记的差别

作者原文写 `CAMEL_RULESET_INDEX`（`program.cpp:662`）与 `CAMEL_TILES_TEST_REASONS`（`region_test_pass.cpp:302`）；这两个文件路径在本树上分别是 `src/compile/gir/program.cpp`（`:662` 现在是别的代码）与 tiles 的测试遍文件——**按符号复查，不按行号**（同 P0-1 那次的教训：行号会漂，符号不会）。