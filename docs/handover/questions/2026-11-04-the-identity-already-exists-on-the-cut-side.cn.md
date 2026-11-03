# 身份其实已经在切点这一侧——只是没交给 `transpose`

**一句话**：2026-11-03 14:55 否掉了"把按位置的约定换成字段记录"（乙）。今晚查清了：**`split` 本来就知道每一格是谁**——`modules/transforms/split.cpp#Residual::parameter` 记着"这个残差是第几个参数"，`#residualsOf` 把参数值端口排在残差表**最前、按端口序**，`#buildLinearClosure` 照这个顺序把它当成 `lin` 的第 `at` 个 `with` 输入交出去。**顺序不是猜出来的，是造出来的。** 缺的只有一件事：**这份身份没有交给 `transpose`**——`transpose` 只拿到 `closurePorts` 这个数组，所以它只能数第 `at` 格。

## 支撑读数（`lane-functrace`，基 `c69a110fa`）

1. **那一格是"源函数第 `at` 个参数自己的值端口"**，不是计算结果：`#residualsOf` 把参数端口排最前并写下 `Residual::parameter`，前一半当自己的普通端口带出（`#buildSplitGraph` 的 `residualMembers` 回落 `forward.portOf[parameter]`），`#buildLinearClosure` 作为 `lin` 的第 `at` 个 `with` 输入交出。
2. **换过去的是"原值"，不是"实参的值"**：调用点实参 0 是 `dxs`，按位置取值递过去的是闭包里的 `xs` 原值；读取处**只查了类型相等**。`test/cases/compile/transforms/grad_array_no_int.cml`（`both(xs) = xs[1] * xs[0]`）今天答 `6.0` / `[2.0, 3.0]` **正是靠这一手**——这是"这段代码有存在必要"的唯一实证。
3. **它是一次调用，不是函数值**：`modules/transforms/rule_expand.cpp` 展开 `stdlib/rulesets/jvp.cml#jvp_idx_arr` 时建进抬升图，`split` 之后由 `#makeCopy` 的 `GCNodeKind::Func` 分支复制（切线半 `#Half::retargets` 开头 `if (tangent) return false;`，后一半只可能是复制）。所以"把函数值挪到前一半"这条路**不成立**：它带 `tangentArgs`，调用点第 0 个实参是后一半自己的切线端口，**切线在切点之前不存在**。
4. **反例说明约定的成立条件**：`vjp_loop_rounds_walk_backwards.cml` 的 `split_of_jvp_of_g_backward` **零命中**（走动前就拒 `skip=call-value-computed-by-the-body`），那半 `residuals=1 / invariants=0`，唯一残差是**算出来的值**、不是参数端口。**所以"第 `at` 格 = 参数 `at` 的值"只在"参数前缀全进残差"的图上成立**——这是可检查的条件，不是模糊的说法。

## 要裁的那一句

**把 `split` 已经知道的身份（`Residual::parameter`）交给 `transpose`，算不算 14:55 否掉的那个"把位置换成字段"？**

**施工方的判断：不算。** 理由：14:55 否掉的是"**保留按位置读**、另外记一笔来源"——位置仍是取值依据，字段只是给位置打补丁。而这里 `split` 本来就以"参数"这个身份在记账（`Residual::parameter` 是既存字段，不是为这件事新加的），`transpose` 只是**不知道**这件事。**若按这个判断推进，第 1 项有解，且不需要新的映射表。**

## 默认建议（施工方默认）

**按"不算乙"推进，但实现方式交负责人定**——与 `Gradient<F>` 那页同形：方向明确、写法需裁定。两条候选写法（都不新建映射表）：

- **甲**：`lin` 交出残差时**同时交出"这一格是哪个参数"**（用既有的 `Residual::parameter`），`transpose` 从闭包类型/签名里读它，而不是数第几格；
- **乙**：让 `transpose` 需要前向值时**按参数身份向切点索取**，取值仍由 `split` 完成——`transpose` 侧不再持有数组下标。

**代价（写清）**：两条都要动 `split`/`transpose` 之间的接口面，会碰 `10200dc26` 那条"闭包端口要声明"的方向；**判据不变**：`transpose_loop_escape` 那两条长红是哨兵（它们今天是 `Runtime indirect call norm-arity mismatch`，任何改法若把它们变成"给出一个数但不报错"，立刻回滚）；`transpose_hand_closed_lambda` 必须仍绿（手写闭包，`split` 不参与）。

## 一条读数的可信度说明

上面第 1–4 条的 `POSPROBE-HIT` 计数（46 条 → `from=call-capture` 9 条）**不可复现**：抓数用的插桩树半路被别的车道重新检出并重编（`POSPROBE` 已不在源码、二进制 11:36 重建），日志是 11:33–11:36 抓的。**结论的依据是源码路径与 `Residual::parameter` 这个字段本身，不是那组计数**；计数只作旁证。**插桩树要独占，别在别人的工作树上插桩。**
