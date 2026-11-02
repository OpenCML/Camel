# 一个在调用点已知的值，该走 norm 端口还是 with 端口？

## ① 分歧

被转置的成员（member）有时需要知道一个**在调用点已知、但不是被转置函数参数**的值。两条路各有实现：

- **develop（`0df98a97b`）**：给缺失值追加 **norm 端口**（`appendNormPort`）—— 值进入函数的 norm 参数表；
- **本支（`lane-b2`）**：追加 **with 端口**（`appendWithPort`）—— 值进入 with 段，调用点用 `setWithInputs` 填。

两者都做到了“调用形状不变”，但**一个往 norm 参数表长、一个往 with 参数表长**。

## ② 为什么这必须由作者裁

`transpose(f)` 的**类型只写 norm 那一段**。走 norm 端口 ⇒ 被转置函数的**类型变长**；而“调用形状不变”正是作者 2026-11-02 15:30 那条裁定的内容（已知参数进成员的闭包，不进端口）。**两者互相矛盾，机器替不了这个取舍。**

## ③ 实测证据

**develop（`517859938`，`feat/compile/transforms`）**：60 条 = **42 通过 / 18 失败**，`killed 0`。红的 18 条里：

- **6 条** `transpose_loop_escape.*`：`transforms: split has no rule for this function`；
- **5 条**（`transpose_many_parameters` ×2、`transpose_no_tangent.bool_condition`、`transpose_walks_each_arm_of_a_branch`、`transpose_unread_parameters` 一类）：`transforms: transpose has no transpose for this function`；
- 其余为 `grad_of_a_division` 等（同一句拒绝）。

**本支（`lane-b2`，`b2990423d`）**：62 条 = **42 通过 / 20 失败**，`killed 0`。同一批用例的差别是**转置成功了**：

- `transpose_unread_parameters`：stderr 有 `TRANSPOSE function=at graph=51 type=(float) => (float, float, float, float) nodes=5 arms=0 rhs=-`，stdout 实测 `(0.0, 0.0, 1.0, 1.0)` / `(0.0, 0.0, 2.0, 2.0)`；`at(x, w, dx, dw) = dx + dw` 在纸上给的四个余切正是 `(0, 0, 1, 1)`，种子加倍为 `(0, 0, 2, 2)` —— **一致**；
- `transpose_runs_the_data_flow_backwards`：实测 `(5.0)` / `(20.0)`（纸上 `2 + 3`、`4*2 + 4*3`），**已转绿**（`b2990423d`）；
- 其余旧形状用例的失败行从“转置不出来”变成 `Function of type '(float) => (float,)' cannot be called with argument types: <> (float, float)` —— **这是用例按旧调用形状写的，不是转置失败**。

**两个红数都对，但红的不是同一批**：本支的红是“转置成功、用例还按旧形状调用”，develop 的红是“转置不出来”。**计数相同不表示状态相同。**

## ④ 三条路与代价

- **（甲）走 with 端口，把 develop 的 norm 端口那半边改过来**。代价：改 develop 上 `appendNormPort` 的那一处调用点与其下游类型推导（约 3 处），加上本支已有的 `capturesForMember`/`addCaptureName` 一整套；**收益是本支 19 条用例不再需要改调用形状**。
- **（乙）保持 norm 端口，本支的用例改成“类型里写多出来的 norm 参数”**。代价：`transpose(f)` 的类型随函数体需要多少值而变化，**15:30 那条“capture 不是类型的一部分”被推翻**；用例侧要改 19 条（改的是调用形状与断言两处）。
- **（丙）两者分用**：**值在闭包里 ⇒ with 端口；值是被转置函数的真参数 ⇒ norm 端口**。代价：需要一条判据而不是一个场合。

**默认建议（丙）**，并把边界写成一条可机器判定的判据：**“这个值是不是被转置函数的参数？”** —— 是 ⇒ norm 端口；否 ⇒ with 端口。**这样就不靠场合记忆，也不靠谁先写的。**

## ⑤ 要问作者的一句

**走（丙），还是整体选（甲）/（乙）？** 若选（丙），这个判据写在 `transpose.cpp` 的哪个位置，就是这页落地时要定的事。

**第三个证人（分支）**：`pick(x) = x > 0 ? 2x : 3x` 的分支**条件值**就是「只有调用点才知道的值」的第二个例子 —— 第一个是 `grad_of_a_division` 的 with-arity，第三个是 `transpose_loop_escape` 的循环逃逸值。`transpose_branch.cml` 里 `back(x, 余切)` 的第一个实参**不是旧形状的残留**，它携带「分支要按调用者测过的那个值重读」这条信息。

**⇒ 这一点对（乙）是致命的**：若保持 norm 端口、把条件值写进 `transpose(f)` 的类型，那么**同一份闭包无法表达「x 为正」与「x 为负」两种情况** —— 除非对同一函数**转两次**、各捕获一次。这与 `pick` 用例想钉的「一套逆向流程、两种取值」正好相反。