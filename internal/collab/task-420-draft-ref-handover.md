# 交接：`task-420` 非法 draft 引用已复现，探针未做

## 结论等级（照此记账，别升级）

- **已证**：`Pass 'std::nvm' failed with an internal error: The norm input list of node 29 names draft node 4294967295 … (it holds 61, the reserved empty reference being 42…)` 在 `wip/c4-replay` 基上可复现；同一程序在 `0967afd615` 上不报这条（尖端没有重放循环那套改动）。
- **强怀疑（未证）**：`src/stdlib/transforms/transpose/walk.cpp:3633-3638` 显式把 `rt::kInvalidNodeRef` 塞进 `stateInitials`；"检查"只在 `3935-3939`（单一状态累加那条支路）；而 `3898 draft.setNormInputs(loopNode, loopNorm)` 是另一处写表、不共用该检查 ⇒ "检查在一条支路、写表在另一条支路"。这是读出来的强怀疑，不是结论。
- **修法建议（未实施）**：`3898` 之前做一次统一非空校验（或让 `stateInitials` 一填出来就断言非空），不改构造语义。

## 现场

```
树：   D:\Projects\Camel-wt\witness
枝：   lane/draft-ref-oob-wip @ c12c06f527（基 = origin/wip/c4-replay）
构建： BUILD_EXIT=0，binary built 2026-11-28T10:57:56+08:00
        tree /home/zjwei/camel-work/witness，tier fast，jobs 1
git -C 该树 status --porcelain：
  ?? probe_oob.plan.toml
  ?? test/cases/probe/draft_ref_oob.cml
两件都是未跟踪探针，未提交、未推；下一格直接用，别删。
```

## 探针一：`D:\Projects\Camel-wt\witness\probe_oob.plan.toml`（未跟踪，全文）

```toml
id = "tmp.oob"
order = 900
tier = "fast"
tags = ["probe"]

[[tests]]
name = "oob_nvm"
kind = "behavior"
case = "./test/cases/probe/draft_ref_oob.cml"
args = ["${case}", "std::nvm"]
expect_exit = 0
expect_stdout_contains = ["__NOPE__"]
timeout_ms = 120000
```

（`expect_stdout_contains` 故意写一个不存在的串，让失败时把完整 stderr 打出来。）

## 探针二：`D:\Projects\Camel-wt\witness\test\cases\probe\draft_ref_oob.cml`（未跟踪，全文）

来源：`test/cases/compile/transforms/transpose_loop_condition_reads_state.cml`（`wip/c4-replay` 上的用例）**原样拷贝**，原用例一字未改。

```camel
// A loop whose condition reads its own state, and whose body leaves no residual array behind: the
// number of rounds is decided by a value only the rounds themselves produce.
//
// `halve(x)`: `s` halves while it is above 1.0, so at x = 16 it takes four rounds (16, 8, 4, 2, 1)
// and answers 1.0. On paper the pullback at cotangent 16.0 is 1.0: the function is `x / 16` there,
// so its derivative is 1/16 and 16 * 1/16 = 1. The transpose has to know how many rounds there were
// before it can say that, and the count is not in the graph as a constant: it is whatever the
// condition decided.
//
// The loop carries three states because the reverse side reads its states as the loop's own ports
// and a loop whose port list is too short to hold one is refused before the round count is asked
// for at all; `t` and `u` are carried unchanged so the case is about the round count and not about
// what the rounds compute.
import rulesets.transpose
import { transpose } from transforms

func halve(x: float) => float {
    let (s, t, u) = loop s = x, t = 0.0, u = 0.0 while s > 1.0 do (s / 2.0, t + 1.0, u)
    return s + t * 0.0 + u * 0.0
}

func main() sync {
    let back = transpose(halve)
    // x = 16: four rounds, the function is x/16 there, so its derivative is 1/16.
    println(halve(16.0))
    println(back(16.0))
    // x = 48: six rounds (48, 24, 12, 6, 3, 1.5 -> 0.75), the function is x/64 there, so the
    // cotangent 64.0 pulls back to 1.0 and the value itself is 0.75.
    println(halve(48.0))
    println(back(64.0))
}
```

## 复现命令与原文（已跑过，读数如下）

```
internal/tools/camel-remote.ps1 -Path D:\Projects\Camel-wt\witness -Plan probe_oob.plan.toml -Jobs 1 -SlotWaitSeconds 1800 -NoGpuWatch
BUILD_EXIT=0
[provenance] binary built 2026-11-28T10:57:56+08:00 | tree /home/zjwei/camel-work/witness | tier fast | jobs 1
[FAIL] oob_nvm   158.89 ms
[stdout] (empty)
[stderr] ():?:?: [Error]: Pass 'std::nvm' failed with an internal error: The norm input list of node 29 names draft node 4294967295, which is not one of the 61 nodes this graph's payload plan holds (it holds 61, the reserved empty reference being 42…
```

## 下一步（两步探针，钉死路径 1 还是路径 2）

1. **`walk.cpp:3898` 之前**：在 `draft.setNormInputs(loopNode, loopNorm)` 前打印 `loopNorm.size()` 与每一位，标出哪一位是 `0xFFFFFFFF`（`rt::kInvalidNodeRef`）——这一条直接回答"非法引用是不是就在写进 loop 的 norm 表的那批里"。
2. **`walk.cpp:3633-3638`**：打印每个状态的 `at`、`oldNorm.size()`、以及该轮是否落到 `kInvalidNodeRef` 分支——这一条回答"哪一位的初值缺位、缺在哪一路"。

两条打印都要**按名删净**（探针不留提交）；跑完仍用上面那条命令（`-Plan probe_oob.plan.toml`），无需重建其它东西。

## 边界

- 不改 `walk.cpp` 的构造逻辑、不动 `src/stdlib/autodiff/` 引擎体、不推远端；
- 探针文件只作未跟踪件留在这棵树里，供下一格直接用；
- `task-419`（条件读状态的循环反向）与本条分开做。