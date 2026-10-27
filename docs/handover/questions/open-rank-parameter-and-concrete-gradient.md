# 开秩形参的梯度与具体秩实参：`perf/training_steps` 那条红的归责

这是 h-shape 车道的读数页：`perf/training_steps` 的 4 条红（10-26 与 10-27 都记、都标 unattributed）
定位到了机制，但**按现有裁定判不出来该谁修**。读数全部来自闸门主机 `88zjwei`（
`D:\Projects\Camel-wt\camel-remote.ps1`），工作树 `h-shape`（`11ac48f38`，`origin/develop` 的祖先）。
**没有改 `src/**`、没有改期望、没有动 `skip`/`known`/`tier`**（唯一被试改的一行已回退，见下）。

## (a) 读数：那条红与它的整行诊断

`-Plan test/plans/perf/training_steps.plan.toml -Tier slow -Jobs 8` ⇒ `[SUMMARY] total: 6, pass: 2, fail: 4`，
`[FAIL] mlp_step.nvm` / `mlp_step.fvm` / `transformer_step.nvm` / `transformer_step.profile`，`RUN_EXIT=1`。整行：

```
SemanticDiag::NoMatchingFunction: No matching function found for argument types: <> ({ w1: f32[784, 128], b1: f32[128], w2: f32[128, 10], b2: f32[10] }, { w1: f32[..], b1: f32[..], w2: f32[..], b2: f32[..] }, OptimizerState, float).   (mlp_step.cml:54:27-49)
```

transformer 同型（`:89`，`g1/g2/wq/wk/wv/wo/w1/b1/w2/b2`）。落到调用是 `mlp_step.cml:55` 的
`adam(m, gradient, opt, lr)`，其中 `m` 的声明是 `type Mlp = { w1: f32[..], … }`，`gradient` 来自
`mlp_step.cml:54` 的 `grad<m>(x, labels)`。

**这条诊断还印错了一处**：`<>` 里本该是实例的类型实参（`<nn:adam<…>>`），现在印成空的 `<>`；而
`<nn:adam>` 的签名串来自另一处（`modules/nn/optim.cpp:568`）。不是本次争议点，但读这条红的人都会读到它。

## (b) 机制三处

1. `modules/nn/optim.cpp:511-514`（`gradientOf`，`sgd`/`adam` 都用它）：
   `return tangent != nullptr && tangent->widened()->equals(grads->widened());` —— **严格相等**。
2. `src/core/type/composite/array.cpp:606-621` `ArrayType::widened()`：只把 extent 换成 `?`、**保留秩**
   （`f32[784, 128]` → `f32[?, ?]`），开秩的 `f32[..]` 原样。
3. `src/core/type/composite/array.cpp:117` `equals`：要求 `anyRank_` **与** extent 个数都相同
   ⇒ `f32[?, ?] != f32[..]`。于是两侧一方开秩、一方具体时，`equals` **恒假**。

诊断里两侧的**真实形态**是：`params`（`mlp_step.cml:55` 的 `m`）带 loop 状态里**具体秩**的字段，
而 `grads`（`grad<m>` 的结果）是**开秩** `f32[..]` 字段 —— 也就是 `grep` 里 10-27 记录写的那一对，
只是"声明写 `f32[..]`、实参给具体形状"这句话把两边的**方向**说反了。

## (c) 裁定检索：没有

`git grep -n 'f32\[\.\.\]' origin/develop -- docs/handover` 的 **41 个文件**逐条看过（reviews/
questions/reports/stdlib），**没有**"开秩形参能否收具体秩实参"或"梯度该按哪一侧定形状"的裁定。
唯一相关且**方向相反**的是设计原则 5.4（`docs/design-principles.cn.md:378`）：

> **形状未知的数组进入声明了形状的位置**：不隐式放行，用 `as` 显式转换，转换处做检查。

（同族：`reviews/questions-2026-10-23.md:135`、`questions-2026-10-24-evening.md:183`、`:1463`。）
**本条 red 是这个规则的反方向**（声明开秩、实参具体秩），5.4 不覆盖它。

## (d) 已量到的三条，与两个候选

探针**不进仓库**：它们在本机 `D:\Projects\Camel-wt\_h-shape-probes\`、在主机 `/home/zjwei/hshape-probes/`，
用主机的 `camel` 直接跑（`LD_LIBRARY_PATH=/home/ycheng/.conda/pkgs/libstdcxx-16.2.0-h934c35e_4/lib`），
所以闸门的 `gate.plan_inventory` / `gate.case_ledger` / `runner.backend_matrix` 一条也不受影响。

**量到 1：语言本来就接受"开秩形参收具体秩实参"，无需 `as`。** `probe_open_matrix.cml`（`q: f32[..]` 接
`zeros([2, 2])`、`q2: f32[?, ?]` 接 `zeros([2, 2])`、`qc: f32[2, 2]` 接 `type Model = { w: f32[2, 2] }` 的
字段 `m.w`、`q`/`q2` 接 `m.w`）逐条编译并执行，末行 `0.0`。`probe_open_unknown.cml`（`q: f32[..]` 接
`[-1.0, 2.0] as f32[]` 的 `f32[?]`）同样跑通，末行 `0.0`。`probe_open_types.cml` 顺手证实这些值的秩：
`mustRank1(w)` / `mustRank1(v)` / `mustRank1(m.w)` 与 `mustRank3(m.w)` 都报
`argument types: <> (f32[2, 2])` —— `ones([2, 2])`、`var` 绑定、结构体字段都**具体秩**，没有丢。
（与 `array.cpp:583-593` 一致：开秩目标接受具体秩源。）

**量到 2（关键）：把该谓词从 `equals` 换成语言自己的入口 `assignableFrom`，这条红**不变**。**
在 `optim.cpp:513` 上试改并**重建**（`[5/7] …optim.cpp.o`、`[6/7] Linking … nn.cmo`、
`fingerprint bdc9eb95560c`）后，`perf/training_steps` 仍 `fail 4`、`RUN_EXIT=1`，诊断整行不变；
`feat/modules/nn` 同时 `pass 70 / fail 4`（4 条都属 perf 那条计划）。改回后 `git diff optim.cpp` 为空。

**量到 3（决定性的方向）：真正对不上的那一对，是"具体秩的切线"对"开秩的梯度"。**
`probe_adam_shape.cml`（`with <m: MlpOpen>` 的 `g1` = `with_grad`，只把 `with` 实参写成名字就足够复现，
报 `WithArgumentCallee`）逐字印出两侧签名：
`<MlpOpen> (f32[..]) => (float, { w1: f32[..], b1: f32[..] })` 与
`<MlpConcrete> (f32[..]) => (float, { w1: f32[2, 2], b1: f32[2] })` —— **`grad` 的结果在两种写法下都是开秩**。
所以 `mlp_step.cml:55` 的 `adam(m, gradient, …)` 里，`gradient` 是开秩（与 10-27 记录写的一致），
而 `norm[0]`/`m` 是**具体秩**；`tangent = gradientTypeOf(params)` 也带具体秩，`widened()` 只把它变成
`f32[?, ?]`（**保留秩**），于是 `tangent` 侧永远是"具体秩的 `?`"，`grads` 侧永远是 `f32[..]`。
**两侧差的是秩这个种类，不是 extent 的精度** —— 这正是 `assignableFrom` 也救不了的原因：`f32[?, ?]`
不该收 `f32[..]`（5.4 的方向），所以 (A) 连同"只去 `widened()`"都不能修好它。

**候选 (A)：在 `optim.cpp` 里修谓词**（`assignableFrom`，或比较前不去 `widened()`）。**已实测不解决**
（量到 2/3）：两侧不是同一个类型，宽严怎么调都只会把"具体形参收开秩梯度"也放进来。
**候选 (B)：让 `gradient` 与它的形参同秩** —— 按"梯度按**声明的**形参类型定"（`optim.cpp:508-510` 自己写的口径）
与 5.5 的实例化，`m` 落进 `type Mlp`（`f32[..]` 字段）后应当就是开秩，或反过来让 `grad<m>` 取到与实参同秩的
形状；两种改法都在**类型推断/实例化一侧**（`with` 实参绑定、模型字面量的类型、`grad` 的结果类型），
不在优化器。选 B 时请同时指定"模型字面量进 `with <m: Mlp>` 位置后，`m` 的类型是声明类型还是字面量的精确类型"，
这决定 `grad<m>` 的形状事实从哪来。

请择一（或指出第三种）。这条红的 4 个用例今天仍红（`fail 4`，见 (a)）。