# 缺陷：12 条 `known_fail` 标记写着"没人认领"，其中 6 条今天还在豁免

**怎么发现的**：底树快层那 1,384 条红里，6 行不是"又多一条红"，而是**标记本身坏了**，所以不并进
红表统计。顺着这 6 行查下去，病比那 6 行大：`known_fail_owner` 里写着 `unowned (…)` 的标记有
**12 条，散在 5 个计划文件里**。

## 读数（底树快层那份跑，9,916 条）

| 计划文件:行 | 用例 | 主人写的 | 那次跑的结果 |
|---|---|---|---|
| `feat/compile/stated_extents:52` | `stated_mean_axis_gradient` | `lane-rank0point` 没了 | **XFAIL**（还在豁免） |
| `feat/compile/stated_extents:79` | `stated_mean_axis_element_gradient` | `lane-statedsig` 没了 | **XFAIL**（还在豁免） |
| `feat/compile/stated_extents:280` | `stated_mean_axis_answered_by_the_engine` | `lane-statedsig` 没了 | **FAIL**（说法过期了） |
| `feat/compile/transforms:1163` | `transpose_loop_escape.two_values_of_two_kinds` | `lane-valports` 没了 | **FAIL**（说法过期了） |
| `feat/compile/transforms:2111` | `transforms.with_grad_from_the_library` | `lane-deleth` 没了 | **XFAIL**（还在豁免） |
| `feat/modules/onnx:2203` | `ort_loop_values_dynamic` | `lane-shapeext` 没了 | **XFAIL**（还在豁免） |
| `feat/modules/onnx:3097` | `ort_loop_loaded_weights` | `lane-shapeext` 没了 | **XFAIL**（还在豁免） |
| `feat/transforms/compositions:58` | `compositions.vmap_grad_f`（×2 后端） | `lane-transforms` 没了 | **XFAIL**（还在豁免） |
| `feat/transforms/compositions:34` | `compositions.grad_vmap_f`（×2） | `lane-transforms` 没了 | **FAIL**（说法过期了） |
| `feat/transforms/compositions:106` | `compositions.grad_vjp_g_zero`（×2） | `lane-transforms` 没了 | **FAIL**（说法过期了） |
| `feat/modules/onnx:894` | `pass_export_equals_export_model` | `lane-shapeext` 没了 | 这一层没跑（不在快层） |
| `perf/showcase_c2:201` | `implementations` | `lane-transforms` 没了 | 这一层没跑（perf 层） |

⇒ **10 条在快层跑到了：6 条还在静默豁免，4 条报红。** 2 条不在这一层，所以今天没有读数。

**那 4 条红的是这样说话的**（`compositions.grad_vjp_g_zero.fvm`）：

```
[FAIL] compositions.grad_vjp_g_zero.fvm    153.62 ms
    [INFO] known_fail_owner             unowned (`lane-transforms` wrote this mark and is gone)
    [INFO] known failure changed its mind: expected "cannot differentiate through this call of a
           function value", got "expected exit 0, got exit 1"
    [stdout] … The call to 'grad' with (<w: f32[4]> () => f32) fits more than one …
```

## 三件分开的事

1. **说法过期会红，不会悄悄豁免。** runner 检查 `known_fail_msg` 是否还在输出里，不在就打
   `known failure changed its mind: expected … got …` 并照常报 FAIL。所以"失效的 `known_fail`
   继续豁免"对**消息**这一半不成立——它的坏法是把自己变成一条红，看得见。
2. **静默豁免的是"没主人"那一半。** 6 条 XFAIL 的标记今天还匹配着、还在豁免；它们的
   `known_fail_owner` 写着"某个车道写的，那个车道没了"。**一个没有主人的豁免，过期了也不会有人
   收到通知**——这正是那 6 条 XFAIL 的处境：它们今天既不是红、也没有人决定它们是红还是绿。
3. **归还条件是一句散文。** `known_fail_ends_when` 全树 30 条以上，没有一处代码读它。所以
   "什么时候该删掉这条豁免"只存在于一个人的记忆里，而那个人是"没了的车道"。

## 与今晚处理过的两类同形

- 12 条 `STALE` 的 pending snippet：欠账已经还了，名单没销账——**但它自己会报出来**；
- 我那条 `08_structured_control_flow` 的 `known_fail_owner`：写成日期 + 提交号 + 车道名，review
  要它改成角色，因为**坐标会移动，读的人以为它还在**；
- 这一条：**主人是一个没了的车道，而归还条件是一句散文。**

三者同一个形状：**豁免的归还条件没有可判的东西。**前两件的坏法会自己冒出来，这一件不会。

## 该做什么（不在这一笔里做）

1. **6 条还在豁免的**：给它们一个角色主人（例如 `the `mean` axis of a stated shape`、
   `the ONNX export of a loop with dynamic extents`），并重写 `known_fail_msg` 指到今天真正的
   那句话。**不许靠把子串放宽让它继续匹配**——那不是修，是把到期日推到看不见的地方。
2. **4 条已经红的**：`grad` 重载有歧义、`transpose_loop_escape` 的端口值，这些是今天要断言的
   行为，不是要豁免的事。要么改期望并写清理由，要么把标记删掉改成一条真红。
3. **加一条读数**：`known_fail_owner` 里出现 `unowned` 的标记应当被一条用例点名报出来（今天
   12 条，所以它只能先当读数、不能当闸门）。这条检查器只要读计划树，不需要编译。

**归属**：`test/plans/{feat/compile,feat/modules,feat/transforms,perf}` 与 `test/plans/perf`
归哪条车道，我不动；这一页是记录与派工，不是修复。