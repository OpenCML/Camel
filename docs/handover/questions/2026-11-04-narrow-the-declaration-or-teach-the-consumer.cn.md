# 声明的类型与运行时的装箱不一致时，改窄声明还是教会消费者：`mean_axis_stated_t` 与六个轴归约的两个实例

**状态**：真矛盾，不是 lint 太严（第 2 节）。**三条路与代价见第 3 节，我方默认见第 4 节**（默认＝路 2，它让“端口声明的类型”等于“运行时的装箱”）。本页只写 A 组的实证与 B 组的路；A 组的修已经在 `18dd544af` 里落地并量过（第 5 节），B 组一行代码都还没动。

**问句（一句）**：当一个归约答案的秩只有运行期知道时，它的余切的秩该由**律的端口**给（声明成元素，让语言在下一次调用处装箱），还是该由**产生余切的那一步**给（端口声明成数组，装箱放在还知道秩的地方）？

## 1. 今晚的同一个母题，两个实例

| | 实例 A：六个轴归约的声明 | 实例 B：`mean_axis_stated_t` 的余切端口 |
|---|---|---|
| 提交 | `768cd6436`（`45f63e472` 带进） | `8fa4f742a`（同一个合并） |
| 动作 | 删掉 `sum_axis` / `mean_axis` / `min_axis` / `max_axis` / `argmax_axis` / `argmin_axis` 的 CML 声明，改由算子推断（`modules/tensor/ops/reduction_ops.cpp#reduceAxisInfer`） | 把律的余切端口从 `(ct: float[..])` 改成 `(ct: T)` |
| 动机 | 声明写不出“随 `axis` 与 `keepdims` 变”的形状，六条 `*_axis` 的答案一直是 `T[..]`，连 `multiply(x, w)` 都跟着答 `float[..]` | 端口是 `float[..]` 时，秩零答案被当指针读，8/8 崩在 `numelKernel` |
| 结果 | 类型诚实了，**代价落在消费者**：`std::cpp` 拒 4 个程序，`feat/trans/cpp` 6 条红 | 崩没了（8/8 → 8/8 通过），**代价落在声明**：端口对“答案有秩”的那一种形状撒谎，lint 报不一致 |
| 收束 | **教会消费者**（`18dd544af`，第 5 节） | **待裁**（第 3 节） |

两个实例是一条轴的两端：A 是把类型做诚实、再让消费者学会新形状；B 是把声明改窄、绕过一个还没学会的消费者。**本页问的是这条轴往后按哪端走**。

## 2. 为什么 B 不是 lint 太严

`node scripts/law-cotangent-ports.mjs`（`test/plans/feat/docs_links.plan.toml#docs.every_laws_port_agrees_with_its_body`，在本树跑）：

```
stdlib/rulesets/transpose.cml:mean_axis_stated_t: cotangent port is 'T' but the body hands it to
'mean_axis_cotangent', which reads an array (stdlib/rulesets/transpose.cml#mean_axis_cotangent,
parameter 1 `ct: T[..]`)
1 finding(s) in 137 law(s) across 5 file(s): the port a law promises is not the one its body reads
```

它说的是真的，而且有两条独立的见证：

- **body 见证**：`stdlib/rulesets/transpose.cml#mean_axis_stated_t` 的右手把 `ct` 交给 `#mean_axis_cotangent`，那里的端口是 `T[..]`，函数体第一句是 `let lanes = float(extent_product(extents)) / float(numel(ct))`；`numel` 的端口是 `T[..]`（`stdlib/tensor.cml#numel`）。今天的通过靠的是语言在**这个调用点**插入 `T` → `T[..]` 的转换（`ArrayType::castSlotFrom` → `Array::createRankZero`），于是 `numel(ct)` 答 1。
- **左右型见证**：`mean_axis_stated` 自己声明 `(t: T[..], at: int[]): T[..]`，它的余切因此是数组；而 `at` 是运行期的值，**没有调用点能说出答案的秩**（`768cd6436` 的提交信息自己写了这一条），所以这个 `T[..]` 不是占位符。

于是矛盾是硬的：**端口真是 `T`（元素）时，`mean_axis_cotangent(ct)` 的静态类型是错的，今天的绿是靠“端口类型与运行时装箱不一致”撑住的；端口真是数组时，秩零值会被当指针读，SIGSEGV 回来。** 更要紧的一句：**在修之前的树上这条 lint 是绿的**（端口与 body 同说“数组”），**它今天报的是修复本身**，不是它存在的那个缺陷。

同一个 key 的孪生律 `stdlib/rulesets/transpose.cml#sum_axis_stated_t` 声明的是 `(ct: T[..])`，左手与 `mean_axis_stated` 同型，从来没崩过；差别只在右手——它把 `ct` 交给 `#lane_spread`（reshape 与乘法），而 `mean_axis_stated_t` 交给一个**读长度**的助手。崩的那一帧也正是长度：`numelKernel`。

## 3. 三条路

**路 1：端口保持 `(ct: T)`，改 body 让它不把 `ct` 当数组读。**
改法：把车道长度从 `at` 算，不碰 `numel(ct)`——`let e = at[2:len(at)]`，`let axis = at[0] < 0 ? at[0] + len(e) : at[0]`，`lanes = float(e[axis])`；余切只以标量身份参与，用已经收元素的 `spread_over`（`T` → `full`）铺开：`spread_over(ct / lanes, e)`。这样 `numel(ct)` 与 `mean_axis_cotangent` 都不再需要数组。
代价：**这条路只服务“答案是秩零”的形状**。同一个 key 也会被数组余切进入——`8fa4f742a` 自己量到的反例是 `mean_axis(x: float[2, 3], 1, true)`，余切是 `[2]`，每个位置分到 `1/3`（不是 `1/6`），读 `[[0.3333… ×3], [0.3333… ×3]]`；标量语义的 body 对数组余切会**静默**给出另一个数（不是崩，是错数，`lane-rank0` 量到过 `1.59513487970007e-310` 这一类）。要两种形状都服务就得两条律，而“同一 key 两条律”被声明索引当场拒（`2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md` §3 的原文）。所以路 1 的完整代价 = 放弃数组形状，或者同时改声明索引 + 选律（`modules/transforms/**`）。

**路 2：端口改回 `(ct: T[..])`（与孪生同型），装箱放到还知道秩的地方。**
改法：端口恢复数组；装箱两条候选，都不动律的右手——(a) 在**产生余切**的那一步装箱：`*_stated` 拼写的 pullback（`modules/tensor/ops/**`）知道答案的秩，那里装出秩零数组；(b) 让 `numel` 的端口自己吃下元素：5.4 已经规定 open rank 接受 element（`src/core/type/composite/array.cpp#ArrayType::assignableFrom`），把 `stdlib/tensor.cml#numel` 的端口写成能收元素的那种形状，于是 `numel(ct)` 这个调用点自己触发转换，与今天的位置相同、但端口说的是真话。
代价：**`develop` 上 8 条 stated 用例会回到 `total: 8, pass: 0, fail: 8, killed by a signal: 8`**，直到装箱落地；`docs.every_laws_port_agrees_with_its_body` 在两步之间也仍是红（端口是数组而 body 读长度，lint 这一半它本来就报不出来，见它自己的注释）。所以这条路只能整段落：装箱先、端口后，或同一合并里两支，不留中间态。

**路 3：lint 的判据允许“端口是标量、body 通过 `numel` 读它”这种形态。**
为什么这不是把 lint 改松、而是把它删掉：它唯一能见证的是“端口与 body 不一致”，而它今天报的**恰好是修复**；一旦这一形态被允许，下一个“元素端口 + 读长度”的律，无论余切真是数组还是元素，都会被判过——而那正是 `1.59e-310` 那一类**静默错数**的形状（`2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer.cn.md`）。代价 = 这一类不再有任何静态见证，且没有任何别的地方在量它（`feat/transforms`、`feat/compile` 都只量数字对不对，量不到“为什么对”）。

## 4. 默认建议（我方按此开工，等作者点名）

**默认走 路 2。** 判据是我方向来用的那一条：**让“端口声明的类型”与“运行时真正的装箱”一致**。路 2 里端口说数组、装箱永远是数组（秩零答案的装箱是 `Array::createRankZero`，5.4 说它就是那个值）；路 1 与路 3 都保留“端口说元素、装箱有时是数组”的那半句谎，而这个系统今晚最贵的一类缺陷正是“类型链跟着端口撒谎”（`float[..]` 那面墙、`addCastNode` 对同型是 no-op、`cotangentTypeOf` = `tangentSpaceOf` 把一个秩零值说成 `float[..]`）。孪生律 `#sum_axis_stated_t` 已经按数组声明，路 2 让同一个左手的两条律同型。

默认的落法（两步，同一合并）：**步 1** 在 `modules/tensor/ops/**` 里给 `*_stated` 拼写的 pullback 装箱（或者按 路 2(b) 把 `#numel` 的端口写成收元素的那个形状）；**步 2** 把 `#mean_axis_stated_t` 的端口改回 `T[..]`，删掉那段“余切按元素写”的解释性注释，并把 `test/plans/feat/compile/stated_extents.plan.toml` 的 8 条与 `docs/handover/questions/2026-11-04-who-states-the-extents-of-a-stated-reduction.cn.md` 的结论一起更新。两步之间不推送。

如果作者选 路 1，需要同一条裁定里回答“数组余切怎么办”（第二条律被索引拒，所以答案只能是“改索引或改选律”）；如果选 路 3，需要裁定“这一类以后由什么量”。

## 5. 实例 A 已经按“教会消费者”收束（本树实测，供对照）

`std::cpp` 拒的 4 个程序 = 3 个 `T → T[..]` 的转换（`sum_axis_rank`、`mean_axis_rank`、`mean_axis_elements`）与 1 个 `tensor:argmax_axis`（`arg_axis_rank`）；`agree_with_fvm` 与 `every_expect_program_links_no_runtime` 的 6 条红是这 4 个的聚合面（171 个一致 / 170 个 standalone / 4 个 "links the runtime and is not in the baseline"）。

`18dd544af` 只动导出器，两处：`src/passes/trans/cpp/function_emitter.cpp#FunctionEmitter::lowerCast` 写出 `rt::Array<T>::element`（`T` → `T[..]` 那次转换就是 `Array::createRankZero`，5.4 说它是同一个值），`src/passes/trans/cpp/operator_rules.cpp` 的 `AxisRule` 与 `tensor:mean_axis` 对“答案是元素”的形状写 `rt::{max,min,argmax,argmin,mean}Axis(...)[0]`（一条车道就是整个 run 的走法，空车道报告的措辞不变）。语言面、rulesets、用例期望一个都没有改。

读数（树 `cppred` @ 88zjwei，`-Tier fast -Jobs 4`，2026-11-04 11:0x，宿主 1m load 11.35–13.82，高于静默阈值）：

| 判据 | 修前 | 修后 |
|---|---|---|
| `test/plans/feat/trans/cpp` | total 55，pass 49，**fail 6** | total 55，pass 55，**fail 0** |
| `test/plans/feat/modules/tensor` | —（本树的红在 `feat/trans/cpp` 的同名用例上） | total 117，pass 117，**fail 0** |

`agree_with_fvm` 把每个程序编出来跑一遍并与 `std::fvm` 逐字节比，所以这 4 个程序的新形状（`element()` 装箱、`[0]` 读出的位置）是被执行过、比对过的，不是“翻译过了”。

## 6. 页里引用的读数与它们的出处

- 8/8 SIGSEGV 与恢复：`8fa4f742a` 的提交信息（树 `~/camel-work/statedsig`，2026-11-04 06:5x–07:1x，load 15.0–21.5）：修前 `total: 8, pass: 0, fail: 8, killed by a signal: 8`，修后 `total: 8, pass: 8, fail: 0`，答 `value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]`；反例 `mean_axis(x: float[2, 3], 1, true)` 读 `[[0.3333… ×3], [0.3333… ×3]]`。**本页没有重跑这两组。**
- 崩溃帧原文（`numelKernel`、`NodeVMSchedPass::evalNativeOperator` 等 10 帧）与 `1.59513487970007e-310` 的静默错数：`2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md` §1–§2，原始页是 `2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer.cn.md`。
- 声明在基线存在、在 `768cd6436` 消失：`git show 34b659107:stdlib/tensor.cml` 有六条 `export func *_axis::<T>(xs: T[..], axis: int, keepdims: bool): T[..]`（`argmax_axis`/`argmin_axis` 为 `int[]`），`git show 768cd6436:stdlib/tensor.cml` 一条都没有。
- lint 的两行原文：本树 `node scripts/law-cotangent-ports.mjs`，第 2 节。