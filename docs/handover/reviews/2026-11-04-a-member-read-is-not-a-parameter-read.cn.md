# 读一个参数的成员，不是读这个参数：`transpose` 该跟到端口吗

## 问句

`transpose` 遇到「一个参数的成员被读」（struct 按名字 `qq.a`，元组按下标 `q.0`）时，该把这个 `Accs` 的余切
算成**那个端口的成员余切**（于是 struct / 元组的余切拼得出来），还是继续按「这个参数从未被读」处理？

## 现在的状态（树 `splitnext`，base `9c6e41182`，2026-11-04 04:2x +08:00，主机 load 18.49 / 21.86 / 21.59）

`split` 本轮把 struct 成员读的 key 抄过去了（`2026-11-04-a-struct-field-read-refused-or-copied-by-key.cn.md`），
所以这个形状第一次走得到 `transpose`。两条读数（探针不入 git，`std::nvm`，`-Tier fast`）：

| 形状 | 程序 | 读数 |
| --- | --- | --- |
| struct 成员**按名字**读 | `type Point = { a: float, b: float }`，`g(qq: Point) = qq.a * 4.0 + qq.b`，`vjp(g)({ a: 2.0, b: 3.0 })` | 切得动：`REINTERP split function=jvp_of_g forward=348 backward=349 type=(Point) => (float, (Point) => float pure) pure residuals=5`；两条规则也转得动：`TRANSPOSE function=autodiff::mul_tangent<float> graph=382`、`add_tangent<float> graph=380`；**后半被拒绝**：`TRANSPOSE function=split_of_jvp_of_g_backward skip=unread-parameter port=5 type=Point`，句子「transforms: transpose has no transpose for this function: a parameter is never read by the body, so it answers no cotangent」，落点 `stdlib/vjp.cml#vjp` 那次 `transpose(lin)`，exit 1 |
| 同一形状写成**元组、按下标读**（对照） | `type Pair = (float, float)`，`g(q) = q.0 * 4.0 + q.1`，`vjp(g)((2.0, 3.0))` | 切好之后**崩**：stdout 空，stderr 的最后一行是 `REINTERP split function=jvp_of_g`、一行 `TRANSPOSE` 都没有，测试跑成「killed by a signal」。**`afb9d40dc` 原样上一样崩**：本轮没有动过 `makeCopy` 的 `TupleIndex` 那一格 |

机制一句话：`modules/transforms/transpose.cpp#buildTransposeMember` 把余切记在**节点**上
（`hasCotangent[ref]`），而问「这个参数被读过吗」问的是**端口节点**（`hasCotangent[ports[port]]`）。成员的
`Accs` 拿到余切，端口拿不到，于是 struct 落到「有切空间、没有余切、零也构不出来」那一档被拒
（同一档里 `float` 答 0、数组答 `:tangent/zeros_arr`），元组更差 —— 它连那句话都没走到。

## 三条路与代价

- **甲**：把成员读算回端口 —— walk 见到 `Accs`（两种 kind 都算）时，把余切记到「端口 + 成员路径」上，参数那一档
  按成员拼（`Tangent<Point>` 就是成员切线的 struct，设计原则 9.9），一个成员都没读到的参数仍走零余切那一档。
  代价：`buildTransposeMember` 里要加一层**路径**（哪条投影到哪个成员），嵌套 struct / 数组要递归；好处：
  struct 与元组一起修，**元组那条 SIGSEGV 也一起没了**，且 `vjp` 给出 4.0 / 1.0 这两个真值。
- **乙**：只补「复合空间的零余切」——像 `:tangent/zeros_arr` 那样再造一个 struct 的零。代价：小，但**元组的崩不动**，
  而且它把「读到了什么」当「读到 0」：`vjp` 会静默答 `{ a: 0.0, b: 0.0 }`，那是**错数**，比今天的拒绝更差。
- **丙**：维持拒绝，只把句子改准（「a parameter whose members the body reads」）。代价：`vjp` 对任何读 struct
  成员的函数都不可用（常见形状）；好处：一句话说得清，且今天的读数就是它。

## 默认建议

**甲单开一条车道**，先钉读数再改归属：两条用例（struct 现在拒绝、元组现在崩）改成期望 `vjp y= 11.0` /
`dq.a= 4.0` / `dq.b= 1.0`。`modules/transforms/transpose.cpp` 不在本 lane 的可动范围，所以本 lane 只把读数写在这里。
甲做成之前丙是正确答案；**元组那条（崩）比 struct 那条（拒绝）更该先修** —— 按「对 > 明确拒绝 > 错数 > 崩」，
它低了两档，而且在 `afb9d40dc` 上就已经是崩的。