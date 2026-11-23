# `src/stdlib/rewrite` 的 31 处直接引用：逐处分类

2026-11-23 ｜ 车道 `lane/rewrite-triaged` ｜ 基线 `907c142743`（develop）｜ 只读，未改一行代码、未构建、未跑计划。

## 0 先纠正一件事：31 行本轮一行未改

题面说「31 处本轮改过、而没有独立判据」。**这 31 行本身一行都没改。** 本轮改的是它们**所在的函数体**，31 行是那批读取被**留下**的地方。两件事都可复核：

1. 位置口径与阶段 3a 的账同一个命令：
   ```
   git grep -n 'GCGraph\|GraphDraft' -- src/stdlib/rewrite
   → 31 行（fact.cpp 4 / module.cpp 26 / proof.h 1）
   ```
   三个数 151 / 47 / 31（transforms / autodiff / rewrite）就是这三个目录的这个计数，与题面一致。
2. 本轮窗口 `v0.2.45..907c142743` 里，`src/stdlib/rewrite`（含搬家前的 `modules/rewrite`）被这 11 笔内容提交改过：
   `987a0ef585`（`saturate`/`check` 的资源声明）、`6141fb3b85`（声明注释）、
   `1a57df2779`（`extract` 的资源声明）、`9d01307b9e`（精度参考换成 `WideReal`、按声明界比较）、
   `cb9bfaec80`（规则集只编一次）、`c4717779bb`（`with` 参数按值定键）、
   `f104541d60`（CAST/COPY 改问 `shapeOfOperation`）、`8de478cee1`（七处借来的图改 `auto`、`portIndexIn` 改问视图）、
   `f3b2bb00c9`（四个施加规则的算子补上"读图"声明）、`e94a84deac`（`fact.cpp` 一处 `auto`）、`88cb37779c`（include 修正）。
   **把这 11 笔的 diff 落到当前行号上核对：31 个目标行落在新增/修改行里的有 0 个**（`git diff -U0` 的 added-line 集合与目标行求交，结果 `NONE`）。
   `v0.2.45` 的 `modules/rewrite/*` 与今天的 `src/stdlib/rewrite/*` 逐字比：`fact.cpp` 只差 :686 一处 `auto`（2 增 / 2 删），`module.cpp` 725 增 / 166 删，`proof.h` 0 行。

所以这一页的判据不是「这 31 行被改成了什么」，而是：**这 31 处读者所在的函数本轮改了什么、它读出来的答案在哪个输入上变了、有没有用例**。31 处里 24 处是传手、别名、注释、或按编号走的循环（阶段 3a 的账 §2 已按「形参/成员、`const_cast`、`using`、注释」分过类），真正会变的只有它们所读的那几段求值。

## 1 分类规则（本页怎么判）

- **N**：这一处读出来的答案本轮**变了**，且**没有任何用例**走过那个变化。
- **M**：答案变了，**有用例**钉住。
- **K**：答案没变。理由是「纯搬运」——这一行本轮逐字未动，且它读的东西在本目录内没变（或变在旁边、此处只是同一指针/同一别名的传手）。
- 判不出差别的，不猜：写「读不出」并指出该看哪里。本页没有这样的行（下面 §5 说明为什么每一条都判得出）。

## 2 表

| # | ① 位置 | ② 改的是什么 | ③ 改前 → 改后（输入 X 得 Y） | ④ 两个行为在什么输入上不同 | ⑤ 用例（计划 / case） | ⑥ 若有差别而无用例，该加什么 |
|---|---|---|---|---|---|---|
| 1 | `src/stdlib/rewrite/fact.cpp:75` | `using camel::runtime::GCGraph;` 别名 | 输入：任意。别名不产生代码，改前改后编译产物逐字相同 | 无（纯搬运） | — | 无 |
| 2 | `src/stdlib/rewrite/fact.cpp:175` | `Source::graph`：一条事实查询的取值来源（另一来源是草稿事务） | 输入：`fact_legal_forms.cml` 的 `when sign(y) == 2`（`fact_legal_forms.cml`）→ 取 `sign` 的冻结图，`node(ref)->kind` 读到该节点种类；改前改后同一指针、同一答案 | 无（纯搬运：该字段与它的读法本轮未动，`fact.cpp` 本轮只动 :686） | `feat/modules/rules.plan.toml` → `rules.fact_legal_forms_stay_compilable`（`fact_legal_forms.cml`） | 无 |
| 3 | `src/stdlib/rewrite/fact.cpp:567` | `compilePattern` 形参：从模式函数的冻结图把节点种类/载荷编成 `on` 模式 | 输入：`fact_legal_forms.cml` 的 `fact sign(v: int): int match v` 编译 → `v` 成 `Port` 模式、`case const c`/`case _` 成常量与兜底模式；改前改后同 | 无（纯搬运） | 同上 `rules.fact_legal_forms_stay_compilable` | 无 |
| 4 | `src/stdlib/rewrite/fact.cpp:693` | `resolveRuntimeTailValueRef(const_cast<GCGraph *>(&graph))`：取模式函数的返回值节点；`const_cast` 是 3c 的写入口（账 §2） | 输入：`fact sign(v: int): int match v` → 返回节点是 `v` 那个端口节点的 ref；改前改后同一 ref | 无（纯搬运） | 同上 | 无 |
| 5 | `src/stdlib/rewrite/module.cpp:248` | `WhenCall::graph` 字段：规则条件所属的图 | 输入：带 `when` 的规则 → 字段指向实例的条件图；改前改后同一指针 | 无（纯搬运） | `feat/modules/rules.plan.toml` → `rules.when_condition_selects_matches`（`when_target.cml`） | 无 |
| 6 | `src/stdlib/rewrite/module.cpp:416` | 把条件图交给 node VM（`const_cast`，VM 按值槽写） | 输入：`when_target.cml` 的条件 → 命中/不命中与改前相同（同一实参、同一 `callGraph`） | 无（纯搬运；VM 自身本轮的变化不在这一处，见 §5） | 同上 | 无 |
| 7 | `src/stdlib/rewrite/module.cpp:461` | `isRuleSide(graph)`：按图名 `__lhs`/`__rhs`/`__when` 判断是不是规则的一侧 | 输入：`target` 图 → `false`；`bump_one__lhs` / `bump_one__rhs` → `true`；改前改后同 | 无（纯搬运：同一行、同一 `name()` 读法） | `rules.rewrite_pass`（`target.cml`）——每条引擎路径都先过它 | 无 |
| 8 | `src/stdlib/rewrite/module.cpp:1033` | `applyRewrite` 的 `subject` 形参：被就地重写的那张图（写路径，账 §2） | 输入：`simplify::apply` 作用在 `target.cml` → 与改前同一条写路径（cache 在 `engineRules`，不在此处） | 无（纯搬运） | `rules.rewrite_pass`（`target.cml`） | 无 |
| 9 | `src/stdlib/rewrite/module.cpp:1037` | 在 `base + subject` 上开编辑会话 | 输入：同上，同一会话、同一 base/subject 对 | 无（纯搬运） | 同上 | 无 |
| 10 | `src/stdlib/rewrite/module.cpp:1038` | `RootScope`：每个草稿的静态值在 safepoint 之间保持可重定位 | 输入：同上，条件/规则在 safepoint 后仍读得到同一批静态值 | 无（纯搬运） | 同上 | 无 |
| 11 | `src/stdlib/rewrite/module.cpp:1598` | `why_not`：在程序副本上开会话，试规则为什么没命中 | 输入：`ordered_explain.cml` → 打印 `generic default true` 等四行；改前改后同 | 无（纯搬运；:1599 起是本轮格式改动） | `feat/modules/rewrite_function.plan.toml` → `rewrite_function.ordered_explain` | 无 |
| 12 | `src/stdlib/rewrite/module.cpp:1602` | 按 `graphId` 取图并 `session.edit`（`const_cast` 写入口） | 输入：同上，编辑的是同一张副本图 | 无（纯搬运） | 同上 | 无 |
| 13 | `src/stdlib/rewrite/module.cpp:1664` | `why_not` 另一支：在 `programNow()` 上开会话 | 输入：同上（具体/泛型两支） | 无（纯搬运） | 同上 | 无 |
| 14 | `src/stdlib/rewrite/module.cpp:1666` | 同 :1602，另一支 | 输入：同上 | 无（纯搬运） | 同上 | 无 |
| 15 | `src/stdlib/rewrite/module.cpp:1699` | `saturate`：在 `base + subject` 上开会话并把图态设成 `Tabled` | 输入：`simplify::saturated` 作用在 `target.cml` → 输出 `56 42`；改前改后同 | 无（纯搬运；cache 在 `engineRules`，不在此处） | `rules.saturate_and_extract`（`target.cml`） | 无 |
| 16 | `src/stdlib/rewrite/module.cpp:1707` | `saturate` 的 `RootScope` | 输入：同上 | 无（纯搬运） | 同上 | 无 |
| 17 | `src/stdlib/rewrite/module.cpp:2616` | `RefEvaluator` 构造形参：收被检一侧的冻结图与实参 | 输入：`check` 一条规则（`check_rules.cml`）→ 构造器收同一张 lhs/rhs 图；读者在 :2890 的 `eval`，见 #18 | 无（纯搬运：形参本身不改答案） | `rules.check_accepts_rules_that_hold`（`check_rules.cml`） | 无 |
| 18 | `src/stdlib/rewrite/module.cpp:2890` | `RefEvaluator::graph_`：参考求值器读的那张图；`evalUncached`/`operate` 都经它读 | 改前：参考算在 `long double` 上（本机 MSVC ABI = 53 位），且 `:op/cast`/`:op/copy` 调用走 `operate()`。改后：参考是 `WideReal`（106 位、2^±100000）；`shapeOfOperation` 把这两种调用答成转换/复制，按输入求值 | **(i)** 输入 `x*48 + x*(-47) => x*1`，采样点 `x=1e+30`：改前两侧的**元素类型结果**彼此比（`1.0000041e+30` 对 `1e+30`，差 27 ulp，超出允许的 11）→ `refuted`；改后两侧各对同一个实值 → 一致，`sampled`。**(ii)** 输入规则一侧写 `copy(x)`/`cast<…>(x)`：改前 `operate()` 抛 `the check has no higher-precision reference for operator ':op/copy'`，行里 `referenceAvailable=false`；改后按输入求值、给出正常判定 | (i) `rules.a_reassociate_rule_is_compared_in_the_higher_precision`（`check_real_equality.cml`）——**有用例**；(ii) **无**：`git grep -n 'copy(' -- test/cases/modules/rules` 与 `git grep -n 'cast<' -- test/cases/modules/rules` 均 0 命中 | (ii) 加 `test/cases/modules/rules/check_copy_call.cml`：`rule cp(x: float[4]) { copy(x) => x }`，`println(check_text([cp]->check<8, 0>))`，断言输出**不含** `no higher-precision reference for operator ':op/copy'`，且 verdict 词是 `sampled` |
| 19 | `src/stdlib/rewrite/module.cpp:2985` | `realValueOf`：把一侧按参考精度求成实值 | 函数体未改；它构造的 `RefEvaluator` 的参考从 `long double`（本机 53 位）换成 `WideReal`（106 位、2^±100000）。输入 `log(1+x) => log1p(x)` 的 100 个样本 → 参考值与误差比随之改 | 同 #18(i) 的点：`x=1e+30` 的 `x*48 + x*(-47)`（27 ulp 那点）、`exp(-849)` 这类落出 double 范围的点 | `rules.accuracy_check_compares_error`（`check_accuracy.cml`）；`rules.a_reassociate_rule_is_compared_in_the_higher_precision`（`check_real_equality.cml`） | 无（M） |
| 20 | `src/stdlib/rewrite/module.cpp:2991` | `compareAgainstReference`：两侧各自对参考比，算 errors/ratio/ok | 函数体未改；它经 `realValueOf` 得到的两个误差随参考替换而改，于是同一规则的 verdict/ratio 改 | 同 #18(i) 的点 | 同上 | 无（M） |
| 21 | `src/stdlib/rewrite/module.cpp:3011` | `compareBeforeAfter`：改写前后各自对同一参考比 | 同上（同一参考替换）；输入：`refine_accuracy` 规则的样本 → 两侧误差与比值改 | 同 #18(i) 的点 | 同上 | 无（M） |
| 22 | `src/stdlib/rewrite/module.cpp:3596` | `outsideLicensedDomain`：`no_infs`/`no_nans` 的点不算反例 | 函数体只换循环变量的 `auto`；它经 `RefEvaluator` 读的中间结果随参考替换而改 | 同一批点（`overflow_licensed`/`nan_licensed` 里越出 f32 范围的那一步）：参考读数的精度/范围变了 | `rules.a_reassociate_rule_is_compared_in_the_higher_precision`（`check_real_equality.cml`，含 `no_infs`/`no_nans` 两半） | 无（M） |
| 23 | `src/stdlib/rewrite/module.cpp:3649` | `propositionComparison` 的左图形参 | 函数体只把 `std::isinf` 换成 `.isInf()`；它经 `refSampleError` 与参考读数随参考替换而改 | 同 #18(i) 的点与上述两半许可 | 同上 | 无（M） |
| 24 | `src/stdlib/rewrite/module.cpp:3650` | 同上的右图形参 | 同上 | 同上 | 同上 | 无（M） |
| 25 | `src/stdlib/rewrite/module.cpp:3799` | `stepsOf`：按编号数 Oper 节点（`check` 的采样步数） | 输入：`check_rules.cml` 的规则 → 数到的 Oper 节点数不变（同一循环、同一图；账 §4 记了「按编号走」的理由） | 无（纯搬运） | `rules.check_accepts_rules_that_hold`（`check_rules.cml`） | 无 |
| 26 | `src/stdlib/rewrite/module.cpp:3808` | `declaredUlpsOf`：按编号累加算子声明的 ulp | 输入：`check_accuracy.cml` 的规则 → 同一批算子、同一累加 | 无（纯搬运） | `rules.accuracy_check_compares_error`（`check_accuracy.cml`） | 无 |
| 27 | `src/stdlib/rewrite/module.cpp:3857` | `axisPortsOf`：标记被算子当轴用的形参 | 输入：`check_accuracy.cml` 的 softmax 规则（rank-1/2/3，axis 2）→ 被标记的轴形参不变；:3870 的 `portIndexIn` 改问视图后，`ports(Normal)` 与 `normPorts()` 是同一串端口、同一次序 | 无（纯搬运：账 §3 记「答案同一串端口、同一个次序」） | 同上 | 无 |
| 28 | `src/stdlib/rewrite/module.cpp:4146` | `checkOneRule`：在左图副本上开会话、抽常量样本 | 输入：`check_rules.cml` 的 8 个样本 → 同一批样本；:3951 本轮只换 `auto`，:4020 只换行 | 无（纯搬运） | `rules.check_accepts_rules_that_hold`（`check_rules.cml`） | 无 |
| 29 | `src/stdlib/rewrite/module.cpp:4149` | 同上的 `const_cast`（只读图交写会话） | 输入：同上 | 无（纯搬运） | 同上 | 无 |
| 30 | `src/stdlib/rewrite/module.cpp:4150` | 同上的 `session.edit`（写入口） | 输入：同上 | 无（纯搬运） | 同上 | 无 |
| 31 | `src/stdlib/rewrite/proof.h:53` | 注释：`GraphDraft::decode` 之后句柄指向程序的哪一步 | 输入：无（注释不参与编译） | 无（纯搬运） | — | 无 |

## 3 汇总

**有差别且无用例 N = 1；有差别有用例 M = 6；无差别（纯搬运）K = 24；N + M + K = 31。**

- **N（1 处，按目录归组）**
  - `src/stdlib/rewrite/` —— `module.cpp:2890`（`RefEvaluator::graph_`）：规则一侧写成 `copy(x)` / `cast<…>(x)` 调用时，参考求值器从「拒绝（no higher-precision reference for operator）」变成「按输入求值」，全部用例里没有一条这样写。
- **M（6 处）**：`module.cpp:2985/2991/3011/3596/3649/3650` —— 精度参考换成 `WideReal` 与按声明界比较，`rules.accuracy_check_compares_error`、`rules.a_reassociate_rule_is_compared_in_the_higher_precision` 钉住（含 `no_infs`/`no_nans`）。
- **K（24 处）**：其余全部。

## 4 K 的抽样验证（3 处）

抽的三处都挑了「最像会变」的，而不是最像不变的。

1. **`module.cpp:3857` `axisPortsOf`** —— 唯一一处**旁边**被改成视图的 K：:3870 现在是 `portIndexIn(graph.view(), inputs[index])`。等价理由：`portIndexIn` 换的是同一个问题的另一种问法，`ports(PortGroup::Normal)` 与旧的 `normPorts()` 是图里同一张形参表（账 §3 原话「答案同一串端口、同一个次序」）；返回的是同一个下标，`marks[*port] = 1` 落在同一格。**输入**：`test/cases/modules/rules/check_accuracy.cml` 里那条 softmax 规则（rank-1、矩阵、rank-3，axis 2）——轴形参的标记向量与改前逐位相同，于是抽样轴不变，该 entry 钉的 verdict 与样本数不变。
2. **`module.cpp:461` `isRuleSide`** —— 本轮逐字未动，但它所在的三个循环（`programValueTypes`、`applyRewrite`、`saturateKernel`）本轮都被改成了 `auto`，所以它是「改在旁边」的典型。等价理由：这一行与 `v0.2.45` 的 `modules/rewrite/module.cpp` 逐字节相同，`graph.name()` 的读法没变。**输入**：`test/cases/modules/rules/target.cml`——程序图名 `target` 得 `false`，规则 `bump_one` 的两侧 `bump_one__lhs`/`bump_one__rhs` 得 `true`；改前改后同样跳过两张规则侧、同样重写 `target`，`rules.rewrite_pass` 的输出 `56 42` 逐字相同。
3. **`fact.cpp:175` `Source::graph`** —— `fact.cpp` 本轮只动 :686 一处 `auto`，这个字段与它的 `transaction ? … : graph->node(ref)->kind` 读法一字未动。**输入**：`test/cases/modules/rules/fact_legal_forms.cml`（`rules.fact_legal_forms_stay_compilable`）——`sign` 的 `match v` 仍把 `v` 读成 `Port`、`case const c` 仍读成常量，编译出的 `on` 模式与改前相同。

这三处的等价不是「我认为」，是**那一行本轮逐字未动**（`git diff v0.2.45:modules/rewrite/… HEAD:src/stdlib/rewrite/…` 的 added-line 集合里没有它们），加上上面那个输入走的就是这条路径。

## 5 边界：什么读得出、什么读不出

- 读得出的：这一行本轮有没有动（diff + blame，逐行核对过 31 行，全在 2026-10-21 至 11-06 之间，本轮 0 行）；它所在的函数本轮动了什么（11 笔提交的 diff 逐段读过）；它读出来的答案在哪个输入上变（用提交说明里的实测点 + 用例里的采样点对齐）；有没有用例（`test/plans` 逐条对名字）。
- 读不出的、本页**没有**当成 K 的：`module.cpp:416`（条件交给 node VM，VM 本目录外，本轮有 4 笔提交）。我把它记为 K 的理由是**这一处的实参与读法未变**，VM 的变化是另一个读者的事，不属于这 31 处；若下一步要动 VM，这一处要重判。

## 6 没有做的事

未构建、未跑任何计划、未改一行代码（本页是唯一新增文件）。`develop` 未动；`internal/collab/**` 未动。
