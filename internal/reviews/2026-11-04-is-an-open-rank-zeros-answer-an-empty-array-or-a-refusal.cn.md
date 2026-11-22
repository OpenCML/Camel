# 开秩数组的零，是空数组还是明确拒绝

2026-11-04 06:35 — 06:40，`integ2/rules-batch2` @ `81c748979`，主机 `88zjwei`，`-Tier fast -Jobs 4`，
1m load 约 16 — 读数属于这棵树这一刻。

## 已经是真的（别重做）

- `modules/transforms/transpose.cpp#zeroOfType` 现在对“类型里有开放式维度”的参数**答一个空数组**：
  维度取 0，元素为空，“每个元素都是零，因为没有元素”。代码注释里写着这条规则是**照抄
  `jvp` 的 `zeroConstantFor`**（`modules/transforms/jvp.cpp`）——“the same rule, in the same words”。
- 同一条规则今晚已经在别处被用过：`liftGraph` 给 `with` 权重的切向写的就是 `zeroConstantFor(...)`
  的零常数（那条“6 个不说话就答错数”里的第 6 个）。
- 所以 `transpose(g)`（`g(x: float[], n: float) = n * 2.0`）答 `([], 2.0)`、exit 0，**不是回归**：
  是 `lane-zerocrash` 故意把 transpose 的零统一到 `jvp` 的规则上。

## 反着的一句话（同一棵树里也有）

`test/cases/compile/transforms/vjp_const_function_open_rank_zero_is_refused_by_a_real_array.cml`
钉的是**开秩参数上的 `vjp(常函数)` 被拒绝，理由是要一个真数组**（`autodiff: a zero tangent array
needs an array with a tangent` 的后续措辞）。它现在是绿的。

于是同一棵树里有两句话：`transpose` 的开秩零答 `[]`，`vjp` 的开秩零要“真数组”。**两条都绿**，因为
它们走的不是同一条路（`vjp` 那条走 `:tangent/zeros_arr` 的注册表，transpose 这条走类型）。

## 没定的事

1. 一个长度没人说过的数组，它的“零”到底该是什么：**空数组**（“每个元素都为零”），还是**拒绝**
   （“零的形状没法从类型得出来”）？
2. 如果选空数组，`vjp` 那条拒绝要不要一起改口径（否则同一个概念两句话）。
3. 如果选拒绝，`jvp` 的 `zeroConstantFor` 也要一起改——那是**三处**，不是一处。

## 我这一轮怎么处置（默认建议）

- **不改实现**。这条属于“语义没定”，按铁律交作者；实现侧现在是自洽的（都以 `zeroConstantFor`
  为准），改了会同时在 `jvp`/`transpose`/`vjp` 三处留下半截。
- **用例钉住现状并写明它换过句子**：`transpose_unread_array_parameter_stops_earlier` 从
  negative（exit 1 + 旧句子）翻成 behavior（exit 0 + `([], 2.0)`），desc 与 case 注释里保留旧句子
  并说明**为什么换**——一条“拒绝”变成“答数”的用例，最怕的是没人知道它换过。
- 推荐：**保 `[]`**（与 `zeroConstantFor` 一致，且 `[]` 是“零”在开秩类型上唯一能写下的表示），
  把 `vjp` 那条拒绝的措辞改成“这次调用需要一个真数组”而不是“零不存在”。