# 四件挂起的事，等作者定

本页把四件待裁的事并成一页：每节只给**裁决点**、**可复现读数**、**出处**，不写论证。读数标了日期、树与提交；凡本次没有重跑的，写明"未重跑"；凡是本页作者自己量的，写明命令。

## 1. progen 前端残留页的三问（接收位、`for … in [字面量]`、18 格效果顺序）

**裁决点**（`docs/handover/questions/progen-frontend-residuals.md:67-72`）：

- **(a)** 接收位 `->map<…>` / `->filter<…>` 要不要把 `T` 的两个写法合成一个 —— 具名 `P` 与匿名 `{ x: int, y: int }` 出自同一条 `type P = { x: int, y: int }`。页里的四条最小对照已把范围收窄到"只在接收位、且 lambda 写 `P` 时不合成"：实参位通过，接收位写匿名拼写也通过（`:37-40`）。若不要合成，生成与归档程序该按元素类型自己的拼写写 lambda。
- **(b)** `for … in [字面量]` 按 loop 结果的元素类型检查那个数组字面量（`[c17, c17, 3.0]` 是 `float[]`，却按 `int` 的元素类型报 `SemanticDiag::ElementTypeMismatch`）这条是否符合裁定？若是，生成器与归档程序按元素类型改写（`:45-54`）。
- **(c)** 种子 16 的 18 格效果顺序错（`q33 10` 从第 4 位挪到第 1 位，`none`/`inline`/`fold` 各格都对）属前端还是 `std::opt` 的规则、该归哪条车道？（`:56-62`）

**页上的读数**（来自 `handover/progen-frontend` 车道自己的分支，`origin/develop` = `e16563e95`；未重跑）：点名计划 `test/plans/feat/opt/progen.plan.toml` 的 `progen_check` 是 25 个种子 × 41 条管道 = 1025 格，每格一次编译（`:4-5`）；修掉三种退役拼写后 **OK 802 / FRONTEND 123 / MISMATCH 100**，计划读数 `223 wrong`（`:8-9`）。123 格是种子 1、14、19 各 41 格；100 格是种子 5、6 各 41 格加种子 16 的 18 格（`:9-10`）。

**我复核的计数口径**（2026-10-27，本机，不需要编译器）：`test/tools/progen/runner.py` 的 `matrix("cross")` 是 41 格（`test/tools/progen/README.md:55-57`）；`test/tools/progen/progen.py:260` 的 `CHECK_RANGE = range(1, 26)` 是 25 个种子，`:271` 的 `CHECK_EXCLUDED = {}` 一个也不排除，故 25 × 41 = 1025，与页上一致。

**没有答案的出处**：该页仍在 `origin/develop` 树里（blob `5466efa7f`，4888 B；随 `ed1aa7e71`、`05b29f77c` 于 10-27 落地）；`docs/handover/reviews/questions-2026-10-26-late.md` 的 12 个 `##` 小节里 grep `progen` **0 命中**，`docs/handover/reviews/` 下也没有任何文件提到该页或"接收位"。

**另记一件，与上面三问分开**：同一条计划**自己**的批，10-24 的记录是 **1025 格里 82 wrong，全是 `FRONTEND: No matching function found`，落在种子 19 与 23（41/41 格，含无 pass 的 `default` 格），自 `07e81237f`（10-23 23:57）就在**（`docs/handover/batch-slow-2026-10-24.md:26`；10-26 的记录仍标它红，`docs/handover/batch-slow-2026-10-26.md:23`）。**但今天（2026-10-27，`c92865e89`）这个批跑不起来**：`test/tools/progen/model.py:350` 的 `func_src` 里 `body` 未定义（该函数只有 `n`、`ind` 两个参数，`model.py` 里也没有模块级的 `body`），`progen.py:56` 的 `emit`（`:400` 调 `func_src`）一进顶层函数就抛 `NameError: name 'body' is not defined`。本次读数，命令 `pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h-pending -Plan test/plans/feat/opt/progen.plan.toml -Tier fast -Jobs 8 -NoBuild`：

```
[FAIL] progen_check     670.00 ms cpu of 150.00 s budget, 1.42 s wall
[INFO] stdout does not contain '0 wrong'
[stderr] NameError: name 'body' is not defined
[PASS] progen_fixed                                           10.26 s
[SUMMARY] total: 2, pass: 1, fail: 1
RUN_EXIT=1
```

也就是 **82 wrong 与 223 wrong 两个数在那天都测不出来**：批还没开始跑就死在生成器里。**该 `NameError` 已由 `6621efcc4`（`handover/progen-generator-fix`）修好，并随 `2c9100ff3` 进 develop**；但**修好不等于已绿**，修好后重跑同一条计划（2026-10-27，`2c9100ff3`，`-Plan test/plans/feat/opt/progen.plan.toml -Tier fast -Jobs 8 -NoBuild`，`-Path` 换成本页所在的工作树）仍是：

```
[FAIL] progen_check      91.12 s cpu of 150.00 s budget, 51.06 s wall
[INFO] stdout does not contain '0 wrong'
[stdout] progen check: 25 programs x 41 pipelines = 1025 cells (0 located refusals), 0 seed(s) left out as known failures
[stdout] 223 wrong
[PASS] progen_fixed                                            9.98 s
[SUMMARY] total: 2, pass: 1, fail: 1
RUN_EXIT=1
```

即 **223 wrong 是今天（`2c9100ff3`）的真读数**：1025 格、0 个种子按已知失败排除；按页上车道读数给的分法，223 = 种子 1／14／19 各 41 格 `FRONTEND`（123）加种子 5／6 各 41 格与种子 16 的 18 格 `MISMATCH`（100）。**这才是本节要呈报给作者的东西，不是"已修好所以已绿"**。另外该计划是 `tier = "milestone"`（`test/plans/feat/opt/progen.plan.toml:3`），所以快闸门从不读它。

## 2. 入口文件与标准库模块同名时的**运行时按名加载**

**裁决点**：设计原则 10.1 的"一个模块的导入从不解析到它自己"要不要**也**扩到**运行时按名加载**，还是记为"另行规定"？即 `src/execute/executor.cpp:87` 的 `context_->importModule(protocol)` 该不该按同一条原则跳过入口文件自己。

**出处（代码与裁定）**：`src/execute/executor.cpp:80-93` 就是那条路 —— 算子的模块没被程序导入时，按 protocol 名加载它（注释写明全局的 `exit`/`sleep` 由 process 模块实现），`:87` 是那次 `importModule`。裁定原文在 `docs/design-principles.cn.md:688`（"一个模块的导入从不解析到它自己……入口文件叫 `math.cml` 时，它的 `import math` 是标准库的那个"）与 `:697`（"模块的 `import` 先在模块自己的目录里找，再按模块根、标准库的顺序找；入口程序与库模块同一条规则"）。`import` **解析**这一侧已按 `docs/handover/reviews/questions-2026-10-26-late.md:64` 修好，本节的按名加载是**另一处机制**。

**本次量的读数**（2026-10-27，`c92865e89`，主机为新工作树新建的 install）：同一计划里两种写法今天**同轮都过**。探针把 `test/plans/feat/modules/math_kernels.plan.toml` 里 `math_kernels.shl_i32_width` 的 `args` 从根相对路径 `test/cases/modules/math_kernels/err_shl_i32/math.cml`（`:27`）改成 `["${case}"]`，运行结果：

```
[PASS] math_kernels.values.nvm                              187.04 ms
[PASS] math_kernels.values.fvm                              166.35 ms
[PASS] math_kernels.shl_i32_width                           112.15 ms
[PASS] math_kernels.shl_int_width                           243.33 ms
[PASS] math_kernels.shr_negative                            132.72 ms
[SUMMARY] total: 5, pass: 5, fail: 0
RUN_EXIT=0
```

未改动的两条（`:36`、`:45`，仍是根相对路径）与改成 `${case}` 的那一条在**同一次运行**里都过，即"这三条**只能**用根相对路径"这一条今天复现不出来。三条用例的入口文件都叫 `math.cml`、都在第 2 行 `import { inf } from math`（`test/cases/modules/math_kernels/err_shl_i32/math.cml:1-2` 等）。探针已还原：`git status` 干净，本分支只多了本页一个文件。

**请作者定**：(i) 把 10.1 的原则扩到运行时按名加载，还是 (ii) 明确记为"另行规定"。附带一问（测试卫生，可另派）：这三条的 `args` 要不要与同计划 `:16` 的 `${case}` 统一。

## 3. 开秩形参的梯度（`c92865e89` 合入的那一页的三问）

**裁决点三问**（`docs/handover/questions/open-rank-parameter-and-concrete-gradient.md`，该页请作者"择一（或指出第三种）"，`:86`）：

1. **秩差异归哪一侧** —— `grad` 的结果与形参谁该改形状（`:33-35`、`:67-76`）。页里判定的方向是：`grad` 的结果在两种写法下**都是开秩**，`m` 是**具体秩**，两侧差的是"秩这个种类"，不是 extent 的精度。
2. **模型字面量进 `with <m: Mlp>` 位置后，`m` 的类型是声明类型还是字面量的精确类型** —— 这决定 `grad<m>` 的形状事实从哪来（`:83-84`）。
3. **该由哪一层统一** —— 页给的落点是类型推断/实例化一侧（`with` 实参绑定、模型字面量的类型、`grad` 的结果类型），不在优化器（`:80-82`）。

**页上的读数**（主机 `88zjwei`，树 `h-shape` `11ac48f38`；未重跑）：`perf/training_steps` 4 条红 —— `[SUMMARY] total: 6, pass: 2, fail: 4`、`RUN_EXIT=1`，即 `mlp_step.nvm`、`mlp_step.fvm`、`transformer_step.nvm`、`transformer_step.profile`（`:10-11`）；候选 **(A)**（把 `modules/nn/optim.cpp` 的严格相等谓词换成语言自己的 `assignableFrom`，或比较前不去 `widened()`）试改并**重建**后仍 `fail 4`、诊断整行不变，已回退（`git diff optim.cpp` 为空）（`:62-65`、`:78-79`）。这 4 条用例今天仍红（`:86`）。

**没有裁定可依**：含 `f32[..]` 的 **20 个 handover 文件（61 行）**逐条看过，唯一相关且**方向相反**的是设计原则 5.4（"形状未知的数组进入声明了形状的位置：不放行，用 `as` 显式转换"），本条是"声明开秩、实参具体秩"，5.4 不覆盖（`:38-48`）。

**口径**（2026-10-27，`2c9100ff3`，自测）：模式按**字面量** `f32[..]` 量 —— `git grep -n -F 'f32[..]' -- docs/handover` ⇒ **20 个文件／61 行**，放宽到 `-- docs` ⇒ **35 个文件／158 行**。这 20／35 **不含引用它的页面**：把本页算进去是 21／62 与 36／159。写成 `'f32[\.\.]'` 时方括号是字符类（`f32` 后跟一个 `\` 或 `.`），量到的是别的数；本页原写的 `41 个文件` 与这条口径不符。

## 4. `docs/technical/30_diagnostics_reporting.md:314` 的测量记录

**裁决点**：频次表里 **`ConstructNotSupported 27`** 那个码已从注册表退场（本车道 `0364b7ab7`，`refactor!`），而这段测量按作者提交信息**故意留用** ⇒ 按新 revision **重做整份调查并改 pin**，还是**只加一句"该码已退场"**？

**出处与读数**：`:314` 在 "Committed programs" 那一条里 —— 2662 个 `.cml` 各自 `camel <file> --error-format json --max-errors 50`，2568 诊断 / 237 个码 / 39 次因自身原因失败（`:305-307`）；整段属于 "the survey of 2026-10-24 on `769b358a9`"（`:296`）。裁定原文是 `681f8e54a` 提交信息里的一行：**"The measurement at 30_diagnostics_reporting.md:272 is a record of what was measured and is untouched."**（同一行在 `681f8e54a` 上是 `:273`，提交信息写的 `:272` 差 1。）那次提交正是清理 `wait` 残留的一批，它在这同一行上留着 `ConstructNotSupported 27`，只改了同文件 `:138` 的机制描述。该文件**不是生成物**：没有任何脚本写它（`scripts/diagnostics-renumber.mjs:59` 只是注释里引用它），所以既不会随 `--write` 更新，也没有"重录"这条路。重做的代价：换 revision（10-27 的树已不是 `769b358a9`）、第一表 2662 个文件各一次编译、第二表 3000 个变异体（`:275-284`）。