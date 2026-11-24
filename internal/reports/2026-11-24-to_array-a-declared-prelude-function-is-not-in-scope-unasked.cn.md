# `to_array`：一条声明在 `prelude.cml` 里的**普通函数**为什么不在作用域里

**2026-11-24，`lane/red-b-fixes`（`C-1123-64`）。** 一句话：**编译器没有把人弄丢——`prelude.cml`
里带函数体的名字，只有写成 `#op` 声明的那一半会进第 5 层（内建层，随每个模块白给）；不写
`#op` 的普通函数只是模块 `prelude` 自己导出的名字，谁不 `import` 谁就没有。** `to_array` 正是
`prelude.cml` 里第一个"有体、无 `#op`、也没有同名算子组"的导出名，所以它是唯一一个连名字都
没有的（`Unresolved reference`），而 `len`/`map`/`reduce`/`print` 只是少了 Range 那支重载
（`No matching function`）。

`C-1123-64` 原来的描述（"声明并导出了却解析不到，是导出的管线坏了"）不成立，见第 4 节。

## 1 复现与读数（宿主上实测，`-Jobs 8`）

五个探针，每个一件小事，互不掩盖：

| 探针 | 写法 | 读数 |
|---|---|---|
| A | `to_array(0 .. 10)`，无 import | **`Unresolved reference: 'to_array'`**（exit 1） |
| B | 同上 + `import { to_array } from prelude` | **pass**，打印 `[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]` |
| C | `let r: Range = 0 .. 5`，无 import | `Unresolved type reference: 'Range'` |
| D | `to_range(0, 5, 1)`，无 import | **pass** —— 它是算子 |
| E | `(0 .. 10)->len()` / `->map<f>`，无 import | `len` 解析到了（`No candidate of 'len' applies to (<> (Range))`），Range 那支不在候选里 |

真实的那个缺陷（程序 `import { key, uniform } from random` 再 `k->uniform::<f32><shape: [4]>`）：

```
(random)out/latest/stdlib/random.cml:114:62: [Error]: Unresolved reference: 'to_array'. Make sure the name is declared and in scope. (name=SemanticDiag::UnresolvedReference, code=0x0300000B)
(random_uniform_repro)test/cases/.../random_uniform_repro.cml:5:1: [Error]: Cannot import module 'random': it has errors (out/latest/stdlib/random.cml).
```

`str.cml` 同形（`123:42`、`148:24`，报错原文见 `internal/reports/2026-11-24-to_array-was-red-when-written-and-the-grad-ambiguity-beside-it.cn.md`）。

B 通过、A 不通过，这一对就是全部：**声明、导出、泛型/重载的管线都是好的，缺的是作用域。**

## 2 解析路径（读代码得到的，可逐条对）

- 第 5 层在代码里叫 ambient，来源是"内建模块"（名字为空的那个）：`UserDefinedModule` 的构造
  函数里 `importAllRefsFromMod(ctx->importModule(""))`（`kernel/core/module/userdef.cpp:73`）。
  它搬的是**内建模块的 `nativeEntityNS`**，而内建模块的名字空间只由 `#op` 声明填：
  `Builder::declareNativeOperator` 认领内建规则 → `operatorOnlyNames`（`gir/builder_modules.cpp:482`、`:771`）。
- 一个**带体**的导出声明走的是另一条：`Builder::exportDeclarations` → `ProgramExports::exportEntity`
  （`gir/builder_modules.cpp:1335`、`kernel/core/module/module.cpp:544`），落在这个程序给模块
  `prelude`（`context.cpp:759` 建的那个对象）的导出表里。
- 查名字：`Builder::visitDRefNode` 依次查 local → 本模块 → 具名 import → `using` →
  `module_->hasImportedRef(written)` / `(written, Ambient)`（`gir/builder_expr.cpp:1222`、`:1241`、`:1248`）。
  没有绑定就没有名字，最后落到 `:1369` 的 `UnresolvedReference`。
- 所以 `random.cml`（`array`/`math`/`tensor`/`types`）与 `str.cml`（一个 import 都没有）里写
  `to_array` 时，五个层都没有这个名字。`len`/`map`/`reduce`/`print` 之所以"解析得到"，是因为它们
  有别处（`globals.cpp`）的算子组占着第 5 层，缺的只是 `prelude.cml` 那支 Range 重载。

**这条规则是写下来的，不是推测**：`prelude.cml:1044`

> These names are written by a program only after `import { ... } from prelude`: the prelude's
> types and its functions that are not operators are not brought along unasked. `to_range` is an
> operator, so it is ambient like the rest of them.

同一句在 `docs/stdlib-reference.md:743`（从源生成的参考）里也在。作者在 `test/cases/compile/overloads/`
下写的 5 个 Range 用例（`range_len.cml`、`range_map.cml`、`range_reduce.cml`、`range_print.cml`、
`range_filter.cml`）全部显式 `import { Range, len } from prelude`，第 10.2 节的层序里第 5 层叫
"built-in"，`docs/design/architecture/layout-map.cn.md:193` 说 `prelude.cml` 是"随每个程序一起编译的
内建**签名表**"——它以前只有签名，`0b2fd35bbc` 起才有了第一个带体的导出名。

## 3 修法：把规则写回去（两行），不动语义

`e36048d104`（`lane/range-ri`，就是写出这笔"债"的那一笔）把 `0 .. n` 换成了 `to_array(0 .. n)`，
却没给这两个文件加上 `import`：

- `src/stdlib/random.cml`：`import { to_array } from prelude`（放进已有的 import 块，和别的模块行一样）。
- `src/stdlib/str.cml`：同一个 import，写在模块文档注释之后（这个文件之前一个 import 都没有）。

两处都留了注释说明"`to_array` 是 prelude 的函数、不是算子，所以写出处"，并保持原来那句
"等 `operator for` 落地就把来源换回 `0 .. n`"的债注（`D-1123-49`）。

**为什么不是改编译器让 prelude 的函数也进第 5 层**：那是改语言语义，第 10.2 节的层序没有这么说，
`prelude.cml` 明确说不是这样，`docs/design/architecture` 把它定位成"内建签名表"而不是环境名字的来源。
按 `AGENTS.md`，设计原则没写的问题要交作者裁，不能在车道里顺手定。可行的改法我留在第 6 节。

## 4 对 `C-1123-64` 描述的更正

| 原描述 | 实测 |
|---|---|
| "声明并导出了，引用却解析不到" | 声明和导出都正常：同一句话加上 `import` 立刻通过（探针 B） |
| "是导出/模块管线的缺陷" | 不是。缺的是作用域，规则在 `prelude.cml` 自己写着 |
| `perf.cml:37` 是"另一个、不同的失败" | **两者同一根因，不同症状**：`reduce`/`len` 有算子组所以"名字在、重载不在"（`No matching function`），`to_array` 连组都没有（`Unresolved reference`）。修了 `random`/`str` 的 import 不改变 `perf.cml`（那里的 Range 重载仍然不在），但它也不是另一条 bug |
| `random.cml:114` 与 `str.cml` 的四处 | 与上一份二分报告（`red-bisect`）一致：四处都在 import 列表不含 `to_array` 的模块里 |

另外顺带量到的一族（**不是**这条车道，也不该算进来）：`test/cases/modules/random/shuffle_kernels.cml:25`
的 `[i for i in 0 .. 5000]` 报 `The source of this list comprehension has type 'Range', which has no
elements to take` —— 同一个 `lane/range-ri` 留下的债（`D-1123-49`），但诊断完全不同（不是名字解析）。

## 5 读数（同三份计划，修前 / 修后）

计划：`feat/modules/stdlib_registry`、`feat/modules/resources`、`feat/parse`（147 条 / 175 条）。

| | total | pass | fail |
|---|---|---|---|
| 修前 | 147 | 125 | **22** |
| 修后 | 175（多了 `feat/modules/random` 与探针） | 159 | **16** |

同一批计划里由 `to_array` 直接变绿的 12 条：`rulings.import_rename.{nvm,fvm,jit,prl}`、
`rulings.import_rename_hides_original`、`registry.purity_declared`、`registry.overload_position_names_agree`、
`resources.core`、`core_test_pass_is_not_in_a_pipeline`、`conflict_per_resource`、`declared_resources`、`resource_names`。
`feat/modules/random` 13 条全绿（含 `keyed_random.std_cpp_agrees`），`feat/modules/random_reference` 全绿。

仍然红的、与本次无关：`grammar.reference_in_step`、`frontend.ast_snapshot`（上一份报告已说明）、
`registry.*` 9 条（签名文件/文档漂移，`tensor.cml:403` 一类"the declaration has a body"）、
`random_shuffle_kernels` 5 条（上面那族 Range 推导）。

## 6 我没能确定的，以及留给作者的两个问题

1. **"594 条" 这个数我核不出来。** 树上按 import 图实读：`src/stdlib` 里**没有**别的模块导入 `random`
   或 `str`；`test/cases` 下直接或间接导入这两个模块的用例 95 个，落在 fast 层里是 **159 条 test entry**
   （算上 `backends` 展开约 **264 次运行**）。要把这个数抬到 594，得把"逐个枚举 stdlib 模块"那类计划
   （`stdlib_registry`、`stdlib_values`、`docs_*`）里被一条模块失败带倒的条目也算进去；树上没有那份
   1512 条红名单可以对账，所以我按"同一个符号直接导致的"报 159/264，而不是 594。
2. **设计问题（要作者裁）：`prelude.cml` 里带体的导出名要不要进第 5 层？** 若要，最小改法是
   `Module::importedBindingsForRef`（`kernel/core/module/module.cpp:265`）在 `Ambient`（和 `All`）
   时给 prelude 加一条兜底绑定，**同时**要处理 `gir/builder_expr.cpp:1309`：一个"算子组 + 函数集"
   的 overload-set 实体现在会走 `isGraphSet` 那支而丢掉组里的规则（`len([1,2])` 会因此坏掉），
   得像 `mergedOverloadsNamed`（`gir/builder_operators.cpp:139`）那样两支一起带。判据：`len`/`print`/`map`
   的数组形与 Range 形同时可用，且 `feat/modules/stdlib_registry` 的 `overload_position_names_agree` 不翻。
   做了这件事，`perf.cml:37` 与上面 `shuffle_kernels` 那族之外的东西会跟着动，所以它该由作者定，不该由本条车道顺手做。
3. 本条车道**没有**跑整层 fast 计划（按约定只跑最小范围）。

## 7 回归

新增两条永久用例（`test/plans/feat/compile/prelude_signatures.plan.toml`）把规则钉住：

- `unimported_function_is_unresolved`：`to_array` 不 import 就是 `Unresolved reference: 'to_array'`。
- `imported_function_beside_an_ambient_operator`：`import { to_array } from prelude` 之后同一个调用通过，
  同文件的算子 `to_range` 不 import 也通过。