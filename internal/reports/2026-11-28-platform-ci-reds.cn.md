# CI 三平台红灯读数（2026-11-28，`develop` = `02208fe542d0`）

这一页只记读数，**不判因、不修**：它把 2026-11-28 收工时在 `develop` 尖端跑的那一轮 GitHub Actions 的
红名，与当天最后一次晋级（第八十五次，`cf730bf232dc`）的红表逐名对照，并写明两套口径为什么会不同。
判因与修复归各车道与协调者；这一页的作用是让下一个人不必再跑一遍就能看见"哪些红是我们已有的、
哪些只在 CI 上出现、以及 CI 与我们根本不是在比同一个集合"。

## 一、这一轮的出处

```
$ gh workflow run ci.yml --ref develop
  → https://github.com/OpenCML/Camel/actions/runs/46132676202
$ gh run list --workflow ci.yml --limit 1
  completed  failure  CI  CI  develop  workflow_dispatch  46132676202  41m51s  2026-11-28T09:54:54Z
```

被测量的提交是 **`develop` = `02208fe542d0da0b4fb7f8079d7525efc31a1fd4`**（当天第八十四次晋级的
`measured_tip`，收工时它已经是 `develop` 的定稿尖端）。跑的是 `CI`（`ci.yml`，id 382251646），
**不是** `CI leg`，**也不是** `Performance baseline`。

`gh workflow list` 的原文（名字照抄，不猜）：

```
Build and package (PyPI wheel)	active	269586706
CI leg	active	393651133
CI	active	382251646
Performance baseline	active	391353886
Dependency Graph	active	269586823
```

`gh auth status`：`✓ Logged in to github.com account Kogler7 (keyring)`，
`Token scopes: 'admin:public_key', 'gist', 'read:org', 'repo'`。

## 二、失败的是哪一步

三个平台腿都是**编译通过、测试步骤红**；`Source headers` 这一步是绿的。

```
✓ Source headers                                       12s
✗ Build and test (linux-x64)   23m18s  job 140941091626  失败步骤：Test (smoke + feat)
✗ Build and test (win-x64)     40m30s  job 140941091670  失败步骤：Test (smoke + feat)
✗ Build and test (macos-arm64)         失败步骤：Test (smoke + feat)
```

步骤级只有一句 `##[error]Process completed with exit code 1.`；**没有"某一步骤名"级别的信息**，
红名要靠运行器自己的逐用例判词行。三个平台腿各自的 `[SUMMARY]`：

```
linux-x64   [SUMMARY] total: 9847, pass: 9656, fail: 133, skip: 25,  known failures: 33
win-x64     [SUMMARY] total: 9847, pass: 9653, fail: 140, skip: 21,  known failures: 33
macos-arm64 [SUMMARY] total: 9847, pass: 8789, fail: 160, skip: 865, known failures: 33
```

失败行的形状是 `[FAIL] <case name> <用时>`：**这是运行器的逐用例判词行（测试名 + 用时）**，
不是 job 名、也不是步骤名。逐名比对就是把这一列取出，和我们红表的 case 列对齐。

## 三、与第八十五次晋级红表逐名对照

我们的红表：`internal/reports/2026-11-28-promotion-85-reds.tsv`，**132 条**。

| 平台 | 唯一红名 | 已知 | 新 |
|---|---|---|---|
| linux-x64 | 133 | 131 | 2 |
| win-x64 | 140 | 130 | 10 |
| macos-arm64 | 145 | 130 | 15 |

三平台并集：**已知 131，新 23**。我们的 132 条里只有 `editview.sec2_log` 没有出现在 CI 红里
（原因见第四节：CI 的选择面比我们小）。

**新红 23 条全部在我们自己的 Linux 主机、同一份源码上通过。** 这一条是这一页最重要的事实：

```
$ python3 -  # 在 /home/zjwei/camel-work/promote 的 20261128_175306_64060_aqew/summary.json 里查
  depth_across.prl_off.exceeds                   ('para.handoff|depth_across.prl_off.exceeds', 'pass')
  depth_across.prl_off.fits                      ('para.handoff|depth_across.prl_off.fits', 'pass')
  entry.exit_after_fault.jit                     ('consistency.entry|entry.exit_after_fault.jit', 'pass')
  entry.exit_call.jit                            ('consistency.entry|entry.exit_call.jit', 'pass')
  examples.case_study.break_rate.jit             ('examples|examples.case_study.break_rate.jit', 'pass')
  facts.self_check                               ('tools.facts|facts.self_check', 'pass')
  fail.kind_message.jit                          ('consistency.fail_never|fail.kind_message.jit', 'pass')
  fail.no_message.jit                            ('consistency.fail_never|fail.no_message.jit', 'pass')
  math_scalar_gradient_through_the_library.nvm   ('transforms|math_scalar_gradient_through_the_library.nvm', 'pass')
  op_record.cpp_registration_states_panic        ('modules.op_record|op_record.cpp_registration_states_panic', 'pass')
  recursion_too_deep.prl_evaluator               ('para.core|recursion_too_deep.prl_evaluator', 'pass')
  residual_mlp_large_bytecode                    ('modules.nn|residual_mlp_large_bytecode', 'pass')
  retention.name                                 ('tools.retention|retention.name', 'pass')
  selfchecks.camel_remote_args                   ('tools.selfchecks|selfchecks.camel_remote_args', 'skip')
  selfchecks.hook_checker_contract               ('tools.selfchecks|selfchecks.hook_checker_contract', 'pass')
  selfchecks.oversight_reload                    ('tools.selfchecks|selfchecks.oversight_reload', 'pass')
  selfchecks.promotion_record                    ('tools.selfchecks|selfchecks.promotion_record', 'pass')
  selfchecks.push_trace_range                    ('tools.selfchecks|selfchecks.push_trace_range', 'pass')
  split_whole_and_member.nvm                     ('split|split_whole_and_member.nvm', 'pass')
  stdlib.fault.zip_with.jit                      ('modules.stdlib_faults|stdlib.fault.zip_with.jit', 'pass')
  unwrap.take_fallible.jit                       ('consistency.unwrap|unwrap.take_fallible.jit', 'pass')
  unwrap.take_nullable.jit                       ('consistency.unwrap|unwrap.take_nullable.jit', 'pass')
  unwrap.take_where_text.jit                     ('consistency.unwrap|unwrap.take_where_text.jit', 'pass')
```

一条是 `skip`（`selfchecks.camel_remote_args`：那台主机上有晋级工具，所以计划跳到下一步），
其余二十二条是 `pass`。所以这 23 条**不是产品回归**——它们是"同一份源码在另一个平台上得到不同答案"，
或者"那个平台上没有我们的工具"。两类都按缺陷记账，但它们是元层/平台面的缺陷，归各车道与协调者。

## 四、两套口径为什么会不同（这一节请随每次比 CI 一起带走）

1. **CI 的测试步骤是 `node scripts/test.js`**（`ci-leg.yml` 原文），按 `AGENTS.md` 的说明，它跑
   **`smoke` 的 fast 层 + 全部 `feat/**` 计划**，并行宽度默认每核一测。
   我们的晋级层是 `internal/tools/camel-remote.ps1 -Plan test/plans -Tier fast -Jobs 2`，
   跑的是**整棵计划树的 fast 层**。
2. **所以条数不同：CI `9847`，我们 `9933`，差 `86` 条**，而且是系统性的（少的是 `feat` 之外的
   计划目录），不是随机抖动。**CI 的红集不是我们红集的超集**。
3. 由此：`editview.sec2_log`（我们 132 里的一条）没有出现在 CI 红里，**是选择面不同造成的，
   它不是"转绿"，不许当乙记账**。

## 五、三类读数（都只记读数，判因不在此页）

### 一、`selfchecks.*`（5 条）—— 三个不同的原因，不是一句"工具不在场"

这 5 条是计划对我们自己工具的体检。**它们不是同一类原因**，逐条的原日志（`gh run view --job <id> --log`）
已经取到，按原因分成三类；把原因混成一句"工具不在场"是错的，所以这里分开写：

#### （甲）要改脚本，三平台一致 —— `selfchecks.camel_remote_args`

linux / win-x64 / macos-arm64 **三个平台全红**，三份日志内容相同，只有用时不同（2.88 s / 4.97 s / 7.67 s）：

```
[FAIL] selfchecks.camel_remote_args                            2.88 s
   [INFO] expected exit 0, got exit 1
   [INFO] stdout does not contain ' checks, 0 failed'
   [stdout] omitted 27 line(s) above -- full output: …/fail/tools.selfchecks/selfchecks.camel_remote_args/
   [stdout]   PASS  a wall checker feeds checkfail
   …
   [stdout]   PASS  the generator refuses a missing row instead of writing a bare interpreter
   [stdout]   FAIL  no script-scope assignment or loop variable shares a parameter name
   [stdout]         line 592: $ChangedBase = "$upstreamRemote/develop"
   [stdout]
   [stdout] 36 checks, 1 failed
   [stderr] (empty)
```

那次体检读的是仓库里的 `internal/tools/camel-remote.ps1` 本身，而那份脚本有两处：

```
internal/tools/camel-remote.ps1:392:    [string]$ChangedBase = 'origin/develop',
internal/tools/camel-remote.ps1:592:    if ($upstreamRemote) { $ChangedBase = "$upstreamRemote/develop" }
```

392 行把 `$ChangedBase` 声明成参数，592 行又在嵌套作用域里给它赋值 —— 脚本自己的检查判自己不通过。
**这一条要改脚本**（换一个不撞参数名的变量名即可），不是 `skip_unless`；它没有外部依赖，
所以在任何一台有这份脚本的机器上都会红。

#### （乙）要探"git 旁边有没有 `sh`" —— `hook_checker_contract`、`push_trace_range`

两条都只在 **macos-arm64** 红（linux 上分别 PASS 179.89 ms / 623.40 ms），原文：

```
[FAIL] selfchecks.hook_checker_contract                      69.32 ms     (macos-arm64)
   [INFO] expected exit 0, got exit 2
   [stdout] hook-checker-contract: reading /Users/runner/work/Camel/Camel/.husky/pre-push
   [stdout] hook-checker-contract: not read -- no sh beside git (looked in /opt/homebrew/opt/usr/bin/sh.exe, /opt/homebrew/opt/bin/sh.exe …)
   [stdout] hook-checker-contract: 0 checks, 0 failed
   [stderr] (empty)
```

```
[FAIL] selfchecks.push_trace_range                           64.76 ms     (macos-arm64)
   [INFO] expected exit 0, got exit 1
   [INFO] stdout does not contain 'push-trace-range: '
   [INFO] stdout does not contain ' checks, 0 failed'
   [stdout] (empty)
   [stderr] file:///Users/runner/work/Camel/Camel/test/tools/push-trace-range.test.mjs:35
   [stderr]     if (!sh) throw new Error(`no sh beside git (${execPath})`)
   [stderr]                    ^
   [stderr] Error: no sh beside git (/opt/homebrew/opt/git/libexec/git-core)
```

它们要的那件东西是 **`sh`（POSIX shell）在 git 旁边**；`hook_checker_contract` 找的还是 `sh.exe`
（Windows 形状的命名），于是在 macOS 的 homebrew git 旁边找不到，`0 checks` 却 `exit 2`。
⇒ 要 `skip_unless` 探同一件东西，且探法要能识别 macOS。

#### （丙）要路径归一 —— `promotion_record`、`oversight_reload`

`promotion_record` 在 **win-x64 + macos-arm64** 红（linux PASS 689.77 ms）：

```
[FAIL] selfchecks.promotion_record                             6.79 s     (win-x64)
   [INFO] expected exit 0, got exit 1
   [stdout]       pre-push:   this tree:      C:/Users/runneradmin/AppData/Local/Temp/promotion-record-qze6mQ
   [stdout]       pre-push:   merge worktree: C:\Users\RUNNER~1\AppData\Local\Temp\promotion-record-qze6mQ
   [stdout] FAIL  the hook refuses a develop push whose tip has none, naming the promotion record
   [stdout]       pre-push: refusing to push refs/heads/develop from this tree: 只有合并位能推 develop
   [stdout]       pre-push: (only the merge worktree may push develop).
```

macos 上同一处是 `this tree: /private/var/folders/q2/…/T/promotion-record-6aEWey` 对
`merge worktree: /var/folders/q2/…/T/promotion-record-6aEWey` —— **同一个目录的两种写法**被判成两棵树。
两个写法分别是 Windows 的 8.3 短名（`RUNNER~1`）与 macOS 的 `/private` 前缀。

`oversight_reload` 只在 **macos-arm64** 红（linux PASS 8.01 s）：

```
[FAIL] selfchecks.oversight_reload                             9.74 s     (macos-arm64)
   [INFO] expected exit 0, got exit 1
   [stdout] FAIL a plugin loaded from the live directory is told to restart: {"ok":false,"changed":true,"sha":"02208fe542d0da0b4fb7f807…"}
   [stdout] FAIL the answer names the loading path: "/private/var/folders/q2/p1t6rgxj5fj32g1dc7dmd49r0000gn/T/oversight-reload-0ysNJa/l…"
   [stdout] PASS the route never reconciles the plugin in place
   [stdout] FAIL a sync that moved the live directory asks for a restart, with or without an hmr service: {"ok":false,"changed":true,"s…"}
   [stdout] FAILED: 22/25 checks
```

它比的字符串是 `/private/var/folders/…`，另一侧是 `/var/folders/…` ⇒ 同一类：**比较之前要归一**。

计划里已经有 `skip_unless` 这个机制（同一份计划里 `camel_remote_fetch`、`run_registry`、
`git_bash` 在 linux 上靠 `test/tools/have-windows.mjs` 正确跳过，日志里是 `[SKIP] … precondition not met`），
所以这 5 条不是"没有机制"，是**这 5 条没用上它**，且其中 1 条根本不是"缺东西"而是脚本自己的缺陷。
已由协调者报给决策者；**这一页不动计划、不改用例**。

### 二、`residual_mlp_large_bytecode`（linux 独有）——同一提交两个平台读数不同

```
[FAIL] residual_mlp_large_bytecode                             4.51 s
   [INFO] used 4340 ms of CPU against a 16000 ms timeout, below the 4x margin: the bound is the work's own
   [stdout] residual_mlp start=0.7921590805053711 value=0.7921590805053711 sgd=4.81122697237879e-10 adam=0.0158
   [stderr] ():?:?: [Warn]: `std::opt` stopped short of a fixed point: all 16 round(s) changed the program,
```

我们主机 pass、CI 的 linux 腿红，`[stderr]` 说 `std::opt` **十六轮都没有到达不动点**。
同一份源码在两个 Linux 上给出不同答案，这一条是确定性面，按缺陷记；读数在此，判因与修另开。

### 三、win / macos 独有的那一串——平台判据不同

win 独有（8 条，去重 `selfchecks.camel_remote_args`）：

```
depth_across.prl_off.exceeds
depth_across.prl_off.fits
facts.self_check
math_scalar_gradient_through_the_library.nvm
op_record.cpp_registration_states_panic
recursion_too_deep.prl_evaluator
retention.name
split_whole_and_member.nvm
```

macos 独有（12 条，去重与 win 共有的 `math_scalar_gradient_through_the_library.nvm`、
`selfchecks.camel_remote_args`、`selfchecks.promotion_record`）：

```
entry.exit_after_fault.jit
entry.exit_call.jit
examples.case_study.break_rate.jit
fail.kind_message.jit
fail.no_message.jit
selfchecks.hook_checker_contract
selfchecks.oversight_reload
selfchecks.push_trace_range
stdlib.fault.zip_with.jit
unwrap.take_fallible.jit
unwrap.take_nullable.jit
unwrap.take_where_text.jit
```

它们的失败行原文（各取一条）：

```
[FAIL] depth_across.prl_off.exceeds                         112.25 ms     (win-x64)
   [INFO] output does not contain 'Maximum recursion depth (1000) exceeded'
   [stdout] start …

[FAIL] unwrap.take_where_text.jit                            36.44 ms     (macos-arm64)
   [INFO] differs from unwrap.take_where_text.nvm: diagnostics
   [INFO] verification exited 1
```

`depth_across.prl_off.*` 是**断言文本不匹配**（期望的句子没有出现），`unwrap.*.jit` 与
`entry.exit_*.jit`、`fail.*.jit`、`stdlib.fault.zip_with.jit` 都在 **jit / 诊断面**。
这一类我们只能在自己的 Linux 主机上取证，所以先"记录不修"。

## 六、这一页没有做的事

- 没有为这 23 条开批、没有改任何期望、没有往 `known failures` 里加名字（它们不是产品回归，
  也还不该被吸收成已知）；
- 没有开新枝：这一页随下一次文档合并进 `integration`；
- 没有把这 23 条写成"环境问题"——写的是它们各自的事实：**平台面不同**、**工具不在场**、
  **同提交不同读数**。