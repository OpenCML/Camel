# 排队的工作项（非优先级一，但已由裁定落到纸面）

## 1. `(d1)` 的删除要改走**编辑接口**，不能在 SSA 这一路另写一份

决策者 2026-11-15 答复（`docs/handover/reviews/q-2026-11-15-should-the-deletion-come-before-the-carrying.cn.md`）里 `(d1)` 的验收原文：

> 只删确认没有读者的节点，**删除走编辑接口**（同日"谁接替被遍取走的持位节点"的裁定），**不在 SSA 这一路另写一份删除**。

已落的 `8d2fa8b8f` 里有一个 `dropCarriedVariableUnits`——**那是一份在 SSA 路上新写的删除**，正是不许的那一种。

**它当时不知道这条裁定**（裁定在它之后到达），所以不是违规，是**待改**。

改法有两条路，选哪条要按「位置交接收进编辑接口」那件事的进度定：
- 若编辑接口已有 `eraseNode`/`replaceNode` 的承接能力 ⇒ `dropCarriedVariableUnits` 改成调用它们；
- 若还没有 ⇒ 先把那个能力补进编辑接口，再改这里。**不要为了这一处单独造第三个删除入口**。

判据不变：删完四后端输出逐字不变，三个度量按下面第 2 条。

## 2. **「必须先删」不等于「删了就绿」——必要性不是充分性**

我在问页里写的是车道给的结论：「三项在 `(d)` 删掉无读者的 `:var/new` 与 `:op/assn_l` **之前不可能变绿**」。

**那是一个必要条件。我把它当成了充分条件来写**，于是决策者的验收口径写成「三个度量转绿后报 3/4」。

**实测是 1/3**（`8d2fa8b8f`）：只有 `var.an_arm_assignment_is_the_branch_result` 转绿；另两项的红另有原因——

> `a_loop_assignment_is_the_loops_state` 与 `loop_write_outside_read_is_the_let_writing`：**循环体里那次读还在读 cell**（`total = total + 1`），所以它**不满足「无读者」判据，删不动**——它要的是循环搬运。

所以正确口径是：
- `(d1)` 后：**1/4**；
- `(c)` 循环搬运后：**3/4**；
- `(d2)` 按值捕获后：**4/4**。

**这条错在措辞上，代价在排期上**：若照「3/4」去验收 `(d1)`，那一笔会被判成不合格，而它其实做对了。

## 判据（给后来的人）
写这种顺序结论时，**把「不可能早于」和「一定晚不了」分开写**：
- 「X 在 Y 之前不可能变绿」= 必要条件；
- 「做了 Y，X 就会绿」= 充分条件，**必须有实测才能说**。

## 3. 一处提交标题说的机制，diff 里没有（2026-11-15 记）

`origin/fsh-dev` 的 `f257714fe` 标题写着：

> Merge develop: a read after a branch takes the JOIN's value, **an unread unit is dropped through the edit interface**

而 `6f686ac40..f257714fe` 的改动文件里，**与编辑接口相关的 0 个**：

```
src/compile/gir/builder.{h,cpp}
src/compile/gir/builder_control.cpp
src/compile/gir/builder_expr.cpp        ← 191 行，删除实现在这里
test/cases/compile/var/arm_var.cml
test/plans/feat/compile/var.plan.toml
test/plans/feat/parse/golden/ast_hashes.txt
test/tools/writing-freedom.mjs
```

**实际做的正是本文第 1 节记的那件事**：`dropCarriedVariableUnits` 在 SSA 路上新写的一份删除。

### 为什么记它
**一个合并标题是别人读这笔改动时最先读到的东西。** 标题说「走编辑接口」，而代码没走——**两种可能，处理方式不同**：
- 若这是一句**期望/待办**（「应当走编辑接口」）⇒ 标题误导了事实；
- 若这是作者的**判断**（认为现有实现已经算走编辑接口）⇒ 那我第 1 节的判断错了，需要作者指认编辑接口是哪一个。

**在澄清之前，任何一方都不许按标题去改代码**——否则会为了对齐一句话而造出真正的第二个删除入口。

**判据**：这件事以「代码里删除从哪条路走」为准，不以标题为准。第 1 节的待改项保持原样。

## 4. 返工：`f883c88ee` 的第 4、5 条要撤（决策者 2026-11-16 凌晨裁定）

裁定原文在 `docs/handover/reviews/questions-2026-10-29.md` 末尾「返工」一节。要点：

- **第 4 条（补回 `IgnoredSideEffect`）要撤**：让循环状态「声明自己写了」会让它报「缺 `sync`」。按原则 3.1 与 11-15 的答复，**`IgnoredSideEffect` 对 `var` 的两支要删，不是要保住**。改的是 `var_read_before_write` 的期望（去掉 `IgnoredSideEffect`）。
- **第 5 条（补回 `COPY → LOOP` 顺序边）要撤**：新语义下 `var` 不是资源，**变量的写之间没有顺序边**，先后只靠数据依赖。`order_edges_regions.gir` 里那一条从金标去掉；同一用例里**对外部资源**的顺序边保留。
- 第 2、3、1、6 条（循环只露出头部声明的状态；无 store 的单元也能删；按函数锚点定位；「认领」与「交出值」分开验）**没有问题，保留**。

### 这件事的形状（比返工本身重要）
我把第 4、5 条写进合并正文，并称它们为「今晚第三次同族」「折叠只搬它承认的那一类边」——**把重建旧机制读成了深刻的一课**。

真正的病不是纪律松，是**判据缺**：「不许改期望」这条规矩单独存在时，**遇到「新语义下旧用例变红」，它只能把我推向「去把旧机制补回来」**。

判据已写进 `agents/test.md` 末条：**先问那条用例测的是不是已撤回的规则。**
