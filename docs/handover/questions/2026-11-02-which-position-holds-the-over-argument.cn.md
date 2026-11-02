# `check` 的"带程序"形式，那个程序在位置上怎么给？

**作者原报的那一条已经达成**：`rules->check<samples: 1000, seed: 7>` 照原样编过（一节里有命令与输出）。
改掉的是**另一个写法**：`rules->check<over: f>` 现在不能写，写成 `rules->check(f)`。

> **状态：待裁。已按 A（保持现在这样）落地，可撤回。**
> 相关：`stdlib/rewrite.cml` 的 `check` 两条声明、`modules/rewrite/module.cpp#checkRules`（读 `over` 的位置）、
> `docs/handover/stdlib/rewrite.notes.md` 里这一笔的来历；
> 判据：`test/plans/feat/modules/rules.plan.toml` 的 `rules.check_named_with_argument` 与
> `rules.check_program_parameter`。

## 现象（数）

| 写法 | 停在哪 | 诊断名 |
| --- | --- | --- |
| `<over: F, samples, seed>`（原样） | `samples` 在一条声明里位置 0、另一条里位置 1，具名实参指定不了它 | `OverloadParameterNames` |
| `<samples, seed, over: F>` | 无默认值的 `with` 参数跟在有默认值的 `seed` 后面 | `WithParameterAfterDefault` |
| `<samples, seed, over: F = null>` | 默认值的类型是 `void`，不是 `F` | `void` 非 `F` |
| `<samples, seed, over: F = null as F>` | `null` 不能成为函数类型 | `null` 不能成为 `F` |
| `<samples, seed, over: F? = null>` | 声明收下，实例化时 `F` 悬空，图被拒 | 未定 `F`，图被拒 |
| `over` 进普通参数位（现方案） | 都过 | — |

**三条要求不能同时成立**：

1. 无 `over` 那条的位置 0 必须是 `samples` —— 位置写法 `check<100, 1>` 和文档都这么读；
2. 带 `over` 那条的位置 0 必须是 `over` —— 它没有默认值，不能排在 `samples`/`seed` 之后；
3. 一个具名实参要在整套声明里落在同一位置、同名 —— 具名 `check<samples: 1000>` 靠这一条成立。

1 与 2 相加，`samples` 在两条声明里必然占不同位置，3 就破了。所以要么 `over` 不再是 `with` 参数
（现方案），要么动规则（B/C），要么让两条重载的名字不一样（D）。

## 一、作者要的那一条：`rules->check<samples: 1000>` 照原样编过（已达成）

文档校验器把库里每个 `@example` 都当成一次调用编一遍（类型按声明的形参绑、提到的东西按需 import）。
`stdlib/rewrite.cml` 里 `check` 的例子原文就是 `rules->check<samples: 1000, seed: 7>`：

```powershell
node scripts/stdlib-reference.mjs --verify-docs --camel out/latest/bin/camel
```

```
every declared name has a description and an @example: 565 example(s) checked, 76 entry(ies) marked a fragment were compiled too
the example counts are the ones the script records: 39 that do not compile in 2 class(es), 0 marked a fragment that compile alone, 39 that compile only with the name they say they need
```

`registry.docs_complete` 跑的就是这条命令（`expect_stdout_contains = ["every declared name has a description and an @example"]`）。

同一件事还有一条直接跑的读数 —— **具名写法与位置写法必须给出同一份报告**：

```powershell
camel test/cases/modules/rules/check_named_with_argument.cml std::nvm
```

```
true
true
true
```

三行分别是 `named == positional`、`named == with_fn`、`len(named) > 0`。

## 二、改掉的是 `<over: f>` 这个写法

`over` 从 `with` 位挪到了箭头的实参位：`rules->check(f)`、`rules->check<100, 1>(f)`
（`x->f<w>(a)` 是 `test/cases/compile/callform/pipeline_arguments.cml` 已有的写法）。
`stdlib/rewrite.cml` 的 `#op('rewrite:check_over')`、`modules/rewrite/module.cpp` 的 `checkOverKernel`、kernel 表
与纯度表都没动；`checkRules` 只是从 `norm` 的第 1 个实参读 `over`，`with` 照位置读 `samples`/`seed`。

## 三、四条路

- **A．保持现在这样**（默认，已在树上）：两条 `with` 列表都是 `<samples, seed>`，`over` 是箭头实参。
  代价：`rules->check<over: f>` 这个写法消失，用户要记 `check(f)` / `check<100, 1>(f)`。
  语义没变：`over` 的程序自带类型仍进泛型律的候选集，`rules.check_program_parameter` 在看着这件事。
- **B．放开 `WithParameterAfterDefault`**：允许无默认值的 `with` 参数跟随有默认值的，`over` 排到
  `<samples, seed, over: F>`，`<over: f>` 保住。代价：动的是语言的默认值规则，所有声明都受影响；
  `over` 在位置写法 `check<100, 1>` 下怎么给还要再定一次。
- **C．具名实参按声明各自解析**（放开 `OverloadParameterNames`）：`samples:` 在一条里是位置 0、另一条里是 1，
  两条声明都不用改。代价：「具名实参」这件事的说法要重写：一个名字在重载集里不再是一个位置。
- **D．两条重载用不同的 `with` 名字**（带 `over` 那条把 `samples` 叫 `count`）：`<over: f>` 保住，两条 `with`
  列表各自自洽。代价：用户要记两套名字 —— 正是这次要消掉的那种不一致。

## 要作者答的一句

**A 认不认？** 不认的话，在 B / C / D 里点一条，我按点名改：D 只动 `stdlib/rewrite.cml` 与
`modules/rewrite/module.cpp` 的读数位置；B、C 是语言规则，另开裁决页。

## 判据

- `rules.check_named_with_argument`：同一个规则集用 `<samples: 100, seed: 1>` 与 `<100, 1>` 写出来必须同一份
  报告，带程序那次与不带那次也同一份。名字漂到别的位置就编不过，红在这里而不是红在文档例子。
- `rules.check_program_parameter`：`check<100, 1>(main)` 的读数里出现 `sampled sum_swap<f32[3]>`，而
  `check(main)` 没有 —— `over` 的程序自带类型进候选集这条不变量跟着形状走。
- 文档例子 `rules->check<samples: 1000, seed: 7>`（A13 的原报）照原样编过，`registry.docs_complete` 绿。