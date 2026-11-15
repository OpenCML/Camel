# 一格会抽签的红，能不能戴 `known_fail` 记成常驻红？

- 日期：2026-11-15（裁决当天落：选甲，见文末）
- 车道：`lane-prlcount`（工作树 `D:\Projects\Camel-wt\prlcount`，基线 `f135bbb16`，`origin/develop` `4d475aaaf`）
- 状态：**已裁：甲**（另两案与全部数据留在页里，让人看见被放弃的路）
- 相关：原则 10.5；`docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md`；
  `docs/handover/reports/known-failures-two.md`

## 问题

`test/plans/feat/compile/ref.plan.toml#ref.boxes_made_per_call_ten` 的 `prl` 臂（`ref.boxes_made_per_call_ten.prl`）
不是稳定红，也不是稳定绿：同一条命令、同一个二进制，`--jobs 4` 下若干轮红、若干轮绿，红的读数是
`created=11 released=5 live=6` 这一类，绿的读数是 `11/11/0`。原因在
`docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md`：唯一把 `live` 减一的入口是收集器，
prl 的收集点落在哪个程序点由调度决定。

要让闸门不因它红，`known_fail` 是唯一会把红登记进 `[SUMMARY]` 的机制。**但它对抽签的格子不成立。**

## 量到的（全部实测，同一棵树、同一个二进制 `50fdda557c`，`--jobs 4`）

### 不戴标，连跑 10 轮

每轮的 `[SUMMARY]` 原文，依次：

```
1  total: 52, pass: 51, fail: 1
2  total: 52, pass: 52, fail: 0
3  total: 52, pass: 51, fail: 1
4  total: 52, pass: 52, fail: 0
5  total: 52, pass: 52, fail: 0
6  total: 52, pass: 52, fail: 0
7  total: 52, pass: 51, fail: 1
8  total: 52, pass: 51, fail: 1
9  total: 52, pass: 51, fail: 1
10 total: 52, pass: 52, fail: 0
```

红 5 轮、绿 5 轮。5 轮红的读数（同一格，退出时那一行）：`live=6` 三回、`live=5` 一回、`live=4` 一回，
例如 `FOREIGN_RESOURCES created=11 released=5 live=6 disposed=6 finalized=5`。绿的那 5 轮是 `live<=3`
（`11/11/0` 那类）；运行器通过时不打印用例输出，所以绿轮没有可引的原文，这正是它「看不见」的地方。

### 戴标 `known_fail = ["prl"]`，连跑 8 轮

标记本身按手册写全：`known_fail_owner`、`known_fail_reason`、`known_fail_msg = "nothing accumulated: the
live blocks do not follow the calls that are over"`，理由写的是「第 5 项：计划侧最后使用分析落地后删除本字段
并把 `live <= 3` 收紧为等值」。每轮的 `[SUMMARY]` 原文，依次：

```
1  total: 52, pass: 51, fail: 1
2  total: 52, pass: 51, fail: 1
3  total: 52, pass: 51, fail: 0, known failures: 1     [XFAIL] ref.boxes_made_per_call_ten.prl
4  total: 52, pass: 51, fail: 1
5  total: 52, pass: 51, fail: 0, known failures: 1     [XFAIL] ref.boxes_made_per_call_ten.prl
6  total: 52, pass: 51, fail: 0, known failures: 1     [XFAIL] ref.boxes_made_per_call_ten.prl
7  total: 52, pass: 51, fail: 1
8  total: 52, pass: 51, fail: 0, known failures: 1     [XFAIL] ref.boxes_made_per_call_ten.prl
```

第 1、2、4、7 轮的红**不是**断言红，是 runner 的两向规则，原文：

```
marked known_fail on 'prl' but passes: remove the mark
```

那 4 轮的读数都是 `live=0`（`FOREIGN_RESOURCES created=11 released=11 live=0 disposed=0 finalized=11`）——
**红的那 4 轮，恰好就是断言通过的那 4 轮。**（`test/tools/run-tests.mjs`：标了 `known_fail` 的用例
**通过**时判红；见 `known-failures-two.md`「`known_fail` 是登记了失败形态、但不许悄悄变绿的机制」。）

### 结论

**戴标不降红率（4/8 对 5/10），只把红换个原因。** 它把红率从 `p` 换成 `1 − p`：原来红的那一轮变成
`known failures`，原来绿的那一轮变成硬红。所以**格子越绿，标记造的红越多** —— 同一格在更空闲的机器上量过
10 轮，只有 1 轮红，那时戴标就是 9 轮红。这一笔没有提交，量完就把标记撤了
（`git checkout -- test/plans/feat/compile/ref.plan.toml`）。

## 三条可选的形状（都不改 `live <= 3`、不删 prl 臂）

| 形状 | 闸门 | `[SUMMARY]` 里的它 | 代价 |
|---|---|---|---|
| **甲**（选中） | 按 `p` 红（`--jobs 4` 实测 5/10） | `fail: 1` 那一轮 | 闸门不稳定，但每次红都是真缺陷 |
| **乙** `known_fail = ["prl"]` | 仍按 `p` 红（实测 4/8），只是红的原因换成「remove the mark」 | `known failures: 1`（只在真红的那轮） | 把缺陷红换成一句与缺陷无关的红；`p` 小时红率暴涨 |
| **丙** `skip_unless = "env.CAMEL_RUN_PRL_COUNT"` | 绿 | `skip: 1` | 真缺陷从 `[SUMMARY]` 里消失 |

`known_fail` 之外没有别的字段能把一格登记成「红但可接受」：`TEST_FIELDS` 里只有 `known_fail`、
`skip_unless` 与断言族，没有「允许通过也允许失败」的形状（`test/tools/run-tests.mjs` 的 `TEST_FIELDS`）。

还有一条**不做**的路，写在这里是为了让人看见它被放弃：把 `known_fail_msg` 钉成 `FOREIGN_RESOURCES`
这种两向都出现的碎片，`known_fail` 就会在「真红」和「标了却通过」两种失败里都命中，于是每轮都是
`known failures: 1, fail: 0`。**它能字面满足三条要求（数仍出现、闸门绿、不改断言），但正是 pin 要防的事**：
`test/tools/run-tests.mjs` 要求「quote the sentence this case prints today」，一句宽到两种失败都匹配的
钉子等于没有钉子，换一个缺陷来穿同一件衣服也没人看得出。

## 裁决：默认建议改为甲

**选甲（维持红）。** 丙被否掉，理由与乙同族：`skip_unless` 是 `known_fail` 的**安静版** —— 它把一个在
`--jobs 1`、无负载下也复现的真缺陷（`docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md` 表五、
表二）从 `fail` 变成 `skip`，让它**从 `[SUMMARY]` 里消失**。既然串行也红，就没有任何「环境不适用」的理由可用。
**不可见比吵人更坏。**

甲的落地只有一处：`test/plans/feat/compile/ref.plan.toml` 该条目前面的一句注释（不改断言、不改 `live <= 3`）：

```
# red by new item 5 (plan-side last-use analysis); remove when it lands, then tighten live <= 3 to an
# equality. p≈0.3-0.5 at --jobs 4, ≈0.25 at --jobs 1. Do not rerun to close it.
```

乙留下的东西只有这一页的测量：它是一条**应该被记住的否定** —— 标记不是「把红变安静」的旋钮，对会抖的格子
它只会把绿的那一侧也变成红，而且红在一句与缺陷无关的话上。
## 决策者答复（2026-11-15）

甲同意：维持红，写明原因。不可见比吵人更坏。根因是 prl 的读数取决于收集器落在哪个程序点；而按原则 10.5，`Ref` 盒子只靠计数回收、不靠收集器。所以这道红由"计数做准"（已提到优先级一）消掉：计数准了，归零当场释放，读数与调度无关，这条用例在四个后端上都应该是 `11/11/0`。到时去掉注释、断言改成精确值。
