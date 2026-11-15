# 一格会抽签的红，能不能戴 `known_fail` 记成常驻红？

- 日期：2026-11-15
- 车道：`lane-prlcount`（工作树 `D:\Projects\Camel-wt\prlcount`，基线 `f135bbb16`，`origin/develop` `4d475aaaf`）
- 状态：**等回答**（默认建议见文末）
- 相关：原则 10.5；`docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md`；
  `docs/handover/reports/known-failures-two.md`

## 问题

`test/plans/feat/compile/ref.plan.toml#ref.boxes_made_per_call_ten` 的 `prl` 臂（`ref.boxes_made_per_call_ten.prl`）
不是稳定红，也不是稳定绿：同一条命令、同一个二进制，`--jobs 4` 下若干轮红、若干轮绿，红的读数是
`created=11 released=5 live=6` 这一类，绿的读数是 `11/11/0`。原因在
`docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md`：唯一把 `live` 减一的入口是收集器，
prl 的收集点落在哪个程序点由调度决定。

`--gc-stress` 下要让闸门不因它红，`known_fail` 是唯一会把红登记进 `[SUMMARY]` 的机制。**但它对抽签的格子不成立。**

## 量到的：`known_fail = ["prl"]` 之后那一格反而更红

在 `ref.boxes_made_per_call_ten` 上写 `known_fail = ["prl"]`（`known_fail_owner`、`known_fail_reason`、
`known_fail_msg = "nothing accumulated: the live blocks do not follow the calls that are over"` 齐全，
理由写的是「第 5 项：计划侧最后使用分析落地后删除本字段并把 `live <= 3` 收紧为等值」），
`node scripts/test.js test/plans/feat/compile/ref.plan.toml --jobs 4` 连跑 8 轮：

| 轮 | `[SUMMARY]` | 那一格 |
|---|---|---|
| 3、5、6、8 | `total: 52, pass: 51, fail: 0, known failures: 1` | `[XFAIL] ref.boxes_made_per_call_ten.prl` |
| 1、2、4、7 | `total: 52, pass: 51, fail: 1`（无 `known failures`） | `[FAIL] ref.boxes_made_per_call_ten.prl` |

同一棵树、同一个二进制、不戴标连跑 10 轮：`fail: 1` **5 轮**、`fail: 0` **5 轮**。

所以：**戴标没有降低红率（4/8 对 5/10），只把红换了原因。** 第 1、2、4、7 轮的红不是断言红，是 runner 的两向规则：

```
marked known_fail on 'prl' but passes: remove the mark
```

（`test/tools/run-tests.mjs`：标了 `known_fail` 的用例**通过**时判红，见 `known-failures-two.md`
「`known_fail` 是登记了失败形态、但不许悄悄变绿的机制」。）

`known_fail` 在这格上把红率从 `p` 换成 `1 − p`：原来红的那一轮变成 `known failures`，原来绿的那一轮变成硬红。
`p ≈ 0.5` 时两者相同；`p` 越小越糟（同一格在更空闲的机器上量过 10 轮，只有 1 轮红，那时戴标就是 9 轮红）。
**闸门不是不再因它红，而是换成一半的轮次红在「remove the mark」这句与缺陷无关的话上。** 这一笔我没有提交，
量完就把标记撤了（`git checkout -- test/plans/feat/compile/ref.plan.toml`）。

## 三条可选的形状（都不改 `live <= 3`、不删 prl 臂）

| 形状 | 闸门 | `[SUMMARY]` 里的它 | 代价 |
|---|---|---|---|
| **甲**（今天的形状） | 按 `p` 红（`--jobs 4` 实测 5/10） | `fail: 1` 那一轮 | 闸门不稳定 |
| **乙** `known_fail = ["prl"]` | 仍按 `p` 红（实测 4/8），只是红的原因换成「remove the mark」 | `known failures: 1`（只在真红的那轮） | 把缺陷红换成一句与缺陷无关的红；`p` 小时红率暴涨 |
| **丙** `skip_unless = "env.CAMEL_RUN_PRL_COUNT"` | 绿 | `skip: 1` | 那格不再出现在失败里：抽签这件事只有页与 `desc` 记着，变好时没人叫 |

`known_fail` 之外没有别的字段能把一格登记成「红但可接受」：`TEST_FIELDS` 里只有 `known_fail`、
`skip_unless` 与断言族，没有「允许通过也允许失败」的形状（`test/tools/run-tests.mjs` 的 `TEST_FIELDS`）。

还有一条**不做**的路，写在这里是为了让人看见它被放弃：把 `known_fail_msg` 钉成 `FOREIGN_RESOURCES`
这种两向都出现的碎片，`known_fail` 就会在「真红」和「标了却通过」两种失败里都命中，于是每轮都是
`known failures: 1, fail: 0` —— 三条要求字面上全满足。它被放弃的原因是**那正是 pin 存在要防的事**：
`test/tools/run-tests.mjs` 要求「quote the sentence this case prints today」，一句宽到两种失败都匹配的
钉子等于没有钉子，换一个缺陷来穿同一件衣服也没人看得出。

## 默认建议

**选丙，并把撤销条件写进条目**：`skip_unless = "env.CAMEL_RUN_PRL_COUNT"`，条目 `desc` 与计划注释里写明
「第 5 项：计划侧最后使用分析落地后删掉 `skip_unless`，把 `live <= 3` 收紧为等值，并让这一格重新无条件运行」。
理由：三条里只有丙同时满足「闸门不因它红」「不改断言」「不删臂」，而它唯一的损失（变好时没人叫）已经被
**同一句撤销条件**接住了 —— 那条车道要交付的正是「`live` 等于计划给的持有者数」，交付时必须回来动这一格。

乙不可选：它不降低红率，只把「缺陷红」换成「remove the mark」——读的人看到的是一句与计数无关的话，
而真正的读数（`live` 抽到了几）不再出现在失败里。这正是 `known_fail` 的两向规则要避免的形状。
甲也不可选，如果闸门必须是稳定的：它把「这一格红」变成抛硬币。

如果作者要的是「红的次数就是这一格的读数」，那就选甲，并接受闸门按 `p` 红；这一页就不需要落地。