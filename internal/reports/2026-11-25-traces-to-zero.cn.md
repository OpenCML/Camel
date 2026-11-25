# 位置标记：1557 → 732，六个族删除，判据一处没松

**状态：已落。** 七个提交在 `lane/traces-zero` 上，基点是 `1b7b986117`（= `origin/integration`）。
本页记三件事：删了什么（逐族前后与证明例）、留着什么（每类一句理由）、以及两次被拒的自动改写
为什么被拒 —— 因为最后这节比数字更能说明剩下的 732 行为什么还在。

## 一、读数

| 项 | 值 |
|---|---|
| 起点 | `node scripts/check-process-traces.mjs --root .` → **1557**（6336 文件，167408 注释行），exit 1 |
| 终点 | 同上 → **732**（6336 文件，167404 注释行），exit 1 |
| 少掉 | **825 行（53%）** |
| 判据 | `scripts/check-process-traces.mjs` 的判据一条未改；改动只有两行注释（一个工作项编号换成形状 `D-<year>-<n>`、一句历史改回 `a dated ruling`），见 `8163ef6d17` |
| 正对照 | `node --test scripts/check-process-traces.test.mjs` → **8/8**：判据仍然拒绝它原本拒绝的那些形状 |
| 其余闸 | gate 1/2/3 绿：1264 篇文档 0 断链（含本页）；1156 个符号引用全验；待审表 28 页 / 243 行 |

派工给的是 1547（1538 + 16），我在这棵树上量到 1557。差的 10 行没有追查，下面的每个数都是
**本树的前后差**，与这 10 行无关。

## 二、桶：起点与现在

一行注释可以同时命中多条判据（`the author's ruling of 2026-11-03` 同时是"谁决定的"和"日期"），
所以列和大于总数。

| 判据 | 起点 | 现在 | 归属 |
|---|---:|---:|---|
| `date`（日历日期） | 1117 | 638 | 剩 |
| `ruling-of-date` / `authors-ruling` / `the-ruling` | 271 / 191 / 148 | 0 / 0 / 0 | 删 |
| `backtick-sha` / `commit-sha` | 184 / 1 | 0 / 0 | 删 |
| `handover-path`（`internal/…` 页路径） | 127 | 127 | 剩 |
| `lane-code` / `this-lane` / `lane-branch` | 84 / 35 / 3 | 30 / 1 / 0 | 删 + 剩 |
| `work-item`（`D-nnnn-n`） | 39 | 0 | 删 |
| `stage-n` / `refactor-stage` | 17 / 2 | 17 / 2 | 剩 |
| `host-88zjwei` / `home-zjwei` / `home-camel-work` | 14 / 2 / 4 | 0 / 0 / 0 | 删 |
| `author-paren` / `role-cn` / `decision-maker` | 50 / 2 / 9 | 0 / 0 / 0 | 删 |
| `iron-rule` / `待审` | 8 / 1 | 8 / 1 | 剩 |

## 三、六个族：规则、例子、提交

### 族 1 · 提交哈希（`6d29f33ccb`，87 文件 +190 −191，1557 → 1419，−138）

规则：留着断言，删掉"是谁哪次提交让它成立"。

```
- // merged as `a1b2c3d4`, and the count is now one per file
+ // merged, and the count is now one per file
```

被拒的版本：先把"`sha`"整体换成"the change"，92 行里有 40 行丢了标识符（`\`x\` merged as
\`sha\``" 变成"x merged as the change"读不出是哪个 x）；把从句切开留下 506 行里 175 行红。
最后是四条窄规则（空括号、列表首末项、`merged as`、`on|at <sha>`、`pre-<sha>`）加一张手写表。

### 族 2 · 日期（`e3095acc11`，171 文件 +316 −316，1419 → 1146，−273）

规则：删掉"这个读数是什么时候量的"。

```
- // The slot `T[...]` is `Tangent<f>`, measured 2026-11-04.
+ // The slot `T[...]` is `Tangent<f>`.
```

被拒的版本：全局压空白把代码段 `` wrote ... re-hashed `` 压成 `wrote... re-hashed`（上一车道
记过同样的伤），表格对齐也被压掉；空括号规则再次吃掉 `update(x, () => x + 1)` 里的 `()`。
修法：只按行改、不压空白，22 处接缝逐条修，5 处双空格，1 处代码段复原。

### 族 3 · 谁决定的（`bd2d84365d`，203 文件 +316 −316，1146 → 861，−285）

规则："the author's ruling of <日>"→"the rule"；"the ruling"→"the rule"；"（作者，日期）"整个删。

```
- // The author's ruling of 2026-10-26 on `never-argument-scope`: a `with` argument…
+ // The rule on `never-argument-scope`: a `with` argument…
```

两遍，因为归属会跨行：短语在行尾、日期在下一行（`design principles 3.1, the author's ruling of ⏎
2026-11-17)`）。第二遍读命中行及其下一行、两行一起写回。**是不是从句主语**由它前面的文字判：
在括号里就删，自己当主语就换成"the rule"——否则 `one of the nodes the author's ruling of <日>
names` 会剩下没有主语的 `the nodes names`。

修的伤：清理规则把每个空括号都删了，连代码段 `update(x, () => x + 1)` 里的 `()` 一起；
`internal/tools/camel-oversight` 五处注释丢了主语（"What pushed that has landed"），改写成
"the other side"，也就是代码自己的叫法。

### 族 4 · 工作项编号（`6f49293df7`，25 文件 +39 −39，861 → 824，−37）

```
- // narrowing to "fast but leaky" (D-1123-84, prohibition 2).
+ // narrowing to "fast but leaky".
```

三行是**形状的例**而不是真实编号（检查器自己那句话、oversight 解析器讲子项与组怎么写），
留着形状、换掉实例：`D-1123-50④` → `D-<year>-<n>④`。

### 族 5 · 车道名（`c2235ac596`，51 文件 +91 −91，824 → 742，−82）

```
- // the debt this lane is paying off
+ // the debt this change is paying off
- // … was measured by `lane-rank0` on 2026-11-04: 8 of 8 runs died
+ // … was measured on 2026-11-04: 8 of 8 runs died
- // `lane-stale` (8d0de4e3e) cleaned six such sentences out of `test/plans/`
+ // A change cleaned six such sentences out of `test/plans/`
```

### 族 6 · 主机与家目录（`c21df6be3c`，15 文件 +18 −18，742 → 732，−10）

```
- // Measured 2026-11-04 on host 88zjwei (1 m load 20.8-21.0, a loaded host):
+ // Measured 2026-11-04 on a loaded host (1 m load 20.8-21.0):
```

命令行示例留参数、去取值（`-BashHost <host>`、`--camel-host <host>`），两条样例路径变成
`<home>/camel-work/` —— 那是工具定义的布局，不是哪台机器的家目录。

## 四、留着的 732 行，每类一句理由

| 类 | 行 | 为什么不删 |
|---|---:|---|
| `date` | 638 | 日期多半和"读数的现场"挤在同一个括号里（`(measured 2026-11-02, a shell without LD_LIBRARY_PATH)`、`(2026-11-04)`）。只抽走日期，剩下那截要么变成宾语（"measured a shell…"）、要么留下空的 `(measured :)`；要修好得逐条重写从句，不是删除。 |
| `handover-path` | 127 | 路径多在句中当主语或宾语（`(\`internal/questions/…\`) is narrower than …`）。把路径换成"the page"，试过 54 行，出现 `/// comment; the page)` 这种比原路径更难读的句子，所以只删了 18 行里干净的，剩下 127 行不动。 |
| `lane-code` | 30 | 是**含 `lane-` 的工具名**（`merge-lane-docs.ps1`、`merge-lane-baseline.test.ps1`、`feat/tools/selfchecks` 里点名它们的 `desc`），以及 `lane-local`（图的子集）、`lane domain`（GPU 调度域）、`lane-load`。没有一条是车道名。把这一类和真车道名分开需要一个"不是路径、不是形容词"的条件，而这个条件会被一个例子推翻：名叫 `lane-docs` 的车道和 `merge-lane.ps1` 得用同一个测试区分。 |
| `stage-n` | 17 | 半数是模型/流水线的"Stage 4"和文件名（`stage2_residual_ln.cml`），半数是重构阶段的真痕迹（`stage 0 of the refactor`）。判据只看 `stage N` 这个形状，而"重构的第 0 阶段"和"第 4 阶段模型"没有可判伪的条件分开 —— 与上一车道记下的三类同类。 |
| `iron-rule` | 8 | `iron rule 17` 是引用仓库的一条规则编号，不是"谁在何时决定"；但它和"铁律 17 是那次事故定的"共用一个形状。同样没有条件能只留下后者。 |
| `refactor-stage` | 2 | 同上，`refactor stage 2.3` 与"检查器文件头举的例子"共用形状（其中一行正是检查器自己在举例）。 |
| `待审` | 1 | 待审表表头自己的词：那里是列名，不是"谁审"。 |
| `this-lane` | 1 | 检查器文件头在引用它自己拒绝的那个字面量（`"this lane" / "待审"`）—— 改成"this change"会让文档说错它拒绝什么。 |

## 五、被拒的做法，与被修的伤

三次自动改写量过又退回：token 替换（丢标识符，92 行）、从句切分（506 行里 175 行红）、
全局压空白（压坏代码段与表格）。两次半途的族也退回了：把页路径换成"the page"（54 行里
多数读得更糟）、日期的"Measured <日>,"一刀切（把同位语当成了宾语、留下 `(measured :)`）。

这一车道自己造的伤，四处，都修了并进了提交：`update(x, () => x + 1)` 的 `()` 被清空规则吃掉；
oversight 五处注释丢主语；`///` 行首留下一个句号；检查器文件头里那句"当时的措辞"被改成了
现在的措辞（提交 `8163ef6d17` 改回 `a dated ruling`，因为一句关于过去的注释不能读成对现在的描述）。

## 六、如果要继续

下一个最大的桶是 638 行日期，形状是"日期 + 同位语"和"括号里只有日期"。可行的做法不是规则，
而是一条判据：**抽掉日期后，剩下的还是一个完整从句吗**——`Measured 2026-11-04: 48.0` 抽掉是
`Measured: 48.0`（可以），`measured 2026-11-02, a shell without LD_LIBRARY_PATH` 抽掉是
`measured a shell without …`（不行）。这个判断逐行做，一条规则写不出来；本车道的时间用在了
前面六个族上，没有做它。