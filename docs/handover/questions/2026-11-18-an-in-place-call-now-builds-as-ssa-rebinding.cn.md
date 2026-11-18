# 一问：`v = v->sort` 现在被构造成 SSA 重新绑定，in-place 改写不再触发

2026-11-18，车道 A（`lane-spell`，「看不到写法」）在收尾时撞到两条红，查下去发现**不是本车道的**：

## ① 在基线 `2e1e77324` 上（`origin/develop`，无本车道任何改动）

```
$ git switch -c base-check origin/develop
$ internal/tools/camel-remote.ps1 -Path ... -Plan test/plans/feat/modules/array_sort_kernels.plan.toml,test/plans/feat/modules/resources.plan.toml -Jobs 8 -NoGpuWatch
BUILD_EXIT=0
[FAIL] core                                                 132.74 ms
[FAIL] array_sort_kernels.paths                             129.46 ms
[SUMMARY] total: 14, pass: 12, fail: 2
```

同一份读数在车道 A 的树上也一模一样（`total: 14, pass: 12, fail: 2`），所以它**与本车道无关**。

## ② 现象与已排除项

* `array_sort_kernels.paths` 期望 `array:sort_*_in_place` 各跑一次，实际一次都没有，普通 `sort_*` 多跑 4 次；
  `resources/core` 的 `main` 足迹少了 `rmw object (any)`——那是 in-place 算子对数组对象的访问。两条同一个根因。
* 临时仪表（已复原）打出：`ASSIGN_SSA name=v_i`——`v_i = v_i->sort` 在 `visitAssignNode` 里被
  `Builder::ssaAssignmentOf` 接走（`src/compile/gir/builder_operators.cpp`），于是它之后那条
  `if (unusedStatement) inPlaceRebindingOf(gct)` 永远轮不到，`inPlaceCalls_` 为空，in-place 形式不再被选中。
* 已排除 `sem::covers`：把 `spelling` 的比较规则放回 `src/core/resource_semantics.cpp` 不改变这条红。
* 本车道没有碰 SSA census（`builder_expr.cpp#scanBodyForSsa` / `ssaDeclarationIsEligible`），也没有碰
  `inPlaceRebindingOf`；本车道的改动只在资源/能力语义、规范顺序与 `.cml` 声明上。

## ③ 搜索范围

`b62b76de5..2e1e77324`（车道 A 的基线到作者的新基线）之间的提交：

* `b62b76de5` gir: a `yield` becomes a node…（动了 `builder_control.cpp`，+17 行）
* `e314b4d70` merge: the generator machine builds the whole of a shape whose yields are literals
* `2e1e77324` merge: two writings of one program are still two canonical forms…

`var` 是否走 SSA 由 `scanBodyForSsa` 的 census 决定，而 census 只看 GCT 形状（`carried`、
`argumentMayWriteBack`、`branchWrites`），不看资源；所以最可能的入口是上面三段里对
`var`/`sync` 体构造方式的改动，或 `synced_` 的取值。

## 默认（未答复前按此办）

归到**执行计划那条线（F/H）**去查，因为 in-place 与 SSA census 都在那一层。在查清之前，
`test/plans/feat/modules/array_sort_kernels.plan.toml#array_sort_kernels.paths` 与
`test/plans/feat/modules/resources.plan.toml#core` 记为**已知基线红**，不算任何车道的责任，
也不许为了让它们转绿去改断言或改 in-place 的语义。