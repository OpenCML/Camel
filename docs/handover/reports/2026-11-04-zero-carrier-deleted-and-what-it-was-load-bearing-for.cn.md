# 零载体删掉之后：它原来扛着哪五条

**本页回答一件事**：作者 2026-11-04 04:20 裁的"**现在就删掉 `shapeFromTypeOf` 里的零载体回落**"已按原话落地，删完之后 `test/plans/feat/compile/transforms.plan.toml` 由绿转红 **5** 条，五条都停在同一个拒绝词上。本页把消费者清单、每处改动的原话、五条的实测读数、以及"这五条的唯一出路是另一个算子"的证据写下来，剩下的两步（孪生算子、零余切的形状）分别交给两份问题页。

树 `D:\Projects\Camel-wt\zerocarrier` → 宿主 `88zjwei`（32 核），分支 `lane-zerocarrier`（基线 `d7f15f2b3`），宿主时钟 2026-11-03 23:0x–23:5x（`-05:00`，本机 2026-11-04 12:0x–12:5x），1 m load 19.12 / 24.29 / 23.63 / 24.09——**宿主在强负载上，读数只在同一时段内比较**。命令一律：

```
camel-remote.ps1 -Path D:\Projects\Camel-wt\zerocarrier -Plan <plan> -Tier fast -Jobs 4 [-NoBuild]
```

## 1. 消费者清单（删之前逐处确认过替代）

`git grep -n shapeFromTypeOf` 在**代码、测试、stdlib 里现在 0 处**（只剩 `docs/handover/**` 三页历史记录，不改）。删之前它的消费者只有一处调用，其余是说明：

| # | 消费点 | 替代 | 状态 |
| --- | --- | --- | --- |
| 1 | `modules/transforms/transpose.cpp#buildTransposeMember` 的 `forward` 端口分支——**唯一调用处** | 不再造值：律要读值就 `skip=cotangent-needs-a-value`（`CotangentNeedsAValue`）；律要读**形状**而声明不写 extents 就 `TransposeNeedsParameterShape`（语言诊断，措辞未改） | 已改 |
| 2 | `shapeFromTypeOf` 本体 | 删除；原位置留一段注记，写明两条替代：规则层的 `shape(x)` 由端口类型陈述（`rule_expand.cpp#answerTheShapeOf`），开秩的长度由 `*_from_type` 那族律的 `at` 端口交进来（`statedExtentsNodeOf`） | 已删 |
| 3 | `#shapeCarrierOf` | **留着**：它造的是**零余切**的形状捐赠者（`zeroOfSpaceOf` 用），不是顶替参数的值。作者的裁定只点 `shapeFromTypeOf` 里的那一个，删一个不能带另一个 | 注记改写 |
| 4 | `rule_expand.cpp#answerTheShapeOf` / `rule_expand.h#statedShapeOf` | 行为未动；注释里"绕一层所有者造个替身"的说法删掉，`valueOf` 是另一个来源这件事写清 | 注释 |
| 5 | `stdlib/rulesets/transpose.cml` 的 `*_from_type` 族与 `sum_to_stated` | 行为未动；`mul_tensor_t` 一个字未改 | 注释 |
| 6 | `include/camel/core/type/composite/array.h#statedExtentsOf`、`src/builtin/operators/struct.cpp#extent_of` | 注释里指路的对象从 `shapeFromTypeOf` 换成 `statedExtentsNodeOf` | 注释 |
| 7 | `test/cases/compile/transforms/transpose_refuses_a_parameter_value_it_cannot_be_handed.cml` + plan 里 8 处 `TRANSPOSE-NEEDS` 说明 | 用例头与 plan 的 `desc`/注释按"答案凭 `extent_of`"重写；**`expect_*` 一个未动** | 文本 |

## 2. 五条红（实测）

`[SUMMARY] total: 170, pass: 151, fail: 5, known failures: 14`（`feat/compile/transforms`，两次读数相同：第一次 `build 694.7 s`，第二次 `-NoBuild`）。

| 用例 | 停止点（原话） |
| --- | --- |
| `erf_gradient_both_legs` | `skip=cotangent-needs-a-value uri=tensor:multiply position=0` |
| `erfc_gradient_both_legs` | 同上 |
| `transpose_gradient_both_legs` | 同上 |
| `mean_axis1_keepdims_gradient_reads_the_row_length` | 同上 |
| `linear_gradient_both_legs` | `skip=cotangent-needs-a-value uri=tensor:matmul position=0` |

**改动前的基线读数本轮没有重跑**；五条在本轮之前是绿的（它们不在这份 plan 的 `known_fail` 名单里，拒绝词也与那 14 条不同）。五条的来龙去脉：它们靠的正是被删掉的那个零——律写 `sum_to_stated(ct * w, shape(x))`，`shape(x)` 要的是**被回答那一位**的形状，那个值在反程里没有，零载体就按参数类型造一串零、让它 `shape` 得出来。删掉之后缺的不是值（另一个操作数的值闭包里都有），是**切线那一侧的形状**，而切线的值按实测的旧账不进闭包（`modules/transforms/split.cpp#residualsOf` 里那条注记：`g(x) = f(x) * x` 答成恒定函数、`grad(g)` 静悄悄 0.0）。所以这五条的唯一出路是作者同一份裁定里的另一句：**把形状当已知实参交给乘法（与 `sum_stated` 同一个做法）**——那是一个新算子，见 `docs/handover/questions/2026-11-04-who-carries-the-shape-as-an-operand.cn.md`。

## 3. 本轮落了什么、没落什么

| 派工项 | 状态 |
| --- | --- |
| 1. 删零载体；固定形状走 `extent_of`；开秩裸 `transpose` 按语言规则拒绝 | **已落**。消费者清单见第 1 节，`git grep` 证据见第 1 节开头；五条红的代价见第 2 节 |
| 2. `sum_to` 收长度时第二个操作数拿到正确目标形状 | **已确认**，`sum_to` 不改。探针（树同上，宿主 23:5x）：`source shape= [2, 3, 4]`、`template shape= [3, 4] numel= 12`、`reduced shape= [3, 4]`，`extent_of::<float[3, 4]>()` 那一支同为 `[3, 4]`。更干净的 `sum_to_at(t, at)` 写成 TODO 页：`2026-11-04-a-sum-to-that-takes-the-length-itself.cn.md`，本轮不做 |
| 3. `exp`/`erf`/`gelu` 的 `jvp` 律把形状当已知实参交给乘法 | **未落**。它要的孪生算子（`mul_stated`/`matmul_stated`）是一整次算子落地，本轮时间不够把它做对；设计、落地清单与"凭什么相信它能关上这五条"写在 `2026-11-04-who-carries-the-shape-as-an-operand.cn.md`。裸 `transpose` 在开秩上仍然按语言规则拒绝——这一点本轮没动，也没被绕开 |
| 4. `vjp` 路径上零余切照原值的形状造 | **未落，且量到作者那句话今天不成立**。试过并已回退：`split.cpp#residualsOf` 收开秩参数（只收类型不陈述 extents 的那些，`residuals` 0→1）、`buildTransposeMember` 的零余切那一段改 `forward(ports[port])` 并去掉 `zeroOfType` 回落，实测 `vjp_const_function_answers_zero` 的开放那一支由 `([])` 变成 `skip=parameter-shape port=1 at=0 no-cotangent-zero`——**值进了闭包，成员却没有端口读它**。所以"原值是残差，在闭包里"缺的是 `split` 交给成员的闭合端口与参数之间的对应，要自己一轮；留着那次改动只会把一条已钉住的读数换成一个拒绝 |

## 4. 期望值变更表

| 文件 | 旧值 | 新值 | 为什么 |
| --- | --- | --- | --- |
| —— | —— | —— | **本轮没有改任何 `expect_*`**。改动集中在注释、`desc`、用例头；`transpose_refuses_a_parameter_value_it_cannot_be_handed` 的两条期望（`expect_exit = 1`、`expect_stdout_contains = ["([0.0, 1.0, 0.0])"]`）原样保留，改的只是它"凭什么"那段话 |

## 5. 给作者的一句话

零载体删掉是对的，它原来扛着五条：律要读"被回答那一位的形状"，而那一侧是切线。把它当已知实参交下来（`mul_stated`/`matmul_stated`，与 `sum_stated` 同一个做法）是唯一出路，本轮没走完；零余切那一半要先补 `split`→成员端口的对应，与孪生算子分开裁。两份问题页各带一个默认。