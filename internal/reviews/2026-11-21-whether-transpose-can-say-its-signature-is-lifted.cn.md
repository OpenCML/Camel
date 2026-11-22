# `Transpose<F>` 说不出「我的参数里有一半是提升出来的」

- 提出：2026-11-21 06:26
- 状态：未决
- 默认建议：**给 `Transpose<F>` 加一格**（见文末）

## 一、这件事是在哪一步被量到的

`compositions.jvp_vjp_f.{nvm,fvm}` 从本次会话开始时的一条**内部错误**，一路被推到今天这个位置。每一次挪动都换掉了一堵墙，而今天是第五堵：

| 次序 | 墙 | 由谁挪掉 |
|---|---|---|
| 1 | 15 条嵌套/复合用例同时红 | `2a2cf31a8` |
| 2 | `jvp` 把**调用**写成了**函数值** ⇒ 内部错误「breaks its edge invariants」 | `991ee512c`（`ed8766304` 合入） |
| 3 | `transpose` 拒：`skip=call-value-missing param=2 node=4 arg=0` | `067376ac5` |
| 4 | `ports=3 promised=1 cause=needs-a-value-outside-the-closure` | `7a0eb3c22` |
| 5 | **`ports=2 promised=1`** —— 差的正是提升签名的值半边 | 当前 |

第 5 堵墙的完整读数（`6309d4740` 之后，`develop = 6d7668f83`）：

```
TRANSPOSE function=jvp_of_split_of_jvp_of_sq_backward ports=2 promised=1
```

而 `transpose` 的成员已经算对了类型：`(f32,(f32,f32)) => (f32,)` —— 值半边加一个余切。它**说出去了两个端口**，而 `Transpose<F>` **只承诺一个**。

## 二、为什么这不是「再改一次 `transpose.cpp`」能解决的

`ports` 是 `transpose` 从**提升后的类型**读出来的，而它读对了：那个签名里确实有两个值位与一个余切位。

`promised` 是运算符类型 `Transpose<F>` **自己声明**的格数，而它只有一格：`(ct) => (cotangents)`。

于是 `2 != 1`，而**没有一处代码写错**。缺的是**类型里的一个词**：`Transpose<F>` 说不出「我这个 `F` 的签名是提升出来的，所以它的参数里有一半是值」；它只能把 `F` 当普通函数读，而普通函数的提升参数列表与**反向半边** `(f32,f32)=>f32` **同型**。

## 三、今晚量到的那个更深的发现（它阻止了一个看似正确的实现）

按「从提升后的类型读」这一个读者实现时，有三例当场变红：

```
floatgate.f32_closure_member_ports
floatgate.f32_array_zero_carrier
residuals_in_the_closure
```

它们报 `(float,float)=>(float,)` 被一个 ct 调用 —— 也就是：**反向半边 `(f32,f32)=>f32` 与二参数函数的提升参数列表，形状完全一样**。

所以「哪一半是值」不能从类型本身读；它读的是**图的事实** `GCGraph::liftedFrom`（`jvp` 造图时记录的）。这是本次实现的实际形状，而它守住了「一个读者」：`lift.h#liftedParametersOf` 是唯一入口，`transpose.cpp` 里四份 `tangentSpaceOf(...).hasSpace()` 与 `valuesForParticipants` 全部删掉，`git grep valuesForParticipants` 归零。

**而这也说明第 5 堵墙不能靠「再从类型里读一次」解决**：类型里根本没有这个词。

## 四、要裁定的是什么

`Transpose<F>` 要不要长出第二个格，用来说「`F` 的签名是提升的，它的值位有一半是我该吃的值」？

两种做法：

**A（现状）**：`Transpose<F>` 保持一格。`ports` 与 `promised` 的差就永远差在那里，而 `compositions.jvp_vjp_f` 停在 `known_fail`。

**B（默认建议）**：给 `Transpose<F>` 加一格，表达「这个签名是提升的」。这一格的**内容**不是新的数据，它是 `GCGraph::liftedFrom` 那个事实在**类型层**的同一个事实 —— 与今晚反复确立的那条「一个事实一个读者」同形。

## 五、为什么默认是 B

三条：

1. **不选 B，`jvp_vjp_f` 的终点就不是一个 bug，而是一个**表达力缺口**。今晚挪了五堵墙，四堵是被改掉的，第五堵改不掉 —— 因为它不在代码里。
2. **B 不引入新事实**。`liftedFrom` 已经是那个事实的存放处；B 只是让它能被类型层读到。反过来，A 会让 `transpose` 在**每一个**提升签名上都要重新推一遍「这是不是提升的」—— 而那正是今晚刚刚删掉的第二个读者。
3. **A 有一个可测的坏处**：`ports=2 promised=1` 是一个**静默的差**。它今天报错了，而如果哪天某处按 `promised` 而不是 `ports` 分配端口，它会**不报错、少一个端口、而用例可能仍然是绿的** —— 与 `linkSyncStatement` 那条漏掉的控制边同一个形状。

## 六、不裁也能往前走的部分

下面这些不依赖本页：

- `compositions.jvp_vjp_f` 的两条 `known_fail` 已按 02:20 的答改成**期望输出**（`1.0` / `2.0` / `3.0`，由 `pull(x)=2vx`、`jvp` 求导 `2v`、方向 `dx=[0.5,1.0,1.5]` 推出，与用例注释、`desc`、`verify_script` 的 `[1,2,3]` 三处吻合），`known_fail_msg` 已删，`changed its mind` 归零。
- `Transpose<F>` 的类型法则本身在 `stdlib`，与 `modules/transforms/` 的 walk 是两层；本页若裁 B，落地在 `stdlib` 侧，而 walk 侧不需要再改。
- `transpose.cpp` 里还剩**一处** `tangentSpaceOf(...).hasSpace()`，位置待 `transpose` 车道自己指出（它报的是「四处全删」，而 `grep` 数到 1）—— 这一处无论本页怎么裁都该被指着看完。

## 七、本页不回答的问题

- `Transpose<F>` 加格之后，**别的**运算符（`Jvp<F>`、`Vjp<F>`）要不要同样加。那是一个更大的问题，本页不碰。
- `floatgate` 那三例今天是靠 `liftedFrom` 守住的；若将来有第二处需要「这个签名是提升的」，本页的答案（B）会同时解决它，但那是 B 的**后果**，不是 B 的**理由**。
## 决策者答（2026-11-21 06:45）

**批 B**，同时**修正我 11-20 19:05 那条"不造标志，类型自己带着"**：§三 的三例证明它的前提不成立——切向类型与原值类型结构相同（`Tangent<float>` 就是 `float`，与 JAX 相同），所以"哪一半是值"读不出于签名本身，只能读于"这张图是 `jvp` 造的"那个事实（`GCGraph::liftedFrom`）。
- `Transpose<F>` 加的那一格**由 `liftedFrom` 推出、不由用户写**：类型法则从 `F` 所指的图读这个事实，`lift.h#liftedParametersOf` 仍是唯一入口。不得出现第二处存放"是否提升"的地方。
- 用户可见的写法不变（`transpose(f)` 不多一个参数）；若某处签名需要把这一格写出来，先列问题。
- `ports` 与 `promised` 之差此后要是**校验**：两者不等即拒并报两数，不允许任何一处按其中一个静默分配端口（§五 第 3 条说的那种静默差）。
- `Jvp<F>`、`Vjp<F>` 要不要同样加，按 §七 不在本页；若实现 B 时发现它们撞上同一处，带读数再问。
- `transpose.cpp` 剩的那一处 `tangentSpaceOf(...).hasSpace()` 同轮指出并处理。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

共同依据：原则 9.9（负责人 2026-11-22）"`jvp(f)` 的类型 `Lift<F>`……按类型解读提升后的参数表没有歧义……所以图上不记'这张图由哪次提升而来'"，以及核心抽象第 1、5、6 节。今天的 C++ `jvp`/`split`/`transpose` 在变换骨架一步重做。 决策者 11-21 07:25 已撤回"给 `Transpose<F>` 加一格"，改为"`lin` 只在切向上线性，提升签名的值半由 cut 作为残差捕获"（JAX `linearize` 的形状）；负责人 11-22 写进原则：残差是闭包的捕获（核心抽象第 5 节），`liftedFrom` 删除（9.9）。于是 `transpose` 不需要知道"是否提升"，`ports == promised` 的端口数校验作为骨架的不变式保留。
