# milestone 层：还写着旧词的地方，和四个待定的口径

三层命名（fast / slow / milestone）已经落地：闸门慢层仍是 14 个具名计划文件（整跑 445 条），
`--tier slow` 只选这 156 条，政策扣下的 25 个计划文件在自己的文件里写 `tier = "milestone"`，
未抽样的 sweep 副本也归 milestone（2761 个副本里 2605 个）。这一页记本次车道权限之外、需要
负责人拍板的事。

## 一、两处我无权改的文件还写着旧词

- `docs/handover/reports/tiles-transfer-case-ownership.md`：说 `gpu/tiles_regions.plan.toml` 是
  `tier = "slow"`，该文件现在是 `milestone`。
- `docs/design/rewrite/plan-verification.cn.md` 第 331 行：要求新求解器计划"标 `tier = "slow"`"；
  按今天的命名，分钟级、只在里程碑跑的应标 `milestone`。

**问：这两处由谁在下一轮改成新词？**（handover 与 design 两棵树本次都禁改。）

## 二、`docs/diagnostics.md` 的两条示例现在指向 XFAIL 的用例

把 `array_alias.plan.toml` 搬进 `test/plans/feat/compile/` 之后，它按路径排序排到了
`array_shape`、`modules/optim` 前面，于是生成器的两条 "**Example**" 换成了
`test/cases/compile/array_alias/{type_text,err_element_type}.cml`——而这两个用例在 P4 落地前是
XFAIL。本次按工具自己的要求重跑了 `--write`（16 增 23 删），没有改生成器。

**问：文档示例是否该跳过 `known_fail` 的测试？** 若要，"示例优先取会通过的测试"是
`scripts/diagnostics-catalog.mjs` 里一行的事，但那是本次裁定之外的改动，所以没做。

## 三、闸门跳过行的数字口径

旧行是 `skipped by policy: 25 plan files, 200 slow cases`（计划文件里的原始行数），新行是
`held back 25 plan files / 225 milestone cases`（runner 自己读出来的测试数：`backends` 一行会
展开成多条，25 条差额全部来自它）。本次选了 runner 的口径，因为闸门扣下的就是这些测试。

**问：以哪个为准？** 若要 200，我把计数换成"原始行"口径。

## 四、裁定里的"约 381 条"与今天的树对不上

`--tier slow` = 156 条；闸门慢层（14 个具名文件整跑）= 445 条；156 + 政策扣下的 225 = 381。
按今天的树，前两个数字都不等于 381。

**问：381 是"慢层勾选 + 政策扣下"，还是另一棵树上的旧测量？** 若是前者，我把它写进
`docs/test-plans.md` 的层定义里。

## 五、`facts-cost.py` 要不要计划条目

它的 docstring 明说"是测量工具，不属于任何计划"，所以本次只改了示例（指到存在的
`test/cases/facts/tensors_and_loops.cml` 与 `test::facts_load_rules`），跑一次用的是临时计划、
跑完删掉。两个 gc 工具则各有一个 `milestone` 条目（新文件 `test/plans/perf/gc_tools.plan.toml`）。

**问：`facts-cost.py` 要不要也进计划（那样 `--tier milestone` 会连测量工具一起跑）？**