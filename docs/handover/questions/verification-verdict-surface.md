# 判定落在哪个面上：`stdlib/rules.cml` 的标注、`check` 的返回类型、覆盖不到的规则怎么标

Author: 车道 `handover/h397-h247-plan`（`D:\Projects\Camel-wt\h397-h247-plan`），2026-10-24。Status: open — 需要设计者与决策者裁定。测量在 Linux 主机上、以 `origin/develop`（`18c98b856`）的构建实测（计划形式：`test/plans/feat/modules/rules.plan.toml` 全绿，121 通过 0 失败，`RUN_EXIT=0`；单条读数用仓库外探针）。

## 问题 1：任务书说"`stdlib/rules.cml` 每条规则都有标注"，标注写在哪

任务书 §5.27 的验收句是"验收是 `stdlib/rules.cml` 每条规则都有 `proved`/`refuted`/`sampled` 的标注"，而 `docs/design/rewrite/v2.cn.md` 第 8.1 节只说"**报告**：每条规则标 `proved`、`refuted`（附反例）或 `sampled`"。两处合读有三种落法：

- **选项 A：标注写进规则语言**（`rule` 前或后加一个子句/标签，把判定记在源码里）。代价：要动规则语言的声明面与解析（`remark`、`#verdict` 一类新面），而 8.1 通篇说的是"报告"不是"声明"；判定是**验证的产物**，写进库会随验证器版本漂移，且一个 `refuted` 的规则按 8.1"不得入库"——库里的标注只可能是 `proved`/`sampled`，信息量最小。
- **选项 B：生成一份报告文件，`stdlib/rules.cml` 一字不改**（每行一条规则：名字、判定、命题种类、证据/反例；`--check` 模式核实"69 条规则一条不少、且没有 `refuted`"）。代价：报告要进某个目录并在 CI 重录；好处是它同时是"报告格式"的落地与验收的读法，且不需要新的语言面。实测今天 `stdlib/rules.cml` 有 **69 条规则**，逐条手写标注必然漂移。
- **选项 C：只在运行时报告里出现**（`check` 的输出就是验收面）。代价："每条规则都有标注"变成"每次跑都重新算一遍"，`stdlib/rules.cml` 与报告之间没有可核对的对应物，`refuted` 的规则可以悄悄回来。

**建议 B**，且报告里每条规则一并给出**命题种类**（`equality` / `real equality` / `error bound`，见 `verification-licence-to-precondition.md`）与**证据来源**（求解器 / 采样）。若作者要 A，请一并裁定标注的语法位置。

## 问题 2：谁发布判定，语言面长什么样

`stdlib/rewrite.cml#check` 今天的签名是 `check(rules: Rule[], samples: int, seed: int): string`。裁定（`docs/handover/reviews/questions-2026-10-23.md` 的 `generic-rule-check-surface-and-multi-parameter-instances`）已经把它改成 `check(rules, samples, seed, program: Program? = null): CheckReport`。但 8.1 同时要求"求解器（Z3）作为可选的构建依赖，只在验证时需要，**不进 `camel` 的运行时**"。于是有两种面，代价不同：

- **选项 A：判定仍由 `check` 发布**，求解器后端是独立构建单元（`modules/verify/` 一类），`check` 通过一个注册钩子问它；`CAMEL_Z3_VERIFY=OFF` 时钩子为空，`check` 如实给每条规则 `sampled`。代价：`CheckReport` 要带上"判定从哪来"的字段，`check` 的输出格式因此是语言面的一部分；好处是任务书的验收句（"`check` 给出反例"）与今天唯一的用户入口一致。
- **选项 B：判定只由验证模块自己的入口发布**（`verify::prove(rules, …) -> CheckReport`），`check` 保持纯采样。代价：多一个入口，两份报告格式要么共享一个类型、要么读者要认两套；好处是 `check` 的语义不变（采样对照），求解器真的是可选的旁观者。
- **选项 C：`check` 直接链求解器**。与 8.1"不进运行时"冲突，不建议。

**建议 A，B 次之**——这正是裁定里"§4 工程选择归施工方"的那一类，但**报告类型长什么样是语言面**，请作者定：`CheckReport` 每行是否带 `verdict` 与 `evidence`，以及 `check` 的文本输出是不是验收面。

## 问题 3：覆盖不到的规则标什么，`refuted` 之外的规则要不要拦 CI

任务书要求"先整数与浮点的标量算子"。实测 `stdlib/rules.cml` 的 69 条分布是：`int` 13、`i32` 13、`float` 16、`f32` 14（含 `f32_log1p`/`f32_expm1`）、张量 15 —— 也就是**标量 54 条、张量 15 条**；带许可的 21 条（`reassociate` 族 16、`refine_accuracy` 5）。张量规则落在第一步的翻译覆盖之外。今天一条张量规则读作：

```
ok tensor_law: 100 samples agree, fired 8 of 8
```

`docs/design/rewrite/v2.cn.md` 第 8.1 节说"覆盖不到的用 `check` 兜底并**在报告里标明**"，所以三种标法里：

- **选项 A：标 `sampled`，报告里另写"超出翻译覆盖"**。代价：`sampled` 一个词承担两种原因（"采样了但没证"与"翻译器管不到"），报告必须把原因写出来。
- **选项 B：给超出覆盖的规则一个第四个词**（如 `out of coverage`）。代价：验收句只点名三个字；但它比 A 更难被误读。
- **选项 C：覆盖不到就不许入库**（把 8.1 的"`refuted` 的规则不得入库"扩到"未证的不入库"）。代价：15 条张量规则与 5 条精度规则立刻要移出标准库，与"先标量后张量"的分步做法冲突。

**建议 A**（`sampled` + 报告里的原因列）。另外请裁定：**CI 要不要把 `refuted` 当硬失败**（建议要：8.1 明写 `refuted` 不得入库），以及 `sampled` 是否允许长期存在（建议允许，但报告里给出计数，让"标准库以 `proved` 为目标"这句话有可追踪的进度）。

**另附一件今天就已经不成立的事**：`stdlib/rules.cml` 的头部注释与 `docs/technical/46_rules.md` 第 5 节都写着"没有一条规则在通过 `check` 之前进入这个文件"，但实测 `check` 今天只跑到 **68** 条：`test/plans/feat/modules/rules.plan.toml` 的 `rules.library_passes_the_check` 走 `check_library.cml`（列了 63 条），`rules.accuracy_library_passes_the_check` 走 `check_accuracy_library.cml`（列了那 5 条 `refine_accuracy` 规则），第 69 条 `tensor_sigmoid_chain` **从未传给 `check`**（它只在 `fusion_schedule.cml` 里被 `rewrite` 跑过）。任务书的验收是"**每条**规则都有标注"，所以这条漏网要么在 H247 里一起补上，要么单独派一条更小的任务——请裁定归哪边。

## 出处

`docs/handover/README.cn.md` 第 5.27 节（H247 的任务与验收句）；`docs/design/rewrite/v2.cn.md` 第 8 节与第 8.1 节；`stdlib/rewrite.cml#check`；`docs/handover/reviews/questions-2026-10-23.md`（`generic-rule-check-surface-and-multi-parameter-instances`、`generic-rule-instantiation` §4）；`stdlib/rules.cml`；`test/plans/feat/modules/rules.plan.toml`；同目录 `verification-licence-to-precondition.md`。