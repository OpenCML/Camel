# 台账改名单：冲刺后清单

作者 2026-11-03 13:30 的裁定（`docs/handover/reviews/questions-2026-10-29.md`，
《裁定（2026-11-03 13:30）：两页账本——数次数的账本改成列名单，或者删》，`origin/fsh-dev`
`b6b15518f`）落在三个文件上。本页把它写成可以照着做的清单：每一项说**动哪个文件、哪一段、
做完之后那几条红变成什么**。

**这是冲刺后的活。** 本页不含任何代码改动，也不占剩下这两个多小时；照裁定，在那之前
`registry.signatures_match`、`registry.result_types_match`、`registry.parameter_types_match`
**照实红着，不通过改数字变绿**。

---

## 0. 判据（一条，可机械判断）

裁定原句：**"能脱离实物漂移的计数是台账，保护的不是行为"**。

由此得到一条可以逐项打勾的判据：

> 这个数字**能不能在行为完全不变的情况下**变动？
> 能 → 它是台账，不保护任何行为 → 改名单，或者删；
> 不能 → 它对应真实的所有权边界/真实的行为 → 保留，但改成**名单**（多一个少一个都点名）。

按这条判据过一遍现有的四项：

| 项目 | 数的是什么 | 行为不变时能不能动 | 处置 |
| --- | --- | --- | --- |
| `GraphDraft` 账本 `references` | 整个词 `GraphDraft` 在 `src/ include/ modules/` 出现了几次，**注释也算** | 能（改注释、改文档排版就动） | **删这一项** |
| `GraphDraft` 账本 `files` | 有几个文件提过这个词 | 能（一个新文件写一句注释就动） | 保留，改名单 |
| `GraphDraft` 账本 `constructions` | `make_unique<...GraphDraft>` 出现几次 —— 每处都是一次"新建可编辑图"的所有权动作 | 不能（多一处就是多一个入口） | 保留，改名单 |
| `GraphDraft` 账本 `decode` | `GraphDraft::decode` 出现几次 —— 每处是一次"读这个图"的边界 | 不能 | 保留，改名单 |
| `EXCEPTIONS`（`result` / `parameter` 两类计数） | 每类里有几个重载读不出类型 | 能（换个写法、改句话就动，行为不变） | 改名单，每条带理由 |
| `EXAMPLE_STOPS` | 每类里有几个例子不编译 | 能（例子改写、名字改了就动） | 改名单，每条带理由 |

---

## 1. 任务 A：`GraphDraft` 直连账本

账本本体：`docs/handover/reviews/graph-store-step2-b-ledger.cn.md`（第 0 节写定义，后面是
一行一次提交的历史表）。检查器：`test/cases/runtime/graph_draft_ledger/check.mjs`。
基准：`test/cases/runtime/graph_draft_ledger/baseline.json`。

### A1 删掉 `references` 这一项

| 位置 | 现在是什么 | 改成什么 |
| --- | --- | --- |
| `check.mjs` 的 `NUMBERS` 数组（约 :105-115） | 第二项 `{ key: 'references', label: 'references', flags: ['-o', '-n', '-w'], pattern: 'GraphDraft' }` | **整条删除**。删掉之后 `files` 是唯一的"整个词计数"，且它 `-l -w`（一个文件一行），正好是名单需要的粒度 |
| `check.mjs` 头部注释（约 :15-24、:39-51、:103-115） | 三处把 `files`/`references` 并排讲，并解释 `-l` 与 `-o` 的区别 | 删掉 `references` 那半句话；`-l` 与 `-o` 的说明**保留**，因为 `constructions`/`decode` 仍然用 `-o` 数出现次数（但门不再比数字，见 A3） |
| `baseline.json` 的 `numbers.references`（约 :5）与 `sites.references`（约 :47） | 两个字段 | 删除。用 `check.mjs --record` 重新生成一次基准即可，不要手改 |
| `check.mjs` 的 `measure()`（约 :233）、`rises()`（约 :288）、`numberRises()`（约 :329）、`printRises()`（约 :311）、`comparisons()`（约 :280）、`summary()`（约 :276） | 都按 `NUMBERS` 遍历 | **不需要改**：它们遍历 `NUMBERS`，删掉一项就少一项 |
| ledger 页第 0 节的定义与历史行 | `references` 有定义、有历次的行 | 历史行**保留**（那是事实记录）；定义处加一句"该项已删（日期、裁定出处）" |

删完之后，`references` 不再出现在任何一次输出里：`[LEDGER] files N <= M constructions .. decode ..`。

### A2 `files` 改成名单

- **粒度**：文件路径（`modules/graph/handle.cpp`），不是 `file#symbol`。
- **排序**：路径字典序（`path.posix` 比较，保证三个平台一致）。
- **存储**：`baseline.json` 里从 `"files": 42` 改成 `"files": ["modules/a.cpp", ...]`，
  每一项就是允许出现的文件；`sites.files` 与 `numbers.files` 合并成这一张名单。
- **报法**：
  - 出现名单外的文件 → `[LEDGER] RED files 名单外多了一个：<path>（<匹配行号与文本>）`；
  - 名单里的文件一个匹配都没有 → `[LEDGER] RED files 名单里的 <path> 已没有匹配，删掉这一条`；
  - 两者都点名，不再出现"rose from A to B"这种只有差值的说法。
- **顺带**：`ZERO_FILES`（`check.mjs` 约 :119-125，那五个必须零匹配的文件）保留原样——
  它本来就是名单（一组文件），只是语义是"必须为空"。

### A3 `constructions` / `decode` 改成名单

- **粒度**：`file#symbol`（`check.mjs` 已经算好了 `symbolAt()`，约 :192；`.json` 里的 `sites`
  用的就是这个键）。行号不进名单——行号随上面的编辑漂移，这正是账本要摆脱的东西。
- **排序**：先路径字典序，再符号字典序。
- **存储**：`"constructions": ["modules/tiles/src/region_extract.cpp#extractRegion", ...]`，
  同一 `file#symbol` 出现两次也只列一次（名单是"哪里还有直连"，不是"连了几次"）。
- **报法**：
  - 新条目 → `[LEDGER] RED constructions 名单外多了一个：<file#symbol>`，附它第一次出现的位置与那一行文本；
  - 消失 → `[LEDGER] RED constructions 名单里的 <file#symbol> 已经不在了，在同一笔里把它从名单删掉`。
- **门**：从 `current <= baseline`（数字只能降）变成**名单逐项对上**。裁定说"多一个少一个都点名"，
  所以两边都要红：新增要人看着，删除要人在同一笔里把名单一起改（那才是"删掉一处直连"的记录）。

### A4 任务 A 做完之后的读数

- `graph_draft_ledger` 这一组检查（`test/plans/feat/runtime` 下的那一项）**仍然绿**——
  它本来就不在红的五条里；改的是它**红了以后说什么**：从"數字涨了 1"变成"谁进了名单"。
- `baseline.json` 的 diff 从"两个数字各自一行"变成"名单里的一行增删"，一次提交看得懂。

---

## 2. 任务 B：`EXCEPTIONS` 与 `EXAMPLE_STOPS` 改成名单

文件只有一个：`scripts/stdlib-reference.mjs`。

### B1 `EXCEPTIONS`（约 :2010-2047）的结构

现在是：

```js
result: { 'rule states a tuple': 6, 'declaration states a generic application': 2, ... }
```

改成"每类一张成员表，每个成员带理由"：

```js
result: {
    'rule states a tuple': {
        'jvp (transforms:jvp)#result': '签名写的是散文 `(value, tangent)`，读不出类型',
        'transpose (transforms:transpose)#result': '`(cotangents..)` 是形状不是类型',
        ...
    },
    ...
}
```

- **成员键**：`<operator 名> (<uri>)#<result|parameter <i>>`。已有的 `one.name`、`one.uri`、
  `one.detail` 三个字段够拼出这个键（见 `resultStopClass()`，约 :2229，与 `tally()`，约 :2755）。
- **理由**：一句话，写"为什么这一条无法用一个类型读写"，不是"因为它一直在这里"。
- **排序**：字符串字典序，保证 diff 只有增删行。

### B2 检查怎么说出"谁进了、谁出了"（`exceptionProblems()`，约 :2193）

把 `recorded[label] !== byLabel[label]` 的比较换成集合比较：

- 成员在运行结果里、不在名单里 →
  `[REF] 进了：'rule states a tuple' 多了 jvp (transforms:jvp)#result —— 名单里没有它，补上它和它的理由`；
- 成员在名单里、运行结果里没有 →
  `[REF] 出了：'declaration states a generic application' 少了 jvp (transforms:jvp)#result —— 它不再属于这一类，删掉这一条并把理由写进提交说明`；
- 只在**成员集合**层面判断：一趟里进了谁、出了谁，各自点名，**不再出现任何 "records 6, found 7"**。

### B3 `EXAMPLE_STOPS`（约 :2061）同改

- 成员键：`<模块文件名>:<行号>:<名字>`（`coverage.stops` 里已带 `file`/`line`/`name`，
  见 `exampleProblems()`，约 :2136）。
- 理由：写这一条**为什么**停在那一类诊断，例如 `sum_to` 的
  `stdlib/tensor.cml:176: 'sum_to' has an @example that does not compile: ...  cannot be represented as type 'float[3]'`，
  理由就是"例子的字面量写成了它转换不了的形状"——一句话说清是例子的问题还是检查的问题。
- 报法同 B2：进/出各自点名。
- **边界**：`STALE_FRAGMENTS`（约 :2132，现在记 0）也是计数，但裁定的两句只点了 `EXCEPTIONS`
  与 `EXAMPLE_STOPS`。本清单一并**不动它**；它要改，等作者把这句扩到它上面。

### B4 任务 B 做完之后的读数

- 三条 `registry.*` 红（`signatures_match`、`result_types_match`、`parameter_types_match`）
  不再是"数字对不上"，而是"名单里多/少了谁"。以今天的两处漂移为例，名单制下会这样报：
  - `jvp`'s `bb6c9d9a5` 把签名从 `(float, float)` 改成散文 `(value, tangent)` →
    `出了：'declaration states a generic application'` + `进了：'rule states a tuple'`，两条都点名 `jvp`；
  - `sum_to` 的例子现在停在 `semantic` →
    `进了：'a semantic error' 多了 stdlib/tensor.cml:176:sum_to`，并附那句诊断。
- 在名单制下，**这两处都不需要二分、也不需要"补记账"**：检查自己说出谁动了。

---

## 3. 完成任务后的门读数（照实写）

| 检查 | 现在（`91c28dc8b`，2026-11-03 21:30） | A 做完 | B 做完 |
| --- | --- | --- | --- |
| `registry.signatures_match` | 红：`rule states a tuple` 6/7、`declaration states a generic application` 2/1 | 不变 | 变绿（改为名单比对） |
| `registry.result_types_match` | 同上两条 | 不变 | 变绿 |
| `registry.parameter_types_match` | 同上两条 | 不变 | 变绿 |
| `registry.docs_complete` | 红：`EXAMPLE_STOPS['a semantic error']` 24/25、`sum_to` 例子 | 不变 | 变绿（B3） |
| `registry.migrations_stay_migrated` | 红：同上 1 条（转发 `--verify-ops` 与 docs 的结论） | 不变 | 变绿 |
| `feat/modules` | **1929 = 1920 / 2 / 7** | 不变 | **1929 = 1922 / 0 / 7** |
| `runtime.graph_draft_ledger` | 绿 | 仍绿，输出换成名单 | 仍绿 |

（`feat/modules` 的"2"就是上表那两条；`rulesets` 67/67、`feat/compile` 2190 = 2185/2/3 与本清单一无关。）

---

## 4. 不做的事（写清楚，免得下一个 agent 顺手做掉）

1. **不改任何数字**去迎合检查——包括临时把 `EXCEPTIONS`/`EXAMPLE_STOPS` 改到今天的读数。
2. **不把红标成"已知失败"**、不加跳过、不加豁免开关。
3. **不在冲刺窗口内动** `scripts/stdlib-reference.mjs` 的实现、`check.mjs`、`baseline.json`。
4. **不碰** `stdlib/tensor.cml` 的声明（`triage` 刚动过）；`sum_to` 的例子怎么改是他们的账，
   本清单只在 B3 里说明名单制下它会怎么被点名。

---

## 5. 顺序与依赖

1. **A1（删 `references`）**先做：纯删除，最小，单独一笔，`--record` 重生成基准。
2. **A2、A3（`files` 与 `constructions`/`decode` 改名单）**同一笔：都改 `NUMBERS` 的存储形状与
   `rises()`/`printRises()` 的报法，分开做会出现"一半名单一半数字"的中间态。
3. **B1-B3（`EXCEPTIONS`、`EXAMPLE_STOPS` 改名单）**一批：`exceptionProblems()` 与
   `exampleProblems()` 是同一个形状比较，一起换才不会留下两套说法。
4. 三项都是**冲刺后**与每日审查 A10（计数台账）、A12（同一件事的另一个计数）同批做，
   顺序见那两条的清单；本页只保证"照做就能做对"，不排日期。