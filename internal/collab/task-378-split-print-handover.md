# `split_print.nvm` 内部错误：交接（`task-378`）

枝 `lane/jvp-loop`，树 `D:\Projects\Camel-wt\pools-red`，当前 tip **`eec324b1bd`**（已 `--ff-only` 到
`origin/integration`，dirty 0，未提交任何东西）。

## 当前一步

缺陷已在本枝复现（不是环境问题）：

```
跑：test/plans/feat/transforms/split.plan.toml（-Jobs 1）
[SUMMARY] total: 30, pass: 29, fail: 1      ← 唯一红：split_print.nvm
[provenance] tip eec324b1bdd4… | binary built 2026-11-28T04:04:28+08:00 | tier fast
BUILD_EXIT=0
实际报错（与 Lead 给的一致）：
  Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of_noisy__first_with_as_param'
  breaks its edge invariants (1 violation(s)): node 6 (13): forwards a float as a (float, float).
```

已排除"上游已有修复"：`jvp.cpp` 最近 8 笔里与本次有关的是 `8bec58ec52`（lane）与两次合并
`bf6de87015`、`f6d5ab9bd0`；两者之间 `jvp.cpp` **零差异**，且其后没有任何 jvp 修复提交。

## 读数与定位（到行）

用例 `test/cases/transforms/split/split_print.cml` 的关键形态是

```cml
with <x: float>
func noisy() => float { println(x); return x * x }
```

即被 lift 的函数有**一个 with 参数 `x`**，图名因此是 `…first_with_as_param`。

嫌疑点：`src/stdlib/transforms/jvp.cpp:2221-2256` 的 `NodeCase::Gate` 分支

- `:2240` `Type *gateType = node->dataType();`
- `:2250-2255` 先 `pairOf(gateType)`、再 `mixedMemberTypeOf(gateType)`，**把 gate 的类型抬成 `(float, float)`**
- `:2256` `addCopiedGate(ref, gateType, copyOf[forwarded])` —— 但被前向的那份 **copy 仍是 `float`**

⇒ gate 声明 `(float,float)`、实际前向一个 `float`，正是 invariant 报的那一句。发展方向（未验证，供接手）：
要么让"被前向的那份"也按同一规则抬成 pair（与 `:2250` 用同一函数、同一顺序），要么让 gate 的类型取自
**它实际前向的那份 copy 的类型**——两者必须同源，不能一处抬一处不抬。**不许**放宽 edge invariant、
**不许**加 `known_fail`、**不许**改用例期望。

## 下一步怎么接（判据，缺一条不算）

1. `split_print` 单跑 `exit 0`、stdout 恰为 `3.0\n6.0\n`；
2. `feat/transforms/split.plan.toml` 全绿（现在 29/30）；
3. 引入笔自带的两条仍绿：`jvp_range_loop_names_its_iterable`、`transpose_loop_escape_int`；
4. 相邻 jvp 计划对同尖端新增红 0（分母用**同尖端同一轮生产**的红表，别借上一轮的）；
5. 交回贴 `numstat`、`[provenance] tip`、`binary built`。

## 边界

- **窗口到 08:20**；修不好就由合并位 `revert -m 1 f6d5ab9bd0` —— 本页写于窗口内、尚无修复提交。
- 不开新枝（闸门 24 破线）、不推远端、一次一条 run、不动用例期望与 invariant。
- 复现只需一次 `-Plan test/plans/feat/transforms/split.plan.toml -Jobs 1`（约 4 分钟含构建）。