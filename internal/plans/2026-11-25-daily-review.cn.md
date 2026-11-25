# 每日审查：11-24 18:30 至 11-25 18:30 合进 integration 的代码

审查范围是 integration，不是 develop：车道都先进 integration，develop 只在晋级时前进。下面只列决策者逐条核实成立的，按轻重排。

## 1. "装入"仍然由转换的安全表来判定

`src/kernel/core/type/base.cpp:291` 的 `Type::castEntersFrom` 先问 `castSafetyFrom(sourceType) == CastSafety::Safe`，安全表说安全就算装入。`tuple.cpp:342` 也一样；`builder_control.cpp` 的 `castNodeTo` 以它为闸。这正是负责人否掉的第 4 项：把装入和转换混成一个概念。另外，`T[..]` 读成 `T` 这条"秩为零"的边也被算成装入，可它要等值出来才知道秩，可能失败。

修法：

- 装入按类型的结构判定：目标是源的 `T?`、`T!`，或者错误值进带 `!` 的类型。不查安全表。
- 秩为零那条边改成形状检查，用 `:op/refine_extent` 那一类算子，可能失败，就不叫装入。
- 安全表只回答用户写的转换。

验收：

- `castEntersFrom`（或替代它的判定）里不出现 `castSafetyFrom`；
- 新增用例：一个安全的数值配对（例如整数加宽）放到要求另一数值类型的位置，编译期报类型错误，因为数值之间不做隐式转换。

## 2. "可自由安排"仍有两份答案，事实层被改成去迎合旧的那份

`runtime::nodeIsFreelySchedulable`（`src/kernel/runtime/graph.cpp:168-216`、`:1897`）自己用 `nodeMayFault` 加 `mayBeRecomputed` 算一遍。下列地方读的都是它，不是事实层的 `core.arrangeable`：

- 建图器 `builder_helpers.h:756,758`；
- 优化遍 `std/cxx/opt/generic/rewrite.cpp`、`inline/engine.cpp`；
- `tiles/src/region_outline.cpp`；
- `graph/read.cpp`、`graph/txn.cpp`；
- 冻结路径。

facts-draft、node-roles-facts、plain-gate-2 三次合并，都把事实改成"和它答得一样"：调用一律答否，门一律答否。比对只靠测试侧的 `facts_compare.cpp`。这违反 D-1124-65 的裁定：可自由安排只在事实层算一处，别处只读。

修法：

- 删掉 `nodeIsFreelySchedulable` 的独立实现，所有读者改读事实层。
- 两边答案不同的地方，按设计原则 3.4（纯、不会出故障、这个节点已知会终止）判对错，不按旧谓词。
- 判下来改了的行为，逐条写进合并说明。

## 3. 过程痕迹：检查器没拦住，三处成片出现

- **`src/`**：
  - `D-1125-18`：`builder_call.cpp:921`
  - `D-1124-65`：`facts.cpp:1032`、`arrangeable.h:38`
  - `C-1123-17`：`transforms/jvp.cpp:1302`、`node_order.cpp:31`、`rule_expand.cpp:842`
  - 提交哈希 `61bab72902`：`cast.cpp:327`
  - "Measured on `lane/std-nop`"：`pass.cpp:1985`
  - "the log entry of 2026-11-25"：`builder_control.cpp:54`
  - 日期与"this lane"：`rulesets/transpose.cml:470,813,1269`
- **`test/plans/`**：191 行"# Slow layer: … in the 2026-11-12 record (appendix B of internal/reports/…)"。慢层要写的是规则（实测超出快层预算），不引报告与日期。另有 `selfchecks.plan.toml` 的"measured 2026-11-25 on the Linux build host"；`autodiff.plan.toml:466` 的 `known_fail_owner` 写的是车道名。
- **工具**：
  - `D-1124-78`：`merge-push-shape.ps1:3`、`merge-lane.ps1:183,754,858`、`merge-batch.ps1:127,849`
  - `C-1124-xxx`：`camel-gate-state.ps1:11`、`red-table-context.mjs:8,239`
  - 个人路径 `/home/zjwei/...`：`host/camel-run.sh:56`

修法：

- 并进待开车道单子第 10 条（过程痕迹检查器扩范围），一次清干净；
- 检查器挂在推送前，查清为什么这些没被拦下来。

## 4. 测试遍又编进了 libcamel

selftest 那次合并把 `test::params`、`test::declared` 搬回了 `src/stdlib/std/selftest/`（D-1125-26 已派根因修）。这里只补一点：两个文件里的"since 2026-11-25"也一并删掉。

## 5. 调用处维度核对：没有测试，还留着旧注释

- `:op/refine_extent` 在 `test/` 里一处都没有：没有用例钉住"图里是 `refine_extent` 而不是 `cast`""它会以形状不符报故障"。
- `builder_call.cpp:2937-2942` 还留着旧注释："THE STOP … the check is a run-time conversion here … the owner decides which arm holds"。这件已经定了，注释要改成现在的做法。

## 6. "时间敏感"的唯一读法没有测试，读坏了还会静默放行

- `run-tests.mjs` 的 `--print-time-sensitive` 和它唯一的调用者 `camel-remote.ps1:~1909-1935`，没有任何计划测到。
- `camel-remote.ps1:1930` 的 `if ($fields.Count -ne 2) { continue }`：读出的行坏了，就当"不时间敏感"放行，这正是"缺了就当否"。

修法：坏行就停下并报"没测"；加一个计划，钉住"声明了的计划被读成时间敏感，坏行被拒"。

## 7. 工具里写死了个人的主机名

`internal/tools/merge-batch.ps1:240` 的 `$script:sshHost = '88zjwei'` 是默认值，而 `camel-remote.ps1` 没有配置就拒绝运行，两个工具的口径不一。改成和 `camel-remote` 一样只读配置，没有就报错。`scripts/bisect-merge.mjs:90` 同样处理。

## 8. 工具自测在闸门主机上从不运行，账面却写成"都由计划启动"

19 个 `*.test.ps1` 都写着 `skip_unless pwsh`，而闸门主机是 Linux、没有 `pwsh`，所以它们在闸门上全部跳过。`plan-inventory.mjs` 只数"有没有被计划点名"，不数"跑没跑"。

修法：

- 账面改成报"量到了几个、跳过了几个"；
- 这 19 个要么改写成主机上能跑的，要么写明在哪台机器、什么时候跑（工作站或每日的 Actions），并且真有那一跑。

## 9. 自测计数写死在计划和 README 里

`selfchecks.plan.toml` 钉死了"17 checks, 0 failed"这类精确数字，`internal/tools/README.md` 又抄了一遍。每加一项自测，计划就会变红，README 就会过时。改成钉"0 failed"加一个下限，README 不写具体数字。
