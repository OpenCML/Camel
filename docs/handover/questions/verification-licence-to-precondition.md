# 许可到前提的映射：三种许可今天读法不同，哪一种算 `proved`

Author: 车道 `handover/h397-h247-plan`（`D:\Projects\Camel-wt\h397-h247-plan`），2026-10-24。Status: open — 需要设计者裁定。测量在 Linux 主机上、以 `origin/develop`（`18c98b856`）的构建实测，探针在仓库外（`/tmp`），不使用计划形式是因为这些是单条规则的一次性读数。

## 事实：`check` 今天把许可读成两样东西，都不是"假设"

`docs/design/rewrite/v2.cn.md` 第 8.1 节写"**许可就是前提**……`finite_math` 的四项是假设；`refine_accuracy` 的规则证明误差界而非相等；`reassociate`/`contract` 一类本来就改变舍入的规则，证明它们在**实数**上相等"。今天的实现里许可只决定两件事：

1. **采样域**：`modules/rewrite/module.cpp#Sampler` 的 `pickFloat` 按 `kNoNans` / `kNoInfs` / `kNoSignedZeros` 决定抽不抽 NaN、无穷、`-0.0`（`#requires<refine_accuracy>` 另外放进取上溢、下溢、次正规、近零的样本）；
2. **数值容差**：`src/core/operator_semantics.cpp#rewriteErrorBound` 按 `kReassociate` 放宽 `steps + 1` 个 ulp、按 `kChangePrecision` 放宽 bf16 的 `2^-8`、按 `kNonDeterministic` 把 `checkable` 置假。

没有任何一处把许可变成"给判定器的假设"。三处因此与 8.1 对不上，逐条实测（探针输出逐字；探针里的规则形状见下，**都不带 `when` 守卫**）：

```
ok plain: 100 samples agree, fired 8 of 8
ok finite: 100 samples agree, fired 8 of 8
FAIL reassoc: the two sides differ at (-inf, 7, -1): -nan and -inf
FAIL accurate: the rewritten left side is less accurate at (-115.74200000000002): its error 0.0087152 exceeds the original's 0 (ratio 1.96249e+13)
unchecked anywhere: the permissions it needs have no numeric bound
```

三条规则分别是 `#requires<reassociate>` 的 `x * c1 + x * c2 => x * (c1 + c2)`、`#requires<refine_accuracy>` 的 `x + 1.0 => x`、`#requires<nondeterministic>` 的 `x * c => c * x`。

## 问题 1：`reassociate` 下的规则，命题是"实数相等"还是"按位相等"

先说清探针的形状，免得读错：探针里那条 `reassoc` **不带 `when` 守卫**（标准库里的 16 条 `reassociate` 规则**都带**守卫，所以它们走的是"条件选择实例"那一行，一次值比较都不做）。两条路的今天读数因此很不相同：

```
FAIL reassoc: the two sides differ at (-inf, 7, -1): -nan and -inf                                  # 不带守卫
ok float_combine: the condition selects the instances, fired 2 of 64                                 # 标准库的形状
```

不带守卫的那条 **FAIL 在 `-inf`**：`reassociate` 一个位都不含 `no_infs`，所以采样抽到了 `-inf`，两侧在那里不等；带守卫的那条压根不比较两侧的值。但按 8.1，许可允许的正是"改变舍入"这一种不等，这两条都应当被证成"实数上相等"。今天一条是 FAIL、一条什么都没说 —— 同一条律在"有守卫"与"无守卫"下给出两种互不相干的报告，这是与 8.1 冲突的那一处。

- **选项 A：实数模型（忽略每一步舍入）**。把两侧译成实数上的精确表达式，`c1 + c2` 也按实数加，证明 `左 = 右`。代价：命题比程序实际做的更强的地方在 `c1 + c2`——浮点里那个和会舍入，实数模型证明的是"没有舍入时的相等"；报告必须写清命题种类，否则读者会把 `proved` 读成按位相等。
- **选项 B：保持按位，`reassociate` 下的规则一律 `sampled`**。代价：8.1 明写要证明；回落到采样等于这一步不做，而 `stdlib/rules.cml` 里 16 条规则带 `reassociate`（实测：`reassociate` 单独 7 条、加 `no_infs` 3 条、加 `no_infs, no_signed_zeros` 6 条），且它们今天连值比较都没有，`sampled` 会比今天**更弱**。
- **选项 C：实数模型 + 显式舍入界**（Alive2 的做法：证 `|左 − 右| ≤ 界`，界由算子声明给出）。代价：最大，也最贴 8.1 的"这正是许可允许的那一种不等"；`RewriteErrorBound` 已经是这个界的一半（`ulps` 与 `relative` 两个字段）。

**建议 A 起步、C 为终点**；B 只有在作者认为"实数相等"不是可判定命题时才选。

## 问题 2：`refine_accuracy` 的规则，成真时叫不叫 `proved`

8.1 说这类规则证的是"右边不比左边差"（误差界），不是相等。今天的措辞是 `FAIL accurate: the rewritten left side is less accurate at (…)`（实测）。另外 `docs/design-principles.cn.md` 第 7.2 节的许可清单里**没有** `refine_accuracy`——它是 8.10 引入的，8.10 自己写着"许可的名字可推翻"。

- **选项 A：复用 `proved`，报告里另设一列写命题种类**（`equality` / `error bound` / `real equality`）。代价：三个字的词表不动，任务书 §5.27 的验收句"每条规则都有 `proved`/`refuted`/`sampled` 的标注"照旧成立；代价是命题种类必须真的落在报告的固定列里，不能靠读者从规则名猜。
- **选项 B：加第四个字**（如 `bounded`）。代价：验收句只点名三个字，改动验收句是作者的事。
- **选项 C：`refine_accuracy` 的规则一律 `sampled`**。代价：8.10 那 5 条（实测：`tensor_log_softmax_of_softmax`、`float_log1p`、`f32_log1p`、`float_expm1`、`f32_expm1`）永远停在采样，而 8.1 与 8.10 都明写要证误差界。

**建议 A。**

## 问题 3：`nondeterministic` 与今天的第四个字 `unchecked`

`rewriteErrorBound` 把 `kNonDeterministic` 读成 `checkable = false`，今天的报告因此出现第四种行 `unchecked anywhere: the permissions it needs have no numeric bound`（实测）。它不是 `sampled`：`sampled` 是"采样做了、没证"，而这条规则今天连采样都没做（`checkKernel` 在读到一个不可判定的界时直接返回 true）。

- **选项 A：报告里分成两行**。`sampled` = 采样做了；`sampled, no numeric bound`（或等价措辞）= 连采样都判不了，并写明为什么。代价：报告多一个措辞，但"不许谎报"这条守住了。
- **选项 B：都写 `sampled`**。代价：读者无法把"采样过了"与"采样根本没做"分开，而这正是任务书"不是谎报 proved"要防的东西的对偶面。

**建议 A。**

## 出处

`docs/design/rewrite/v2.cn.md` 第 8.1 节（翻译、许可就是前提、覆盖不到、报告、依赖）与第 8.10 节；`docs/design-principles.cn.md` 第 7.2–7.4 节；`include/camel/core/operator_semantics.h#Permission`、`#RewriteErrorBound`、`#kFiniteMath`；`src/core/operator_semantics.cpp#rewriteErrorBound`；`modules/rewrite/module.cpp#Sampler`、`#checkKernel`；`stdlib/rules.cml`；`docs/handover/reviews/questions-2026-10-23.md`。