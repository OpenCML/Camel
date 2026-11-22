# 六条 milestone 红：`fail 0` 没有撒谎，但它是"这一层没测"的 0

> 2026-11-03 00:25。`h-fold2` 报它在自己树上跑 `test/plans/feat/linear/jit_framed.plan.toml` 读到 `57 / 51 / 6`。这一页回答"是不是它弄红的"，并把结论记成一条债。
>
> **更正（2026-11-03，A3 车道）**：第 1 节的结论"这些是 milestone 层自己的既有红"是错的——那 6 条红不是 GC 失败，是计划里 `--gc-check verify=1` 这个 CLI 不认的旧写法（详见"那 6 条红"一节）。把它改成 `verify=both` 之后，同一棵树上 `total: 57, pass: 57, fail: 0`，**milestone 层今天的红是 0 条**。下面 00:25 的读数照原样保留，只当"当时是怎么找到的"的纪录。教训一句：**一条测试如果从来没跑起来过，它的"红"和"绿"都不是读数——先证明它跑起来了，再读它的结果。**

## 结论：不是它弄红的（那 6 条红也不是既有缺陷，是参数写错）

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

**它们不是 `fast` 层、也不是 `slow` 层的测试，而是 `milestone` 层**（`test/plans` 下 `tier = "milestone"` 现为 **30 行**：21 行写在计划头、也就是 21 个整文件归这一层；9 行写在单条测试上，按后端展开是 13 次运行。此前记作"20 条"，实测 30 行 / 13 次运行，核对命令见"`tier = "milestone"` 到底是几条"一节）。我们每轮的闸门命令是 `-Plan all -Tier fast`，`fast` 层按 `tier` 跳过它们；而 `scripts/plan-inventory.mjs#plan` 又把这个**计划**登记在 `FORBIDDEN` 里（理由："its 2 milestone tests are GC stress"），所以连"整份计划被哪一层跑到"这条兜底也不指向 milestone。

⇒ **`fail 0` 是真的，但它只覆盖 `fast` 层；这 6 条红从来没有被任何一层跑到。** 这正是"绿是因为没测到"。

## 债（怎么还）

1. **~~不要现在修这 6 条~~（已撤）**：原文担心"它们是 GC stress，改它们要先有那一层的基准"。实际那 6 条根本没跑起来（参数写错），改的是一个从没被校验过的命令行，不碰任何断言：改成 `verify=both` 后 `57/57/0`。
2. **要修的是"看不见"这件事**：`--tier` 的覆盖面应该有一处**能被读到的账**——谁跑 milestone、多久一次、上一次是什么时候、红的是哪几条。今天这个信息只活在某条车道的临时输出里。
3. **~~短期：把这条记成具名待办（milestone 层的 6 条 `jit_framed` gc_stress 红）~~（已撤）**：待办没有对象了。留下的是原来那半句——在 `agents/test.md` 的 milestone 段里写明"这一层不进每轮闸门，所以它的红只能在被点名时看到"。
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
| `feat/linear/jit_framed` | 改前 `total: 57, pass: 51, fail: 6`；**改后 `total: 57, pass: 57, fail: 0`** | 2 条 × 3 后端：改前 6 红（参数写错），**改后全绿**（4.79–5.75 s） |
| `feat/modules/onnx` | `total: 157, pass: 126, fail: 0, skip: 31` | 3 条 skip（缺 torch / onnxruntime）、`export_tree_recursion` 过 |
| `feat/showcase_i2` | `total: 5, pass: 5, fail: 0` | `diffusion_step.gpu.denoise` 过 |
| `perf/fib` | `total: 6, pass: 6, fail: 0` | 过（另可注意：它整份是 benchmark，数只对空机器有意义） |
| `perf/startup` | `total: 1, pass: 1, fail: 0` | 过 |

⇒ **这 9 条（13 次运行）今天的结果：0 条红、6 条绿、3 条按条件 skip、4 条绿在 onnx/showcase/fib/startup 上。** 改前读到的那 6 条红不是 GC，是下一节的那个旧参数；**milestone 层今天的红是 0 条。**

## 那 6 条红：不是 GC，是计划里一个从来没跑起来的旧参数（已修）

6 条红的原文不是断言失败，是**命令行解析失败**：

```
[stderr] Invalid --gc-check verify: '1'. Expected before, after or both.
```

计划里写的（`jit_framed.plan.toml:59` 与 `:171`）是 `--gc-check verify=1`；CLI 收的是 `verify=before|after|both`。两边同一天落地：`e566ff707`（2026-10-31，计划改成用命令行选项设收集器）与 `f6b37bf01`（2026-10-31，收集器参数从命令行读）。**这两行从写下那天起就没成功跑过一次**——所以它们的"红"从来不是读数，是"这条测试没跑起来"。按 CLI 自己点名的合法值改成 `verify=both` 之后，同一棵树上 `total: 57, pass: 57, fail: 0`，`framed.operators.gc_stress.fvm/jit/inline_jit` 5.75 / 4.96 / 4.79 s、`framed.loops.gc_stress.*` 4.83 / 3.72 / 3.12 s 全绿 ⇒ **原来那 6 条"既有红"整笔撤销，它们是参数错误，不是 GC stress 失败**。

- 归属：计划 sweep 那条车道（`e566ff707`），不是 GC、不是 JIT 后端、不是 `lane-fold2`。
- 修法是一行参数（`verify=1` → `verify=both`）——**已改**（`jit_framed.plan.toml:59` 与 `:171`），改后 `57/57/0`。上一节第 1 条原来写"不要现在修这 6 条"，按这条读数撤销：修的不是 GC 断言，是一个从没跑起来的参数。

## 纪律（A3 车道这半句）

**报 `[SUMMARY]` 必须带 `-Tier`；一个"绿"只有在说清它盖住哪一层之后才是绿。** 今天这条纪律有两个具体落点：`fail 0` 要先变成 `-Tier fast → 8429 条 0 红`，再问"那 312 个文件之外的 26 个文件、以及 312 个文件里 9 条 milestone，谁跑"。上一节那种"文件被走到、条目被 tier 跳过"的情形，光看 `fail 0` 是看不出来的。

## 一个文件里有一条跑不动，就把整份文件从所有层里摘掉

`scripts/plan-inventory.mjs#plan` 把 `jit_framed` 这**整份计划**登记进 `FORBIDDEN`，理由却是"its 2 milestone tests are GC stress"——而它是 57 条里 2 条（`modules/onnx` 157 里 4 条、`modules/autodiff` 263 里 5 条、`feat/mm/gc` 45 里 43 条）。**按文件排除，而实际只需要按条排除**：464 条 fast 用例因为 58 条非 fast 用例一起被划走。

建议、代价与验收写在单独一页（**不改工具**，这是测试工具的选层粒度设计）：[`2026-11-03-a-whole-plan-excluded-for-one-entry-in-it.cn.md`](2026-11-03-a-whole-plan-excluded-for-one-entry-in-it.cn.md)——`FORBIDDEN` 改记条目名单，前提是先给 runner 一个按名字选测试的开关（`plan-inventory.mjs:75-77` 的注释说明它今天做不到）。

## 教训

**一条测试如果从来没跑起来过，它的"红"和"绿"都不是读数——先证明它跑起来了，再读它的结果。** 这一页的两个发现都是这一条的实例：6 条"红"是参数写错（没跑起来），20 条"挡在 milestone 层"是数错（数出来是 30 行 / 13 次运行）。
