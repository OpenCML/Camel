# 参与微分的那个参数自己的值，由谁交给转置？

**状态**：主目标**未达成**；本页是读数、最小改动面与三条路的代价。**默认建议**：不动代码（第 6 节丁），
把“值”这一半交给一条新裁（丙）。

写这页的原因：派工的前提是「把 `modules/transforms/transpose.cpp#buildTransposeMember` 的
`wantsCotangent` 兜底从 `return true` 改掉，实测翻 16 条，那 16 条的表是主交付」。**那张表在实时
`origin/develop` 上量不出来**：按字面翻兜底，8624 条里**一条都不动**（第 2 节）。把同一条裁定读在
“问”那一侧，能动 7 条，但**没有一条是靠兜底才对的那种“翻”**（第 3 节）。7 条 `known_fail` 真正缺的东西
既不是闭包成员的槽、也不是兜底，而是**参数自己的值**（第 4 节）。

---

## 1 两句裁定与今天的代码差在哪

| 裁定 | 原文 | 今天落到哪 |
| --- | --- | --- |
| 2026-11-03 17:35 / 19:35 | 「**由规则层当作已知操作数交给转置**」「**不要零载体**」「形状与长度必须由规则层当作已知操作数交给转置，绝不许从操作数的值里读」 | 形状那一半在 `lane-shapes`（`c6bbd67f5`，注释补充 `b7538f9cb`）；**这两个提交不在 `origin/develop` 里**（`git merge-base --is-ancestor c6bbd67f5 origin/develop` 为假；本树 `34b659107` 正是 `c6bbd67f5` 的父提交）。所以本页量的 `stdlib/rulesets/transpose.cml#mul_tensor_t` 仍是 `(sum_to(ct * w, x), sum_to(ct * x, w))`：`x`、`w` 既当被乘的数、又当 `sum_to` 的 `like` |
| 2026-11-03 19:55 | 乙：**闭包成员不给余切槽**；「修好之后 `wantsCotangent` 对闭包端口答 `false`」 | `wantsCotangent` 今天对闭包端口答 `true`，但**没有一处余切会落到闭包端口上**，所以那一行今天是**死行为**（第 2 节的两个探针） |

> 给协调者一条现状：派工简报说“形状那一半刚有人做完了”。它在 `lane-shapes` 分支上，**没合进
> `develop`**。本车道的基线因此不带它的两个新用例：`feat/compile` 今天是 2243 条，不是那条分支量到的
> 2245 条。

## 2 读数 A：翻 `wantsCotangent` 的兜底 = **0 条**（不是 16 条）

> **这一节的结论，一句话，请当作读数引用：**
> **「16 条」只适用于 `d1aa176ee` 之前的树；在今天的 `origin/develop`（`34b659107`）上，字面翻兜底的读数是
> 0 条（8422 条逐条比对，0 变动）；把同一条裁定读在“问”那一侧，读数是 7 条。**
> 记这一条的理由不只是数字：**一个“兜底”在它的消费者被删掉之后留在原地，看起来仍然像在兜底**。它不是
> 无害的死代码——它今天在纸面上支撑着“翻它会翻 16 条”这个结论，而那个结论会让下一个人不敢动它，也让
> 6 个 `known_fail_note` 写着一句今天不成立的话。

树 `D:\Projects\Camel-wt\knownop`（`lane-knownoperand`，基线 `34b659107`）；宿主 `88zjwei`（32 核）；
2026-11-04 07:31–07:44；宿主 load 40.36/28.33/22.13（1m/5m/15m，跑前）。闸门只走 `camel-remote.ps1`，
`-Tier fast -Jobs 4`，一次一个目标。

改动（**只用于测量，未提交，已 `git checkout --` 回退**）：

```cpp
        const auto wantsCotangent = [&](gc_node_ref_t target) {
+           if (closurePortOf.find(target) != closurePortOf.end()) {
+               return false;
+           }
            if (!partial || target >= nodeCount) {   // 兜底本来在下面，这里提到它前面
                return true;
            }
```

放在 `!partial` 早返回**之前**：被转置的 `split` 后半月是整函数转置，`partial` 为假，兜底原本一进函数
就答 `true`。

| 读数 | 基线 | 翻后 |
| --- | --- | --- |
| `test/plans/feat`（fast 层） | `total: 8624, pass: 8585, fail: 7, skip: 11, known failures: 21` | **一模一样** |
| 逐用例（8422 条带名字的结果） | — | **0 条变动**（`PASS`/`FAIL`/`XFAIL`/`SKIP` 逐条比对） |

构建确实带了改动，不是量到旧二进制：`[87/88] Building CXX object
modules/transforms/CMakeFiles/transforms_cmo_objects.dir/Release/transpose.cpp.o`，
`[STEP] Stamped out/latest/build-info.json: 34b659107b (dirty: 1 file(s)) …, fingerprint e6861dc7c928`
（基线 `6469cf0e84fd`）。

**为什么 0 条。** 两个探针（只用于测量，已回退）：

1. `modules/transforms/transpose.cpp#buildTransposeMember` 入口打 `PROBE-member … closure=N`：确有成员带闭包
   ——`function=split_of_jvp_of_g_backward closure=2`、`function=__g_322__ closure=1`；
2. `wantsCotangent` 入口打 `PROBE-closure-port-asked`：那几条用例上**一次也没打**。

原因是 d1aa176ee（`split/transpose: a read states its own facts, so no rule reads the linear operand`）删掉了
“按参数位置从闭包取值”那一段（`closurePorts[slot]`、`wantedValues`、`TRANSPOSE-NEEDS`），并加了
`reachesAParticipant`。**按位置取值在的时候，闭包端口才是余切的目标**；那一段没了，兜底那一行就没有
消费者——它今天既不掩盖真绿，也不撑起任何数。

## 3 读数 B：把乙裁读在“问”那一侧 = 7 条，仍无一条红→绿（除一条被旧标记压住）

同一裁定的第二种读法：闭包成员是已知值，**所以没有律该为它磨余切**。落到
`modules/transforms/transpose.cpp#buildTransposeMember` 的 `reachesAParticipant`（而不是落到
`wantsCotangent`）：去掉 `!partial` 的早返回，闭包端口不算 participant。**只用于测量，已回退。**

`test/plans/feat`（fast 层）：`8624 = 8583 pass / 14 fail / 11 skip / 16 known`（基线 `8585 / 7 / 11 / 21`）。
逐用例只有 7 条变动，全部落在 `test/plans/feat/compile/transforms.plan.toml`：

| # | 用例（`feat/compile/transforms`） | 方向 | 一句原因 | 属“被兜底掩盖”还是“靠兜底才对” |
| --- | --- | --- | --- | --- |
| 1 | `transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` | **红→绿**（答案 `(6.0)` / `(18.0)` 都对；只是 `known_fail` 标记还在，runner 报 `marked known_fail on 'nvm' but passes`，所以计成 FAIL） | 位置 0 的 `2.0*x` 只由常量与闭包成员算出，不问之后不再需要 `dx` 的值；位置 1 的答案读 `2.0*x` 的值，`forward` 从闭包端口给得出 | **被兜底掩盖**：兜底让位置 0 被问，问出来的是一句拒绝，把对的答案挡在后面 |
| 2 | `transpose_tuple_component.two_reads_of_one_tuple_add.nvm` | 绿→红 | 读数从 `3.0` / `(3.0)` / `(6.0)` 掉成 `3.0` / `(0.0)`，**静默零**；`x` 是参数，余切以前经参数槽进答案 | 靠：它靠的是参数槽，不是闭包（这一条说明“不问闭包”这条规则**不能**推广到参数） |
| 3 | `transpose_no_tangent.tuple_member.nvm` | 绿→红 | `(1.0)` / `(3.0)` 与 `type=int space=none` 全没了 | 同上 |
| 4 | `relu_gradient_through_the_library.nvm` | 红→红（拒绝换位） | 从 `position=0` 移到 `position=1`：位置 0 不再被问，位置 1 的答案要读位置 0 那个**残差**的值，而它在后半月里是一个**带切空间的参数**，也没有端口 | 靠兜底才停在位置 0的那句；换位后仍是同一句拒绝 |
| 5 | `exp_gradient_through_the_library.nvm` | 红→红（同上） | 同上 | 同上 |
| 6 | `sigmoid_gradient_through_the_library.nvm` | 红→红（同上） | 同上 | 同上 |
| 7 | `tanh_gradient_through_the_library.nvm` | 红→红（同上） | 同上 | 同上 |

`transpose_loop_escape.two_values_of_two_kinds` 与 `transpose_loop_escape.second_state_of_the_same_loop`
在基线就是红，这一改**不改善也不加深**（前者仍停在 `function=read_outer … position=0`）。

**结论**：#1 是唯一真正的红→绿，且它要的是“别去问一个只由已知值算出的东西”——这正是 19:55 那句话的
正面用法；#2/#3 说明同一条规则一刀切会**静默送零**，所以它只能落在“问”上、不能落在“收”上（与第 2 节的
0 条互为印证：收的那一侧今天根本没被用到）。

### 3.0 第 1 条那条标记查过了：**今天没过期**

协调者的怀疑是「`transpose_hand_closed_lambda` 今天已经绿了，只是标记还挂着」。**量过了，不是。**
在**没翻任何东西**的树上（`c84d3e17d`，`build-info.json` 里 `dirty: false`、`dirtyCount: 0`，
`footprint 261103_235935_c84d3e17d`）直接跑这一条：

```
$ out/latest/bin/camel test/cases/compile/transforms/transpose_hand_closed_lambda.cml std::nvm
cut ct1= (6.0)
cut ct3= (18.0)
TRANSPOSE function=__g_322__ skip=cotangent-needs-a-value uri=:op/mul_d position=0 reason=the rule's right side reads a value the caller does not carry
REAL_EXIT=1
```

- `cut ct1` / `cut ct3` 两行在，`hand ct1` / `hand ct3` 两行不在（`expect_output_contains` 的四条它只满足两条）；
- 退出码 1；
- 拒绝的原话与 `known_fail_msg` 逐字一致（`skip=cotangent-needs-a-value uri=:op/mul_d position=0`）。

所以**标记今天是有效的**，不是过期标记；它是第 3 节实验 3 之后才“本来能绿”。把标记摘掉要等答案真的变了
（也就是那条裁定落地）——**现在摘会把一条拒绝登记成缺陷**。

## 4 读数 C：缺的那个值到底是什么

在 `modules/transforms/transpose.cpp#buildTransposeMember` 的 `ruleCotangent`（`valueOf` 处）加探针，打印
“律要的那个值”的节点种类与归属，手工跑 5 条用例（宿主上 `out/latest/bin/camel <case> std::nvm`）：

| 用例 | 被转置的函数 | 律的槽 | 节点 | kind | closure | with | 端口=参数 | partial | 类型 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `transpose_hand_closed_lambda` | `__g_322__`（`(dx) => 2.0*x*dx`） | 1 | 0 | 1（Port） | 0 | 0 | 0 | 0 | `float` |
| `relu_gradient_through_the_library` | `split_of_jvp_of_total_backward` | 1 | 3 | 1（Port） | 0 | 0 | 0 | 0 | `float[]` |
| `transpose_array_product` | `split_of_jvp_of_energy_backward` | 0 | 4 | 1（Port） | 0 | 0 | 0 | 0 | `float[..]` |
| `jvp_vjp_array_product_cross` | `split_of_jvp_of_energy_backward` | 0 | 4 | 1（Port） | 0 | 0 | 0 | 0 | `float[..]` |
| `transpose_loop_escape_reads_outer` | `read_outer` | 1 | 0 | 1（Port） | 0 | 0 | 0 | 0 | `float` |

**五行是同一种东西**：`kind=Port`、`closure=0`、`with=0`、`port-param=0`、`partial=0` —— **参数 0 自己的值**，
而且是参与微分的那个参数（切向量 / 线性输入）。不是闭包成员，不是 `with` 参数，不是常量。

所以 7 条缺的不是槽的形状，是**一个在转置里根本不存在的值**：`Transpose<F>` 的答案按参数排，
被答余切的参数不占端口，于是律在 `position=0` 那一侧要读的对侧操作数（`ct * w` 的 `w`）没有来源。

## 5 “普通操作数”这条路今天通不通

### 5.1 成员内部（有调用点时）：通，管线已经写好

- `modules/transforms/transpose.cpp#transposeSignature` 的捕获表：给一个**参与微分的参数**在余切**之前**加一个
  值参数（原文 “It is the parameter's own value, in the parameter's own type … a value the member reads and
  the answer does not answer for”）；
- `modules/transforms/transpose.cpp#buildTransposeMember`：`capturesOf` 的每个下标做成端口（`capturePortOf`，
  原文 “the values the body asked for and no port gives it are capture ports of the member: one per
  parameter, of the parameter's own type, in the order the set names them”）；
- 调用点：`state.calleeNeeds[callee]` 有三处消费者（函数调用处的 `arguments.push_back(captured)`、
  `transposeSignature(callee->funcType(), participates, &answersOf, &state.calleeNeeds[callee])`、
  `transposesACut` 里的 `for (const size_t at : state.calleeNeeds[cut.backward])`），值由
  `forward(inputs[argumentAt + idx])` 转发；
- 规则层：`modules/transforms/rule_expand.cpp#expandRuleRightSide` 只对**选中位置的答案真正读到的**捕获调
  `valueOf(argument)`（原文 “const gc_node_ref_t value = valueOf == nullptr ? rt::kInvalidNodeRef :
  valueOf(argument)”，取不到即 `the rule's right side reads a value the caller does not carry`）；
  `valueOf` 由 `modules/transforms/transpose.cpp#buildTransposeMember` 的 `knownValue` 实现（常量直读，其余走
  `forward`）；`modules/transforms/rule_expand.cpp#expandRuleByName` 的注释也写明“哪一处捕获被问，由
  `expandRuleRightSide` 决定”。

**唯一断掉的一环是“记录”**：d1aa176ee 之后 `needs` 永远是空的（原文 “`needs` is therefore empty for every
member built here”），所以捕获表从来不会被填，调用点从来没有值可转发。把它接回去是**一个函数、一处**
（`forward` 里参数取不到值的那一支）。

### 5.2 没有调用点的地方：不通，而且不是本车道能改的

- 顶层 `transpose(f)`：`modules/transforms/transpose_operator.cpp#transposeOperatorSignature` **只读 `f` 的
  类型**，把 `transpose(f)` 的调用点写成 `(ct) => (cotangents)`；`modules/transforms/transpose.cpp#buildTransposeGraph`
  因此比对端口数（原文 “The graph must take exactly what the operator's own type says it takes … the build
  compares the two counts here … and refuses by name”），多一个值端口就按
  `NeedsAValueOutsideTheClosure` / `ParameterShape` 拒绝。
- **cut 的后半月有一个具体的缺口，且补它不需要动协议**：`transposesACut` 用 `buildTransposeMember(state,
  cut.backward, member, os, {})` 与 `transposeSignature(cut.backward->funcType(), {}, …)`（原文 “`participates`
  is empty -- every parameter of the second half is a linear input”），于是后半月里**携带残差的那些参数**
  （它们有切空间：`float[]`、`float[..]`）也被当成线性输入。而“不给端口、由调用点填值”的那条路**已经存在**
  同一函数里：只对**没有切空间的参数**（`space.hasSpace()` 为假）`continue`，其余取
  `forward(call.arguments[half + at])` 当值参数。所以第 4 节里 #4–#7 的下一步是**一处调用点 + 一个谓词**：
  让后半月把“残差参数”排除出参与集（值参数），不去问它们的余切。

### 5.3 已有页第几节说了什么

`docs/handover/questions/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md`：

- **§4**：19:55 的改法是“`forward` 遇到闭包端口就转发成成员自己的闭包端口”——**今天已经实现**
  （`modules/transforms/transpose.cpp#forward` 第一段就是 `closurePortOf` 查找，`forwardInArm` 同）。所以那一节
  给的地图已经走完，7 条仍卡在别的节点上（第 4 节：那是参数，不是闭包）。
- **§4.1**：恢复“按位置读”（`closurePorts[slot]` + `wantedValues` + `TRANSPOSE-NEEDS`）能让 7 条回号码且不动
  那 2 条；只有“恢复交”（`needsOut.released`）会把 `transpose_many_parameters` 与
  `pow_gradient_refused_by_the_library` 弄红。本页第 5.1 节的“一个函数一处”正是那一段里最小的一步。
- 该页 §1.3 的“一翻 16 条”是 **d1aa176ee 之前**的代码状态（那时闭包端口经 `closurePorts[slot]` 收余切）；
  在今天的 `develop` 上，**收的那一侧翻不动任何东西**（第 2 节）。

### 5.4 实验 4：把“记录”接回去（第 5.1 节的一处）——不合判据，已回退

改动（**只用于测量，已 `git checkout --` 回退**）：在 `ruleCotangent` 的 `valueOf` 里，当律要的值取不到、
而它正是某个**参与微分的参数**的端口时，把这个参数下标记进 `askedValues`；由一个 RAII 在
`buildTransposeMember` 的每条出口把它交给 `needs`（`needs` 只有探针那一趟会传）。签名、捕获端口、三个调用点
一处没动——这就是 §5.1 说的“一个函数、一处”。

`test/plans/feat/compile/transforms`：`105 = 97 pass / 8 fail / 0 known`（基线 `98 / 2 / 5`）。

| 判据 | 结果 |
| --- | --- |
| #2 `transpose_tuple_component.two_reads_of_one_tuple_add` 不许变静默零 | **守住**：仍绿（`add` 的律不读操作数的值，成员没多出端口） |
| #3 `transpose_no_tangent.tuple_member` 不许变静默零 | **守住**：仍绿 |
| #4–#7 四条不许“红得不对” | 仍红，但**句子换了**（`known failure changed its mind: expected "skip=…position=0", got "expected exit 0, got exit 1"`） |
| #1 若变绿按过期标记处理 | **没变绿**：`TRANSPOSE function=__g_322__ ports=2 promised=1 cause=needs-a-value-outside-the-closure`——成员确实拿到了第二个端口（参数的值），**`buildTransposeGraph` 按端口数把它拒了**。这是第 5.2 节那道墙的现场证据 |
| 不许超出上面四条 | **越界**：`transpose_many_parameters` **PASS→FAIL**（它断言的原句 `skip=cotangent-needs-a-value uri=:op/mul_d position=0` 变成了 `autodiff::mul_tangent<float>` 上的 `position=1`）；另有 `__g_323__ skip=call-no-transpose node=4 callee=mt` 这种新句子 |

结论：**按判据停手，回退，取默认丁**。它同时给出一条有价值的读数：**把值接进成员是能做的**
（`ports=2` 造出来了），**卡住的是顶层 `Transpose<F>` 的名字**——这正好把“值这一半”指到丙。

> 同一实验还纠正了本节 5.2 自己的一个说法：第 4 节的探针显示缺的那个端口**就是切向量参数**（`float[]` /
> `float[..]`，`port-param=0`），不是“残差参数”。所以“把残差参数排除出参与集”的谓词不能修 #4–#7——它们
> 要的恰恰是那个必须被答余切的参数的值。

## 6 四种裁法与代价

| 裁 | 做法 | 代价（量过的） |
| --- | --- | --- |
| **甲**：闭包成员给一个槽 | `Transpose<F>` 的答案按“参数 + 闭包成员”排，并恢复按位置取值 | 答案形状变：`transposeSignature` 造答案处、`split.cpp#answersAPair` 的对半切、每个调用点的 arity 检查；2 条用例的数字要改 |
| **乙**：闭包成员无槽 | 兜底答 `false` | **今天 0 条**（第 2 节，8422 条逐条比对）；读成“问”那一侧则 **7 条变动：1 红→绿（被旧标记压住）+ 2 绿→红 + 4 换句子**（第 3 节） |
| **丙**：让 `Transpose<F>` 的名字带上被转置函数的值 | 成员签名 `(values…, ct) => (cotangents)`，值由调用点填 | 要动 `stdlib` 的 `Transpose` / `transpose` 声明、`transposeOperatorSignature`、`buildTransposeGraph` 的端口数守卫、`vjp` / `grad` 的调用点，**以及 `jvp` / `vmap` / `transpose` 共用的选律协议**（`rule_expand.h#rulePortsOf` 一族）——超出本车道允许面（`jvp.cpp`、`split.cpp` 不得碰） |
| **丁（默认）**：不动代码 | 本页 + 第 5.2 节那条“一处调用点 + 一个谓词”留给下一个车道 | 4 条 `known_fail` 继续红；读数与标记一致 |

默认选**丁**的理由：乙的两种读法都量过——一种是 0 条，另一种是 1 绿 + 2 红 + 4 换句子；丙要动三个 transform
共用的协议，本车道没有那个授权，硬做的风险是把 19 条 `feat/transforms/split` 一起翻。**“可靠地落地”做不到**，
所以按派工的降级条款：交页，主目标记未达成。

## 7 主目标：未达成

- `mul_tensor_t` 在**广播**下两侧同数：**没做到**。位置 0 要读 `h` 的值、位置 1 要读 `x` 的值
  （`stdlib/rulesets/transpose.cml#mul_tensor_t` 今天仍这么写），两者都是参与微分的参数，顶层 `transpose`
  没有位置交给它们。形状那一半（`sum_to_stated(ct * w, shape(x))`）能换掉 `sum_to` 的 `like`，**换不掉
  `ct * w` 的 `w`**。
- 手算（跑之前写的）：`x: float[2,2,3]`、`h: float[3]`、`L = sum(x*h)` 时 `dL/dx[i,j,k] = h[k]`
  （`h = (1,2,3)` 时是 12 个数：`1 2 3` 四遍），`dL/dh[k] = Σ_{i,j} x[i,j,k]`。今天这两条读数没有任何路径
  算得出来：两侧都停在 `skip=cotangent-needs-a-value`。
- 基线读数（本树 `34b659107`，`lane-shapes` 未合入；宿主 88zjwei，2026-11-04 07:19–07:44，load 40/28/22）：
  `feat/compile` 2243 = 2230 / 2 / 11；`feat/compile/transforms` 105 = 98 / 2 / 5；
  `feat/transforms` 29 = 21 / 0 / 8；`feat/transforms/split` 19 = 19 / 0；
  `feat/modules/autodiff` 263 = 263 / 0；整棵 `feat` 8624 = 8585 / 7 / 11 skip / 21 known。
- 本页只新增一个 `docs/` 文件，不改代码，所以上面每个读数都仍是终态读数。

## 要作者答的一句

**「`transpose(f)` 的调用点类型是 `(ct) => (cotangents)`，而被转置的函数里，参与微分的那个参数的**值**
是律要读的第二个操作数（`mul_tensor_t` 的 `ct * w`）。要它进成员，只能让 `Transpose<F>` 的名字带上那些值
——那要同时改 `transpose`、`jvp`、`vmap` 共用的选律协议。是开一条丙的车道，还是让“值参与乘法”的律在
转置里一律拒绝、只留能写成形状的部分（今天的丁）？」**
