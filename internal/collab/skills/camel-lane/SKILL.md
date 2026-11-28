---
name: camel-lane
description: Camel 项目里一条实现车道（子代理）要遵守的一切基本要求：不许做的事、改语义与断言的分寸、架构四条唯一、注释与提交的写法、在哪构建、报告格式、判据分类、两种绿的口径。派工只写这件活特有的（目标、工作树 cwd、判据、交回什么）并点名本 skill；两者不一致以本 skill 为准。
---

# 一条车道的全部基本要求

**这一页是唯一一份。**派工里出现与本页相同的交代，说明有人又手写了一遍 —— 把它删掉，只留「先读 `camel-lane` skill」。

## 1. 不许做（每一条都付过代价）

```
· **不许在本机构建、不许在本机跑计划** —— 工作站是负责人自己的机器。
  构建与测试一律走 88 主机（§6）。
  **唯一例外：`lane/backend-speed` 的性能工作树**（`D:\Projects\Camel-wt\perf`）。
  负责人 2026-11-25 定：后端提速的迭代计时放在工作站做。例外只给这一条车道、只有这一棵工作树；
  计时前先看工作站负载，**负责人在用就等**，不抢；一轮计时控制在几分钟内。
  它的读数必须写明「工作站的读数」，**不与 88 主机的读数相比**（§6 最后一条对它是加倍成立的）。
· 不许 push、不许 merge、不许碰 `develop` 与 `main`；你只在自己工作树里改文件。
· 不许 `-Tier all` / `-Plan all` / 整棵树；一次一个计划目录。
· 不许打 `known_fail`、不许改 `baseline.json`。
  一个红要么修掉，要么报出来；**标成已知失败等于删掉判据**。
· 不许杀别人的进程、不许杀别人的 git。**只结束你自己记录过 PID 的进程**（不许按名字、`/IM`、通配去结束）；
  结束闸门或子代理时**整棵进程树一起**（`taskkill /T`）—— 只杀父进程会留下孤儿占着构建槽。
  自己启动的辅助进程可以结束，但必须在交回里报出来（结束了什么 PID、为什么）。
· **不许删 C++ 对照实现**：求导引擎（`src/stdlib/autodiff/`）、规则表（`rule_templates.cpp`）、
  C++ 版优化遍都**冻结保留**，它们是库表达的对照与性能基线；它们的完成靠库覆盖到位，不靠删掉对手。
· **不许用 `git stash`**（整仓上百棵工作树共用，`pop` 会取到别人的条目，已被跨树抢包证实多次）。
· **不许对 `test/plans/probe/`、`test/cases/probe/` 做目录级删除**（那里有别的车道 tracked 的探针）。
· **不许一步删分支**：清理脚本必须先打印「将要删什么、为什么」整份清单并停下；
  保留名单按主干类枚举（`main`/`develop`/`integration`/`docs/*`/`fsh-dev`/`record/*`/`seat/*`），
  **不许用「已合进就删」这一条判断** —— 它删掉过 `main` 与 `integration`。
· **不许为让红变绿改断言。** 改期望是允许的，但必须在提交信息里写明依据：**哪一条裁定、哪一条设计原则、
  或哪一次语义迁移**（例如遍改名、`range` 上的 `map` 语义）。没有依据的改期望，就是改判据。
· 不许为了「让它绿」换题目、换写法、换断言的粒度 —— 那是改判据，不是修缺陷。
· **撞墙就停手报**，尤其当你要修的东西不在授权文件里时：那是结论，不是失败。
· **派工里的前提若查不到，先核再开工**：一条活的名字、路径、数字、标识符，先各查一次
  （`git branch`/`git worktree`/`git grep`/`git log -S`/backlog）；查不到就停手报，
  **不要为了让命令跑起来自己编一个靶子**。
```

## 1½. 接到卡就做，不以上下文余量为由停手

```
· **会话的上下文到边界会自己压缩，然后接着做**。"余量只剩百分之几、接不住"不是交回或拒卡的理由；
  接到卡就从第一步落笔，不先估余量、不先报余量。
· **防压缩丢进度只靠两件事**：每做到一个可验的小步就提交一次；在分支里留一行「做到哪、下一步」
  （提交说明末行或卡上的状态行），压缩后照它接着做。卡本来就写成自成一体，不靠你之前的上下文。
· 真做不下去，理由只能是读数（缺前置、判据冲突、工具红），写清是哪一条，照常交回。
· **写文件只用绝对路径**：进程工作目录**不等于** shell 的当前目录（`.NET` 的 `File`/`ReadAllText` 之类
  按**进程 cwd** 解析相对路径，`cd` 在上一句不代表这一句的 cwd）。相对路径写文件已经三次写进负责人的
  主检出 `D:\Projects\Camel` —— 那不是任何车道的树。批处理替换、脚本、`git checkout --` 一律先
  `Resolve-Path` 出绝对路径，`git` 用 `git -C <绝对树路径>`。
· **机械动作（不做就是会犯）**：进自己的工作树之后第一件事、以及**每次 `cd` 之后**，执行一次
  `[Environment]::CurrentDirectory = (Get-Location).Path`。这样即便某处漏了绝对路径，相对路径也落在**自己的树**里，
  而不是车道 shell 的起点（`D:\Projects\Camel`，负责人的主检出）。写完立刻在本树核 `git -C <树> diff --numstat`：
  **"git 说 clean" 可能是"你写到了别的树"**，diff 为空而 `Get-Content` 看得到内容时，先怀疑写错了树。
· **泄漏检测（一条命令，每写完一批就跑）**：`git -C D:\Projects\Camel status --porcelain` 必须是**空**。
  它一旦非空，就是有车道的写落进了负责人的主检出（2026-11-28 已发生两次，都是相对路径按**进程 cwd** 解析到主检出）。
  看到非空：先与**本树** `git -C <树> status --porcelain` 对照，内容对得上就
  `git -C D:\Projects\Camel checkout -- <绝对路径>` 复原，对不上**不动**、报协调者。
· **批处理/替换脚本**：脚本里 `Set-Location`、`ProcessStartInfo.WorkingDirectory`、`cd()` 也要显式给出**绝对树路径**；
  只改外层 shell 的 cwd **不会**改子进程的 cwd —— 2026-11-28 的第二次泄漏就是这条。
```

## 2. 改语义之前

```
· **先读 `docs/design-principles.cn.md`**（中文为准）。它是规范：与代码、文档、测试冲突时以它为准，
  冲突**记为缺陷**，不改代码去凑测试。
· 它没写、或列为未定的，**交语言作者，不自己定**：把读数与两条路写成一条话报上去，按默认继续别的活。
· 你可以在实现里选「怎么做到」，不可以在实现里选「要求什么」。
```

## 2½. 报「视图/接口缺 X」之前的搜法（付过三次代价）

「`GraphView` 没有这个读法」这类结论，已经三次是**按旧名字搜、没按语义搜**得出的假结论（`nodes()`、`inputs(ref, PortGroup::Context)`、`constantOf` 都是）。报之前必须走完四步：

```
① 按**语义**搜，不止按旧名：`git grep -n -E 'constant|const|value|static|literal|slot|data|origin|region' -- src/kernel/graph/graph_view.h`
② 读旧机制的**替代注释**：老函数被替代处常写着 `Replaces staticArea()` / `Replaces exitNodeRef` / `Replaces … loopBodyOf`，
   那些就是新名字的指路牌 —— 用被替代的旧名去搜注释，比搜标识符有效。
③ 读那张头的**公开面**本身（`class GraphView {` 之后到文件尾），而不是只 grep 关键词。
④ 仍找不到才报，并**附上你搜过的词、每条 grep 的原文输出**。没有这份输出，报告按「没查」退回。
```

推论：接口请求（给 `GraphView` 加口）是**最后手段**。它是所有适配器共用的一张脸，动它要单独一笔、由决策者当轮审；一个 kind 的迁移不值得。

## 2¾. 写类型名先抄现成用例的名字，别按读者以为的名字写（付过两次代价）

Camel 的两个实数类型**按编译器解析的名字写，不按读者以为的名字写**：`float` 是 **64 位**那个（**没有 `f64`**），单精度叫 `f32`；实数字面量**没有 `f` 后缀**（`1.0f / 3.0f` 报的是 `UnresolvedReference: 'f'`，不是"不支持后缀"）。

```
⇒ 2026-11-27 两条车道各撞一次（一次写 `let a: f64`、一次写 `1.0f/3.0f`），都白跑一轮 88 构建
判据只有一条：写探针之前先 `git grep -n 'let .*: f32\|let .*: float' -- test/cases | head`，**照现成用例里的类型名抄**，
  不要凭"我记得 C++ 里怎么叫"来写。名字错时编译器说的是"未解析的名字"，不是"这个类型不存在" —— 别被那句话带偏。
```

**调一个成员之前先看它是字段还是方法**：`DraftNodeHeader::dataType` 是**字段**（写 `->dataType`），写成 `->dataType()` 编译器报的是 `called object type 'Type *' is not a function` —— 这句话看着像类型不匹配，实际是"你把字段当函数调了"。同理 `camel::core::type::tree::arity` 要 `#include "camel/core/type/tree.h"`，且会被同作用域同名局部 `arity` 遮蔽。

```
⇒ 2026-11-28 jvp 值投影为此白跑 4 轮构建（每轮 Release 增量 ~2 分钟）
 判据：写 `.h` 里出现过的成员之前，先 `git grep -n 'dataType\|arity' -- <该头文件>` 看那一行的形状是 `Type *dataType;` 还是 `Type *dataType() const;`。
```

## 2¾½. 库里的 `@example` 是**一行一条**、各自独立（付过三次代价）

`src/stdlib/**/*.cml` 的 `/// @example` 行由 `scripts/check-doc-snippets.mjs --source-examples` 编译。两条实测规则：

```
① **连续多行 `/// @example` 不是同一片段的续行**，每条各自编译、各自计数（`734` 条示例加两行 `@example` 就变 `736`）。
   ⇒ 想写多行示例，得把整个程序写在**同一条 `@example`** 的那一行里；另起一行 `@example` 等于另起一个示例。
② 示例按"**写在该模块里的代码**"读：它看得见所在文件的导出名，也应当看得见**该文件自己的 `import`**；
   要用的跨模块名字没解析，先补**模块自身的 import**（真代码的 import），不是给示例加 import 行
   （示例里单写一行 `import { x } from y` 会被当独立片段编译，报语法错）。
```

⇒ 2026-11-27 三条车道先后在示例里加 import 行都白跑（`is_same`、`to_bytes` 那批）。判据：`@example` 只在**一行**里写完整程序；跨模块名字靠**模块 import** 带进来。

## 3. 代码形状：架构的四个唯一

`docs/design/architecture/README.cn.md` 定的四条，每日审查反复抓到的就是这四类：

```
① **不造第二份真相**：一件事只有一处记录。派生可用，但必须由那一处生成，且生成物标明它是生成物。
② **不按名字或 URI 字符串开特例**：`if (name == "std::xxx")` 这类分支是缺陷，不是捷径；
   该判断的是那条记录 / 那个形状本身。
③ **后端不自己推语义**：执行期只做被写下来的事，不在运行期补一个「大概应该这样」。
④ **内核不为一个库加字段**：内核的类型与节点不为某个库的需要长出新字段；库的需要用库自己的办法表达。
```

## 4. 注释、测试描述、提交信息：不留过程痕迹

仓库要公开，**过程痕迹不进代码**：

```
· 注释只写**做什么**与**技术理由**。不写：派工编号、裁定日期、车道名、角色（负责人 / 决策者 / 协调者）、
  提交哈希、对话里的简称。
· 测试描述与用例名同理：写被钉住的行为，不写「某次评审要求的」。
· **提交信息也不写编号与车道名**。写清：这件事是什么、为什么这样改、被实测否掉的另一种改法。
· 判据可以写进提交（§8），但用技术语言写：写「为什么这个检查能分辨这两件事」，不写「谁要求加的」。
· **`pre-push` 真的会拦，而且拦得比你想的宽**（2026-11-27 连拒两次，同一个文件第 8 行）：
  第 1 次拒「`# internal/reports/2026-11-27-resnet-block-space.cn.md, section 10)`」= **日期 + 内部页路径**；
  第 2 次拒「`# report this lane wrote, under the internal reports ...`」= **`lane` 这个词本身**也算过程痕迹。
  ⇒ 一条注释里想说“读数记在哪”时，写成**不含日期、不含 `internal/` 路径、不含 `lane`/角色词**的说法，例如
  「the report the repository keeps under its internal reports」；**不要**为了过钩子把注释删空 —— 钩子拦的是措辞，不是内容。
· **PowerShell 会把提交信息吃掉，两次实测**：① `git commit -m "…"` 里出现 `` ` `` + 字母（如 `` `vjp ``）⇒ 反引号被当转义、
  串被截断成半个词；② `git commit … | Select-Object -First 2` 提前关管道 ⇒ 报假「No staged files」。
  ⇒ 提交信息一律走**单引号 here-string + `-F <文件>`**（`@'…'@` 不做任何替换），提交后**必看** `git log -1 --format='%h %s'`；
  subject 与预期不一致就 `--amend -F` 重来，**报给协调者的永远是 amend 之后的 sha**，且报之前先自查 subject。
```

## 5. 不计兼容

```
· 旧写法**删干净**：不留别名、不留「旧名 → 新名」的诊断、不留只为一处调用存在的兼容分支。
· 改名就全改：调用点、文档、生成的参考表、用例文本一起改；留一个别名等于把旧名字放回来。
· 一个不可写的名字，不该有一条能解析的别名。
```

## 6. 在哪构建、在哪跑（只这一种写法）

```powershell
pwsh -NoProfile -File <你的工作树>\internal\tools\camel-remote.ps1 `
  -Path <你的工作树> -Plan <计划名或路径> -Jobs 8
```
```
· **必须用你自己工作树里的那份脚本**，不要用主检出的：主检出那份可能落后，
  `-Check` 会「只构建、不跑检查、退出 0」—— 一个照它跑的车道会把「什么都没跑」读成绿。
  判据：输出里有没有 `CHECKS_START` 与 checks 阶段的读数。
· `-Plan` 用**正斜杠**；`-Jobs 8` 是远端并行度。
· **几份小计划合成一条命令**：`-Plan 'a.plan.toml,b.plan.toml'` 用逗号连起来，一次排队量完。
  宿主拥挤时这条能省掉几次 1800 s 的排队（2026-11-27 一条车道要量的两格计划合成一条命令，省了一次排队）。
  `camel-build.ps1` 的参数是 `-Parallel`，不是 `-Jobs` —— 混用会被当成没写。
· 免 GPU 观测、只要计划自己的退出码：加 `-NoGpuWatch`。
· 退出码：0 绿／1 红／**4 没跑起来**／**7 读数齐了但没拿到退出码**。4 与 7 都不是绿。
· **只跑一两份小计划的车道，传 `-SlotWaitSeconds 300`**（默认 1800）。宿主忙碌、令牌池被占满时构造桶会一直
  排队等令牌，等到上限才 `no token`（登记里仍是 `state=noslot`）：2026-11-26 一条车道两次各等 **1804 s**、换到 **0 个读数**。
  ⇒ 等不到就按 `state=noslot` 交回，写上「未取到 + 当时 `[load]`」，**不要重试第三次**。
  ⇒ 要长跑的全层计划（`-Tier fast`/`slow` 那种）才用它默认的 1800。
· **`-SlotWaitSeconds` 管不到普通槽位**（2026-11-27 核的工具事实）：它只用在**时间敏感**计划那条分支
  （`camel-remote.ps1:2892`）；普通槽位的等待是**硬编码** `flock -w 1800`（`:2578`），
  所以你看到的那句 `no slot on this checkout within 1800 s` 和这个参数**无关** —— 传 300 还是 5400 都一样等 1800 秒再 `exit 4`。
  ⇒ **不要靠“把等待放长”来抢槽**；排一次、等它出结果或 `exit 4`，别用短等待反复重试。
  ⇒ **上面这两句会随 `task-333` 过时（它已排队，未合）**：那次改动把普通槽位从"硬编码等 1800 s"改成**默认立即 `exit 4` 并打印持有者四元组**（`pid`/`since`/`run`/`cmd`），要排队必须显式加 `-QueueOnTreeLock`（之后每 60 s 打一行持有者）。合进 integration 之后按新行为读本节。
· **一条车道同一时刻只许一条 run，且不许“排不上就后台自动重试”**（2026-11-27 一条车道排不上后，自动重试那条
  也进了后台、只传了 2 KB 就停，等于替全队多占一把锁；当时十棵树同时在跑、`[load]` 68–88、一次 transfer
  花掉 **1802.8 s**）。⇒ 排不上就回来写「未取到（排队）+ 当时的 `[load]`」，把命令与日志路径备好，等下一轮再排。
  ⇒ 要把这个等待也变成可配的，属元层（`internal/tools`），由决策者那边改，我们不碰。
· **在一台构建机上新建一棵检出，必须先把 `internal/tools/camel-gate.local.json` 从一棵已有检出复制过去**
  （它被 git 忽略、约 455 B，`promote` 那棵有一份；2026-11-28 `daily-all` 首次建成时缺它，第一次启动直接死在
  `the local settings file has no 'sshHost': …\daily-all\internal\tools\camel-gate.local.json`，补上后一次就成）。
  另：`camel-remote` 的 `-Path` **只认 Windows 路径**，宿主上的树靠**同名叶名**对应
  （`D:\…\daily-all` ↔ `/home/zjwei/camel-work/daily-all`）；两边叶名不一致时会默默跑到别处去。
· 读数要带 provenance（`tip`、binary 的构建时刻、`tree`、`tier`、`jobs`）与 `[TREE] clean`。
  没有 `[load]` 行的读数（本机跑的）**只能当名字集用**，不能当时间基线。
· **把一个用例移出快层（`tier = "slow"`）时，提交说明必须点名它带走的红**
  （`moves X to slow; X is red: <判词原文首段>`，样例在 `c3f87277a7d` 与 `a5cd47367c9` 的正文）。理由：
  快层红表是唯一逐名台账，"移层"本身合理（成本型用例不该占快层预算），
  但**红不会跟着弃权** —— 不点名，下一次读快层的人会看到一条红"消失"，
  而真正看 slow 层的是每日全量闸门（`-Tier all`），它的红表才是那一层的账。
  ⇒ 移层**不禁止**，只是**不许静默**；判断"这条红是不是新出现的"，永远按**同一层的前一天**逐名比。
· **比红之前先确认两边选法相同**：同层同目标对晋级表，点名计划文件对 `daily-all` 表或同树基线。
  一张红表只是**同一选法**的基线：晋级表是"全快层"那个选法（含计划自己声明的风险抽样），
  点名计划文件会把那份计划**整份跑**，选法不同就不能拿晋级表比（`D-1128-132`）。
· **不要用 `Select-String` / `Where-Object` 把判词行滤掉。** 判词行
  （`[camel-remote] <plan> exit N (transfer …, wait …, build …, plan …, load …; total …)`，wait 是在宿主上等这棵检出的锁的时间，不算传输）
  是相位读数的唯一来源：24 小时 1679 次调用里只有 82 次保留到它，**其中 911 次是调用方自己滤掉的**。
  要少噪音就 `Tee-Object` 存一份再看，**不许让管道决定这条读数存在与否**（2026-11-26 决策者定）。
· **做“修前/修后”对比时，`Copy-Item` 会保留目标文件的 `LastWriteTime`**（2026-11-27 一条车道实测）：
  「备份修复 → `git checkout` 回退 → `Copy-Item` 恢复」之后源文件仍比 `.o`/`.cmo` 旧，ninja 判 up-to-date **直接跳过编译**，
  于是一整轮“修后”测的其实是旧二进制（它第一轮拿到 8/20、13/20 全是假的）。
  ⇒ 判据：核 `build\src\...\Release\*.cmo` 的 **mtime 与大小**（修前 772096 / 修后 773120），换了才算真的重编。
· **构建机只管对错**：不跑性能测试、不判耗时预算；墙钟数字只用来判断「有没有卡死」。
  超时先**单独重跑一次**（同一计划、同样的 `-Jobs`）再判红 —— 超时是读数缺失的一种，不是红。
· **它只量提交**：工作树里有未提交改动时也打 `0 changed file(s)` / `measuring: the commit alone`，量的是那棵树上的提交。
  ⇒ **先 commit，再量**；拿工作树里的改动去量，量到的是上一笔。
· **每条“取计划读数”的 run 都会自动追加 `check-doc-links`**（不需要你传 `-Check`，`camel-remote.ps1:856-859` 自己会加）。
  **这条检查曾经挂 40 分钟并持着 tree lock**（`no slot on this checkout within 1800 s` 的真凶之一，2026-11-27 一次车道因此连续四次 `exit 4`）；
  根因是 `camel-remote` 里 `[string[]]$Entry` 与局部变量 `$entry` 同名（PowerShell 不区分大小写）⇒ 检查器被拼成裸 `node` 等 stdin。
  **已在 integration 修好**（正确形状：`--- check check-doc-links: node scripts/check-doc-links.mjs --citations ---` → `--- check-doc-links: exit 0 ---`，约 3 秒）。
  ⇒ 你那条 run 若**仍然**挂在这行，先看是不是你的树还没换底到带修复的 integration，别再去查宿主、也别为绕锁去抢别人的树。
  ⇒ 例外照旧：读到 `[SUMMARY]` / `RUN_EXIT` / `[TREE]` 就算读数到手，挂住超过 3 分钟就按你自己记录的 PID 整棵收掉。
  ⇒ **`exit` 非 0 时先看 `RUN_EXIT` 与 `[camel-remote] checks:` 两行，再开口说红**（2026-11-27 一条车道实测）：`RUN_EXIT=0` + `checks: 1 run, 1 failed (check-doc-links exit 1)` = **计划全绿、收尾文档检查红**（那次是别人探针文件里一条失效引用 `test/cases/probe/probe_pools_reverse.cml:1` 把行号当引用）。这类“计划绿但命令红”最常见的误读是把红记到自己的改动上 —— 先分清是 `[SUMMARY]` 里的 fail 还是 `checks:` 里的 failed。
· **要换一把宿主 tree lock，就换 checkout 路径**：锁是按 checkout 路径的 cksum 建的。合法的换法只有“另开一棵树”（`-Detach <sha>` 造测量树，或 `git worktree add --detach`）并跑完删掉；**不要动别人的树，也不要去动别人的锁**。
· **新开的树没有钩子**：`git worktree add` 不会装 `.husky/_`，而 `npx husky` 需要那棵树里有 `node_modules`。缺钩子时 git **不报错**（`core.hooksPath` 指向不存在的目录 ⇒ 每一次 push 的所有钩子静默跳过）。要么用 `new-worktree.ps1` 建（它把这件事做成一步），要么从主检出复制 **husky 自己的 `_` 目录**（不是手写），再用 `-VerifyOnly` 读到检查器自己的输出才算有守卫。
· **新开的树还缺 `internal/tools/camel-gate.local.json`**（git-ignored 的本地闸门配置）：2026-11-27 一条车道换到新树后，第一次起跑直接报错退出，把本地配置从原树复制过去才跑起来。`new-worktree.ps1` 不管这个文件（它不入库，所以也不该入库）。换树时先 `Test-Path <新树>\internal\tools\camel-gate.local.json`，缺就从原树复制（同机、只读来源）。
· **`-Check` 这一步现在是坏的**：出现 `[camel-remote] --- check <name>: node   ---`（脚本路径为空）之后 `node`
  会等 stdin 永久挂住，40 分钟后才 `exit 124`。见到这一行**不要等**：记下它、结束自己那个作业，改按
  `[SUMMARY]` / `RUN_EXIT` / `[TREE]` 取数，并在交回里写一行「该检查未取到、工具态」。
· **让读数落盘，别只活在会话里**：会话被唤醒或在上下文压缩后重启时，作业表会被清空，**跑完的结果跟着丢**
  （2026-11-27 一条车道因此丢了两次读数，白等两轮）。起跑时就把整条输出重定向到文件，并把 PID 写下来：
  ```powershell
  $log = 'D:\Temp\<lane>-<plan>.log'
  $p = Start-Process pwsh -ArgumentList '-NoProfile','-Command',
    "Set-Location '<你的工作树>'; pwsh -NoProfile -File .\internal\tools\camel-remote.ps1 -Path . -Plan <计划> -SlotWaitSeconds 300 -Jobs 1 -NoGpuWatch *> '$log'" `
    -PassThru -WindowStyle Hidden
  $p.Id | Set-Content 'D:\Temp\<lane>-<plan>.pid'
  ```
  开工先 `Get-Content $log -Tail 40`；**PID 还活着就不起第二条**；PID 死了且 log 里没有 `[SUMMARY]`，才按同一形状再起一次。
· **批量改文本只用 `edit` 的 `replace_all`，不要用 `Set-Content` / `-NoNewline` 之类的整文件重写**
  （2026-11-27 一条车道用它做批量替换，把一个 234 行的用例折成 1 行；靠 `git checkout --` 从提交恢复才没损失，
  最终 diff 与预期一致，但这是一次量具事故）。整文件重写会把你没打算改的换行、BOM、编码一起改掉，
  而 diff 只看内容，看不出来。
· **判断一条 run 还在不在跑，只看日志，不看父进程。** 用 `Start-Process` 有意脱离父进程时，**父进程一定是死的**
  —— 「父进程已死」于是把每一条正常的落盘 run 都判成崩溃遗留。2026-11-27 协调者按这条错判，`taskkill /T /F`
  杀掉了别条车道一条正在排队的 run（已记为客户/协调者侧缺陷）；**同一晚稍后他又照同一条错判做了一次**
  —— 扫「父进程已死的 `ssh.exe`」清掉 8 条，其中 `kinds-oper`（03:24 起）与 `merge-seat-1`（03:26 起）两条
  其实是仍在排队的 run。判据只有两条：
  ① **日志最后一行的时间**（几分钟没动 = 在排队或卡住）② **有没有 `state=` / `[SUMMARY]` / `exit` 行**。
  ⇒ **`ssh.exe` 一律不动**：它不是 run 的边界，`pwsh` 包装壳才是。没有包装壳可收时，把它当“仪器态”记一行、
  报给协调者，由合并位在宿主上只读确认是谁持着锁（`/proc/<pid>/cwd`），**不要靠杀进程腾槽**。
  ⇒ 真正能解决 `exit 4` 的两条路是：**换 checkout 路径**（锁按路径 cksum 建）与**等 `[load]` 落下来**。
· **同一计划不要并行两条**：宿主槽位只有 8 个，2026-11-27 一夜同时在跑的 run 有 **11 条**，于是所有人的 `exit 4`
  变多却没人拿到读数。一条车道同一时刻只允许**一条** run；要换计划就等前一条出结果或 `exit 4`。
· **`taskkill` 掉本地包装壳不会释放宿主机端的车道 token**（2026-11-27 实测）：杀了整棵 pwsh 树、`effect-audit` 的
  `ssh.exe` 归零之后，新 run 仍停在 `live worktrees: …` 之后**九分钟不打印 `host-tokens`** —— 那是“宿主机端的车道 token
  还没释放”的签名，不是排队，也不是负载。换一棵 checkout（`-Path`）并换车道名，一次就打出
  `host-tokens: running at 4 of 4 worker(s) (lane=<新名>, 0s to take them)`。
  ⇒ 所以：**别在同一棵树上连续重排**；看到“停在 `live worktrees` 之后没了”就换树。本地杀进程只清本地，宿主那份要等它自己回收。
· **本地收壳 ≠ 宿主侧收壳，而宿主那棵树自己持着 `<checkout>` 的 `flock`**（2026-11-27 实测，这条解释了今晚大部分 `exit 4`）：
  一条 run 在本地 `taskkill` 之后，**宿主侧的 `run.sh` / `timeout … host-tokens.sh` 仍然活着**，一直持着
  `/tmp/camel-tree-<cksum>.lock`；下一条 run 于是卡在 `flock -w 1800` 里，`-FetchFile` 那次 transfer 花掉 **1,254.6 秒**
  就是这么来的（不是宿主慢、不是负载）。按 **PID**（宿主的 `run.sh` 父进程 + 它下面 `timeout`/`host-tokens.sh`）结束之后，
  锁立刻释放、排队的那条 run 立即跑完。
  ⇒ 收壳要收**两处**：本地包装壳的进程树，和宿主上**这次 run 目录名对应**的那几个进程（`…-<run id>-2026…`）。
  ⇒ 只认自己记过 PID 的进程；**不要**按名字杀（`run.sh`/`timeout` 别人也在用），也不要动 `ssh.exe`。
```

## 7. 路径与工作树

```
· **开工第一步（或每次被唤起的第一步）自己绑格子**：调 `oversight_bind({slot, lane, worktree})`，
  **不要传 `agent`** —— 它绑的是调用它的那个会话自己。`slot` 取 `dev`（瘦身/论文/提速）、`misc`（常驻杂项）、
  `buffer`（加急、探针、临时活），合并位自己的那条车道用 `merge`。条带上的「开发 x/8」就是这么数出来的；
  不绑的话你在条带上不存在，提醒会把开发位读成 0/8（假警报）。收工时 `oversight_unbind()` 解掉自己那一行。
· 路径一律**绝对**：`D:\Projects\Camel-wt\<lane>\…`，`edit` / `write` 也一样。
· 判据不是「我确认过」，是两行输出：
    `git -C <你的工作树> status --porcelain` 里能看到你的改动；
    `git -C D:\Projects\Camel status --porcelain` **为空**。
· 主检出 `D:\Projects\Camel` 归负责人：不提交、不快进、不动它的 `out/latest`。
· **要「改前读数」而仓库不许建额外工作树时**：在**同一棵树**里把要量的那几个文件 `git checkout <base> -- <paths>`，
   构建+跑，量完 `git checkout HEAD -- <paths>` 复原，贴 `git status --porcelain` 为空与那次的 `[provenance] binary built …`。
   判据不变（构建产物来自那次 build），代价只是两次构建；**不要**为了取改前读数去建第三棵树。
   三个容易读错的地方：① 退的是**源文件**，你新加的用例与计划不退 —— 要量的计划正是你加的那一份时，它也得一起退；
   新加的头文件留着即可（base 的源文件不 include 它，不参与编译）；② 读数里 `[TREE] … N paths were already
   uncommitted when this run started` 会列出这几个文件，属正常，照抄进交回；③ **改前与改后要挂在同一个 base 上**：
   跨 base 的差先当“base 的差”处理 —— 同一个用例可以在一格上红、在下一格绿，只因后者的祖先重钉了快照；
   既不要把跨 base 的差算成自己造成的，也不要把同一 base 上的新红说成既存，两句话都要有读数。
· **优先领一个池位，不另建树**：工作树是按槽位命名的固定一组、车道间轮用，忽略的构建产物（`build/`、`out/`）
  留着，下一条车道的构建是增量的。**池子没有 `-Reuse`/`-Release`/`-Pool` 这些开关**（`new-worktree.ps1` 的参数只有
  `Path/Branch/Base/FromCheckout/VerifyOnly/NoHusky/NoFetch`；2026-11-27 之前这里写着有，害得两条车道找了半天）：
  领槽位 = **挑一棵 detached 且干净的树**，直接 `git -C <槽位> checkout -B <branch>`；还槽位 = `git -C <槽位> checkout --detach origin/integration`
  （分支由合并的那一方删，不在这里删）。「空闲」只认两条：`rev-parse --abbrev-ref HEAD` 是 `HEAD`、`status --porcelain` 为空。
· **要换一把 `flock` 锁就换路径**：宿主的 tree lock 按 checkout 路径的 `cksum` 建，同一棵树同时只许一条 run。
  换锁的合法办法是另开一棵（`-Path D:\Projects\Camel-wt\<新名字> -Branch <branch> -Base <sha>`，或 `git worktree add --detach <路径>`），
  跑完 `git worktree remove` + `git worktree prune`；**不要去动别人的树，也不要去动别人的锁。**
  **锁文件一律只 `cat`、不 `rm`**（2026-11-27 两次代价）：`cat` 出来的持有者 pid 会在你 `ps` 它之前就换人
  —— 第一次删时文件里已是 `pid=726230`（另一个 run 刚拿走），第二次 `cat` 到 `783448` 却仍按上一轮的 pid 验，
  两次都删掉了**活锁**（都按删前原字节恢复、持有者未受惊动，但那是运气）。持有者已死时留下的文件只是旧记录，
  `flock` 已自动放手，**它不挡下一条**：所以它既不需要你删，也轮不到你删 —— 只把 `cat` 与 `ps -p <当次 cat 的 pid>` 两条原文报给协调者。
· **建树只走一个入口**（槽位第一次建；池里没有空位时），不要手抄 `git worktree add`：
    pwsh -NoProfile -File internal/tools/new-worktree.ps1 `
      -Path D:\Projects\Camel-wt\<lane> -Branch <branch> -Base origin/develop
  它把两件 git-ignored 的事做成一步 —— `camel-gate.local.json` 与 `.husky/_` —— 建完拿空输入试跑一次钩子，
  **读到检查器自己的输出才算建好**。少了 `.husky/_` 时 git **不报错**：`core.hooksPath` 指向一个不存在的
  目录，这棵树推出去的每一次 push 都不跑任何钩子（记录校验、develop 锁、`check-process-traces` 全静默跳过）。
· **一棵树有没有守卫，读钩子的输出，不是看目录在不在**（`.husky/_/h` 按自己所在位置找 `.husky/pre-push`，
  目录在而链子断掉同样是「没有守卫」）：
    `git -C <树> hook run pre-push` 不许回 `cannot find a hook named pre-push`
    `new-worktree.ps1 -VerifyOnly -Path <树>` 拿空输入试跑，读不到检查器的行就是没守卫
  补装只跑 `npx husky`（或 `prepare` 里那条），**不许手写 `.husky/_` 里的文件**。
· **提交前的格式化只碰你这一笔改到的行**：钩子读暂存差异的区段，把它们当 `--lines` 传给 `clang-format`，
  没改的行一个字节不动；版本钉在 `.clang-format-version`（现在是 22）。机器上的 `clang-format` 主版本与钉子
  不同、或根本没装时，钩子**不改文件、只打一行**说明，提交照常走 —— 遇到这一行就读它、照常提交，
  **不要用 `--no-verify`**：绕过它等于同时绕过记录校验、`check-process-traces` 与文档引用检查。
  树里的旧排版看着「不合格式」是正常的（它由多个版本排过），**不要为了整齐去整份格式化一个文件**。
· **清理只删自己那一个文件名，永不删目录**：`test/plans/probe/` 里可能躺着别的车道的 tracked 计划
  （今天一次 `Remove-Item test/plans/probe -Recurse` 就删掉了 7 个别人的探针计划，靠 `git checkout -- <dir>` 才恢复）。
  删之前 `git status --porcelain <dir>` 看一眼；tracked 的东西删了会静默变成“别人的活没了”，而 `git status` 在你自己树上还是干净的。
```

## 7½. 交回

```
· **交回 = 本地提交在 `lane/<名字>` 上**，附 `git rev-parse HEAD` 与 `git log --oneline <base>..HEAD` 原文。
  车道分支**不推远端**（钩子会拒）；远端只收 `integration`、`develop`、`fsh-dev`，归档引用也只留本地。
· 交回必须带**对上次晋级红表的逐名比读数**：`plan_id` + 用例名逐条比，给出「红数、与哪一份表比、新增几条」，
  并把那条**判词行原样**贴出。判词行是相位读数的唯一来源：调 `camel-remote` 时**不许用管道把它滤掉**，
  要少噪音就 `Tee-Object` 存一份再看。
· 判据是「绿 = 没有新红」，不是「全绿」；车道自己的目标用例要真绿（§10）。
· 交回后**两小时内**要进合并队列（协调者的责任）；没进按卡住处理 —— 卡住要报，不要等。
· **“前置已就绪”必须附尖端原文**（2026-11-27 一天内栽了两次）：凡交回里写“XX 已合入/已绿/基线没漂移”，
  必须附一句在**当前 `origin/integration` 上读到的原文**（`git log -1 --oneline`、`git grep -n <符号> origin/integration -- <路径>`、
  或该计划在尖端跑出的 `[SUMMARY]`）。没有这一句，“已就绪”不算数，协调者会当未就绪处理。
  两次实例：一次是两道车道报 `feat/modules 1967/120` 漂移，实测是树落后 55 笔；一次是“`core.shape` 已迁到 `facts::Kind`”，
  实测集成尖端仍是 `KindSpec<T>` ⇒ 整条效应链的开闸条件其实没满足。
· **基线口径按尖端重读，不按记忆**：计划数会因上游修红而变（2026-11-27 `modules.autodiff` 由 `277/15` 变 `288/4`，
  11 条被上游修掉）。交回里引用的每一个基线，要么附尖端读数，要么注明“引自上轮晋级表 <日期/名>”；两者都没有就重跑。
```

## 7¾. 换底的四个时机

```
换底 = 把 integration 接上来，**不把 integration 合进你的分支**；但**用 `rebase` 还是 `merge` 取决于这条分支推没推过**（2026-11-27 一条车道
按仓库规矩纠正了协调者的说法）：`agents/branching.md:9` “do not fast-forward, **rebase shared history** or force-push shared branches”、
`:42` “Synchronize by **merging** `origin/develop` into the lane, **never by rebasing a pushed lane**”
  · **没推过的车道分支** ⇒ `git rebase origin/integration`（历史干净、`range-diff` 好看）
  · **推过的分支**（gate/staging 推过的）⇒ `git merge origin/integration`，产生一笔 “Merge origin/integration (…) into lane/<名字>”
  · 不确定推没推过 ⇒ 按 `merge` 做（它不会错）；反过来做错就是重写共享历史
四处必须换：
  ① 交回之前（即使只是报进度）
  ② 插件点名你落后超过 100 笔时，当轮换
  ③ 暂停之前
  ④ 恢复之后再换一次
换底后原有读数不算数：在新尖端上重测，交回里写明「读数是在哪个尖端上量的」。
**一条踩过的坑**：`git fetch origin integration` 只写 `FETCH_HEAD`，**不动 `refs/remotes/origin/integration`**；
之后拿 `origin/integration` 核“某笔是不是已进去”会得到**反结论**（2026-11-27 一条车道因此报「我的改动被回退了」，
实际是它本地的远程追踪 ref 落后 40 多笔）。一律 `git fetch origin`（无 refspec），
并先打印 `git log -1 --format=%h origin/integration` 的 sha 再动手。
**被唤起续做时，第一件事是重读本 skill**，再 `git log --oneline -5` 认自己的位置。
**你的 `git fetch` 可能根本取不到远端（2026-11-27 实测）**：`origin` 是 SSH（`git@github.com:…`），而机器上的代理只配给
HTTP(S)（`http.proxy=127.0.0.1:7890`），SSH 走 `ssh.github.com:443` 直连会 `Connection timed out`（握手前就失败，
`git` 后面那两行 “correct access rights / repository exists” 只是它的通用提示，**不是凭据被拒**）。
⇒ 这**不影响**换底：所有工作树与主检出共享同一个 `.git`，refs 是共享的，协调者的 fetch 会把它更新。
⇒ 所以你的做法只有一条：**不 fetch、不推远端**；换底前 `git rev-parse origin/integration` 与协调者报的 sha
**逐字符比对**，相同才 `rebase`，不同就**停下问**，不许默认自己那份是新的。
**不许用 `Select-Object -Last N` 截一条命令的输出去读错误**（2026-11-27 一条车道因此把 `fetch` 失败的**第一行**丢掉，
把“TCP 超时”报成了“凭据被拒”，差点被当成权限问题去修）：要截就 `Tee-Object` 存全文再读尾巴。
```

## 8. 报告格式（≤12 行，跑完报、等点名）

```
① 你被问到的那件事的读数（**必答项放第一行**）
② 改了什么（文件+函数逐条）
③ 改后读数
④ 计划 `[SUMMARY]` + **真退出码**（逐项写，不要只给合计）
⑤ 阳性对照：证明你没有顺手放宽别的东西的那一格
⑥ 提交 sha + `git diff --numstat`
⑦ 卡在哪（**含「这件事不存在」这类结论**）
```
```
· 数字带四样：哪个群体、哪个时刻、哪个来源、哪个前提。
· **两个不同树上的 `[SUMMARY]` 永远不许相加。**
· `skip`、`known`、`fail`、`pass`、`total` 分开写；`skip` 不等于 `pass`。
· 判据写进提交时用技术语言（§4）。
```

## 9. 判据分五类（派工会点名哪一类）

| 类 | 形状 | 关键句 |
|---|---|---|
| **A 之前/之后** | 同一程序、同一命令，改前一个数、改后另一个数 | 「两个都要报」 |
| **B 问到了没** | 判据是「这件事被问过」，不是「它被拒了」 | **「问到了、然后通过」是正确结果** |
| **C 阴性/阳性对照** | 一个该通过、一个该拒，读数必须**不同** | 「两者读数一样 ⇒ 你什么也没加」 |
| **D 复现优先** | 第一步确认那件事真的存在，复现不出就**撤回这一格** | **「复现不出比修好更值钱」** |
| **E 位置不变** | 结构改动的判据是位置/顺序没动 | 「节点消失**且**位置不变」 |

**D 类必须写在第一句。**

**A 类的「改前」没取到怎么办（会真的发生：先改后跑）**：不要撤提交、也不要拿“我判断是既有红”顶上 ——
**只回退那一个文件**补读数，树其余部分不动：
```
git rev-parse HEAD                        # 记下当前提交与原文件的改前提交
git checkout <改前提交> -- <那一个文件>
跑那条计划，贴 [SUMMARY] 与失败的原文行
git checkout HEAD -- <那一个文件>          # 恢复，git status --porcelain 必须回到空
```
两行并列写“改前 / 改后”；若改后多的红在改前就有 ⇒ 与这笔无关；若改前是绿的 ⇒ 这笔要重做。
（改动跨几个文件时是同一手法，见 §7 那条“要「改前读数」而仓库不许建额外工作树时”——那里是它的唯一一份，
不要在这里再写一遍。）

## 10. 「绿」有两种口径，别混用

> **红表上的条目名是“测试项”，第一行失败文本才是“测量对象”。** 两者不一致时，先看第一行，再决定改什么。
> 实例：`Unexpected ':'`（解析错，不是容量）、`unmeasurable`（没有对象可测，不是断言不成立）、
> “草稿句柄没有权限”（对象状态，不是图的事实）、`refused` 量级差（两边跑的不是同一批程序）。
> **先修测试，不要改期望**；判不出类型就写着“待解释的差”，不要写成“仪器态”。

> **“静态规则 0 命中”要区分两种 0**：一种是文件干净，一种是**规则看不见这种写法**
> （模板洞 `func f{i}(...)` 对要求字面名字的规则永远是 0 命中；生成器输出只有跑一次才露）。
> 区分办法只有一个：**让被测对象真的跑一次**。

> **瘦身/拆头的收益口径**：收益 ≈ **这一族声明自身的改动次数** × (原重编单元 − 拆后单元)，
> 不是“头文件改动次数 × 单元数”。前者用 `git log -S<该族符号> -- <头>`（并滤 merge）量，
> 后者会被路径搬迁这类改动抬高。**冷族拆出来结构上仍然对，但当期回报是 0 —— 照实写 0，别凑数。**

> **红表看不出病史**：一条红可能是「这两天被改坏的」（回归），也可能是「慢层长期欠账」——两者在红表里长得一模一样。
> 动手前先查这条用例的历史（`internal/reports/*-the-slow-layer-population.tsv` 这类 census，或 `git log` 该用例）。
> 回归要**找到那一笔**并说清它把什么改成了什么；欠账要先判“它原来绿过没有”。

> **改了「诊断往哪一路走 / 诊断怎么渲染 / 什么算一次故障」的代码，会红在“该报的错没报、程序跑出了值”上**——
> 那是静默的错误结果，红表上离你很远。**这件事不靠你记**：`test/plan-selection.json` 里有一条文件规则，
> 把 `src/kernel/core/error/`、`src/kernel/execute/`、`src/tools/camel-cli/`、`src/stdlib/std/nvm/`、
> `src/stdlib/std/fvm/` 映射到 `feat/compile/diag_render`、`feat/compile/diag_options`、`feat/linear/runtime_negative`
> ⇒ `node scripts/test.js --changed` 会自动带上它们。**若你改了这些路径而 `--changed` 没选上那三份计划，
> 那是那条规则漏了，先补规则、再干活。**

```
· **车道自己的活**：目标用例要**真绿** —— 那条断言从红变 PASS。
  「它没有变红」不是完成，只有「它变绿了」才是。
· **整层比对（合并闸门的口径）**：**绿 = 没有新红**。空集的差比相等的总数强；
  不要拿子集总数和整层总数比；比对按 `plan_id` + 用例名，逐名比。
· 说「绿了」时讲清是哪一种，否则同一句话在两种口径下差得很远。
```

> **有第三种“绿”是假绿：扫“新增行”的断言会随 HEAD 变答案。** `selfchecks.push_trace_range` 这类检查扫的是
> **这次 push 新增的注释行**；HEAD 本身已经在远端时，新增集为空，它必然报 `no trace in the lines added by 0` ——
> 那是**假红**，与你的改动无关（2026-11-27 一条车道为它想回退 16 处注释，复验两轮才发现）。规矩：**单跑这类断言前
> 先确认 HEAD 是未推送的枝头**；跑出红而内容没动时，先换 HEAD 状态复跑一次，再谈是不是你造成的。
> 镜像的一条是**假绿**：`-Check` 或 `BUILD_EXIT=0` 只说明“跑了/编过了”，不说明模块装好了
> （2026-11-27 一次 `BUILD_EXIT=0` 而整层 140 红：模块缺一个 `import` 就整片装不上）。
> ⇒ **每次交回同时给 `BUILD_EXIT` 与计划 `[SUMMARY]`**，两个都读得过才算绿。

> **还有一条“转绿”不是修复，是过渡重钉**：用例的断言被改钉到**新的拒绝文本或新的位置**，而它断言的那条**前提已被撤回**。
> 它从红表里消失，缺陷还在 —— 只是这张表不再量它了。
> 实例：`D-1128-39` 撤回了 “a Loop carries a state whose shape is the same on every round”（ONNX 的 `Loop`
> **可以**携带 sequence 或长度逐轮变化的张量，只是这个导出器还不会降 —— 实现缺口，不是表达力边界）；
> `modules.onnx.export_loop_carried_array_field` 在 72 表里“转绿”、73 表里**仍然没有它**（73 的 record 写明
> “export_loop_carried_array_field stayed green, so the seventy-second reading's one green was not a one-off”），
> 实际是 `ba4d1ec233` 把 `expect_diagnostic_column` 从 `81` 改钉到 `9`，而 `verify_script` 断言的仍是那句已撤回的前提。按 `D-1128-52`
> 它要改成正向用例，降级落地前照旧红。
> ⇒ **交回里不许把它写成“修复”**：写“过渡（钉位置 / 钉文本，前提待撤回方向落地）”或“仍红”。
> 一句判据：**这次改动有没有让用例开始检查一件仍然成立的事？** 没有 ⇒ 那是过渡，不是修复。

> **二分一个“运行结果”类谓词时，每个判据点有三种结果，不是两种：真 / 假 / 崩。** 中间提交可能根本跑不起来
> （2026-11-27 一条车道在 `8749ac78c0` 上量到 `expected exit 0, got signal SIGABRT`、`std::jgir did not print JSON` ——
> 那是它所在侧枝还没补齐，不是“哈希对不上”）。**崩记成 skip，不记成假** —— 这正是 `git bisect skip` 存在的原因；
> 把崩当假会把真端判错一整段。每点交回三列：`<sha> 真/假/崩` + 耗时。顺带查一句它是否在**第一父链**上
> （`git rev-list --first-parent --count <sha>..origin/integration`）：不在第一父链，崩就完全说得通。

> **二分之前先“量形状”再“跑运行”**：把判据化简成“哪些输入进了这个值”，再按“改这些输入的文件”收窄提交集，
> 能把几千笔压到十几笔（2026-11-27 一条车道先把归一化哈希的输入量清：进哈希的是整份 jgir 字节、只有源路径与旧模块名被归一化，
> 于是 6599 笔 → 13 笔）。**先量再跑，比先跑再量便宜一个数量级。**

> **找“某个侧枝提交经哪次 merge 进入主线”，不能用 `--first-parent --reverse <sha>..<tip> | head -1`**（它给的是
> “第一个不再包含它的第一父母提交”，未必是 merge 本身）；用
> `git rev-list --merges --ancestry-path --reverse <sha>..<tip> | head -1`，那才是**入口 merge**。
> 拿到入口 merge 后判据点是“第一父链上的 merge”（每个 merge 都是完整树、可运行），**不是侧枝中途提交**。
> 若 `入口merge^1` 为真、`入口merge` 为假 ⇒ 变化就在这次 merge 里，逐笔看它带的实质提交（`git show --stat`，静态判），
> 别去检出那些候选提交——它们在侧枝上是中途态，可能跑不起来。

## 11. 这几条是读数语义，不是风格

```
· 「读不到 ≠ 没变化」。读不到就报读不到。
· **量具的三态里，「不知道」不许归到「通过」**：前置探针能答「有」、能答「没有」，还会答第三种——**读不到**
  （命令没跑起来、样例没走到被测路径、标志的措辞变了）。第三态必须 exit 非零、让用例**跳过**，并把读到的原文写进理由；
  把「没读到那条标志」当成「就是有」，整片用例就带着坏量具跑成红，而红表上只看得出用例红了、看不出量具坏了。
  例：`have-native-jit.mjs` 拿一次 `-v hello.cml` 打不打那条告警当答案，无 codegen 的构建对那个样例一句话都不说 ⇒ 被判成「有」。
  同理：探针只用**一个样例**回答时，先问一句「这个样例会不会走到那条路径上」。
· 量的那棵与要改的那棵不是同一棵时，读数无效。
· **合并两套“位置”之前，先确认两边数的是同一张图的形参**（2026-11-27 一条值特化车道踩过）：
  副本上的形参下标与根上的形参下标不是一套索引。把副本的位置当根的位置去合并绑定，会把绑定安到错的形参上，
  报出来却是 `internal error: … FUNC node 3 passing 0 arguments and 0 'with' arguments to 'walk$spec10', which takes 1`——
  **错误信息指向的是签名不匹配，真因是键建在了另一张图上**。做法：先把副本上的位置按**同一个节点**翻成根上的位置，
  再用根的坐标建键、建副本（`formalIndexInGraph`）。同一条教训的另一面：**键要落在哪张图上，比键长什么样更要紧**。
· 内部错误是编译器在承认自己不懂 —— 别把它当噪声，也别当成用例过期。
· 一条永远不会失败的断言不是检查，是装饰：**新加的守卫要先被看见变红**。
· 两个数不一样 ≠ 有一个是错的。
· 「读不到」还有一形：**读到的是上一次的值**。固定路径取回的文件、按路径缓存的报告，都会把上一轮的结果端到你面前；
  取回物按运行/提交命名，并把它的路径打进输出、让计划断言这一行，才算同源。
· 身份与文本分开看：**识别按 id，命名按文本**。凡是「要从名字的拼写推出另一个身份」的都是缺陷；
  凡是「要把身份交给一个只收文本的接口」的都是命名处，必须留 —— 判据看它交出去的是身份还是文本，不看调用点长什么样。
  **形参是文本的辅助函数内部比较也算在内**（`f(const std::string_view uri)` 里 `uri == "…"`），但要分两半判：
  由节点调用、那段文本就是节点身份 ⇒ **该改**（把 id 传进去）；它是查表/降级链的一环、手上只有文本 ⇒ **半张真相**
  （需要结构化查表口，跨内核，别自己造）。按调用点 grep 会漏掉这一类 —— 要按**形参类型**去找，报“该改 N 处”时说明这个数是怎么数出来的。
· **改代码用 `edit` 一处一次，不要用字符串批量替换**：批量替换**没有“没命中”的报错**，静默不命中而你已经在跑下一步，
  是最难发现的状态（今天一次批量替换没生效、文件却已被写过，长度变了但内容没变 ⇒ 状态不可信）。
  已经写过就立即 `git checkout HEAD -- <file>` 还原，再核 `git status --porcelain` 为空；改完用 `git grep -c <旧文本>` 看数**从几减到几**。
· **报错的属性不是病因**：`graph.permissions` 报“句柄不指向任何节点”，真正的错在更上游那次“边替换边比较”——
  报错点只是第一个碰到失效句柄的地方。定位先从报错往上追一跳，别在报错那一行上改。
· **在 GCT 的 LINK 节点上读 `name()` 会 SIGSEGV**（2026-11-27 `lane-editview-commit` 两次崩都是探针自己的错，不是判据）：
  探针先判 `type()` 再读 `name()`，**只在 `DREF` 上读**；`name<args>(…)` 的 callee 在 `WITH` 子节点里，
  只扫直接子节点会**一条不响**（看着像“这条路没进”，其实是探针没命中）。探针打完 `reset --hard`，
  并核 `git grep '<标记>'` = 0 才交回。
· **`--block-limit` 管的是“预留”，不是“在用”**：`room = limit − reserved`，而 `[camel] blocks: peak=` 记的是**在用**。
  两者不是同一口径 —— 2026-11-27 那晚 `limit=3808` 时“在用只有 3767.1 MB、还剩 40.9 MB，却拒了一个 9.0 MB 的分配”，
  就是这条口径差的直接证据（拒绝与“在用”无关）。**拿阶梯探针读出来的数，是“跑得完的最小 limit”，不是峰值用量**；
  要峰值就用 `[camel] blocks: peak=`。块空间的段宽也**不是固定值**（`growable.cpp:103-121`：从 8 MB 起、按 `reserved` 翻倍、
  上限 `maxSegmentBytes = 256 MB`，最后被 `room` 截断）⇒ **两档 limit 之差不能当成“段对齐开销”**。
· **推断要当场标成推断，并且标出它在哪一层**：一条报告里“读数 / 机制 / 注明为推断的猜测”必须分开写。
  2026-11-27 一页报告把“3840 − 3775.9 = 64.1 MB 是段对齐”写成了事实，随后的更正说明它是推断且不成立 ——
  同一页里混着这两层，读者没法知道哪句可以复算。改法是三层并列，机制那句附上 `文件:行` 让读者自己核。
· **按需跑的量具不需要 Debug 构建**：`--tier debug` 确实要 `out/debug` 的 Debug 二进制（没有会 exit 2 并提示构建命令），
  但**不传 `-Tier` 的显式文件目标会整份跑、用 Release 二进制** ⇒ 把探针挪到 `tier = "debug"` 只表示“不进任何层”，
  不表示“必须为它建一棵 Debug”。
· **重录金标一律带 `-Entry`**：`camel-remote.ps1 -UpdateGolden -Entry <名字>[,<名字>…]`（名字或 `计划id/名字`，
  可用 `*` 通配）；只有选中的条目会跑、会写金标，**名字一个都没对上就退出 2**。不给 `-Entry` 仍是整份计划重录 ——
  那会把同计划里别人的快照一起重写（`cse_written_variable.dot` 就是这么被“顺手”改掉的）。
  重录前先判“这份计划是否整份绿”：**计划还在报错时录，会把诊断或空图写进金标**。
  录后 `git status --porcelain` 只许出现你点名的那几份；多出来的 `git checkout HEAD -- <file>` 还原。
```
**读数里出现密钥先隐去**：任何 `sk-` 开头的值、`-e KEY=…`、`--token`、`api_key`、密码，贴 `ps`/命令行/环境变量原文时一律先遮成 `sk-[已隐去]` / `KEY=[已隐去]` 再报。**一次没遮，等于把密钥写进通道日志与仓库历史**；进程参数在同机任何用户 `ps` 都看得到，它本来就已经泄露过一遍。
· **判一条“新红要不要修”之前，先看它在 `origin/integration` 尖端还在不在。**一条 `git grep` 就够：表里的红对应 `065bfb1690`，活可能早已由别的车道做完并合进 integration。**表没错、活已有人干完** ≠ 看旧表；**“它在自己基上还红、在尖端已绿”= 已被别人做掉**，两者不一致就别开工。
· **`camel-remote` 打了 `0 changed file(s)` / `measuring: the commit alone (no tracked change)` ⇒ 它测的是已提交的树，不是工作树。**改完立刻起跑（实测 3 秒内）测到的是旧内容。规程：**在拷贝上改完 → 提交 → 再跑**；看 `[provenance] tip` 是不是刚提交的 sha，不是就作废重跑。
· **`job_kill` 只收本地包装进程；远端 `/tmp/camel-remote-<tree>-<jobid>-<stamp>/run.sh` 会继续跑，并一直持着 checkout 的 flock。**下次 run 报 `exit 4 no slot on this checkout within 1800 s` ⇒ 那是"没读到"，不是红。收尾要按**远端 run 目录名**找 PID 一并收，不只看本地 job 状态。
· **`-SlotWaitSeconds` 只影响时间敏感分支（`camel-remote.ps1:2892` 一带）；普通槽位是硬编码 `flock -w 1800`（`:2578`）。**传个大数不会延长等待，实际仍是 1800 s 后 `exit 4`。排不上槽位时：① 换 `-Path`（新 checkout = 换锁换 token）② 确认锁已空后用同一棵树另起 lane 名（宿主 token 按 lane 名分）。**`[load]` 高于 75 就别起**，白等一轮就是 1800 s。
· **`pre-push` 只接受“在 integration 上逐条 `--no-ff` 合车道”这一种到达方式**：把 `origin/integration` 合进自己的合并批次再推会被拒（原文：`redo it with scripts/merge-batch.mjs, or git checkout -B tmp origin/integration; git merge --no-ff <lane>.`）。远端前进了，唯一办法是 `reset --hard` + 逐条重做；重做加重新编译约 6–20 s（撞上 header 重建才几百秒）。**协调者推文档会让合并位的批次作废，所以推送要按“窗口”来：合并位说“起批”之前/之后各推一次，别在它起批中途推。**
· **`camel-remote` 的锁与令牌按“远程目录名”取**（`$Path = Resolve-Path`、`$name = Split-Path -Leaf $Path`、`tree_key = cksum($HOME/camel-work/$name)`）⇒ **“换 `-Path` 换锁”只在 leaf 名真的不同时成立**；junction、相对路径、尾斜杠都会被 `Resolve-Path` 折叠回同一棵。要换锁必须有一棵真 checkout、leaf 名不同、且含要测的那个提交。**但先别急着换树**：上一跑的锁会随它的 `RUN_EXIT` 释放，同一棵树再起一次往往直接取到（实测多次）。
· **一条“期望四条全错”的红，可能是第三类：用例脚手架踩在还没落地的特性上。**判法：把用例里**与这条断言无关的脚手架**换成一个能过的写法（临时副本，仓库文件不动）再跑一次 —— 若钉住的四样（诊断名/位置/类型/签名）随后**一个不差地出现**，说明期望是对的、只是被前面那一行挡住了。这时修的是脚手架那一行，**期望值一个字都不改**。⇒ 这类红既不是缺陷、也不是记录过期；派工时先问“它前面有没有一行依赖未落地特性”。
· **`-E short`（以及默认的短格式）诊断行带文件名与列**：`(case)…/x.cml:4:16: [Error]: …` ⇒ 看到计划里只有 `.cml:4:` 这种钉子时，那是**钉子写得弱**（钉不到文件、钉不到列，换另一个 `.cml:4:` 一样能过），**不是 normalizer 抹的**。判这一类问题前先直跑一次看原始行，别先怪归一化。**`exit 4` 的第一反应也不是换树：先查锁是不是自己的旧 run**（按 `/tmp/camel-remote-<tree>-<jobid>-<stamp>` 找 PID）。
· **判“基上缺不缺它”，`grep` 只答“代码在不在”，还要问“这条路在本次程序里会不会被执行”。**例：`exportOperatorOverloads` 基上就有，但它只为“本次构建的模块”跑，对 `.cmo` 载入的模块永远不跑 ⇒ **存在 ≠ 会走**。把“最接近做完”翻成“没通”的，常常就是这一步。
· **派“验证某分支能不能合”之前，先查记录里有没有已经判过它**：`git grep <分支名或那笔 sha>` + 看 `internal/questions/` 与 `internal/backlog.md`。今晚两次漏过这一层：一次是判“要不要做”的活其实早已由另一笔提交做完（`CAMEL_MM_BLOCK_TRACE`），一次是判“能不能合”的活其实早就写在问题页里“不进合并队列”。**判据若在记录里已经定了，读数再多也改不了判词 —— 先查记录，再排 run。**
· **诊断顺序：两股流合流后 = 发射顺序（编译期诊断先写、执行期故障后写），不是源码位置顺序。**判一条"索引钉错"的红，别只看"哪句更像规范"：在 `test/plans/**` 里找**同族且两类诊断共存**的绿用例——若它钉的是"语义在前"，那就是现行规矩，红的那条是记录没跟上合流；只有"一条诊断"的绿用例（如 `length === 1`）不涉及共存，**不能当证据**。
· **`exit 4` 的第一件事：看树锁的持有者是不是自己某次"本地已拿到 `[SUMMARY]`、远端还活着"的 run。**例：`/tmp/camel-remote-<tree>-<jobid>-<stamp>/run.sh`（形如 `bash run.sh → timeout → node` 卡住 40 分钟）仍占着树锁与一个 token ⇒ 按 PID 整棵收掉，锁立刻空、排队的 run 当场开工。**不是换树、不是加 `-SlotWaitSeconds`。**只动 scratch 名字里带自己树名的那棵。
· **一棵工作树只归一条车道**：两车道同树会互相 `checkout`、把对方的分支指针顶回去。实例：`console2` 上 `lane/plan-spelling-2` 与 `lane/gen-artifacts` 撞车，前者提交后分支指针回到 base、一条 run 量的是没改动的树（读数作废）。发现被顶掉的处置：**`git reflog` 看是谁的 checkout** → 把它钉回自己的分支 → 我的分支 `reset` 回原 base → `cherry-pick` 我的提交；**结果当“崩中受损”对待**（同一笔结论要重新取读数，不复用旧的）。开工前先确认 `git worktree list` 里你那个路径上挂的就是你的分支。
· **`[INFO] test scratch kept: …/tmp/test/run-<id>` 不是失败产物的位置**：那个目录只有空目录骨架（0 个文件）。真正的 per-case 产物在 **`test/results/log/<run id>/fail/<plan>/<test>/`**（`meta.json` + `output.txt` + `stderr.txt` + `stdout.txt`，部分另有 `verify_*`）。要把某一轮的失败产物取回来做逐条对照，按 **run id** 去 `test/results/log/` 取，别去 scratch。
· **在 PowerShell 里改文件用绝对路径**：`[System.IO.File]::ReadAllText("x.md")` 里的相对路径按**进程当前目录**解析，不是按 `cd` 之后的目录解析 —— 在文档工作树里 `cd` 后仍会读到主检出。本会话因此把一处改动错写进负责人的主检出（已 `git restore` 还原）。规则：读写的每个路径都写绝对路径；改完用 `git status` 在**目标树**里核一遍只有预期文件。
· **日志只写文件、事后再 grep，不在管道里截断**：`... | Select-Object -First N` 会让 PowerShell 提前结束上游，把 `camel-remote` 中途掐断、**没有 `[SUMMARY]`**，这轮读数就作废。要短看输出，先落盘再检索。
· **“回退到 base”要看这份用例是被拒绝的、还是要被执行的**：负例（`parse/**` 那类词法/语法拒绝）回退到 base 是修记录；**会被执行的用例**（`compile/**` 里要跑起来的）回退到 base 等于把旧写法配新内核 —— 会成批红。迁移类的活，可执行用例要按新写法重改，只有真正调用某个导入符号的文件才补 import（注释/字符串里的同名文字不算）。
· **批在飞的时候不推文档**：合并位起批时会报“起批，远端钉在 X”。从那一刻到“批完”之间，`origin/integration` 只能由合并位写；此时从文档工作树推一格会让它的合并提交变成 non-fast-forward。**协调者的文档提交要等“批完”之后再推**，或先问一句批是否在飞。
· **上下文将尽时先写交接卡，再停**（`D-1128-17`）：不要硬撑到“改了一半、读数交不上”。卡落 `internal/collab/<任务号>-<题名>.md`，四节固定：① **当前一步**（做到哪、下一步是什么）② **读数**（已取到的原文，带 `[provenance] tip` 与 `binary built`）③ **下一步怎么接**（枝名、树、要跑的命令、判据）④ **边界**（别动什么、已知坑）。卡提交在车道枝上；要它先进 `integration` 由协调者单独 `cherry-pick` 那一笔。反例：`opt::inline` 与 C4 ② 都是“上下文用尽、没有卡”断掉的，接手方得从头找基线。正例：`internal/collab/task-361-opt-inline-steps-2-3-handoff.md`。
· **开工第一句自己绑槽**（`D-1128-18`）：`oversight_bind({slot: "dev"|"misc"|"buffer", lane: "<你的枝>", worktree: "<你的树>"})`，**不带 `agent` 参数** —— 工具原文就是“车道自己绑自己时省略 `agent`，被绑的是调用它的那个会话”。按名字替车道绑会留下 `no live child has that id or name` 的 stale 行，插件的槽位计数会把它们当占用，所以：谁开槽谁绑，协调者不代绑。绑错了（换枝、换树、交回）自己再绑一次覆盖同一行，交回后由协调者 `oversight_unbind`。
· **两处同名实现并存时，先问读数是由哪一处给的**（`D-1128-18` 之后）：例 —— 卡上的验收读 `FACTS node_other node=revalidated` 由 `Context::facts()` 返回的 **runtime** store（`src/kernel/runtime/facts.cpp`）给出，而卡要点名的四处（`Entry::versions`/`StoredFact::versions`/`forgetGraph`/`clearSlot`）全在 **framework** store（`src/kernel/facts/`）⇒ 改得再对，读数也不会动。判法：插桩打在你要改的那一处的入口（打不出来就证明读数不是它给的），然后**把验收读挂在你改的那一处上**，另一侧另立卡并写明前置。反例代价：整格改完才发现"读数对不上你改的那一行"。
· **"陈账"的实际形态是记录没跟着落地更新，不是开了没做**（`cap-probe`，`D-1128-18` 那一轮）：抽查 10 条挂在名下很久的 `in_progress`，结果是 **7 条已落地（能贴 integration 上的提交/代码证据）、3 条卡在"缺一次读数"、0 条"从未开工且无枝"**。所以清板先按"已落地 / 缺一次读数 / 有活人活枝"三分，**别按名字批量标 complete 或 delete**；判"已落地"只认 integration 上的证据（`git merge-base --is-ancestor` 或 `git grep` 命中行），判"从未开工"要 `git branch --list` 与 `git log --grep` 两条都空。· **改拼写时，判据本体是唯一不许动的那一行**（`task-369`）：`git grep` 里剩下的命中要分三类 —— **判据本体**（`expect_output_not_contains` 那个字面量，删了测试就不再检查任何东西 = 放宽判据，必须留）、**历史叙述**（"当时打成什么样"的史料，留，改了反而断史）、**已经为假的事实句/写法示意**（说的是现在这台打印机的行为，改动使它不成立，**必须迁**）。所以"grep 归零"常常**结构上做不到**，正确终态是 `0 + 判据 + 史料`；交回时把剩余命中逐行标类别，别只报一个数。
· **改语法或改拼写时，先 grep 文档里教这个写法的地方**（`task-369` 的 `{  }`、`no-const-with` 的 `const` 形参，今晚两次同类漏）：`git grep -n -F '<旧写法>' -- docs src/stdlib test` 之后按同一套三分法 —— **教写法的必须迁**（它现在写不通了）、**历史叙述留着**（"当时打成什么样"）、**判据本体不动**（那是检查本身）。语法/拼写离开代码而文档还在教，就是第二份真相。
· **语言参考里的语法块是生成的**（`lane-ssa-residue` 交回时查清）：`scripts/check-grammar.mjs` 从 `src/parse/src/syntax/parser*.cpp` 的 `/// name : …` production 注释（含 `///   ` 续行）抽出，写进 `docs/language/08_top_level_and_grammar.md` 与 `docs/language/en/…`。所以改语法的正确顺序是 **改 production 注释 → `node scripts/check-grammar.mjs --write` → 才轮到散文**；手改那两块文档会被下一次 `--write` 覆盖，且 `grammar.reference_in_step` 会当场红。同类：`docs/stdlib-reference.md`、`docs/diagnostics.md` 也都是生成物（"只读→重生成→再只读"）。
· **读一处发射点，先看它所在的分支条件，再看行号**（`lane-parked-rebase` 的教训：`graph_of` 的 `makeDRef` 在 `hasRootParam = !isEdit && …` 里，view 专用，被误判成"降级里的第二条实现"）。
· **身份由持图方传入，框架不许自己造、连问都不该问**（`lane-arrangeable` 的 SIGSEGV）：`GCGraphManager::noteGraph` 里"问图要身份"会在**图还在构造**时被调用（`EncodeSession::run`），对半成品对象 `dynamic_cast` 是 UB ⇒ 崩在 `runtime::graphIdentity`。改法：身份由 owner 传（`toValue(graph->id())`），store 只记指针、从不解引用；`forgetGraph`/`~GCGraph`/`~GraphDraft` 同样传身份。
· **"`src/**` 零改动"不等于"不用重录 AST"**（`seat-merge-1` 落地 `docs/notes6` 时踩到，`D-1128-64` 那轮）：那枝 `src/**` 零改动，但它**改了 `test/cases/modules/rules/check_when.cml`、加了 `test/cases/modules/stdlib/abs_smallest_int.cml`** ⇒ 首趟 `parse.frontend frontend.ast_record` 报甲。判"要不要跑整层"看 `src/**`；判"要不要重录 AST"看**有没有动 `src/**/*.cml`、`test/cases/**`、`docs/tutorial/examples/**`** —— 三者任一都要看记录（`ast_hashes.txt` 收的是 `.cml` 的内容哈希，含 `src/stdlib/**`，注释也参与）。重录走"只读 check → `-UpdateAstHashes` → 只读 check"三步，**单列一笔只动 `ast_hashes.txt`**，交回里贴三次输出（`N lines recorded, a added and b removed` + `N file(s) checked, 0 stale`）。
· **"注释参与 AST 哈希"**：只改注释的车道会带 7～N 条 `ast stale` —— 那不是红，是**合并后在合并树上重采一次**的活（铁律 14 顺序）；车道不许去改 `ast_hashes.txt`，也不许与别人的整份重采同时动同一份金标。
· **"用例跑不到"比"用例红"更坏**：新增用例只在 `test/cases/` 存在、`test/plans/` 里没有条目 ⇒ 它永远不跑、也不报警（只在 `ast_hashes.txt` 里留下名字）。交回前用 `git grep -rln <用例名> test/plans` 自查一次；引入笔自带用例跑不到，补条目比留着强。
· **一处抬、一处不抬 ⇒ 只留一处**：同一条边上有两处各自决定"抬不抬类型"（例：`pairOf`/`mixedMemberTypeOf` 在 gate 侧抬、被前向的 copy 侧不抬）时，正解是**让其中一处做决定**（读另一处的类型），不是两处都抬；判据是"两条边由构造相等"。
· **重建签名要带上原属性**：`DeclaredFuncTypeResolver` 的 `closedSignature()`/`withoutPacks()` 只写 `name/type/isVar`，会把 `with <constas …>` 标记与 `withNeedsValueAt` 一起**重写丢掉**（不是"丢在 graft 之前"，是"根本没有被抄过去"）。凡见到"某个属性在构造/重写/包装后不见了"，先找**产生端**：`git grep 'addWithArg'`/`addWithConstasArg` 这类重建调用，而不是去补调用点的保护。
· **补调用点保护是改判据**：一条不变量（如 `hasNames_`）在几十处调用点触发时，要问"这类事实是不是该出现在这个通道里"；`src/compile` 里 `argNameAt(` 有 ~70 处，补 70 处保护等于抹平一类事实。
· **同一个结构两边读法不同 ⇒ 让结构只有一处事实**（`lane-block-hash`，`D-1128-30` 那一轮）：同一个收集循环，`pullback` 那侧说"3 状态 1 收集"，导出器 `emitLoopScanning` 那侧说"8 收集 2 携带"——**事实不是缺失，是两边各算各的**。判法：在**报错那一处**插桩打"当前图名/节点 id/条目 id + 分量数"（一次构建就能判清落点在图集合的哪一支），**先判再改**；结论只有三种（分量数与 `states+scans` 对不上 / 替换只做了一部分 / 覆盖根本没到那个节点），写清是哪一种再动手。不要用"补一个数"的方式让两边恰好相等：那是第二次真相。
· **"两个数对不上"先问"数的是同一个东西吗"**（`D-1128-30` 的 Figure 1）：我们报 `19/28/24/18`，论文画 `f` 的"前 10 后 6"。查清后是 **`19/28/24/18` = 全程序四张图里所有 `Nxx` 节点之和、不含 `ARGS`/`EXIT` 记账行**，而 `f` 自己是 `9→10→8→5`。所以对比"我们的数"与"别人的数"之前，先给出**单位与范围**（哪几张图、算不算记账行、算节点还是边），否则争论的是两把不同的尺子。
· **原文与摘要的分工**：给外部（论文/决策者）的核对请求，交**工具原文**（`std::gir` dump、`GRAPH_STATS`、`cmp` 结果），不交转述、不交"等价"判断；有矛盾时**只摆数不下结论**，让有权判断的人判。"我们对了"与"我们数的范围和它不同"是两件事，读数一摆就分得开。
· **数字对得上、认领对不上**（`lane-block-hash`，`D-1128-30` 那一轮）：一个收集循环 `components=11 = states(3) + scans(8)` **完全自洽**，可是 8 路 scan 输出里只有 **1 路**被 `stateIsTheCollection` 认领（`fromScan=[S0,-,-]`），另外 7 路没有替换来源 ⇒ body 里那支 `:struct/append_arr` 落到普通降级。教训分两层：① **先问"每一路谁认领"，再问"数从哪来"** —— 总数自洽会让人以为没事；② 判断"某处的数是读来的还是减出来的"**直接看源码那一行**（`scans = loop.collectionCount()`），别靠行为推断——那个车道据此撤回了一句已写进交回的错判。
· **改法猜错不丢人，把错判留在账上才丢人**：交回后发现自己上一轮的结论被源码/读数否掉，**当轮明确写"我撤回那句"**并给出否掉它的那一行/那一条读数；协调者据此改卡。反例是把错判留在卡面上，下一个人照着错的改法做一遍。
· **两条不同的循环不要当成"同一结构两边读法不同"**：`loop22.tape`（payload collection=1，派生 tape 循环）与导出器这次发出来的 `collectionCount=8` 的循环**是两条**。看到"两边数字不一样"之前先确认**标的物是不是同一个**（图名/循环 id/节点 id 能对上才算同一个）。
· **枝名与会话名不是一回事**（`lane-block-hash` 那一轮）：它干的活叫 `block-hash`，可实际枝是 **`lane/cc-loop-forward`** —— 我在队列里按昵称写，合并位差点取空。规矩：**派工与排队只用 `git branch --list` 读到的真名并附 sha**；看到"没有这条枝"先 `git branch -a`/`ls-remote` 核，不要先怀疑合并位。
· **读数必须来自同一次"构建成功 + 运行"的 binary**（`lane-oper-shape-guard`，`D-1128-32` 之后）：两条读数报"访问违例可复现"，实际两次**构建都失败了**（一次是字符串里一个裸换行让 `c117` 失败）、跑的是**上一版 binary**；第三次 `build_exit=0` 才第一次跑新 binary，结果是 `exit=1` + 一行干净拒绝。规矩：交回里 `binary built` 与 `build_exit=0` 必须是**同一次运行**的；构建失败/未重建时跑出的任何读数一律作废，尤其宿主负载 40+ 时"顺手用旧 binary 跑一次"极难自己发现。
· **手工跑 `camel.exe` 之前先确认两件事**（`D-1128-33`）：① 这一次构建 `build_exit=0`；② 手上这颗 binary 确实是这次构建产出的（`node scripts/check-build-stamp.mjs` 或读 `out/latest/build-info`）。**`camel-remote.ps1` 这条路本身是安全的**：构建失败即 `Set-BuildPhase fail` 并停、不跑计划（`camel-remote.ps1` 里 "A plan runs only after a successful build"），`-NoBuild` 还带 H149 守卫（`out/<tag>` 与源码不一致就拒跑，除非显式 `-AllowStale`）；**出问题的是绕开工具、直接在 shell 里跑上一版 binary**。
· **合并位开批期间，协调者不推任何东西**（`D-1128-34`，2026-11-28 的一小时空转）：`merge-batch` 一批要"合 → 编译 → 读碰过的计划（7408 例约 346 s）→ 推"，**这一整段时间 `origin/integration` 必须静止**。只要有别的写者（协调者推文档/台账也算）在窗口里推一次，最后那步 `git push` 必然 `non-fast-forward`，而工具提示是"the integration tip may have moved, or a hook refused it; run the batch again from the new tip"——**读数白跑一次（6 分钟）**。那晚 04:27 之后四次尝试全部停在最后一秒，每批读数都是好的（`5324 case(s) read, 91 red, 0 new`），develop 因此四小时没动。
· **推送被拒时不要重读**：读数已经在手，`fetch` 后从新尖端**重合并**再推，别重跑 7408 例；同时**当轮**把"谁在窗口里推了"查清（`git ls-remote` + 协调者日志），否则下一批还是同样结局。
· **协调者的台账/文档提交要攒批**：攒到合并位报 `pushed yes` 之后再推一次（多笔一起），别每写一行就推 —— 一个"快"的写者会把合并位的时间预算全部吃掉。
· **合并方向由钩子管**（`D-1128-37` 之后的现场读数，2026-11-28）：`pre-push` 只认"**远端尖端为第一父**"的合并；在合并位的树里 `git merge origin/integration` 再推会被拒，原文 `pre-push: integration was merged into the seat instead of the lanes into integration; redo it with scripts/merge-batch.mjs, or git checkout -B tmp origin/integration; git merge --no-ff <lane>`。**正确做法**：`git checkout -B tmp origin/integration` → `git merge --no-ff <lane>` → 推；"远端只加了文档"的窗口由 `merge-batch` 自己重试吃掉，**不要手工 `merge origin/integration`**，也不要拿"第一父是我那笔带读数的合并"当规则 —— 那条只对"重试时把文档提交合进批"成立，手工反向合并会被钩子按方向拒。
· **写进 record / 判词 / 提交正文 / 消息里的 sha，一律 `git rev-parse` 取全，禁止凭 12 位前缀手写尾部**（2026-11-28，晋级 79 自曝）：用前缀手写的尾部可能**在仓库里不存在**，而本地一切看起来正常 —— 前缀是真的；错要到 `git push …:develop` 才炸（`bad object` / `needs force`），**而那一刻 record 已经是伪证**（它写着"已 ff develop"，实际没有）。处理次序也记下：**取回真 sha → 更正 record 的每个 sha 字段 → 把错误写进纠正提交正文（不偷偷改）→ 用一个真合并把状态修正到位**（不要 revert 那个错提交，那会把正确的 record 一起撤掉）。
· **`-FetchFile` 只跟"产生这次失败的那一次 `-Plan` 调用"配 `-FromThisRun`**（2026-11-28，`lane-editview-a`）：它要读的是**那一次运行**留下的失败文件；另起一次 `-Plan` 调用再 `-FetchFile -FromThisRun`，得到的是 `the run did not write …` —— 白跑一轮。要复核一次已经过去的失败，**重跑同一条 `-Plan` 并带 `-FetchFile`**，不是"再拉一次文件"。
· **探针用例不要放在会被同步的路径下**（2026-11-28，`lane-arrangeable` 补测事实存储）：它把 `mixed20/60/200/500.cml` scp 到宿主 `test/cases/probe/cap386/`（git 排除、不入库），`camel-remote` 包装器同步树时**把这个目录清掉了**，于是"连跑 42 次"得到 `HARNESS_42 ok=42 → 0/42`，**表现完全是"全崩"**；把用例重新 scp 回去、同一颗 binary 立刻 `42/42`。规矩：**探针与量具用例放在同步不到的独立根**（`/tmp/<name>` 或宿主上的专用目录，并写一份清单），**不要放在 `test/cases/` 下**；看到"一批用例同时全红"，先查**它们还在不在**（`ls` 原文），再怀疑语义 —— 这是"一个假抖动能洗掉一批真红"的反面：**一次全清也能伪造出一批崩溃**。
- **报任何行数都必须同时写量具。** 这个仓库里行数有三种量法，差不是一个常数：`wc -l` 是**物理行**（含空行与注释，论文里"库有多少行"用它）；`Get-Content | Measure-Object -Line` **只数非空行**（`int8.cml` 71 物理行 / 69 非空）；`(Get-Content $f).Count` 与 `wc -l` **只在文件末尾没有换行时差 1**（`std/cxx/opt/**` 8771 vs 8768，27 个文件里 3 个末尾无换行）。**改了旧数不要删旧值**：加一列旧值 + 一列旧量具 + 一列更正理由，并附一条硬读数证明不是内容变了（例如 `git diff --name-only <old> <new> -- src` 为空）。
- **计划级绿不等于整层绿，全树守卫要单独点名跑。** 合并工具按"被碰到的路径"选计划，所以只改 `test/`、`docs/` 的批次会得到**计划级**读数：它**不读** `docs.headers` 这类全树守卫。批里有 `internal/reports/**`、`scripts/**`、`test/tools/**` 的**新文件**时，必须点名跑 `docs.headers`（或直接对载体跑 `node scripts/headers.mjs --check`），否则缺版权行这种红会**在 integration 上留到下一次整层读**（`make-C-gen.py` 就是这么漏进来的）。报读数时也要写明这一批是计划级还是整层。
· **`merge-batch --read` 是整体覆盖，不是追加**（2026-11-28，`promo2` 上验「取消后成功合并」时第一次就栽在这）：覆盖时要把默认尾巴一起带上，默认读数是 `… -Plan {plans} -Tier fast -Jobs 8 -TokenClass funnel -NoGpuWatch -FetchFile summary.json -FromThisRun`。**少了 `-FetchFile summary.json -FromThisRun`，plan 跑完（exit 0）也不会把 summary 取回 seat，失败出现在读数之后**：`merge-batch: the plan run left no readable summary.json … ENOENT; nothing was pushed`（exit 2）—— **极易被误读成工具红**，实际是探针写残。这条尾巴同时正是「取消的 run 拒读」那条路的入口，所以覆盖 `--read` 时带上它，一次探针才同时走到这两条路。
· **换枝之后不能再用 `-NoBuild`，`internal/collab/**` 的冲突一律取 integration 那版**（`lane-oper-shape-guard` 复现 25 那一轮）：`git switch -c tmp-… <sha>` 之后 install 与树不匹配，`camel-remote.ps1 -NoBuild` 会按设计拒跑（"Re-run without -NoBuild … or pass `-AllowStale`"）—— 这是守卫做对了，**别拿 `-AllowStale` 绕过**，去掉 `-NoBuild` 重建（约 40 s）。随后 `git merge` 撞到的冲突若只在 `internal/collab/**`（卡、交接文档），**取 integration 那版继续**（`git checkout <base> -- internal/collab/` 再 `git commit --no-edit`），因为卡是文档、冲突没有语义；**冲突里出现 `src/`、`test/`、`test/plans/` 的文件才停**，`merge --abort` 报协调者。另：建临时枝前先 `git branch --list '<name>*'`（撞名会让 `switch -c` 整条失败而后续命令照样执行）。
· **`provenance` 里 `dirty` 非 0 的读数不算读数**（同一天，`lane-oper-shape-guard` 的 25 vs 26）：一次 `dirty: 4089 files` 的跑（临时枝建失败 + merge 失败留下的半合树）读出的 25 被误当成"内联器对执行宽度不可重入"，七跑（`-Jobs 1`×3、`8`×3、`4`×1）全是 26 才洗清。交回里每段贴完整 `[provenance]`（`tip`/`binary built`/`tier`/`dirty`），`dirty` 非 0 一律打回；临时枝建失败或 merge 失败之后**先 `git status` 核空再跑**。
· **改到内存与生命周期（facts / gc / 草稿与图的接手 / 槽位表）**：交回里**必须**带"同一程序连跑多次"的读数（形状照 `internal/collab/merge-seat.md` 里 `D-1128-42` 那一节：连跑 42 次、每笔提交各一张 0/42 vs 28/42、崩溃原文、规模阈值 n=20/60/200/500），**在工作站上跑，不上构建机**（构建机不跑高负载慢测试，合并闸门只跑快层）。**没有这份读数，合并位不收**。这条来自一次真事：一次 `18/18` 被当成"修好了"，同一程序连跑 42 次里有 **28 次崩**（`malloc_consolidate(): unaligned fastbin chunk detected`），而且红名每次不同。先上 **ASAN/gdb 拿到写越界那一行再改**；`-StackTrace` 在这条路径可能不出回溯。
· **证"没人调"要搜调用点，不搜拼写**：`git grep 'graph::commit' src/compile` 只证明"编译器没发这个名字"，证明不了"没人调它" —— 库里与例子里写的是 `commit(d)`/`abort(d)`（`src/stdlib/opt.cml:192,246,337`、`:341`、`examples/case-study/edits.cml:86,93`）。判"能删吗"要三段：**公开面在不在 / 谁在调（按调用点查、分别说编译器降级发不发、库与用例调不调）/ 删了会断什么**。这条来自一次真实的错：一条车道据"零调用"差点建议删 `commit`，而它被 `opt` 的三个遍与两个例子调用。**范围也要写死**：只搜 `src/`（或只搜 `src/compile`）同样会漏 —— 调用面 = `src/stdlib/**.cml` + `examples/**/*.cml` + `test/cases/**`（含 `.cml` 与 `.py` 用例）+ 需要时 `internal/` 的计划与说明。一句话记法：**"证'没人调'要搜调用点，不搜拼写；调用面也不止 `src/`。"**
· **`docs/design/**` 与 `docs/design-principles*` 写的是目标设计，不是现状**：文档对照代码时若发现两者不一致，**报给决策者，不把设计页改成现状**。现状写在 `docs/technical/**`、代码注释与提交正文里；设计页那句若与今天的代码不同，多半是"还没做到"，不是"写错了"（`D-1128-47`：有人把设计页第 90 行的 `Draft<F>` 按今天的 `commit_of(d, f)` 改了回来，方向恰好相反）。
· **改签名、改调用解析、改类型检查的车道，读数至少覆盖 `feat/compile` 与 `feat/modules` 两个目录**：只跑自己关心的计划集不够（`cc-loop-forward` 两次都栽在这：它的读数覆盖 `type_packs` + `feat/compile`，而 `feat/modules/graph_inline` 的 `a_call_through_a_value_reaches_what_its_type_declares` 被它弄红，第二次进合并批时才被合并位用单枝探针抓出来）。判据：两组目录各自贴 `[SUMMARY]` 与红名逐名，对 `newestRedTable()` 比，**任何新名逐个报**。
· **文档里引用符号要写 `path#Symbol`，不写 `path:line`**：`check-doc-links.mjs --citations` 把 `path:123` 算作"只给了行/行简写"并逐条报错（一次 36 条）。状态标注要指到**符号**（`src/stdlib/graph.cml#graph_of`、`src/stdlib/opt.cml#merge_hits`），行号一条都留不住 —— 行号还会随代码漂移。

· **ASAN 在构建机上能跑，配方必须写全（`-fPIC` 与运行期库路径）**（`lane-arrangeable`，2026-11-28，前三次失败的根因）：单独配一个诊断构建目录（不碰 `out/latest`、验收读数仍走 `camel-remote.ps1`）：
  ```
  $CM -S . -B /tmp/asan-<卡号> -G "Ninja Multi-Config" -DCMAKE_MAKE_PROGRAM=<ninja> \
    -DCMAKE_TOOLCHAIN_FILE=$PWD/build/conan_toolchain.cmake -DCMAKE_C_COMPILER=<clang> -DCMAKE_CXX_COMPILER=<clang++> \
    -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -fPIC" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -fPIC" \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address
  ```
  **`-fPIC` 三个字必须显式写**：用 `CMAKE_CXX_FLAGS` 覆盖时会把 CMake 原本追加的 PIC 冲掉，链接期报 `relocation R_X86_64_32S` —— 这被误当成"这台机器不能建 ASAN"三次。运行还需 `LD_LIBRARY_PATH=<工具链 lib>:<asan 构建的 Release 目录>`（否则 `GLIBCXX_3.4.36 not found`），`ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=0`。
· **ASAN 二进制下每一条 `camel …` 都可能先撞 CLI 的既有 UAF，别把它记成被测模块的账**（同一天）：`clipp::group` 先析构、`parsing_result` 还引用它去数未映射参数（`third_party/clipp/clipp.h:2750` / `config.cpp:1062`），`--version` 也崩 ⇒ 撞到它时先修 CLI（`config.cpp` 的生命期），再谈被测模块；判断口径是"**换一条与被测模块无关的命令是否同样崩**"。

· **闸门的拒绝理由，在它跑的那棵树、那个 ref 上才成立**（2026-11-28，`frontend.ast_record` 的 `--drop` 一格）：`check.py snapshot` 的"dropped line(s) still name a .cml file the repository has"来自 `behind`，而 `behind` 只由 `git cat-file -e <base>:<path>` 成功而来（`--base` 默认 `origin/develop`）。记录是在**宿主**上写的，宿主那棵树的 `origin/develop` 可能落后好几个小时 ⇒ 你在本地新 fetch 的 ref 上核出"没有"，闸门在宿主旧 ref 上核出"有"，**两边都对**。遇到这种"闸门说假话"的直觉，先问三句：**两边看的是同一个 ref 吗、是不是同一个 fetch 时刻、这条检查是在哪棵树跑的**；然后让**宿主先 `git fetch origin --prune`** 再跑，仍拒才用 `--base ''`（只看磁盘，工具自己的正当通路），**不要**为了过闸门去挪存档标签或改工具。

· **`camel-remote.ps1 -Check <名>` 是"树级检查"，与 plan 里的 `[[tests]]` 名不是一套**（2026-11-28）：`-Check` 的合法取值是 `diagnostics-catalog / camel-for-ai / check-doc-links / headers / plan-dir-markers / check-grammar / stdlib-reference / conformance / export-matrix / model-generators / rule-proofs / plan-debt / e9-full / gate-static / plan-fields / clang-syntax / ast-hashes / build-stamp / process-traces / program-holders / edge-kind-layer / effect-facts / operator-…`（以工具自己的报错清单为准）。`frontend.ast_record` 是**测试名**，拿它当 `-Check` 会 `CHECK_EXIT=2 unknown -Check name`。**AST 记录的正规入口 = `-Check ast-hashes`（+ `-UpdateAstHashes`）**，参考页 = `-Check stdlib-reference`。
· **重录 AST 之前先 `-Check build-stamp`**（同一条命令的前置断言）：它报"这棵树与现装二进制不同源"时**先修同源再录**。不同源就录，会把整份记录按别人的指纹重写一遍 —— 实例：宿主上用现成 `camel` 跑裸 `check.py snapshot --update`，得到 `re-hashed 4705 line(s), kept 0 recorded line(s) of the 4696`、`3839/3830` 行变化；正解是走 `camel-remote.ps1 -Path <这棵树>`（它自己构建并重打 stamp），变化应是**几行**。

· **整份指纹都变 ≠ "用了别人的二进制"：先看 `out/latest/build-info.json` 的 `revision`/`dirty`，再看**只读** `snapshot` 自己报几条**（2026-11-28，`lane/onnx-run` 的 `ast_hashes.txt`）：`revision` 等于当前 HEAD、`dirty:false` ⇒ 二进制与树同源；而不带 `--update` 的只读 `snapshot` 自己就打印 `3830 difference(s)` 与 `recorded file no longer exists: …` ⇒ **是这棵枝的金标整体过期**（历史遗留），不是本次重录弄坏的。判定顺序：① `build-info.json` 定同源 ② 只读 `snapshot` 定"差几条" ③ 只有"同源 + 差几条"才是本格该落的改动；差几百几千条 ⇒ **记成已知陈账，交记录所有者在新树（合并后）上整份重录**，不要手工删单行充数（删了那行也只解一条，其余仍红，留下一笔"看起来修了"的噪声）。

· **临时借补丁取读数（`cherry-pick -n <sha>` 后 `git restore`）必须在交回里点名**（2026-11-28，`lane/block-hash`）：借用的提交号、它**毒化出哪几条假红**、复现命令，三者缺一不可。实例：借 `616f65b436`（给函数类型的形参起名）绕开 `argNameAt` abort 时，会凭空造出 `modules.graph_inline.a_call_through_a_value_reaches_what_its_type_declares` 与 `compile.type_packs.gradient_type.printed` 两条红 —— 不点名，别人拿自己的子树去比同一张表就会把这两条当自己的甲。判据：**借补丁期间拿到的"甲"一律先怀疑补丁**，先在不借的树上单独跑该计划（实例里 `type_packs` 不借时 25/25 全绿）。

· **"临时借补丁"多半是基点落后，不是补丁没合**（2026-11-28，`lane/block-hash`）：借之前先 `git merge-base --is-ancestor <sha> HEAD`。非 0 ⇒ 你的枝不含它，**别借补丁跑整层**（借了会毒化读数：`616f65b436` 会造出 `modules.graph_inline.a_call_through_a_value_reaches_what_its_type_declares` 与 `compile.type_packs.gradient_type.printed` 两条假红）；非借不可时在交回里点名该提交号、两条假红原名、以及"不借时绿"的那次读数；更好的做法是把基点移到含它的 `origin/integration`。

· **借补丁会在树上留下暂存区残留**（2026-11-28）：`cherry-pick -n` 之后即使 `git restore` 了工作区，`git add` 过的内容仍可能在别人的 `git status` 里显示成"有人在改这个文件"——实例：`mm-capacity` 的 `staged.patch` 就是 `cherry-pick -n 616f65b436` 的残留，被当成了"第三个人的重复实现"。所以借补丁的正确收尾是**三件**：`git restore --staged` 撤暂存、`git restore` 撤工作区、`git status --porcelain` 必须为空；并把借用的提交号写进交回。

· **提速类派工要先读那个类型的契约再写改法**（2026-11-28）：我曾在 `task-401` 的卡面写"把 `nodesOfTopo()` 的表存进 `ExecPlan` 一份"，而 `src/kernel/include/camel/runtime/exec_plan.h:16-19`/`:165-168` 明确写着"plan 是值、不持有读者填的表；后端自己的状态放后端（`fvm/version_state.h` 按 plan 键）"⇒ 那个改法会造**第二份真相**、违反契约。正确形状永远是**读者侧缓存**（后端自己的状态，keyed by plan）。教训：写"缓存/共享一张表"这类改法前，先读该类型头文件里的契约段与既有样板（`fvm/version_state.h`），并把"收益上限"写清（例如"只有两个调用点、每次一张表的一次性复制，不在节点循环里"⇒ 上限是每次进入一次，不是每节点）。

· **浮点文本的参考实现是"最短可回读"，不是字面 `%g`**（2026-11-28）：判"打印对不对"时，参考是 `repr`/Ryu/`std::to_chars` shortest（设计原则 7.7 的可回读要求）；字面 C `%g` 会丢数字（`1.0→1`、`-0.0→-0`、`1.0+ulp→1`），拿它当参考等于要求破坏可回读。读数实例：Camel 与 Python `repr` 十值逐字相同、与字面 `%g` 有 4/10 不同 —— 那 4 处不是缺陷。

· **跑 `std::cpp` 生成的程序要两件事，缺一就误判成"桥接/导出坏了"**（2026-11-28实测）：① 链接行必须**显式写出 install 自带的 `$LIBSTD/libstdc++.so.6`**（`-L` 顺序提前**不够**，驱动的 `-lstdc++` 仍会挑编译器自带的旧版 ⇒ `GLIBCXX_3.4.35/.36` 未定义）；② 运行要给 `CAMEL_HOME=$TREE/out/latest`（或把二进制放到 install 的 `bin/` 旁），否则运行时从可执行文件目录找模块、抛 `camel::core::error::Diagnostic`。这两条都不是桥接代码或导出表的问题。

· **PowerShell 里 `git grep -A 5 pattern` 会把 `-A` 当成 revision 而失败**：写成 `git grep --after-context=5 pattern`（同理 `--before-context=`），或直接用 grep 工具。同类陷阱：`git log -S` 的 `-S` 正常，但任何单字母选项都可能被 PowerShell 抢参，长选项最稳。

## 11½. 两条失败例：假红来自行号，grep 来自跨行拼接（2026-11-28）

```
· **头注释的行数会移动金标行号，那条 entry 因此假红。**`test/plans/feat/modules/onnx.plan.toml:2310` 钉的是
  `expect_diagnostic_line = 22`（`:2311` 钉 `expect_diagnostic_column = 9`），指的是
  `test/cases/modules/onnx/export_loop_carried_array_field.cml:22` 的 `walk(x, { xs: [0], k: 0 })`。
  把文件头注释从 5 行写成 7 行（+2 行），那条调用就从第 22 行落到第 24 行（按钉子行 22 复算；那条车道当时的记录只写 "a longer header moved it"、未给数，故此处以可复算的 24 为准）—— entry 只差位置就整条红。
  **假红与真红的区别：真红是诊断变了（文本/诊断名/code 其一），假红是诊断一字未改、只有行号对不上。**
  规矩：改用例头注释前先看该 entry 有没有 `line`/`column` 钉子，改完数一遍行数再跑（`lane/loop-tape-values` 因此把头注释保持 5 行，提交原文 "The header keeps its five lines: the entry pins the located diagnostic at line 22, column 9, and a longer header moved it."）。
· **跨行拼接的字面量让行级 grep 说谎。**`src/stdlib/onnx/exporter.cpp:2088-2093` 那句拒绝是五个相邻字面量拼的，
  **整句**行级 grep 改前改后都是 0（`a recursive call changes argument 2 …: it is` base=0 head=0）⇒「搜不到」不等于「没改」。
  正确做法是按片段搜、给 before → after 的计数表：`whose length changes from` 1→0、`one round to the next (the body appends` 1→0、
  `shape is the same on every ` 2→0（提交正文自报 1、实测 2 —— 另一处在 `:1434` 的注释里），新片段 `this exporter does not yet carry an array` 0→7（`0ed8967a43` 前后，`git grep -c -F`）。
  自证的形状 = **片段 + 前后计数 + 一句「整句行级 grep 为 0 是因为跨行拼接」**；缺了最后一句，「搜不到」就会被当成「没改」。
```

## 读数要说实话：把计划范围写进结论（`D-1128-98`）

写「甲 0」时**必须带上你跑的范围**：`跑的是 <这些计划>，对 <哪张表>，甲 0`。只跑了子集就只对子集下结论 —— 把「我跑的这几层里没有表外红」写成「甲 0」，别人会当整层读，合并位一进整层就露。

整层读数是 `merge-batch` 的活，**不要**为了让交回好看而自己重跑 fast 层（构建机负载已经 30–50，那是把闸门的活复制 N 遍）。交回里写清：跑了什么、对哪张表、结论只覆盖这些。
