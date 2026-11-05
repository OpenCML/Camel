# 扫描派生的 3272 条副本，该和其余 246 条一起被当作「一层」吗？

## 问题

`milestone` 层今天读作 **28 张计划 / 3518 条**，其中 **3272 条（93%）不是谁写下的，
是 `test/plans/feat/mm/gc_poison_sweep.plan.toml` 的 `[[sweeps]]` 从别的计划里展开出来的**
——它自己一条测试都不写（`own tests: 0`）。于是「跑 milestone 层」这句命令实际做的事，
有九成是「把全树的测试带 `--gc-check poison` 再跑一遍」，而剩下 246 条才是那些计划自己声明的
条目。**一层里混了两件事，读数就只有一个数。**

这件事和「谁跑 milestone 层」（`2026-11-04-who-runs-a-milestone-tier-plan-and-when.cn.md`）
是两个问题：那个问「谁跑」，这个问「那 28 张里 93% 的条目算不算这一层的内容」。

## 读数（2026-11-04，`origin/develop` `33daa3003`）

**一、3272 条怎么来的。** 展开逻辑在 `test/tools/run-tests.mjs#expandSweeps`，
参数在 `test/plans/feat/mm/gc_poison_sweep.plan.toml#sweeps`：

| 步 | 数 |
|---|---|
| 扫到的源计划文件（`plans` 的 12 个目录/文件，去掉 `exclude` 的 autodiff、onnx，去掉自己） | 214 |
| 这些计划自己的条目 | 5386 |
| 展开前被滤掉 | **1902** = 非 `behavior`/`snapshot` 1269、`sweep = false` 56、`known_fail` 22、非 `camel` 工具 93、自带的 `--gc-` 参数 80、首个参数不是跑程序的 382 |
| 合格条目 = 产物副本 | **3484** |
| 其中被抽样留快层（`fast_sample = 1`，风险标签 3） | 212 |
| 其余 = `milestone` | **3272** |

抽样是**按源计划**做的：一次 `[[sweeps]]` 里每个源计划留 1 条（`tags` 命中 `risk_tags`
的留 3 条）。214 个源里 **184 个产出了副本、30 个一条也没有**；16 个命中风险标签，其中 14 个
真的留了 3 条，`modules.rules_differential` 因为合格条目不够只留了 1 条，
169 个非风险源各留 1 条——`14×3 + 1 + 169 = 212`。**未抽中的部分一张计划最多 241 条
（`feat/linear/var_reference`，242 条副本里只留 1 条），中位数 5 条。**

**二、层里的两件事各是多少。** `scripts/plan-inventory.mjs#milestoneLayer` 读：
**3518 = 计划自己声明的 246 + 扫描派生的 3272**。所以「milestone 层 28 张计划」这句话里，
27 张各出几条到几十条，1 张（`gc_poison_sweep`）出 3272。

**三、如果按源计划抽样，一次改动实际跑多少条。** 拿一笔真的改动量：`624e41a87` 的 2 个文件
（`include/camel/runtime/draft.h`、`src/passes/opt/generic/generic.cpp`）选中 197 张计划，
带 `milestone` 条目的 6 张：

| 计划 | 今天 | 按源计划抽样（每源 1 条 / 风险 3 条） |
|---|---|---|
| `feat/linear/jit_framed` | 6 | 6 |
| `feat/mm/gc` | 44 | 44 |
| `feat/mm/gc_collections` | 73 | 73 |
| `feat/mm/gc_elder_exhaustion` | 22 | 22 |
| `feat/opt/progen` | 2 | 2 |
| `feat/mm/gc_poison_sweep` | **3272** | **212** |
| 合计 | **3419** | **359** |

**四、还有别的计划这样派生吗？没有。** `git grep -h '^\[\[[a-z_]*\]\]' -- test/plans`
只给出两种表：`[[tests]]` 6256 行、`[[sweeps]]` **1 行**（就是 `gc_poison_sweep`）。
`backends` 与 `compare_to` 会把一条已有条目再跑一遍或带上同伴，但**不新造条目、也不改它的层**
（`test/tools/run-tests.mjs#expandBackends`、`#selectTier`），所以它们不进这个数。

## 三条路与代价

**A. 拆成两层：`milestone` 只留 246 条声明条目，扫描全量另立一层（显式 `--sweep full`）。**
代价：`--tier milestone` 不再顺带做全量 poison 扫描——**而今天也没有任何闸门跑它**，所以这
不是新增的损失，是把「没有」变成「明说没有」。收益：一笔改动要补的 milestone 读数从 3419 条
降到 **147 条**（`624e41a87` 那笔：6+44+73+22+2），闸门按改动选计划的建议因此才成立。

**B. 保持现状（一页一层，3419 条）。**
代价：任何触及 `include/camel/runtime/`、`src/core/`、`src/passes/` 的改动都会通过目录规则选中
`feat/mm`，从而把 3272 条镜像拉进名单——**名单看起来是按影响面选的，实际被一张镜像全树的计划
接管**。这正是「判据指向了别的东西」：读数不假，但它回答的不是「这笔改动影响了什么」。

**C. 把 `fast_sample` 的抽样规则也用到 milestone 层。**
代价：层从 3518 → **458**（246 + 212），`--tier milestone` 从此不再是「全量扫描」，
计划自己的 `desc` 那句「the full sweep of every test is `--tier milestone`」随之作废；而且
抽样偏好「跑到底的用例」，**跨 safepoint 最危险的 GC stress 类恰恰可能被抽掉**——抽样的一张
永远不知道剩下 240 张里有没有一条会崩。

## 默认建议（已按此继续，不等回复）

**按 A 拆：`milestone` 层是「计划声明的 246 条」，`gc_poison_sweep` 的 3272 条全量副本是
另一层（`--sweep full`），由里程碑时点或明确的点名驱动。** 理由：

1. **一层的读数必须回答一个问题。** 今天 `[tier] milestone: 28 plan file(s), 3518 test
   entries` 里 93% 是一个计划的一次扫描，读者无法从这一个数里分出「谁在管哪些计划」；
2. **拆开之后闸门才付得起。** 按改动选计划那一版（合并车道加一步）在 A 下是 147 条，在 B 下
   是 3419 条——**同一个判据，代价差 23 倍**，差别只在于这 3272 条算不算这一层；
3. **全量扫描仍然要有出口，但不能假装它便宜。** `--sweep full` 是一个可以点名的读数，
   3272 条跑一次就是一次全树复跑（`timeout_scale = 20`），该在主机安静时由里程碑驱动，
   而不是被任何一笔改动的目录规则顺手带上。

**不动 `tier`、不动 `expandSweeps`。** 抽样怎么定的规则是 runner 的设计决定，这一页只把
两件事分开量清楚；按 A 落地要改的是层的登记（`test/tools/run-tests.mjs#coveredTiers` 与
`scripts/plan-inventory.mjs#milestoneLayer` 各多认一层），不是 `tier` 的值。

## 关联

- `docs/handover/questions/2026-11-04-who-runs-a-milestone-tier-plan-and-when.cn.md`：同一层的
  「谁跑」那一问；这一页是它第 9 条提醒的展开；
- `docs/handover/reports/2026-11-02-the-age-field-saturates-at-fifteen.cn.md`：`poison:` 变体
  第一次抓到真缺陷的那一夜，「有一个机制在替我们做组合覆盖，而这个机制所在的那一层从没被跑过」；
- `agents/test.md`「Run the plans your change touches」：这一层归「at a milestone, by the
  coordinator」，也就是靠人记得。