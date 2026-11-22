# `parked/` 指的是「暂停」还是「已删」？两种含义并存时，读者该按哪个行动？

**日期**：2026-11-20
**提出者**：标签审计车道（`lane-tag-audit`，基线 `origin/develop` = `a4fa54cff`）
**状态**：等作者裁定。**默认建议：把前缀拆成两个 —— `parked/` 只表示「活着但暂停」，`deleted/` 表示「分支已删、内容在别处」；并对现有 6 个逐条贴新前缀，判不出的一律留在 `parked/` 并标「未判」。**
**限制**：本页只有 git 读操作。没有删 tag、没有删分支、没有改 `agents/branching.md`；主机 `10.126.62.88:22` 不通，所以没有构建、没有跑计划，本页任何一格都不是跑出来的读数，而是 ref 与 blob 的读数。

## 1. 二义的形状

`parked/lane-front-ip-del` 这个标签**在删分支之前就存在**，而它指向一个当时**还活着**的分支。今天那条分支已经删了，标签还在，指向同一笔提交。所以同一个前缀底下同时住着两件事：

- **（甲）** 我们暂停在这条线上，**分支还在**；
- **（乙）** 这条分支**已删**，内容在别处。

⇒ **⇒ 而「一个有两种含义的记录，比没有记录更坏」**：读者会按其中一种理解行动，而那可能不是另一种。今天这 6 个里，有 1 个是甲、3 个是乙，而**没有任何一格在标签上写清了它属于哪一种**——这才是问题，不是那 6 笔提交本身。

## 2. 6 个 `parked/*` 的实测（`origin/develop` = `a4fa54cff`）

**先说一件影响整张表的读数**：6 个里有 **5 个是轻量 tag**（`for-each-ref` 的 `%(objecttype)` = `commit`），**它们没有 tag message**。`git tag -l -n99` 对这 5 个打印出来的是**那笔提交的信息**，不是标签自己的说明。只有 `parked/lane-vjp-forward-backup` 是附注 tag（`objecttype` = `tag`），它有一句真正的 tag message（tagger 时间 `2026-11-20 09:54:59 +0800`，与 `archive/lane-vjp-forward-backup` 同一秒）。⇒ **⇒ 所以「标签的 message 说它是哪一种含义」这个问题，对其中 5 个的前提就不成立：它们没有自己的话可说。**

| 标签 | ① 指向的对象类型 | ② 那笔提交现在在哪 | ③ message 逐字 | ④ 判甲或乙 |
| --- | --- | --- | --- | --- |
| `parked/lane-K-rule-access` | `commit` `17b6698c8`（轻量，无 tag message） | 本地与远端**都没有分支含它**；非 develop 祖先（`--is-ancestor` exit 1）；其页面 blob `cd2fff213` 在 develop 上**0 命中** | `docs: which of the check's two paths a case walks, and which one is only read`（+ 5 行正文） | **未判**：分支已删，但内容**只在这枚 tag 上**——甲的「分支还在」不成立，乙的「内容在别处」也不成立 |
| `parked/lane-eval` | `commit` `c9e28898d`（轻量，无 tag message） | **是 `origin/lane-eval` 的 tip**（活分支，`ahead=9` / `behind=217`）；非 develop 祖先（exit 1）；`eval-round10` 的 9 笔里 8 笔的内容已在 develop（同一 blob），只有 `item5-tie-width-repro.md` 与 `test/cases/trans/tie_width_while.cml` 不在 | `eval-round10: what the four Camel passes do to the mlp's canonical document`（+ 5 行正文） | **（甲）**：分支还在。但它的 message 没有一个字说「仍在用」或「待删」 |
| `parked/lane-front-ip-del` | `commit` `cfc5f74ea`（轻量，无 tag message） | 无分支含它（**分支已删**）；非 develop 祖先（exit 1）；但页面 blob `ec7ed1357` 在 develop 上**字节相同**（`848f151a7`，同一提交主题的另一笔） | `docs(question): the in-place page names what is true, and what it costs`（+ 正文） | **（乙）**：内容已在树里，分支已删。今天 `archive/lane-front-ip-del` 的 tag message 自己写「Deleted 2026-11-20: content already in the tree」 |
| `parked/lane-gate-reds-a-parked` | `commit` `b604554d2`（轻量，无 tag message；**合并提交**） | 无分支含它；非 develop 祖先（exit 1）；合并点下唯一非合并提交 `81f1c369e` 的特征串（`arm_write.cml`、`not the returned name`）在 develop 的 `src/passes/trans/cml/cml.cpp` 里**都有** | `Merge remote-tracking branch 'origin/develop' into lane-gate-reds-a`（无正文） | **（乙）**：分支已删，那笔内容已在树里 |
| `parked/lane-ssa` | `commit` `4d308fc72`（轻量，无 tag message；**合并提交**） | 无分支含它；非 develop 祖先（exit 1）；`ahead=26`，其打包实现（`variableParameterTypesOf` / `handBackVariableParameters`）在 develop 上**只有文档提到、代码 0 命中** | `merge origin/develop: the undo journal, the original types, and the forward ruleset closure arrive beside this lane's exit-type derivation`（+ 5 行正文） | **未判**：分支已删，内容**不在树里**、也不在别处分支——develop 自己的页 `2026-11-19-the-three-steps-target-code-exists-only-on-the-parked-branch.cn.md` 说这段代码只在 `lane-ssa` 上 |
| `parked/lane-vjp-forward-backup` | **`tag`** `186b1475b` → `commit` `3f752a95c`（**附注 tag**） | 无分支含它；非 develop 祖先（exit 1）；它自己的 message 说内容已捞进的问题页在 develop 上**存在**；本页 blob `450d7f947` 不在 develop（被丢弃的 golden 重录） | `Consumed by internal/questions/2026-11-20-a-law-that-states-its-angle-answers-only-that-angle.cn.md, and deleted when that page lands. Two readings unverified, lifted into that page; the golden re-record was stale and discarded.` | **（乙）**：message 逐字点名消费者（那页）与死期（那页落地时） |

**表外一处必须记下的事实**：`archive/lane-front-ip-del` 与 `parked/lane-front-ip-del` 指向**同一笔** `cfc5f74ea`；`archive/lane-vjp-forward-backup` 与 `parked/lane-vjp-forward-backup` 也指向**同一笔** `3f752a95c`。⇒ **⇒ 「已删」这件事已经被 `archive/` 记过了，而 `parked/` 又记了一遍、且措辞相反。** 同一个对象上两枚标签说两件事，这正是二义的物理形态。

**再看一个数**：这 6 个里只有 3 个推到了 `origin`（`parked/lane-front-ip-del`、`parked/lane-gate-reds-a-parked`、`parked/lane-ssa`），其余 3 个只在本地。⇒ **⇒ 所以这个二义在远端只露出一半，另一半在下一个人本地的 `tag -l` 里。**

## 3. 判据（本页写死，供后来清理时逐条比对）

> **若一个 `parked/` 标签存在，而它指向的分支仍然存在，那它的 message 必须说「仍在用」或「待删」；不说，就与已删的混在一起。**

按这条判据，今天 6 个的合格数是 **0**：唯一的甲（`lane-eval`）没说「仍在用」，3 个乙没说「已删」，2 个未判什么都没说。

## 4. 默认建议（本页写死，作者可推翻）

**把前缀拆成两个，语义各占一边**：

- `parked/` ＝ **活着但暂停**（它指向的分支必须仍存在，且 message 或同名注释必须说「仍在用」或「待删」）；
- `deleted/` ＝ **已删，内容在别处**（message 必须点名消费者：哪条提交、哪一页、哪条分支）。

**对现有 6 个逐条贴新前缀**（不动被指向的提交，只加标签或改标签名）：

| 现名 | 新名 | 理由 |
| --- | --- | --- |
| `parked/lane-eval` | `parked/lane-eval`（保留，**补一句「仍在用：`origin/lane-eval` 仍存在」**） | 唯一的甲；分支 `ahead=9` 还在 |
| `parked/lane-front-ip-del` | `deleted/lane-front-ip-del` | 内容在树里（`848f151a7`），分支已删 |
| `parked/lane-gate-reds-a-parked` | `deleted/lane-gate-reds-a-parked` | 内容在树里（`cml.cpp` 四处特征串），分支已删 |
| `parked/lane-vjp-forward-backup` | `deleted/lane-vjp-forward-backup` | 它的 message 已经写了消费者与死期 |
| `parked/lane-K-rule-access` | `parked/lane-K-rule-access`（**保留，标「未判：分支已删、内容只在此 tag」**） | 判不出，不填一个看起来对的 |
| `parked/lane-ssa` | `parked/lane-ssa`（**保留，标「未判：分支已删、内容只在 `lane-ssa`，`develop` 无代码命中」**） | 同上；它是一条载体分支，不是合并候选 |

**保留 `parked/` 的那三个必须带「未判」二字**，理由是本页的判据：一个不说明自己属于哪种的 `parked/`，其读者无法据它行动。**「未判」是一个诚实的答案；把判不出的填成乙，会让下一个人去树里找一段并不存在的内容。**

**同时建议（不由本车道落）**：`agents/branching.md` 的「Closing and preserving branches」一节目前只说「First create `archive/<name>` pointing at the old tip」，没有说 `parked/` 是什么。建议在那里加一句「`parked/` 只用于仍存在的分支，且 message 必须写「仍在用」或「待删」；已删的分支用 `archive/`（或按本页拆出的 `deleted/`），并点名消费者」。**这一句要改由作者或该文件的作者落，本页只给建议。**

## 5. 要作者回答的一句

`parked/` 要不要拆成 `parked/`（活着但暂停）与 `deleted/`（已删、内容在别处）两个前缀，并把现有 6 个按第 4 节那条表逐条改贴、判不出的留 `parked/` 标「未判」？

## 附：`git cherry` 说「已在」与 grep 说「不在树里」可以同时为真

**`git cherry` 与 grep 可以同时为真：`782007912` 把打包从 `develop` 撤掉，于是那两笔「进过 `develop`」（cherry 说 `-`）而「内容现在不在树里」（grep 0 命中）。删分支看现状，不看历史。**

复现（`origin/develop` = `a4fa54cff`，`land/packing-and-rule` = `1cbb4c07b`）：

- `git cherry origin/develop origin/land/packing-and-rule` → `- 6790ffe6f`、`- 8f7b9687f`、`+ 1cbb4c07b`：三笔里两笔被报「已在 `develop`」；
- 同一棵树的 `git grep` 在 `develop` 上对 `variableParameterTypesOf`、`handBackVariableParameters`、`packsVariableParameters`、`packsVarParameters` **全部 0 命中**；
- 两件事都对：那笔打包确实进过 `develop`（`9997910f4`，与 `6790ffe6f` 同一 patch-id），随后 `782007912` 把它从 `develop` 撤了，只留在 `land/packing-and-rule`。

⇒ **⇒ 所以 `--is-ancestor` exit 1、cherry 报 `-`、grep 0 命中，是三个不同的问句**：祖先问「这笔提交在不在这个 base 的历史里」，cherry 问「有没有一笔等价的改动进过这个 base」，grep 问「这段代码现在在不在树里」。**删分支与判「内容已在树里」看的是第三个，而能回答它的只有第三个。** 本页第 2 节那 6 个标签的判据也因此不能只靠 `--is-ancestor` 或 cherry 之一。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

不新增 `deleted/` 前缀。已删的分支一律用 `agents/branching.md` 已有的 `archive/<名>`，附注写明消费者；`parked/` 只给仍存在的分支，附注写"仍在用"或"待删"。现有 6 个这样处理：`lane-front-ip-del`、`lane-gate-reds-a-parked`、`lane-vjp-forward-backup` 转成 `archive/`（前两个已有同名 `archive/`，删掉 `parked/` 即可）；`lane-ssa` 改成 `archive/`，附注"被核心抽象第 8 节取代，用例已收进前端阶段计划"；`lane-K-rule-access` 判断它的页面内容是否还要，不要就归档；`lane-eval` 补附注，或按落后分支的四问处理。
