# loop 那一半的转置要的值，在调用点存在吗？在——但那是一个循环的值

## 问什么

`vjp_loop.rounds_are_undone_in_reverse` 到今天仍停在同一句上（`017003247` 上实测的原文）：

```
TRANSPOSE function=split_of_jvp_of_g_backward skip=call-value-missing param=0 node=23 arg=2
error[RuntimeDiag::RuntimeError]: transforms: transpose has no transpose for this function:
  the callee's transpose was built, but the parameter at position 1 reads a value this call does not carry.
```

`param=0` 是 0 起算（句子里的 "position 1" 是同一格的 1 起算写法）。这一句有两个可能的因：
调用点**没有**这个值（那就要先给调用点补一个值，是更大的事），或者调用点**有**、只是没搬过去。
这一页给出的是：**有**，它在 `node 23` 的 `inputs[0]`，也就是 `ref 2`；而它搬不过去的原因既不是"按类型猜"、
也不是"没按身份"，是 `forward` 不认识那种节点。

## 读数（临时探针，读完即撤；`git grep PROBE-` 现在是空的）

```
PROBE-NEED function=split_of_jvp_of_g_backward callee=rulesets.jvp::index_into<float> idx=0 \
  node=23 arg=2 kind=15 type=float[] argumentAt=0 calleeParams=2 isCall=0 \
  calleeType=(x: float[], i: int) => float pure inputs=[2:15:float[] 22:0:int]
PROBE-FWD function=split_of_jvp_of_g_backward ref=2 kind=15 type=float[]
```

拆开看：

- `idx=0` 是 `state.calleeNeeds[index_into]` 的内容，也就是"成员的**体**自己问了参数 0 的值"。参数 0 是
  `x: float[]`，不是那个 `int`：`y = x[i]` 的转置要把余切写进 `zeros_like(x)`，而一个 `T[]` 的长度
  不在它的类型里，只在它的值里。这正是 `split.cpp` 那句注释说的形状。
- `argumentAt=0`、`inputs=[2:Loop(float[]) 22:Data(int)]`：`inputs[0]` 就是参数 0。**序号是对的**，
  身份是 `node 23` 的 `inputs[0]` → `ref 2`。站 1/2 的"按身份补"这条路没有走错格。
- `ref=2` 的 `kind=15` 是 `GCNodeKind::Loop`（`include/camel/runtime/graph.h:126`），类型 `float[]`
  ——它是那个循环**收上来的逐轮数组**（同一条 split 诊断里 `residuals=1 loop=1 tape=5 invariants=0`）。
- 所以 `forward(2)` 拒绝的原因在 `modules/transforms/transpose.cpp:1017`：
  `if (node->kind != rt::GCNodeKind::Oper) { complete = false; return kInvalidNodeRef; }`。
  `forward` 认识的只有 `Port`、`Fill`、`Accs`、`Call`、`Func`、`Oper`，**没有 `Loop`**。

## 结论

这个值在调用点**存在**，身份也**已知**（`split_of_jvp_of_g_backward` 的 `node 23`、`inputs[0]`、`ref 2`），
缺的不是"把实参搬过去"——搬的那条路是对的——而是 `forward` 能把一个 `Loop` 节点连同它的
`LoopCond` / `LoopBody` 两个区域抄进转置图。**那是"抄一个循环"，不是"补一格值"。**

因此我停在这里，没有自己造。要给的三条路：

- **A：给 `forward` 加 `Loop` 分支**：抄 payload（`collectionCount`、区域引用、端口）、抄两个区域。
  代价是 `forward` 从此要能抄区域，而它今天连 `Brch`/`Join` 都不抄。
- **B：让 `idx_arr_t` 的转置不读 tape 的值**：现在它靠 `zeros_like(x)` 拿长度，改成由余切/形状之外的
  东西决定长度，是算子契约的改法，要作者裁。
- **C：把"loop 那一半的 tape"划出这一站**：这一站做到"数组那种形状按身份走通"，loop 那种形状连同
  `docs/handover/questions/2026-11-03-does-a-one-member-closure-prove-the-slot-identity.cn.md` 的校验收紧
  **同一个动作**做。

我不替作者选。要提醒的一点：`docs/handover/questions/2026-11-03-does-a-one-member-closure-prove-the-slot-identity` 里
说的"后面还有一道墙挡着"，那道墙就是这一页的 `forward` 缺 `Loop` 分支；也就是说，选 A 或 B 的人等于
同时拆掉那道墙，那时单成员闭包的误过校验就会走到出口——两件事必须一起做。

## 与既有判断的出入

`56aa32a42` 之后我说过"`call-value-missing` 不再出现"。那句只对**数组那一种形状**成立：数组案例要的值
是参数自己的值，站 1/2 之后确实不再缺。loop 这一种形状要的是**循环收上来的 tape**，它缺的从来不是
"参数自己的值"，而是"让转置图能持有一个循环的值"。两种形状碰巧共用同一句诊断，所以那句话读起来像
"修好了"，其实只覆盖了一半。

## 文件与读数

- 拒绝点：`modules/transforms/transpose.cpp:1017`（`forward` 的兜底返回）。
- 需要值的那一侧：`modules/transforms/transpose.cpp` 的 `calleeNeeds` 循环（`state.calleeNeeds[callee]`）。
- 案例：`test/plans/feat/compile/transforms.plan.toml#vjp_loop.rounds_are_undone_in_reverse`（`known_fail`，
  原文见上）。
- 车道读数：`feat/compile/transforms` 80 = 77 过 / 2 红 / 1 known；`split` 19/19；`remat` 6/6。