# 一问：`check` 两条重载的 `with` 位置名必须一致，而 `over` 不给默认值就不能排在 `samples`/`seed` 之后——两条规则相撞时改哪一边？

> 状态：**等作者裁决。已按默认选项 ① 落地（`over` 移到普通参数位），可撤回。**
> 相关：`stdlib/rewrite.cml:144-156`（两条声明）、`modules/rewrite/module.cpp:3934-3954`（
> `checkRules` 读 `with`/`norm` 的位置）、`docs/handover/stdlib/rewrite.notes.md:14`（这一拆分的来历）；
> 判据落在 `test/plans/feat/modules/rules.plan.toml` 的 `rules.check_named_with_argument`。

## 发生了什么（数）

A13 的原报：`stdlib/rewrite.cml` 的文档例子 `rules->check<samples: 1000, seed: 7>` 编不过，报
`OverloadParameterNames`，因为 `check` 的两条重载在那一位置写了不同的名字（`<samples, seed>` 与
`<over: F, samples, seed>`）。把名字改成一致这件事，被语言的两条规则夹住了。六种写法都量过：

| 写法 | 结果（贴的是编译器原话） |
| --- | --- |
| `<samples, seed>` + `<over: F, samples, seed>`（今天） | `SemanticDiag::OverloadParameterNames`：`samples` 在一条里是位置 0、另一条里是位置 1（`src/compile/gir/builder_calls.cpp:1294`） |
| `<samples, seed, over: F>`（重排） | `rewrite.cml:152:42: The 'with' parameter 'over' has no default and follows 'seed', which has one.`（`WithParameterAfterDefault`，`src/compile/gct/builder.cpp:7055`） |
| `<samples, seed, over: F = null>` | `rewrite.cml:154:52: The default of the 'with' parameter 'over' has type 'void', not 'F'.` |
| `<samples, seed, over: F = null as F>` | `rewrite.cml:155:52: The literal 'null' cannot be represented as type 'F': the value cannot become that type.` |
| `<samples, seed, over: F? = null>` | 声明收下，但**每次实例化都留一个没定的 `F`**：`Runtime graph '__root__' has an undetermined type: 'F' in its static slots`（`rules` 全部 red） |
| `over` 移出 `with`（现方案） | 两条 `with` 列表都是 `<samples, seed>`，具名、位置、`over` 三种写法都过 |

一句话：**位置 0 在无 `over` 那条里必须是 `samples`**（位置写法 `check<100, 1>` 与文档都这么读），而
**在带 `over` 那条里位置 0 必须是 `over`**（它没有默认值，不能跟在有默认值的后面）；具名实参又要求
同一名字在整套声明里落在同一位置。三条同时要，无解——除非 `over` 不再是 `with` 参数。

## 已落地的默认选项

- `over` 进普通参数位：`#op('rewrite:check_over') export func check::<F>(rules: Rule[], over: F)`，
  `with <samples: int = 100, seed: int = 0>`。
- 调用形式随之变：`rules->check<over: f>` → **`rules->check(f)`**；原来位置写法
  `rules->check<main, 100, 1>` → **`rules->check<100, 1>(main)`**（`x->f<w>(a)` 是
  `test/cases/compile/callform/pipeline_arguments.cml:29` 已有的写法）。
- `modules/rewrite/module.cpp` 的 `checkRules` 从 `norm` 的第 1 个实参读 `over`（原来从 `with` 的第 0 个读）。
- 受影响的三处调用都跟着改了：`test/cases/modules/rules/check_program_parameter.cml:31`、
  `test/cases/modules/rewrite_function/apply.cml:27`、以及 `stdlib/rewrite.cml` 的 `@example`。
- 语义没变：`checkRules` 仍然把 `over` 的程序的类型加进泛型规则的候选集，
  `rules.check_program_parameter` 仍在量这件事。

## 选项（要作者选一句）

- **① 认这个形状**（默认，已在树上）：`with` 两条一致，具名 `check<samples: 1000>` 可用；代价是
  `over` 从一个 `with` 实参变成箭头的实参，`rules->check<over: f>` 这个写法消失。
- **② 放开 `WithParameterAfterDefault`**：允许无默认值的 `with` 参数跟在有默认值的后面，`over` 就能
  排在 `samples`/`seed` 之后（`<samples, seed, over: F>`），`<over: f>` 保住。代价：动的是语言的默认值
  规则，所有声明都受影响；且"位置写法 `check<100, 1>`"仍把 `over` 挤在位置 0 之外的顺序里，要再定
  一次 `over` 在位置写法下怎么给。
- **③ 放开 `OverloadParameterNames`**：具名实参按每条声明各自解析（`samples:` 在一条里是 0、在另一条
  里是 1），两条声明都不用改。代价：一个名字在重载集里不再是一个位置，"具名实参"这件事的说法要重写。
- **④ 两条重载用不同的 `with` 名字**（带 `over` 那条把 `samples` 叫成 `count`）：保住 `<over: f>`，
  但用户要记两套名字——正是这次要消掉的那种不一致。

**要作者答的一句**：`check` 的带程序形式，认"`over` 作为箭头的实参、`rules->check(f)`"（①，已在树上），
还是必须保住 `rules->check<over: f>`、由 ②/③/④ 里选一条改语言或改名字？

## 判据

- 现在绿灯的是 `rules.check_named_with_argument`：同一个规则集用 `<samples: 100, seed: 1>` 与 `<100, 1>`
  写出来必须同一份报告（名字漂到别的位置就会编不过），带程序那次与不带那次也同一份。
- `rules.check_program_parameter` 仍量"`over` 的程序自带类型进候选集"：那条不变量没跟着走形状。
- 文档例子 `rules->check<samples: 1000, seed: 7>`（A13 的原报）现在**照原样编过**。