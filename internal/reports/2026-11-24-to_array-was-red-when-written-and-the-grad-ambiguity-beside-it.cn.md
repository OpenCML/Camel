# `to_array` 这笔债：写它的那一笔就是红的

**2026-11-24。** `C-1123-64` 的二分结论。**结论与最初的猜测相反**：这个名字没有"写的时候是绿的、
后来被人弄红"的两段历史 —— 写下它的提交本身就是红的。同一轮里另有一族"先绿后红"的，是
`grad` 的重载歧义，不是这一族，两族不能一起算（见第 5 节）。

## 1 读数

`test/plans/feat/modules/stdlib_registry.plan.toml` + `test/plans/feat/modules/resources.plan.toml`
（这两个计划里有 `random` 被编译：`cases/modules/stdlib/registry_all.cml`、`cases/resources/names.cml`）：

| 树 | 读数 | `random.cml:114:62 Unresolved reference: 'to_array'` |
|---|---|---|
| `6b844be42b`（`lane/range-ri` 合入之前） | 20 条：11 pass / **9 fail** | **没有**（那时 `random.cml` 还写 `0 .. n`） |
| `e36048d104`（写下 `to_array(0 .. n)` 那一笔） | 4 pass / **16 fail** | **有** |
| `develop d04a238519`（基线） | 5 pass / **15 fail** | **有** |

诊断原文：

```
out/latest/stdlib/random.cml:114:62: [Error]: Unresolved reference: 'to_array'.
Make sure the name is declared and in scope. (name=SemanticDiag::Unresolved...)
```

`src/stdlib/random.cml:114`（`uniform::<T>`）与 `:148`（`normal::<T>`）都是
`[f(fold(k, i)) for i in to_array(0 .. n)]`；`src/stdlib/str.cml:123:42` 与 `:148:24` 同形 ——
`str` 那一份在 `feat/parse` 里读得到（`rulings.import_rename` 直接 `import str`）：

```
out/latest/stdlib/str.cml:123:42: [Error]: Unresolved reference: 'to_array'.
out/latest/stdlib/str.cml:148:24: [Error]: Unresolved reference: 'to_array'.
Cannot import module 'str': it has errors ...
```

**四处都写在 `import` 列表不含 `to_array` 的模块里**（`D-1123-81` 说的"四处"就是这四处；`prelude`
自己那处同模块，不算）。

## 1b 同一棵树上另外两个计划的读数（基线 `d04a238519`）

```
smoke        10 条：10 pass / 0 fail
feat/parse  127 条：120 pass / 7 fail
```

`feat/parse` 那 7 条里 5 条是这个债的连带（`rulings.import_rename.{nvm,fvm,jit,prl}` 与
`rulings.import_rename_hides_original` —— `str` 编不过，导入它的用例就报 `Cannot import module 'str'`），
另两条与它无关：`grammar.reference_in_step`（`scripts/context-keywords.mjs` 引了一句解析器已经没有的
读法）与 `frontend.ast_snapshot`（AST 哈希；`e49bdeae88` 的提交信息里已说明 `ast_hashes.txt`
押后单独重录）。

## 2 为什么这个名字本来就不该在作用域里

`prelude.cml` 自己写着规则（`0b2fd35bbc` 那一笔，是 `e36048d104` 的祖先）：

> These names are written by a program only after `import { ... } from prelude`: the prelude's
> types and its functions that are not operators are not brought along unasked.

`to_array` 是普通函数，不是算子（算子 `to_range` 才是环境里的）。所以 `random.cml` 写它之前
必须有 `import { to_array } from prelude`。**实测这个写法是通的**（宿主上验过）：

```
import { to_array } from prelude
func main() => int { println(len(to_array(0 .. 3))) ; return 0 }   // → 3
```

所以这笔"债"不只是"以后要还"，它在被写下的那一刻就**编译不过** —— 债的注释说"等 `operator for`
落地就把来源换回 `0 .. n`"，但换回之前它先得能编译。

## 3 两个人是同一个

- **写的**：`e36048d104`（"range-ri: 库不再把 a..b 当数组，流的形状照 unfold 写"，2026-11-23，
  `lane/range-ri`）。
- **弄红的**：同一笔。它的提交信息里没有它自己这两个计划的读数（它报的是
  `feat/compile/stream` 144/150）。

所以这一族没有"作者无辜、后人弄红"的情节。要追的是另一件事：**为什么这条车道在合入前没有跑
到会编译 `random` 的计划**。三个计划读的是 `random`，它们都不是 `feat/compile/stream`。

## 4 归组

属 **A**：随 `46e257734d`（merge `lane/range-ri`）进 `develop`，与 `red-bisect` 的 A=229 里
`lane/range-ri` 的 122 条同源。

修法上有两条路，都不在这条车道：补 `import { to_array } from prelude`（一行，让树先编译过），
或按 `D-1123-81` 由 `lane/op-overload-merge` 落地后把来源换回 `0 .. n`（并删掉债的注释）。
两条路都要那条线来裁，因为第二条会删掉第一条加的东西。

## 5 顺带量到的另一族（不要与上面混在一起）

`feat/compile/transforms` 里另有一族是**先绿后红**的，症状完全不同 —— `grad` 的
`SemanticDiag::AmbiguousCall`：

| 树 | `feat/compile/transforms` |
|---|---|
| `d18c15b6f9` | 8 fail（7 条转置 + `cml_var_holding_a_lambda`），**没有**歧义 |
| `c1f3d458ff`（"a name's #op rules and its body declarations are one overload set"） | **41 fail** |
| `1539dd9a24`（"a module exports a name's whole overload set"） | 35 fail —— 把 41 里的 6 条变绿 |

```
The call to 'grad' with (<x: float[...]> () => float pure) fits more than one overload ...
grad: (f: F) => ... (module 'vjp'); the built-in or module operator <autodiff:grad>: ...
```

即 `vjp` 模块里与算子组同名的 body 声明，自 `c1f3d458ff` 起成为该算子组的一个重载，
于是 `grad(...)` 的调用同时拟合两支。这一族属 `lane/op-overload-merge`（`601b89e8bb`），
与 `red-bisect` 的 A 里那 116 条同源。**它看起来像 `to_array` 那边的东西只是因为它们都落在
同一批"梯度"用例上：一组是名字解析（`to_array`），一组是重载合并（`grad`）。**