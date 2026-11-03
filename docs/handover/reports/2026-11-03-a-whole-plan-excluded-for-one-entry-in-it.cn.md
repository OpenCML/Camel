# 一个文件里有一条跑不动，就把整份文件从所有层里摘掉

> 2026-11-03，A3 车道。`scripts/plan-inventory.mjs` 的 `FORBIDDEN` 是按**整份计划文件**登记的，而登记的理由常常只是"这份文件里有 N 条测试归 milestone 层"。这不是设计失误的边角，它决定了一个测试文件是"部分被看见"还是"整体消失"。**这一页只写建议，不改工具**——按条排除要先给 runner 一个今天它没有的能力。

## 现象：一条测试的层，决定了整份文件的去处

`scripts/plan-inventory.mjs:121`：

```js
{ file: 'test/plans/feat/linear/jit_framed.plan.toml', reason: 'its 2 milestone tests are GC stress (`--gc-stress safepoint` + `--gc-check verify`)' },
```

读数（`loadPlanTests` + `expandSweeps` + `testTier`，与闸门同一套函数）：

| 计划文件 | 用例总数 | fast 层该跑的 | 非 fast（原因是它被登记进 `FORBIDDEN`） |
| --- | --- | --- | --- |
| `feat/linear/jit_framed` | 57 | **51** | 6（2 条 gc_stress × 3 后端，milestone） |
| `feat/modules/onnx` | 157 | **153** | 4（PyTorch/onnxruntime 比较，milestone） |
| `feat/modules/autodiff` | 263 | **258** | 5（`*_vs_torch64`，slow） |
| `feat/mm/gc` | 45 | **2** | 43（GC stress，milestone） |

四行加起来：**464 条 fast 用例，因为 58 条非 fast 用例，被"整份文件"这个粒度一起划走。**

要先说清楚一件事，免得把账记错：**每轮的 `-Plan all -Tier fast` 并不会因此少跑**——它按目录走，走到 `test/plans/feat` 就进了这些文件，按 `tier` 逐条筛，那 464 条照跑（今天 `total: 8429` 里就含 `jit_framed` 的 51 条）。会少跑的是**发布闸门**（`scripts/release-gate.mjs`）：它按 `FAST_TARGETS + SLOW_PLANS` 点名，而 `FORBIDDEN` 里的文件不进任何一份名单——**"这份文件不进任何一层"这句话在闸门的账上是字面成立的**。

## 三条代价

1. **理由只能写成以偏概全的一句。** "its 2 milestone tests are GC stress" 读起来像"这份文件主要是 GC stress"，而实际是 57 条里的 2 条；`onnx` 更是 157 条里的 4 条。读者按理由判断该绕开哪块地，就会绕开一块好地。
2. **"每个计划文件都有归宿"这条保证，对这几份文件只是逻辑上成立。** 归宿指向 milestone 层，而 milestone 层今天没有一条流水线在跑（同页量过：9 条单条 milestone 测试 + 21 个整文件，无人跑）。**有归宿 ≠ 有人跑。**
3. **错误会一直藏到有人点名。** `jit_framed` 那两条 gc_stress 的参数从 2026-10-31 写错到 2026-11-03 被发现（`--gc-check verify=1` 与 CLI 的 `before|after|both` 不一致，测试从来没跑起来过），靠的是另一条车道碰巧按名字跑了这份文件。

## 建议：按条排除，而不是按文件排除

1. **`FORBIDDEN` 从"文件 → 理由"改成"文件 → 条目名单（或理由 + 条目名单）"**：登记的是"`feat/linear/jit_framed` 的 `framed.operators.gc_stress`、`framed.loops.gc_stress`"，其余 51 条照常归 fast 层。理由也就能写准：这 2 条 × 3 个后端是 GC stress。
2. **前提是给 runner 一个"按名字选测试"的开关**。今天它做不到，`scripts/plan-inventory.mjs:75-77` 的注释就是在说这件事：*"the runner cannot select a test by name, so naming such a file would start the excluded tests too (the ONNX row)"*。所以顺序是：先给 runner `--skip-test <plan>:<name>`（或 `--only-test`），再让 `FORBIDDEN` 记条目。没有第一步，第二步只是把"整份跑"换成"整份不跑"，问题不变。
3. **在开关落地之前，先让"这份文件里有几条被跳过"可见**：闸门跑 `-Plan all` 时，按文件打出"读到 N 条、跑 M 条、按 tier 跳过 K 条"。今天的读数里只有 8429 这个总数，`文件被走到` 与 `条目被跳过` 是两件事，看总数分辨不出来。

## 验收

按条排除落地后，应当能读到：`jit_framed` 的 51 条进 fast 层的读数、6 条进 milestone 层的读数，两者在各自的 `[SUMMARY]` 里分开计数；`FORBIDDEN` 的理由行能直接对着文件名与条目名核对；发布闸门的 fast 层不再因为 4 条 PyTorch 比较就整份丢掉 `onnx` 的 153 条。

**不改工具的理由**：这是"测试工具的选层粒度"设计，牵动 `FORBIDDEN`、`SLOW_PLANS`、闸门的三处账与它自己的自检（`test/tools/selftest/over-selection.mjs` 就在测"有没有少选或多选"）。先有这一页与上面的读数，再决定改哪一层。