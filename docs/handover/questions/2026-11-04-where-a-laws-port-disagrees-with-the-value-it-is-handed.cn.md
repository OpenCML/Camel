# 律的端口和它收到的那个值不符时，该在哪一层拒绝？

**状态**：待裁；我方默认见第 3 节（**甲：静态检查留在源码这一层，transform 那一轴另记一项**）。

写这页的直接原因：2026-11-04 夜里 `lane-rank0` 的读数 —— **8/8 SIGSEGV 在 `numelKernel`**。律 `mean_axis_stated_t` 的端口写 `(ct: float[..])`，律体把 `ct` 交给 `mean_axis_cotangent`（`stdlib/rulesets/transpose.cml#mean_axis_cotangent` 第一句就是 `numel(ct)`），而 transform 往那个槽里放的是一次**元素**（double）；`numel` 把这个 double 当指针读。同一天同一族里另一条律 `sum_stated_t` 的端口是 `(ct: T)`、律体交给 `spread_over`（`stdlib/rulesets/transpose.cml#spread_over`，`full(at, ct)` 要一个 `number`），两份说法一致，所以它不崩。

本轮做的是**两端里的一端**：`scripts/law-cotangent-ports.mjs` 把每条律的端口和律体（以及律自己的左值答案、自己的 `when`）对照，端口与用法不符就报，非零退出。它已经接在 `test/plans/feat/docs_links.plan.toml` 上。

## 1. 读数（实测，含树、时间、负载）

树 `D:/Projects/Camel-wt/lawlint`（`34b659107`），本机 Windows 28 核，2026-11-04 06:3x–06:5x，1 m load 13–16（不安静，见每条读数旁的宿主输出）。

1. **今天树上的律，端口与律体逐条相符，0 条不符。** `stdlib/rulesets/transpose.cml` 20 条律 / 20 个端口全数比较，`stdlib/rulesets/jvp.cml` 36 条律 / 32 个端口全数比较，都是 0 发现（`node scripts/law-cotangent-ports.mjs`；整棵 `stdlib/rulesets` 读 88 条律、115 个端口：58 个与律体比较过、8 个两侧都没有证据（计入、不算通过）、49 个是 `Lift<...>` 那种本检查不比较的类型）。
   ⇒ **今晚那一崩不在这一轴上。** 端口说"数组"、律体也按数组用（`numel`），两端自洽；崩的是这两端之外的**第三端**：transform 递进去的那个值。

2. **这一轴可静态判，而且判得住。** 把等价坏律造出来（端口写 `(ct: T)`，律体仍走 `numel`），脚本报

       stdlib/rulesets/transpose.cml:mean_axis_stated_t: cotangent port is 'T' but the body hands it to 'mean_axis_cotangent', which reads an array (stdlib/rulesets/transpose.cml#mean_axis_cotangent, parameter 1 `ct: T[..]`)

   反方向也判得住：端口写 `(ct: T[..])`、律体交给 `spread_over` 时，报 `array ... which takes an element`。

3. **transform 那一轴今天没有闸门，而且不相符时不是拒绝。** `modules/transforms/transpose.cpp#ruleCotangent` 按操作数的类型把律实例化（`genericRuleInstanceByName(context, ruleName, types, ...)`），**端口类型与递过去的实参类型没有任何比较**；不相符时没有诊断，只有"把一种类型当另一种读"——这就是 `numelKernel` 那 8 次 SIGSEGV 的机制，也是它能在库里躺一晚没人发现的原因。

4. **本页不动的两处**：`stdlib/rulesets/**` 是多车道在改的文件（本轮只读，对照实验用"造临时坏律→删掉"）；`modules/**` 同样不在本车道。

## 2. 两条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：静态检查只留在源码这一层**（本轮所做），transform 那一轴另开一项 | `scripts/law-cotangent-ports.mjs` 判"端口 vs 律体 / 左值答案 / 自身 guard"；`ruleCotangent` 不动 | 今晚那一类错在**构建期**仍然不拒：下一次有人把端口写成与 transform 递过去的类型不同的样子，仍然要等运行时崩或等有人记得跑脚本。好处是**不碰三条车道今晚都在改的 `transpose.cpp`**，也不动诊断名录（`scripts/diagnostics-catalog.mjs`、`docs/diagnostics.md`） |
| **乙：在 `ruleCotangent` 里加一次"端口类型 = 递过去的类型"的比较**，不符就按拒绝给出 | `modules/transforms/transpose.cpp#ruleCotangent` 加比较，新 `Diagnostic`（名字进登记表 + 生成 `docs/diagnostics.md`），用例钉住拒绝的形状 | ① 它要和 `docs/handover/questions/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` 那条待裁**共用一个判决**：那条一旦裁成"给闭包成员一个槽"，端口类型集合就变，乙的判据也要跟着改；② 实例化用的 `types` 是**猜**出来的（`operandTypes` 为空时退回 `float`），比较的失败可能来自"猜错"而不是"律写错"，拒绝会指错地方；③ 一个 C++ 改动落在三条车道同时在改的文件里，冲突成本高于本轮收益 |
| 丙（今天先不做）：端口类型不再拼写，由 transform 决定 | `rule` 的右值端口去掉类型（或写成 `_`），transform 把递过去的类型当作端口的类型 | **语言改动**，语法与语义都要作者裁；它把"律写错"这一类错误变成"写不出来"，但 `spread_over` 那一族（要元素）与 `numel` 那一族（要数组）的区分也就没了，律体自己还得判 |

## 3. 我方默认

**甲。** 理由：

- 本轮能落地的那一条（端口 vs 律体）**今天就是干净的**（读数 1），它不是今晚崩的那条，但它**是**"一个人把律写成看起来对的样子、律体默默按另一种类型用"这一类的唯一静态闸门，而且不需要构建、不需要 solver，几秒跑完；
- 乙要动的 `modules/transforms/transpose.cpp` 与那条待裁页**共用一个判决**（端口集合的形状由"闭包成员有没有槽"决定），先落地乙等于替那条待裁先裁一次，违反"一页一问"；
- 丙是语言改动，超出一个车道。

**阈值（写死，便于推翻）**：若作者裁成乙，本轮的脚本**保留**（它仍是源码层的闸门，与 C++ 侧的检查互补），只是端口表多一条"transform 递过来的类型"证据；若裁成丙，脚本的端口表可以删掉一半，检查改为"律体与左值答案同形状"。

**这一页要的判决只有一句**：律的端口与 transform 递过去的类型不符时，**拒绝**（乙）还是**今天先只做源码层**（甲）。