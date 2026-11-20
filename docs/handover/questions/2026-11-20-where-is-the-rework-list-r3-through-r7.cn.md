# 返工表 R3 到 R7 在哪：在 `questions-2026-10-29.md` 的 `.md` 那一半里

**2026-11-20 21:40。决策者当日第三项：**

> 「按序做每日审查的返工：
> - R1 收尾：lane-ecoop1 合入后，复核四处读者是不是都只调用那一个函数；
> - 之后依次是 R3、R4、R5、R6、R7。」

**R1 已做完并于 `ae093154c` 入库**（A `reachOfCall`、B `isEffectNode` 的 Call 分支、B′ 算子操作数分支
三处改调 `core::functionValueClauseAt`，第二入口 `core::functionOperandClause` 随之删除；
C `Resolver::call` 与 E `EffectWalk::ofNodes` 本来就走它；D `declaredClauseAt` 故意不调，
那里要「判」不要「拒」，`commit_checks.a_type_that_promises_nothing` 钉住）。

**R3 到 R7 找不到定义。** 已查、都为空：

| 查过的地方 | 结果 |
| --- | --- |
| `docs/handover/README.cn.md`（87 行，任务书入口） | 无 `R1`–`R7`，无「返工」「每日审查」 |
| `docs/handover/reviews/`（9 个文件命中 `\bR[1-7]\b`） | 全是别的意思（零载体 `R3/R11`、87 枝审计的 R3 列、架构审查的「R3 改效应系统」），没有一张返工表 |
| `docs/handover/questions/` | 无 |
| `docs/roadmap.cn.md` | 无 |

## 找到了：它在 `docs/handover/reviews/questions-2026-10-29.md`（`.md`，**没有 `.cn`**）

**上表那次搜索是错的，范围报得不完整：我用了 `-Filter '*.cn.md'`，于是 `reviews/` 里那批 `.md` 文件一个都没被扫到。
那份文件里有 62 处 `R1`–`R7`。**这正是我自己记过的那条：零命中要报出搜索范围。**

**逐条（`:3106`–`:3112`）：**

| 件 | 内容 |
| --- | --- |
| **R3** | `let _ = export_model(…)` 还在 8 条用例和官方示例里（`examples/ml/export_onnx.cml`、`test/cases/modules/onnx/export_train…`） |
| **R4** | `autodiff-migration-ledger.mjs` 的钉死常量（`:212` 的 `LEDGER_RECORDED`；plan 的 `desc` 与期望里又写一遍） |
| **R5** | `extent_of` 的属性在 C++ 里重述了一遍（`builtin.cpp:673`）—— 属性表只该从实现表登记 |
| **R6** | 两个「有没有路径」的遍历（`draft_inline.cpp:1367` `waitsOnAnyEdge` 与 `:1614` 的 lambda `waitsOn`） |
| **R7** | 拒绝原因有两套表 + 一条只给测试看的通道（`refusal_readings.h` 的 16 项枚举、手写 JSON、stderr 的 `READING_REFUSAL`） |

**排程（`:3125`）：**R2、R3 跟着 ONNX 那条车道走；R1 作为机械清理可与主线并行；R4 到 R11 排在第七节第 3 项（库层 `grad`）之后、删引擎之前。

**⇒ 而这张表一直在被追加，取最新版要读到最后：**`:3201`/`:3273` 是「R3 返工（第三次）」、`:3276`「R4 常开读数又添一处」、
`:3277`「R5 同一段复制粘贴出现三份」。**读 R3 之前先读 `:3273` 那一版，`3106` 那版已是旧的。**

## 要问的

**R3、R4、R5、R6、R7 各是哪一件事？** 或者：那张表在哪（页名 / 提交 / 我该按什么顺序取）？

**⇒ 而这一句要写清楚，因为它是我今晚犯过的第五次同类错：**
**没有定义就派工 = 按记忆派工。** 我不会为了把八格填满而猜 R3。

## 默认建议（写死；本轮按它开工）

**按 `docs/roadmap.cn.md` 的第一档顺序取下一件，并把取到的第一件**当作 R3 继续往下走**；
每做一件之前，先在这页追加一行「第 N 件 = 什么」，让这张表**从今天起存在**。**

**⇒ 若决策者手上有那张原表 ⇒ 直接给出五条，我把这页改成对照表并关掉它。**

## 同时的另一件（同一类问题）

决策者第五项说「收敛车道 N/O/P 至少常开一格：SSA 收尾、事实与算子各留一份、中心文件净减」——
**这一条自带了三个名字**（SSA 收尾 / 事实与算子各留一份 / 中心文件净减），**所以它可派**，
与 R3–R7 缺定义不同。**下一轮先派它，不等这页。**