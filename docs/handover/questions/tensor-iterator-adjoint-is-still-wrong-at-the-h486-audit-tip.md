# 张量迭代的伴随在 h486 审计点仍然是错值（h465 集成 lane，2026-10-25）

> 本文只报读数，不定语义。除本文件外 `docs/handover/**` 未动；`docs/design-principles*`、
> `docs/design/**`、`docs/language/**`、`changes.log` 一个字未动。

审计点：`handover/h465-flip-integration` 的 tip `2dde76da8`，与 `origin/develop`（`1df5d16da`）合并后的树
`c92c5c4cd`。本轮 8 次 lane 合并里有 4 次带冲突（`42d5ef8c0`、`eb0a243ed`、`1161c966d`、`2dde76da8`），
逐 hunk 核对过，没有哪一侧独有的改动被丢弃（`git merge-tree --write-tree` 重演出 4 个冲突文件：
`src/compile/gir/builder_expr.cpp`、`test/plans/feat/modules/autodiff.plan.toml`、
`test/plans/feat/modules/tiles.plan.toml`、
`docs/handover/questions/recursion-specialization-fixed-point-followups.md`）。

## 一、闸门读数

口径：`camel-remote.ps1 -Path <本树> -Plan smoke,test/plans/feat -Tier fast -Jobs 8`。

```
[SUMMARY] total: 6210, pass: 6174, fail: 17, skip: 11, known failures: 8
```

那 8 条 known 是 `test/plans/feat/modules/array_reduction_kernels.plan.toml` 的两例在 `fvm`/`jit`，与
`test/plans/feat/linear/effects_model_edges.plan.toml` 的四例在 `gir`；其余 17 条红在树里都没有
`known_fail` 标记。

## 二、张量迭代的伴随：裁定的正例是红的，给出的正是被禁止的第三种

`test/plans/feat/modules/autodiff.plan.toml` 的 `loop_grad_tensor_iterable.runtime.nvm` 与
`.macro.fvm`（用例 `test/cases/modules/autodiff/loop_grad_tensor_iterable.cml`）：

```
expected "loop_grad_tensor_iterable f=21.0 d=[[1.0, 1.0, 1.0], [1.0, 1.0, 1.0]]\n"
got      "loop_grad_tensor_iterable f=21.0 d=[6.77627e-21, 6.7760632e-21]\n"      (nvm)
got      "loop_grad_tensor_iterable f=21.0 d=[3.9447822e-31, 3.9445453e-31]\n"   (fvm)
```

裁定（`docs/handover/reviews/questions-2026-10-24-evening.md` 的 tensor-iter-path-left-on-develop，随
`dfca62ccd` 落到 develop）要的是"要么给出正确的导数，要么在编译期拒绝并点名这个运算；第三种（跑通、给出
错值）不允许"，并说"这条用例从负例改为正例"是 P4 合入的门。树里用例已是正例、期望值按裁定写，实现给的
正是被禁止的第三种：两个近零数——正是用例注释说的"把每轮的梯度摊成轮自己的形状，会印两个数而不是六个"。
这一条是**引用与落地不一致**，不是解冲突丢东西：冲突解没有碰 autodiff 这条路径。

## 三、另外两处引用与落地不一致（只记读数）

- `h461-retired-syntax-leftovers` 第 2 条要内部机器整套删除：代码侧已为 0 处（`visitWaitNode`、
  `NodeKind::WAIT`、`WaitLoad`、`waited_` 在全树 grep 为 0，GCT 里也没有 `WAIT` 这个种类），但
  `docs/technical/04_compilation_pipeline.md` 与
  `docs/technical/11_semantic_models_and_reverse_mapping_foundation.md` 仍把 `WAIT` 列在 GCT 节点
  种类与语义表里。
- `docs.snippets_compile` 红：`docs/tutorial/tutorial.cn.md` 与 `docs/tutorial/tutorial.en.md` 的同一个
  代码块还写着旧打印输出 `tensor([[      3.],`，程序现在印 `[[3.0], [3.0]]`（一条打印形式的裁定）。