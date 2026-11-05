# 声明的类型与运行时的装箱不一致时，改窄声明还是教会消费者：`mean_axis_stated_t` 与六个轴归约的两个实例

**状态**：作者已裁，且**裁定推翻了本页第 3 节的落地方式**（2026-11-04 04:20，`docs/handover/reviews/questions-2026-10-29.md`「三、秩 0：只有一个转换点」，原文：“规则的端口照实写 `T[..]`，不把每条 `*_stated` 规则都约定成写 `ct: T`，那样同一件事就成了 N 处约定……由编译器在可赋值转换那一处统一做表示上的转换……**不在 `ruleCotangent` 或各条规则里各补一次**”）。实例 A 仍按“教会消费者”收束（`18dd544af`，第 5 节）；实例 B **不再收窄**：端口回到 `(ct: T[..])`，`scripts/law-cotangent-ports.mjs` 的许可（`isRankZeroEquivalence`）已删除，秩零那一对现在与其它不一致一样**被报**（第 4 节重写）。本页保留路 2 的代价读数与路 3 的原文，作为“为什么当时那样做、为什么不成立”的记录。

**问句（一句）**：当一个归约答案的秩只有运行期知道时，它的余切的秩该由**律的端口**给（声明成元素，让语言在下一次调用处装箱），还是该由**产生余切的那一步**给（端口声明成数组，装箱放在还知道秩的地方）？

## 1. 今晚的同一个母题，两个实例

| | 实例 A：六个轴归约的声明 | 实例 B：`mean_axis_stated_t` 的余切端口 |
|---|---|---|
| 提交 | `768cd6436`（`45f63e472` 带进） | `8fa4f742a`（同一个合并） |
| 动作 | 删掉 `sum_axis` / `mean_axis` / `min_axis` / `max_axis` / `argmax_axis` / `argmin_axis` 的 CML 声明，改由算子推断（`modules/tensor/ops/reduction_ops.cpp#reduceAxisInfer`） | 把律的余切端口从 `(ct: float[..])` 改成 `(ct: T)` |
| 动机 | 声明写不出“随 `axis` 与 `keepdims` 变”的形状，六条 `*_axis` 的答案一直是 `T[..]`，连 `multiply(x, w)` 都跟着答 `float[..]` | 端口是 `float[..]` 时，秩零答案被当指针读，8/8 崩在 `numelKernel` |
| 结果 | 类型诚实了，**代价落在消费者**：`std::cpp` 拒 4 个程序，`feat/trans/cpp` 6 条红 | 崩没了（8/8 → 8/8 通过），**代价落在声明**：端口对“答案有秩”的那一种形状不成立，lint 报不一致 |
| 收束 | **教会消费者**（`18dd544af`，第 5 节） | **许可收窄**（第 3 节的路 3，已落地） |

两个实例是一条轴的两端：A 是把类型做诚实、再让消费者学会新形状；B 是把声明改窄、绕过一个还没学会的消费者。作者的判据把两者分开了——**同一个提交既可能是回退的理由，也可能是补齐消费者的理由，区别在于它让类型更诚实还是更含糊**。A 更诚实；B 那句 `(ct: T)` 也不含糊，它说的是 5.4 已经规定的那个等价（见下），含糊的是**检查器**。

## 2. 这条 lint 之前为什么报，报的为什么是修复

裁定之前，`node scripts/law-cotangent-ports.mjs`（`test/plans/feat/docs_links.plan.toml#docs.every_laws_port_agrees_with_its_body`）的原文是：

```
stdlib/rulesets/transpose.cml:mean_axis_stated_t: cotangent port is 'T' but the body hands it to
'mean_axis_cotangent', which reads an array (stdlib/rulesets/transpose.cml#mean_axis_cotangent,
parameter 1 `ct: T[..]`)
1 finding(s) in 137 law(s) across 5 file(s): the port a law promises is not the one its body reads
```

它说的是真的（body 确实把 `ct` 交给一个 `T[..]` 形参），但**在修之前的树上它是绿的**：那时端口与 body 同说“数组”，而那条树 8/8 崩。所以它报的**恰好是修复本身**，不是它存在的那个缺陷。

两半拆开看：它只能见证“端口与 body 不一致”；而今晚的崩属于另一半——“端口与 body 一致、但与 transform 交过来的值不一致”（`ruleCotangent` 在 build 期定类型，源码里没有见证）。这个半句写在脚本自己开头，`docs/handover/questions/2026-11-04-where-a-laws-port-disagrees-with-the-value-it-is-handed.cn.md` 记着。

裁定改的是**判据本身**，理由是 5.4：**秩零数组就是它的元素**（同一个值、同样的打印、同样的比较），语言在“值进入数组位置”的每处都做这次转换（`ArrayType::castSlotFrom` → `Array::createRankZero`，由 `Builder::coerceTo` 插入；导出器写它写的就是 `rt::Array<T>::element`，见 `18dd544af`）。所以“元素端口交给同一元素的**无前缀开秩**形参”不是不一致——**语言已经把这两个拼写当成同一个值，那是唯一还在说它们不同的地方**。

## 3. 三条路与裁定

**路 1：端口保持 `(ct: T)`，改 body 让它不把 `ct` 当数组读。** 长度从 `at` 算（`extents[axis]`），余切只以标量身份参与（`spread_over(ct / lanes, e)`）。**代价**：只服务“答案是秩零”的形状；同一个 key 也会被数组余切进入（`8fa4f742a` 量到的反例 `mean_axis(x: float[2, 3], 1, true)`，余切是 `[2]`，每位 `1/3`），标量语义的 body 对它**静默**给出另一个数。要两种形状都服务就得两条律，而声明索引当场拒（`2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md` §3）。**未采纳。**

**路 2：端口改回 `(ct: T[..])`（与孪生 `#sum_axis_stated_t` 同型），装箱放到还知道秩的地方。** 装箱两条候选：`*_stated` 拼写的 pullback（`modules/tensor/ops/**`），或让 `#numel` 的端口收元素。**代价（量过的，留作证据）**：`develop` 上 8 条 stated 用例回到 `total: 8, pass: 0, fail: 8, killed by a signal: 8`，直到装箱落地；两步之间 `develop` 不能推。**未采纳**，理由三条：

1. **这个代价今晚已经付过。** 为了一条 lint 把 8 条 SIGSEGV 请回来，是拿最坏的一类换一条账面。
2. **那个 `T` → `T[..]` 转换是真的**，而且 `18dd544af` 刚刚教过导出器写它（`rt::Array<T>::element`）。5.4 已经让“元素”与“无前缀开秩的秩零数组”是同一个值，**那 lint 就是唯一还在说它们不同的地方**。
3. 那 8 条是**量出来的**（`value=2.5`、`dL/dx=[0.25, 0.25, 0.25, 0.25]`、反例 `[[0.3333… ×3], [0.3333… ×3]]`）；lint 那句 “which reads an array” **没有量过运行时真正的装箱**——它是一句判断，不是一条读数。

**路 3（当时采纳，2026-11-04 04:20 被作者推翻）：lint 的判据允许“端口是元素、body 交给无前缀开秩形参”，三条同时满足才允许。** 落地形态留着，供对照：

- 端口是**裸元素类型**（`T`，或 `float` 这样的标量名）——**不是** `T[3]`，**不是** `T[..]`；
- body 把它交给的形参是**无前缀开秩**（`T[..]`，**没有**固定 extents；`T[3]` 一律不算）；
- **元素类型相同**（`T` 交 `T[..]`，不是 `T` 交 `U[..]`）。

**推翻的理由（作者原文，见 §3 的状态行）**：许可把“元素与无前缀开秩是同一个值”写成了**每一条 `*_stated` 律都要记住的约定**，而 5.4 已经把这次表示转换规定成**编译器在可赋值转换那一处**统一做的事（和 `T` 进 `T?` 时自动包一层同一种机制）；写进 lint 就变成 N 处约定，下一次谁少写一处就又要一段新许可。**所以撤销的是许可，不是那条转换**：端口照实写 `T[..]`（与孪生 `#sum_axis_stated_t` 同型），秩零答案由编译器装箱。

**撤销之后判据怎么变**：`isRankZeroEquivalence` 删除，`checkLaw` 不再滤任何等价用法，秩零那一对（端口 `T`、body 读数组）与其它不一致一样报。**两个负例都还在**：固定 extents（`element_port_fixed_extent.cml`）仍红，数组端口交标量（`array_port_spread.cml`）仍红；原来那条“秩零对不报”的正例翻成**第三个负例**（`element_port_open_rank.cml`，端口 `T` + body 读数组 → 必须报），而库本身**天然绿**，因为端口已经写成答案的样子（自测与库的读数见第 6 节）。曾用来演示“许可看不见的那一格”的探针 `element_port_many.cml` 随之删除：那一格是许可造成的，许可没了，它就不是一个可允许的形状了。

## 4. 裁定的落法（已落地）与它看不见的那一格

落地的是**撤销许可**：`scripts/law-cotangent-ports.mjs` 一处判据（删 `isRankZeroEquivalence`、`checkLaw` 不再滤等价用法、探针集合改为三个负例＋一份按库里写法写成的 clean control、开头说明文字改写）、`stdlib/rulesets/transpose.cml` 一处端口（`mean_axis_stated_t` 回 `(ct: T[..])`，注释按裁定重写）、`test/plans/feat/docs_links.plan.toml`（两条 port-check 的说明与期望重写，原“元素端口可进同元素开秩”那条改成钉住 `mean_axis_stated_t` 的 `--explain` 行）。

**这一格现在由谁管**：不是 lint。端口写对以后，剩下的风险是**值到端口之间**的那半句（transform 在 build 期交过来的类型），源码里没有见证，lint 也读不到——它记在 `docs/handover/questions/2026-11-04-where-a-laws-port-disagrees-with-the-value-it-is-handed.cn.md`。今晚实测：端口回到 `T[..]` 后这条路径**不再崩**，转换点在 `camel/core/type/composite/array.h#isRankZeroEdge`（`ArrayType::castSlotFrom` → `Array::createRankZero`），`numel` 对标量进入 `float[..]` 位置答 1、`shape` 答 `[]`。

## 5. 实例 A 已经按“教会消费者”收束（本树实测，供对照）

`std::cpp` 拒的 4 个程序 = 3 个 `T → T[..]` 的转换（`sum_axis_rank`、`mean_axis_rank`、`mean_axis_elements`）与 1 个 `tensor:argmax_axis`（`arg_axis_rank`）；`agree_with_fvm` 与 `every_expect_program_links_no_runtime` 的 6 条红是这 4 个的聚合面（171 个一致 / 170 个 standalone / 4 个 “links the runtime and is not in the baseline”）。

`18dd544af` 只动导出器，两处：`src/passes/trans/cpp/function_emitter.cpp#FunctionEmitter::lowerCast` 写出 `rt::Array<T>::element`，`src/passes/trans/cpp/operator_rules.cpp` 的 `AxisRule` 与 `tensor:mean_axis` 对“答案是元素”的形状写 `rt::{max,min,argmax,argmin,mean}Axis(...)[0]`（一条车道就是整个 run 的走法，空车道报告的措辞不变）。语言面、rulesets、用例期望一个都没有改。

读数（树 `cppred` @ 88zjwei，`-Tier fast -Jobs 4`，2026-11-04 11:0x，宿主 1m load 11.35–13.82，高于静默阈值）：

| 判据 | 修前 | 修后 |
|---|---|---|
| `test/plans/feat/trans/cpp` | total 55，pass 49，**fail 6** | total 55，pass 55，**fail 0** |
| `test/plans/feat/modules/tensor` | —（本树的红在 `feat/trans/cpp` 的同名用例上） | total 117，pass 117，**fail 0** |

`agree_with_fvm` 把每个程序编出来跑一遍并与 `std::fvm` 逐字节比，所以这 4 个程序的新形状是被执行过、比对过的。

## 6. 读数与它们的出处

**撤销许可之后的读数（本树实测，2026-11-04 12:2x–13:0x，`rank0point` @ 88zjwei，宿主 1m load 23–27，不静默）**

`node scripts/law-cotangent-ports.mjs --self-test`（探针写在系统临时目录，跑完删掉）：

```
element_port_fixed_extent.cml:element_port_fixed_extent: cotangent port is 'T' but the body hands it to 'three_lanes', which reads an array (element_port_fixed_extent.cml#three_lanes, parameter 1 `ct: T[3]`)
array_port_spread.cml:array_port_spread: cotangent port is 'T[..]' but the body hands it to 'spread_over', which takes an element (array_port_spread.cml#spread_over, parameter 1 `ct: T`)
element_port_open_rank.cml:element_port_open_rank: cotangent port is 'T' but the body hands it to 'lanes_of', which reads an array (element_port_open_rank.cml#lanes_of, parameter 1 `ct: T[..]`)
self-test: a port 'T' handed to a parameter 'T[3]' is reported, a port 'T[..]' handed to a parameter 'T' is reported
self-test: the rank-zero pair 'T' -> 'T[..]' is reported, and the two laws of the library as they are written are not reported
self-test: all three disagreements are caught, in both directions and in the open rank, and the laws of the library are not -- a port and its body are compared and nothing is allowed
```

`node scripts/law-cotangent-ports.mjs`（全库）：`no law's port disagrees with its body or its left side: 137 law(s) in 5 file(s), 170 port(s), 9 with no witness on either side (counted, not passed)`，exit 0 —— 端口回到 `T[..]` 之后它**天然绿**，不再需要许可。

`node scripts/law-cotangent-ports.mjs --explain` 里那一行（`mean_axis_stated_t`）：

```
mean_axis_stated_t     ct: T[..]              mean_axis_cotangent(ct, at)
                           array (stdlib/rulesets/transpose.cml#mean_axis_cotangent, parameter 1 `ct: T[..]`)
```

`test/plans/feat/docs_links.plan.toml`：`total: 21, pass: 21, fail: 0`（含重写的那两条）。
`test/plans/feat/compile/stated_extents.plan.toml`：`total: 18, pass: 15, fail: 1, known failures: 2` —— 那一条红是 `stated_mean_axis_gradient`（本例的 `keepdims` 按裁定改回 `false` 之后露出来的），它撞的是**另一件事**：切口出口上的秩零切向量由谁定类型，见 `2026-11-04-who-types-the-rank-zero-tangent-at-a-cuts-exit.cn.md`。所以本页的撤销**没有**把那 8 条重新弄崩：`killed by a signal` 一条都没有。

**引用的读数（本页未重跑）**

- 8/8 SIGSEGV 与恢复：`8fa4f742a` 的提交信息（树 `~/camel-work/statedsig`，2026-11-04 06:5x–07:1x，load 15.0–21.5）：修前 `total: 8, pass: 0, fail: 8, killed by a signal: 8`，修后 `total: 8, pass: 8, fail: 0`，答 `value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]`；反例 `mean_axis(x: float[2, 3], 1, true)` 读 `[[0.3333… ×3], [0.3333… ×3]]`。
- 崩溃帧原文（`numelKernel`、`NodeVMSchedPass::evalNativeOperator` 等 10 帧）与 `1.59513487970007e-310` 的静默错数：`2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md` §1–§2。
- 声明在基线存在、在 `768cd6436` 消失：`git show 34b659107:stdlib/tensor.cml` 有六条 `export func *_axis::<T>(xs: T[..], axis: int, keepdims: bool): T[..]`（`argmax_axis`/`argmin_axis` 为 `int[]`），`git show 768cd6436:stdlib/tensor.cml` 一条都没有。