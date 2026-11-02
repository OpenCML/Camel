# 六条 milestone 红：`fail 0` 没有撒谎，但它是"这一层没测"的 0

> 2026-11-03 00:25。`h-fold2` 报它在自己树上跑 `test/plans/feat/linear/jit_framed.plan.toml` 读到 `57 / 51 / 6`。这一页回答"是不是它弄红的"，并把结论记成一条债。

## 结论：不是它弄红的，是 milestone 层自己的既有红

在 `develop`（`bf64149aa` = `63bf99814` + 当轮已并的 `lane-m4b` 三笔，全量 `8375/8363/0`）上跑同一个计划文件，原文：

```
[FAIL] framed.operators.gc_stress.fvm                       184.36 ms
[FAIL] framed.operators.gc_stress.jit                       113.10 ms
[FAIL] framed.operators.gc_stress.inline_jit                 90.13 ms
[FAIL] framed.loops.gc_stress.fvm                            85.14 ms
[FAIL] framed.loops.gc_stress.jit                           100.34 ms
[FAIL] framed.loops.gc_stress.inline_jit                     83.35 ms
[SUMMARY] total: 57, pass: 51, fail: 6
```

**六条名字与 `h-fold2` 贴的一字不差**——同名前缀、同六个后端组合。所以 `lane-fold2` 不背这个。

## 为什么"全量 fail 0"里看不见它们

计划文件里这两条自己写着：

```toml
[[tests]]
name = "framed.operators.gc_stress"
# Slow layer: measured 14.5 s in the full serial run.
tier = "milestone"
```

**它们不是 `fast` 层、也不是 `slow` 层的测试，而是 `milestone` 层**（`test/plans` 下 `tier = "milestone"` 的测试现在 **20 条**）。我们每轮的闸门命令是 `-Plan all -Tier fast`，`fast` 层按 `tier` 跳过它们；而 `scripts/plan-inventory.mjs:121` 又把这个**计划**登记在 `FORBIDDEN` 里（理由："its 2 milestone tests are GC stress"），所以连"整份计划被哪一层跑到"这条兜底也不指向 milestone。

⇒ **`fail 0` 是真的，但它只覆盖 `fast` 层；这 6 条红从来没有被任何一层跑到。** 这正是"绿是因为没测到"。

## 债（怎么还）

1. **不要现在修这 6 条**：它们是 GC stress（`--gc-stress safepoint` + `--gc-check verify`），`agents/test.md` 把这一层定成"由协调者在一台安静的机器上跑"的 milestone 层，改它们要先有那一层的基准。
2. **要修的是"看不见"这件事**：`--tier` 的覆盖面应该有一处**能被读到的账**——谁跑 milestone、多久一次、上一次是什么时候、红的是哪几条。今天这个信息只活在某条车道的临时输出里。
3. **短期**：把这条记成具名待办（milestone 层的 6 条 `jit_framed` gc_stress 红），并在 `agents/test.md` 的 milestone 段里写明"这一层不进每轮闸门，所以它的红只能在被点名时看到"。
4. **反面教训写在这里**：**一个"绿"只有在说清它盖住哪一层之后才是绿。** 报 `[SUMMARY]` 时要带上 `-Tier`，这一条以后每轮都写。

## 覆盖账：338 个计划文件，fast 走到 312（A3 车道补，2026-11-03）

上面第 4 条要求"报 `[SUMMARY]` 带 `-Tier`"。这一节把"带 `-Tier` 之后那个数到底盖住什么"量到底：计划文件数、用例数、以及哪些文件被走到却一条用例都不跑。

| 层 | 计划文件 | 今天（`2b30f3cfa` + `bf64149aa`，`-Plan all -Tier fast -Jobs 8`） |
| --- | --- | --- |
| 全树 | **338** | smoke 1 / feat 309 / gpu 6 / perf 21 / debug 1 |
| `-Plan all -Tier fast`（每轮） | **312** | smoke 1 + feat 309 + `gpu/profile`、`gpu/reading`；读到 **8429** 条用例 = 8417 过 + 10 skip + 2 known failure |
| 走不到 | **26** | `SLOW_PLANS` 6、`FORBIDDEN` 18、只剩自己一层可选的 2（`perf/gc_tools` 整份 milestone、`debug/view_escape` 整份 debug） |

三条要一起读的读数：

- **312 里有 5 个文件一条 fast 用例都不出**：`feat/mm/gc_collections`（73 条全是 milestone）、`gc_elder_exhaustion`（22/22）、`gc_pacing`、`gc_soak`、`feat/opt/progen`（2/2）。它们在 fast 的路径下，但每条都写着 slow/milestone ⇒ **"文件被走到"不等于"有用例跑"**，所以"覆盖"必须按用例数报，不能按文件数报。
- **`SLOW_PLANS`（14）与 `FORBIDDEN`（25）不交**（交集 0 个文件）。所以"谁都不跑"的那些**不在两个名单的交集里**，而在 fast 文件内部：那是 9 条单条写着 milestone 的测试。
- `feat/linear` 的 27 个计划文件**全部**被 fast 走到，`jit_framed` 也在其中：它 57 条用例里 51 条属于 fast 层、照常跑，**只有那 6 条 gc_stress 是 milestone**。所以"全量看不见 jit_framed"不是"这份计划没被走到"，而是"其中 6 条按 tier 被跳过"。

## `tier = "milestone"` 到底是几条：9 条单条 + 21 个整文件

逐行数 `tier = "milestone"`：全树 **30 处**。

- **21 处写在计划头**（整份文件归 milestone，`FORBIDDEN` 或它自己的 `tier` 说的就是这个）。
- **9 处写在单条测试上**，分布在 5 个文件（按 runner 展开后端是 **13 次运行**）：`feat/linear/jit_framed` 2 条（`framed.operators.gc_stress`、`framed.loops.gc_stress`，各 3 个后端 = 就是那 6 条红）、`feat/modules/onnx` 4 条（3 条 PyTorch/onnxruntime 比较、1 条 `export_tree_recursion`）、`feat/showcase_i2` 1 条（`diffusion_step.gpu.denoise`）、`perf/fib` 1 条、`perf/startup` 1 条。

"20 条"这个数在今天的树上**对不上任何一组**；能量到的是 **9**（单条）或 **30**（含整文件 21）。核对命令（两分钟，不需要编译）：

```
grep -rn '^tier = "milestone"' test/plans | wc -l        # 30
grep -rn '^tier = "milestone"' test/plans | awk -F: '$2 < 5' | wc -l   # 21（计划头）
```

## milestone 层今天真跑了一遍：13 次运行，6 红 4 绿 3 skip

5 个文件各自按名字跑（命名文件 = 整份跑，`--tier` 不再筛），原始读数：

| 计划文件 | 读数 | milestone 那几条的结果 |
| --- | --- | --- |
| `feat/linear/jit_framed` | `total: 57, pass: 51, fail: 6` | 2 条 × 3 后端，**6 条全红** |
| `feat/modules/onnx` | `total: 157, pass: 126, fail: 0, skip: 31` | 3 条 skip（缺 torch / onnxruntime）、`export_tree_recursion` 过 |
| `feat/showcase_i2` | `total: 5, pass: 5, fail: 0` | `diffusion_step.gpu.denoise` 过 |
| `perf/fib` | `total: 6, pass: 6, fail: 0` | 过（另可注意：它整份是 benchmark，数只对空机器有意义） |
| `perf/startup` | `total: 1, pass: 1, fail: 0` | 过 |

⇒ **这 9 条里今天 6 条红，全部是 `linear.jit_framed` 的两条 gc_stress × 3 个后端；其余 3 条 pass 之外，onnx 的 3 条是按条件 skip（缺 Python 模块），不是绿。子系统只有一个：JIT framed 代码的 GC stress（`--gc-stress` + `--gc-check`）。**

## 那 6 条红的账：不是 GC，是计划里一个从来没跑过的旧写法

6 条红的原文不是断言失败，是**命令行解析失败**：

```
[stderr] Invalid --gc-check verify: '1'. Expected before, after or both.
```

计划里写的（`jit_framed.plan.toml:59` 与 `:171`）是 `--gc-check verify=1`；CLI 收的是 `verify=before|after|both`。两边同一天落地：`e566ff707`（2026-10-31，计划改成用命令行选项设收集器）与 `f6b37bf01`（2026-10-31，收集器参数从命令行读）。**这两行从写下那天起就没成功跑过一次**，而它们归 milestone 层，所以没人发现。

- 归属：计划 sweep 那条车道（`e566ff707`），不是 GC、不是 JIT 后端、不是 `lane-fold2`。
- 修法是一行参数（`verify=1` → `verify=both`），但按上一节的第 1 条，milestone 层要改先有那一层的基准，所以**只记账，不改**。

## 纪律（A3 车道这半句）

**报 `[SUMMARY]` 必须带 `-Tier`；一个"绿"只有在说清它盖住哪一层之后才是绿。** 今天这条纪律有两个具体落点：`fail 0` 要先变成 `-Tier fast → 8429 条 0 红`，再问"那 312 个文件之外的 26 个文件、以及 312 个文件里 9 条 milestone，谁跑"。上一节那种"文件被走到、条目被 tier 跳过"的情形，光看 `fail 0` 是看不出来的。

## 一个文件里有一条跑不动，就把整份文件从所有层里摘掉

`scripts/plan-inventory.mjs:121` 把 `jit_framed` 这**整份计划**登记进 `FORBIDDEN`，理由却是"its 2 milestone tests are GC stress"。这是"**按文件排除，而实际只需要按条排除**"：

- 代价一：理由只能写成以偏概全的一句。`jit_framed` 是 57 条里 2 条、`modules/onnx` 是 157 条里 4 条、`modules/autodiff` 是 263 条里 5 条（slow）——写着"its N milestone tests"，读者会以为其余不用管，而 `FORBIDDEN` 在语义上是"这份文件不进任何一层"。
- 代价二：**`plan_inventory` 那条"每个计划文件都有归宿"的保证，对这三份文件只是逻辑上成立**：归宿指向 milestone 层，而 milestone 层今天没有任何一条流水线在跑（这一页前面量的就是这件事）。有归宿 ≠ 有人跑。
- 建议（**不改，这是工具的设计改动**）：`FORBIDDEN` 从"文件 → 理由"改成"文件 → 条目名单"，或者给 runner 一个能按名字选测试的开关（今天它做不到，`plan-inventory.mjs:75-77` 的注释就是在说这件事：命名文件会把被排除的条目一起启动）。有了按条排除，`jit_framed` 的 51 条 fast 与 6 条 milestone 各归各层，理由也能写准："这 2 条 × 3 个后端是 GC stress"。
- 在改之前，至少让"这份文件里有一条跑不动"这件事**在读数上可见**：闸门跑 `-Plan all` 时把每个文件里被 tier 跳过的条目数打出来（今天只能靠人翻 TOML）。
