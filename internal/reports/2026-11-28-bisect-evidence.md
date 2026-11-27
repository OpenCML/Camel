# 二分证据：promo64 那 6 条新红，各自落在哪一笔

（`seat-merge-2` 交回，协调者存档。原始材料 `D:\Temp\bisect-evidence.md`，本页是仓库内的一份。）

口径：每次迭代 = 检出一个候选 sha → 构建 → 只跑相关计划；判据是**同组同跑、只差一笔**，即父提交全绿、子提交恰好红。

## ① lambda 形参类型推断那 5 条（`foreach_lambda` ×4 + `docs.snippets_compile`）

```
-Plan 'test/plans/probe/foreach_lambda.plan.toml,
       test/plans/probe/foreach_lambda_family.plan.toml,
       test/plans/feat/transforms/split.plan.toml,
       test/plans/feat/docs_links.plan.toml'
```

```
3a120ec59d (= cb878a8170^)   [SUMMARY] total 69 / pass 69 / fail 0    exit 0
cb878a8170                   [SUMMARY] total 69 / pass 64 / fail 5    exit 1
```

```
[FAIL] docs.snippets_compile
[FAIL] foreach_lambda_family.range_map
[FAIL] foreach_lambda_family.range_reduce
[FAIL] foreach_lambda.range_form
[FAIL] foreach_lambda.range_filter
```

`docs.snippets_compile` 的 10 条 stale 全在**文档正文的 ```camel 围栏**里（不是片段文件、不是计划内联），同一诊断 `SemanticDiag::LambdaParameterWithoutType`（形参 `x` / `i`）：

```
docs/language/06_expressions.md:109 / :718
docs/language/en/06_expressions.md:109 / :717
docs/tutorial/05_arrays_and_loops.cn.md:123 / :157
docs/tutorial/05_arrays_and_loops.en.md:132 / :167
docs/tutorial/tutorial.cn.md:307
docs/tutorial/tutorial.en.md:307
10 stale snippet problem(s) in 195 reader-facing document(s) (350 snippet(s) checked, 4.1 s)
```

⇒ **不是文档写错，是编译器不再从实参推出 lambda 形参类型**：正文里本来正确的示例被拒。

## ② `split_print.nvm`（真内部错误，编译器缺陷）

二分轨迹（7 次迭代收敛，`range=2` 稳定）：

```
iter1 8a75587f93 (02:28)  绿   → good=8a75587f93
iter2 f6d5ab9bd0 (02:40)  红   → bad=f6d5ab9bd0
iter3 41dc5c7e84 (02:39)  绿
iter4-7                    收敛到 f6d5ab9bd0 与其父之间
```

父/子对照（同一口径）：

```
715e8adfb0 (= f6d5ab9bd0^1)   build exit=0   split_print exit=0   stdout=[3.0|6.0|]
f6d5ab9bd0                    build exit=0   split_print exit=1   stdout=[]
                              stderr: RuntimeDiag::PassInternalError
                              Runtime graph 'jvp_of_noisy__first_with_as_param' breaks its edge invariants (1 violation(s)):
                              node 6 (13): forwards a float as a (float, float)
```

引入笔：`Merge lane/jvp-loop: a gate's order is copied in the kind it is written in, and a paired value is lifted as one pair`，parents `715e8adfb0` / `00d3169801`；车道内三笔 `00d3169801`（02:17）、`8bec58ec52`（00:46）、`e80048a15d`（22:57）。

numstat（`^1..merge`）：

```
449/48  src/stdlib/transforms/jvp.cpp
 34/0   test/cases/compile/transforms/jvp_range_loop_names_its_iterable.cml
 10/2   …/transpose_loop_escape_int.cml
 11/1   test/plans/feat/compile/transforms.plan.toml
```

⇒ 落在 `jvp.cpp`；**图名出自 jvp 那侧，不是 split 用例**。

## 处置

| 红 | 引入笔 | 归属 | 窗口 |
|---|---|---|---|
| lambda ×5 | `cb878a8170` | `lane/call-resolve-fix`（`lane-enter-shape`） | ~07:55，修不好 `revert -m 1` |
| `split_print.nvm` | `f6d5ab9bd0` | `lane/jvp-loop`（`lane-backend-speed`） | ~08:20，修不好 `revert -m 1` |

两条都满足"父绿子红、只差一笔"，撤或修都可以直接引本页。