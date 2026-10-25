# 问题：慢层的构成——慢层里哪些用例占了位置、它们从哪一步来，慢层要不要有别的口径

事实与读数，不含该归哪一层的判断。开发线 `tip=d733bfd47`；测量车道的读数未入库，本页引述其
`[SUMMARY]` 与日志。所有读数都能用第 5 节的办法复算。

## 1. 事实一：慢层约九成是 `gc_poison_sweep` 生成出来、而没被采样到的副本

测量车道对两条线各做一次整层慢层测量（`-Tier slow`，逐子树，排除共享主机禁跑的 `perf/**` 与
`feat/opt/progen`），两次的 `poison:` 用例占比：

| 线 | `poison:` / 慢层总数 |
|---|---|
| 开发线 | 2649 / 2961 |
| 张量翻转线 | 2658 / 2968 |

我在 `tip=d733bfd47` 上用只读枚举复算（不跑测试，见第 5 节）：整棵 `test/plans` 的 `-Tier slow`
选中 2985 条；连同"被选中用例必须比较的参照"一起是 3030 条，其中 `poison:` 2649 条，87.4%。
两条线各测一次都落在这个比例上，说明它不是某条线的产物。

### 是哪一步决定的

四步，逐字引：

1. **计划文件自己是快层。** `test/plans/feat/mm/gc_poison_sweep.plan.toml` 开头就是 `tier = "fast"`。
   它自己一条 `[[tests]]` 都没有，只有 `[[sweeps]]`。
2. **采样参数在同文件。** `fast_sample = 1`、`risk_sample = 3`、
   `risk_tags = ["collections", "closures", "iterators", "comprehension", "loops", "collect", "higher_order"]`：
   每个源计划留 1 条在快层，`tags` 命中 `risk_tags` 的源计划留 3 条。取哪个数的这一行是
   `test/tools/run-tests.mjs` 里的
   `const sampleCount = fastSample > 0 && risky && riskSample > fastSample ? riskSample : fastSample`。
3. **决定性的那一步：`expandSweeps` 给每一条生成的副本盖一个显式 `tier`，采到的盖 `fast`、
   没采到的盖 `slow`。** 逐字（`test/tools/run-tests.mjs`）：
   ```js
   ...(sampleCount > 0 ? { tier: sampled.has(test) ? 'fast' : 'slow' } : {}),
   ```
4. **这一盖压过计划文件的 `tier`。** `testTier` 先读用例自己的字段，再退回计划的（`test/tools/run-tests.mjs`）：
   ```js
   const tier = test.tier !== undefined
       ? test.tier
       : plan.tier === 'slow'
           ? 'slow'
   ```
   所以第 3 步盖上的 `tier = 'slow'` 一盖就生效，`test/plans/feat/mm/gc_poison_sweep.plan.toml`
   的 `tier = "fast"` 对生成副本完全不适用。

计划文件自己的 `desc` 也是这么写的：`the unsampled copies are the slow layer, and --tier all adds the
sampled fast ones`。所以慢层里"每 10 条有 8 到 9 条是它"这条构成，是这套采样规则直接算出来的；
本页要说的是它的**规模**以前没有被记下来。

### 这两千多条对既有台账不可见

- `scripts/plan-inventory.mjs` 的第四归宿检查只数 `[[tests]]`：
  `const slowTests = (plan.tests ?? []).filter((test) => (test.tier ?? planTier) === 'slow').length`。
  生成副本不在 `plan.tests` 里，所以 `test/plans/feat/mm/gc_poison_sweep.plan.toml` 算出来是
  "被快目标 `test/plans/feat` 达得到、且 0 条 slow 用例"，检查通过（该文件在检查里从不出现）。
- `scripts/release-gate.mjs` 的两层也不跑它们：快层是
  `const fastArgv = [...FAST_TARGETS, '--tier', 'fast', '--jobs', String(options.jobs), ...camelArg]`
  （`--tier fast` 只选 fast 用例），慢层是 `const slowPlans = [...SLOW_PLANS, ...extra]` 逐个点名，
  名单里没有 `test/plans/feat/mm/gc_poison_sweep.plan.toml`，`FORBIDDEN` 里也没有它。
  所以这 2649 条只在 `--tier slow` / `--tier all`（`npm run test:all`）下存在，发布闸门两层都看不到。

## 2. 事实二：慢层 9 次运行 `skip 0 / known 0`，快层是 `skip 11 / known 8`

测量车道的 9 次整层慢层运行的日志里没有一条 `[SKIP]`、也没有一条 `[XFAIL]`，`[SUMMARY]` 里
`skip:` 与 `known failures:` 两个字段都没出现——`test/tools/run-tests.mjs` 的 `printRunSummary`
只在大于 0 时打印它们（`const skipped = summary.skip > 0 ? ...` / `const known = summary.xfail > 0 ? ...`，
`total = pass + fail + skip + xfail`）。开发线快层基线是 `6178 / 6159 / fail 0 / skip 11 / known 8`。

只读枚举在 `tip=d733bfd47`、同样目标（`smoke` + `test/plans/feat`）下复算：

| | 选中 | 带 `skip_unless` | 带 `known_fail` |
|---|---|---|---|
| 快层 | **6178** | 59 条 / 12 条不同前置命令 | **8** |
| 慢层 | 2850 | 22 条 / 8 条不同前置命令 | **0** |

6178 = 6159 + 11 + 8，与记录到的快层基线逐字相同，所以这个枚举与既有读数对齐。

- **`known 8` 是结构性的，慢层不可能有。** 慢层选中的用例里 `__knownFail` 为真的是 **0** 条。8 条
  全在快层：`test/plans/feat/linear/effects_model_edges.plan.toml` 的 4 条 `edges.*.gir`，与
  `test/plans/feat/modules/array_reduction_kernels.plan.toml` 的 4 条 `array_reduction_*` 的
  `fvm`/`jit`。而且 `expandSweeps` 的筛选用例那一行直接写
  `if (test.sweep === false || test.__knownFail) continue`——标了 `known_fail` 的副本**根本不会生成**。
- **`skip 11` 逐条都落在快层独有的两条命令上。**
  慢层的选中集合里有 22 条带 `skip_unless`（`test/tools/have-native-jit.mjs`、
  `test/tools/have-cuda-device.mjs`、`test/tools/have-cuda-build.mjs`、
  `test/tools/cppexec/run.py --have-compiler`、`${python} -c "import numpy, onnxruntime"` 等）。
  快层求值 12 条不同的前置命令、慢层求值 8 条，两层共有 4 条（`have-native-jit`、两条 cuda、
  `numpy,onnxruntime`）。这 4 条在这些运行里都答"有"——否则慢层也会 skip。
  我在本工作树上跑的那次快层闸门把 11 格逐条打了出来，两句原因逐字：

  ```
  10 条  grammar.scopes.*    在 test/plans/feat/tools/grammar.plan.toml
  [INFO] precondition not met (node editors/vscode/scripts/have-deps.mjs): vscode-textmate and
  vscode-oniguruma not installed: run `npm install` in editors/vscode to enable the grammar snapshot tests

   1 条  unicode.paths       在 test/plans/feat/modules/unicode_paths.plan.toml
  [INFO] precondition not met (node test/tools/on-windows.mjs): this test asserts Windows'
  wide-character process boundary: not on linux
  ```
  10 + 1 = 11，与只读枚举里这两条命令的挂载数（10 / 1）逐字相同。这两个计划都到不了慢层：
  `test/plans/feat/tools/grammar.plan.toml` 不在 `[[sweeps]]` 的源目录里（源是 `smoke`、
  `modules`、`compile`、`linear`、`para`、`trans` 与 `consistency` 的几个文件），
  `test/plans/feat/modules/unicode_paths.plan.toml` 虽在 `modules` 里，但它的 `[defaults]` 写
  `tool = "node"`，而 `expandSweeps` 只收 `camel` 的用例：
  `if ((test.tool || 'camel') !== 'camel') continue`。所以慢层不是"忽略了"这 11 格，
  是它根本不求值这两条命令。
- **runner 里没有跨层检查。** `skip_unless` 与 `known_fail` 都是"本次运行、本层选中"才求值：
  `test/tools/run-tests.mjs` 里 `summary.skip++` 只对被选中的用例发生，`summary.xfail` 同理。
  所以"某个前置条件只在一层答了没有"这件事，没有任何一处会比对。

## 3. 事实三：慢层的时间与规模量级

来源一，已入库的整层读数 `docs/handover/batch-slow-2026-10-24.md`（32 核 EPYC 主机、`-Jobs 8`、
load average 69–96）：

```
[SUMMARY] total: 2922, pass: 2914, fail: 8
[INFO] wall: 1953.4 s, jobs: 8, tier: slow
```

同页在合并后的新 tip 再跑一次：`[SUMMARY] total: 2947, pass: 2938, fail: 9`、`[INFO] wall: 2191.6 s`；
同页的 `perf` 层 `-Tier all` 是 45 条 / `881.6 s`。

来源二，我在 `tip=d733bfd47` 的只读枚举（不跑测试）：2850（`smoke` + `test/plans/feat`）、
2985（整棵 `test/plans`，纯 slow 层）、3030（连带参照）、2755 条 `poison:` 副本（151 条被采样进快层，
2604 条盖成 `slow`）。

合起来：整层慢层是 **1900–2200 s、约 2900–3000 条**的量级，一次"批次点慢层"就是半小时以上、
把 32 核占满；其中 2649 条是同一个属性的 `poison:` 副本。

## 4. 慢层露出的 5 条红（只为说明慢层为什么这么大，不是本页主题）

慢层在开发线露 5 条红，其中 4 条已有页：

- `debugger.dap_protocol` —— `docs/handover/questions/diagnostics-limit-becomes-rewrite-invalid.md`
- `poison:modules.rules:rules.library_verdicts_are_exactly_these`、
  `poison:modules.rules:rules.reduced_element_type_instances_are_checked`、
  `poison:modules.rules:rules.every_exported_rule_enters_the_check` ——
  `docs/handover/questions/rule-when-condition-hidden-node-parameters.md`
- `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update` —— 在 `docs/handover/questions/`
  与各分支上都没有找到对应的页。

## 5. 要问的

1. **未采样到的生成用例该不该归 `slow`？** 今天它归 `slow` 是一行显式的
   `{ tier: sampled.has(test) ? 'fast' : 'slow' }`。要不要给它一个独立的层（例如只跑生成副本的层），
   或者标成 `all`（两层都跑）？——三种做法对"谁排批次点"的代价不同：现在这 2649 条只能整层跑。
2. **慢层要不要有一个"只跑非生成用例"的口径？** 一个批次点的慢层若去掉生成副本，剩下的是
   2985 − 2604 = 381 条（我这次枚举的读数），量级从半小时降到与别的层相近；那 2604 条
   `poison:` 覆盖就只能靠整层跑或 `--tier all`。这个口径要不要有、叫什么、谁跑，属于安排问题。
3. **快层那 19 格（`skip 11` / `known 8`）在慢层不可见，要不要有一条机械检查？** 今天这 19 格
   在慢层不可见，靠的是三件互不相干的事：`__knownFail` 的副本根本不生成；`grammar` 计划不在
   `[[sweeps]]` 的源目录里；`unicode_paths` 是 `tool = "node"`。也就是说没有一条规则在保证
   "只在某一层出现的东西必须被另一层看见"——一个前置条件若加到一个会被 sweep 到的 `camel` 用例上，
   它就会在两层都求值（今天慢层那 22 条就是这种），而加在快层独有的用例上就只在快层求值。
   要不要有一条检查保证"被 skip 或标 `known_fail` 的东西不会只在某一层静默"——例如：同一个源用例的
   前置条件在两层都求值，或一层出现的 skip 必须在另一层被点名？

## 6. 核实方法（可复算）

读了这些文件（全路径，不含行号）：

- `test/plans/feat/mm/gc_poison_sweep.plan.toml`（`tier`、`fast_sample`、`risk_sample`、`risk_tags`、`desc`）
- `test/tools/run-tests.mjs`（`expandSweeps`、`expandBackends`、`loadPlanTests`、`loadPlans`、
  `testTier`、`selectTier`、`printRunSummary`、`summary.skip++`、`__knownFail` 那一段）
- `scripts/plan-inventory.mjs`（`FAST_TARGETS`、`SLOW_PLANS`、`FORBIDDEN`、`planInventoryProblems`）
- `scripts/release-gate.mjs`（`fastArgv`、`slowPlans`、`forbiddenSlowTests`、`printSkipList`）
- `test/tools/toml-lite.mjs`（只为拿到 `parseTomlFile`）
- `docs/handover/batch-slow-2026-10-24.md`（第 3 节的时间读数）
- `docs/handover/questions/` 下已有的 4 页（第 4 节的对照）
- `agents/test.md` 的 General Policy 那一条（它点名"full suite / GC poison sweep / GC stress /
  progen / PyTorch 对比 / perf-timeit"是 milestone 才跑的）

跑了什么：

- 只读枚举：一份放在 `%TEMP%` 下的脚本（**不在仓库里**，不进任何提交），用 `file://` 导入
  `test/tools/toml-lite.mjs` 的 `parseTomlFile`，逐字复制 `test/tools/run-tests.mjs` 里
  `expandBackends` / `expandSweeps` / `testTier` / `selectTier` 的选择规则，遍历整棵
  `test/plans/**/*.plan.toml`，统计每层选中的条数、`poison:` 占比、`skip_unless`、`__knownFail`、
  各前置命令的挂载数。它不跑任何用例、不读二进制、不写仓库。
  - `tip=d733bfd47`：快层 6178（= 6159 + 11 + 8，与记录到的基线一致）；慢层整棵树 2985 / 连带参照
    3030，其中 `poison:` 2649；`poison:` 副本合计 2755（151 fast / 2604 slow）；
    慢层 `__knownFail` 0；慢层 `skip_unless` 31（整棵树）/ 22（`smoke` + `feat`）。
- 全量快层闸门，只走 `D:\Projects\Camel-wt\camel-remote.ps1`（`-Path` 指向本工作树），
  目标 `smoke,test/plans/feat -Tier fast -Jobs 8`，`tip=d733bfd47`，退出码 0：
  ```
  [SUMMARY] total: 6178, pass: 6159, fail: 0, skip: 11, known failures: 8
  [INFO] wall: 258.5 s, jobs: 8, tier: fast
  [SPLIT] build 17.1 s, test 258.5 s, total 275.6 s
  ```
  与开发线基线逐字相同；第 2 节那 11 格的两句原因就是从这次运行的输出里读出来的。

没做的：没有为了写这页去跑整层慢层、`perf/**`、`feat/opt/progen`、GC-stress 或 PyTorch 对比；
没有改任何代码、计划、期望、`tier`、`known_fail`；没有动
`scripts/plan-inventory.mjs` 的三张表；没有去碰第 4 节那 5 条红；没有测慢层那 22 条前置条件
在本机与主机上是否答得一样（第 2 节引的是主机上的那次快层运行的日志）。