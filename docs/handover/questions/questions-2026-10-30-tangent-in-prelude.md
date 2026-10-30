# `prelude` 放不下 `Tangent` 与 `export tag`：一引入类型函数，它自己的声明整片连锁失败

裁定 ① 要求把 `jvp`/`vjp`/`transpose` 三个标签、以及标量原语的规则，都放进最底层的 `prelude.cml`。实测发现 **`prelude` 一碰到 `types` 里的类型函数就散架**，标签和规则两条路都以同一种方式失败。

## 现象到符号

**（一）搬标签**：把三条 `export tag ... on func when is_function::<F>()` 从 `stdlib/autodiff.cml` 搬到 `stdlib/prelude.cml`（`is_function` 也加进 `prelude` 从 `types` 的引入列表）后，凡是从 `prelude` 引入标签的用例都报 `prelude` 自己有错：

```
(prelude)prelude.cml:311:1: [Error]: The declaration 'string' names the operator ':op/*tos', which no loaded module implements.
(prelude)prelude.cml:564:1: [Error]: The declaration 'attempt' names the operator ':mark/attempt', which no loaded module implements.
(prelude)prelude.cml:1:1: [Error]: Unresolved reference: 'string'. Make sure the name is declared and in scope.
```

`prelude.cml:311` 是 `#op(':op/*tos') export func string::<T>(x: T): string`——**它是文件里原有的声明，此前从没报过错**。报错落在它上面，而真正新增的只有那三条标签。整闸 `total 7898 / pass 7873 / fail 16`，16 条全是这一族。

**（二）搬规则**：把 `add_jvp`/`sub_jvp` 搬进 `prelude.cml`（并把 `Tangent` 加进同一个引入列表）后，同样是这三条报错，一字不差；把用 `*`、`/` 的 `mul_jvp`/`div_jvp` 也撤出去之后，错误照旧。

**（三）分层现状**：`stdlib/prelude.cml` 现在只从 `types` 引入值层函数（`is_same`、`is_array` 等），`Tangent` 与 `is_function` 是 `types` 的**类型函数**；`stdlib/autodiff.cml` 引入 `is_function` 并用在 `export tag` 的 `when` 子句里，一切正常。

## 为什么不能自己定

两种失败都只在"类型函数出现在 `prelude`"时出现，而 `prelude` 是最底层模块、所有模块都隐式可见。要么是引入机制对类型函数有限制，要么是 `prelude` 的解析顺序对"声明依赖类型函数求值"没有准备好。无论哪一种，改的都是模块分层或引入机制，不是写法。

## 选项

- **① 查清引入机制，让 `prelude` 能用 `types` 的类型函数**（若只是引入列表位置/顺序问题，改动可能很小；若是"最底层模块不得依赖类型函数"，则这条路不存在）。**要多造**：先定位是限制还是缺陷，可能需要在语义检查里改求值顺序。**会不会改变已有语义**：若属缺陷、修好后不改变任何既有程序。
- **② 规则与标签放在紧邻 `prelude` 的一层**（一个只依赖 `prelude`、被 `autodiff` 等引入的小模块），`#jvp` 的实参从那里引入。**要多造**：一个新模块 + 裁定里"标签一处声明"的落点改到该模块。**会不会改变已有语义**：不改变既有程序，但把"最底层一处声明"挪到了第二层，与裁定原文的"放进 `prelude`"不完全一致。

**默认建议：先查 ①**（一次实验就能判定是限制还是缺陷），判不定再走 ②。

裁定之前三条标签仍留在 `stdlib/autodiff.cml`（`git show 04cf9c01e:stdlib/autodiff.cml`），标量规则留在 `stdlib/rulesets/jvp.cml`。## 最小事实（2026-10-31 补测，四组各一次构建，均只跑 `feat.compile.rulesets`）

在模块末尾加一条**无用**的标签声明 `export tag probe_tag::<F>(rule: F) on func`：

| 加在哪 | 结果 |
|---|---|
| a. `stdlib/prelude.cml` | **绿**（`total 2 / pass 2 / fail 0`） |
| b. `stdlib/autodiff.cml` | **绿**（`total 2 / pass 2 / fail 0`） |
| c. `stdlib/types.cml` | **绿**（`total 2 / pass 2 / fail 0`） |
| d. `stdlib/prelude.cml`，带 `when is_function::<F>()` 的 `export tag probe_tag2::<F>(rule: F) on func when is_function::<F>()` | **绿**（`total 2 / pass 2 / fail 0`） |

**所以"`export tag` 在 `prelude` 里不成立"这个结论是错的，本节推翻上面的推断。** 四组都绿说明：`prelude` 能声明标签，也能写 `when is_function::<F>()`。

真正触发那三条连锁报错的，**不是"标签进了 `prelude`"这件事本身**，而是搬迁那一笔里的其他东西——待定。下一步的做法是把搬迁拆成"只搬一条标签 → 构建 → 只搬三条 → 构建 → 再让用例从 `prelude` 引入"，逐层定位；`transpose` 这个名字在 `tensor` 里另有一个同名函数，是首先要排掉的嫌疑。

**在这之前，三条标签仍留在 `stdlib/autodiff.cml`，标量规则仍留在 `stdlib/rulesets/jvp.cml`。**
