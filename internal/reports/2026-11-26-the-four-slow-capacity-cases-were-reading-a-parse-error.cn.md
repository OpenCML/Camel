# 2026-11-26 四条慢层容量用例今天对容量没有读数：红的是输入

## 一句话

`capacity.scale_check`、`capacity.growth_fvm_gc_stress`、`capacity.growth_simplify_gc_stress`、
`capacity.dead_function_closure_verify` 这四条慢层用例红的不是容量、也不是 GC：它们的程序由
`test/tools/scale/scale.py` 生成，而生成器仍在写 2026-11-23 记法裁定之前的函数头
（`func f0(x: int): int`），每个程序都停在第一行第 16 列的冒号上，**一条也没有跑到自己的断言**。
把生成器的六行拼法改过来以后，同一个计划、同一条尖端从 `18/7/11` 变 `18/18/0`，四条全绿；
它们的段数读数是 `graph/perm/meta = 6/5/5`、`8/6/5`、`12/11/8`，所以断言里那个 `>= 3` 的余量是
**2 到 5 段** —— 它是这一族用例的**设计参数**，不是某次运行的记录值，不该因为读数去调。

## 读者须知

> 这一页不是"容量回归"。四条用例的产能断言（每个空间至少 3 段、`OK mixed n=500`、`OK mixed n=100`）
> 在改拼法之前**一次也没有被执行过**：程序在编译的第一行就停了。所以任何"慢层容量族全红"的读法，
> 从 2026-11-23 到 2026-11-26 之间都只能说"这四条没有读数"，不能说"容量坏了"。

## ① 四条改拼法之前的失败首行（计划文件 `test/plans/feat/mm/capacity.plan.toml`）

```
capacity.scale_check                  expected exit 0, got exit 1
  FAIL arith n=500 ... exit 1, expected 'scale total=126744':
    scale_arith_500.cml:1:16: [Error]: Unexpected ':'; expected a declaration or the end of the file.

capacity.growth_fvm_gc_stress         expected exit 0, got exit 1 / stdout does not contain 'OK mixed n=500'
  verification: no segment count of the graph space in: FAIL mixed n=500 passes=[... std::fvm] ... exit 1:
    scale_mixed_500.cml:1:16: [Error]: Unexpected ':'; ...

capacity.growth_simplify_gc_stress    同上（passes=[... std::opt std::nvm]），同一行同一列

capacity.dead_function_closure_verify expected exit 0, got exit 1 / stdout does not contain 'OK mixed n=100'
  FAIL mixed n=100 passes=[... std::opt std::nvm] ... exit 1:
    scale_mixed_100.cml:1:16: [Error]: Unexpected ':'; ...
```

`1:16` 就是 `func f0(x: int): int` 里返回值前面那个冒号。裁定之后返回值写在 `=>` 后面，这个头不再能被读。

## ② 为什么没有任何一处读数发现它：三层同时是盲的

1. **静态 lint 看不见**。`test/tools/spelling/check.py` 的 "result-type colon" 规则是
   `func\s+(?:operator\s+\S+|\w+)\s*…\([^)]*\)\s*:\s*[A-Za-z_(]`，它要求 `func` 后面跟一个**字面名字**；
   生成器写的是 `func f{i}(x: int): int` 与 `func sum{c // chunk}(): int`，名字里的模板洞 `{i}`、`{c // chunk}`
   让规则在 `{` 处就断了。按这条正则在该文件上跑：改前 **0 命中**、改后 **0 命中** —— 也就是说
   2026-11-23 那次扫漏的六行，从一开始就在 lint 的视野之外。
2. **快层不选它们**。四条全是 slow 层（`capacity.plan.toml:37`、`:201`、`:277`、`:291`），
   `feat/mm` 的快层跑 12 条，这四条一条都不在里面。
3. **唯一会编译生成器输出的地方就是这个计划**。那次扫描的推理是"每个写程序的地方都写箭头，跑它们的计划就是读数"，
   在这一个文件上恰好是空的：它写的程序没有任何一层在跑。

## ③ 前后读数（同一棵树、同一条二进制，显式计划文件路径，`[TREE] clean`）

```
树 D:\Projects\Camel-wt\frame-inlines    binary built 2026-11-26T20:49:08   tier fast   jobs 8
计划：-Plan test/plans/feat/mm/capacity.plan.toml（18 条；不吃目录，所以禁跑的 mm.gc_poison_sweep 不在里面）

961fc03410（未改）                 [SUMMARY] total: 18, pass: 7,  fail: 11   RUN_EXIT=1
a76209bd22（生成器六行改拼法）    [SUMMARY] total: 18, pass: 17, fail: 1    RUN_EXIT=1
ba7387e89e（用例一行改转换）      [SUMMARY] total: 18, pass: 18, fail: 0    RUN_EXIT=0
```

四条现在的判词行与时间、峰值（`ba7387e89e`）：

```
[PASS] capacity.scale_check                    4.42 s   9 of 9 scale programs ran; PASS，单程序 0.18–1.22 s、58–190 MB
[PASS] capacity.growth_fvm_gc_stress           1.21 s   OK mixed n=500 1.01s peak=68MB
[PASS] capacity.growth_simplify_gc_stress     12.49 s   OK mixed n=500 12.22s peak=109MB
[PASS] capacity.dead_function_closure_verify   3.83 s   OK mixed n=100 3.61s peak=69MB
```

（`scale_check` 的那 9 条里最慢的是 2000 个函数的 mixed，1.20–1.22 s、188–190 MB；这与计划 desc 里记的
"0.07 to 0.5 s each, 49 to 226 MB" 不是同一台机器，那组数字是记录值、且没有被断言。）

## ④ `>= 3` 是设计参数，不是记录值（行号）

| 写死的数字 | 位置 | 性质 |
|---|---|---|
| `assert(Number(m[1]) >= 3, …)` | `capacity.plan.toml:172, :194, :219, :241, :263` | **设计参数**：desc 说的性质是"space 长到 several segments"（`:158` 起），`--space-segment-kb 32` 在同一句里被写明是 "a test control"；3 与这个控制量是一起选的（desc 还留着"64 KB 时 perm 已不再到 3 段"的理由） |
| `--space-segment-kb 32`、`n=500`、`kind=major` | `:162, :184, :209, :231, :253` | 测试控制量 / 工作量（设计参数） |
| `--space-segment-kb 1`、`kind=minor`、`n=100` | `:269–:276`、`:284` | **设计参数**，用例自己写死："The parameters below are the case's proof, not tuning … Do not 'speed it up' by weakening any of the three." |
| `graph_limit_mb=256` / `perm` / `meta` | profile 行 | 运行时选项默认值（设计参数），计划不断言 |
| `9 of 9 scale programs ran` | `:31` | 固定集的大小，不是阈值 |

余量实测（三次运行、三遍同值）：

```
capacity.growth_fvm_gc_stress       graph=6  perm=5  meta=5
capacity.growth_simplify_gc_stress  graph=8  perm=6  meta=5
capacity.dead_function_closure_verify  graph=12 perm=11 meta=8   （这一条不断言段数）
```

⇒ 谁以后想把 `>= 3` 调大或调小，先看这张表：断言的最低值是 5，余量 2 段；`>= 3` 通过的原因是
"空间确实在多段上增长"，不是"刚好擦过"。**这四条的两处数字（`>= 3` 与 test control）本次一个字未动。**

## ⑤ 两笔改动

- `a76209bd22` — `test/tools/scale/scale.py`：五个 shape 函数头（`:61,64,70,75,80`）与分块助手（`:102`）
  改成 `=> int`。这是 2026-11-23 那次扫描漏掉的六行；改的是**测试材料**（生成器写的程序），不是引擎。
  同一个生成器写出的程序、期望值、总数、test control 全不变。
- `ba7387e89e` — `test/cases/opt/generic/fold_constant_clone_limit.cml:15`：`let a: int[] = 0 .. n` 改成
  `let a: int[] = to_array(0 .. n)`。本意由两处决定：用例自己的注释说"the array an interval **converts to**"，
  而 prelude 说 `to_array` 是"the one conversion from a range to the integers it names"（`src/stdlib/prelude.cml:1094-1100`）。
  改后的诊断原文从 `not the declared type 'int[]'` 变成它该报的那条：

```
[Error]: Out of memory in pass 'std::opt constant_budget=100000000': The block space (data blocks) is
exhausted: an allocation of 15.3 MB does not fit, 15.3 MB of the limit of 24.0 MB are in use; raise it
with --block-limit=<megabytes> (it is 24 now). … (name=RuntimeDiag::MemoryExhausted, code=0x04000014)
```

  也就是用例要测的那件事本身：一份 15.3 MB 的折叠常量放得下，第二份放不下。

## ⑥ 仍然红的两处（不在本次范围）

- `trans.source_spelling / every_generator_writes_source_the_parser_reads` 在 `ba7387e89e` 上仍红：
  `SOURCE-SPELLING FAIL: 6 of 14 readings`，六处命中都在本次没碰的文件里 ——
  `src/kernel/diagnostics/registry.tsv:124,419,421,423`（untyped paren lambda 的散文）与
  `test/cases/trans/spelling/greedy.cml:148,157`。它本来就在 2026-11-26 晋级红表里。
- 那条 lint 对模板洞 `f{i}` 仍然看不见（② 的第一层）。要让这类"生成器里的旧拼法"以后能被静态发现，
  得让 "result-type colon" 规则接受名字里的洞 —— 那是另一笔，且要先证明它不会把 `.cml` 里合法的模板误报。