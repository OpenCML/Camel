# 踩过的坑（新坑往里加）

## 派工

| 坑 | 表现 | 正确做法 |
|---|---|---|
| **任务书是旧的** | 三次按任务书派工，agent 干到一半发现任务早已合入（H37、H39、H43 的 JIT 循环编译 `48345984`） | 派工前 `git log --oneline origin/develop --grep="Hxx"` + 看 `reports/Hxx*.md`；已合入就核实并记录，不重做 |
| 分支基线过期 | agent 基于两周前的 develop，量出的数字与现行代码不符（JIT 案例：分支里没有 48345984，于是"jit 和 fvm 一样慢"） | 派工时写明基线 `origin/develop <sha>`；收到结果先看它基于什么 |
| 提示不自包含 | 子代理看不到本会话，缺 `AGENTS.md`/设计原则就会自己发明规则 | 提示里给：读哪些文件、任务原文、主机命令、禁止项、报告要求、验收标准 |
| 相对路径写错树 | 有 agent 把改动写进了主检出 `D:\Projects\Camel`（两次） | 要求所有写入用绝对路径；发现后立刻 `git checkout --` 还原并核对 diff |
| **`[IO.File]::` 写进主检出**（2026-10-23，我自己犯的） | 命令以 `cd D:\Projects\Camel-wt\verify;` 开头，然后用 `[IO.File]::ReadAllText('test/plans/...')` 读、`WriteAllText` 写：**PowerShell 的 `cd` 不改 .NET 的当前目录**，.NET 用的是会话工作目录（主检出），于是读写的都是主检出那份文件，改动落到 `D:\Projects\Camel` 且两棵树各改了一半。当场表现为"写了但没生效"（`Get-Content`/`git diff` 走 PowerShell provider 看的是 verify，200 行没少） | `[IO.File]::`/`[IO.Path]::` 一律用**绝对路径**（`Join-Path (Get-Location) ...` 或写全）；批量改文件优先用编辑工具或 `-Path` 取绝对路径的 cmdlet（`Set-Content`/`Get-Content`），不要混用。收尾核对：`git -C D:\Projects\Camel status --porcelain` 必须为空 |
| **判据只给单文件目标，漏掉同目录的其它计划**（2026-11-03） | 合并闸门跑 `feat/compile/transforms`（单文件）得 80 = 77/2/1 全绿，但**目录**一跑出 4 红：`rules_angle_args.mean_axis_transpose.nvm/.fvm` 报 `known failure changed its mind`——它的 `known_fail_msg` 钉在旧墙（`reason=no-rule`），而新律让它走过那道墙、停在下一道 `skip=arity uri=tensor:mean_axis` | 改动**规则/律/诊断**时，判据要跑**容纳它们的目录**，不是那一支所在的那个 plan 文件：邻居计划的 `known_fail_msg` 与按目录采样的中毒副本都会跟着动。给判据时写"`feat/compile` 目录层 + 单文件各一份"，两份都要 |
| **车道对"另一支该不该合"的判断要自己核一遍**（2026-11-03） | 合并闸门说不合 `lane-docsync`，理由是"它缺 `sum_to`，合进来会删掉那一行"；实际 grep 是反的——`docsync` 有 `sum_to`、**缺 `may_be_rearranged`**。结论（不合）对，理由说错了 | 这类"要不要合"的判断**用 `git grep -c <符号> <ref> -- <文件>` 核一遍再落**，别只转述；理由写错和结论写错一样会误导下一个人（这已经是今晚第三次"转述别人的量法"出偏差） |
| **批量清理按"分支已合并"判活死**（2026-11-03） | 清理开工时 `lane-dedupe` 还是"已合并 + 13 处未提交改动"，清理过程中它自己提交了，于是**纯按"已合并"它会被当成尸体连树一起删**——那次是保护名单挡住的，不是规矩挡住的 | 判定基准必须**现取**：每删一棵之前对那棵树重新 `git status`，**有未提交改动本身就是活着的证据**；已合并但上方有未跟踪/未提交改动的树一律进保护名单。手动点名的保护名单不能替代这条 |
| **`git branch -d` 因上游过期拒删**（2026-11-03） | `not yet merged to refs/remotes/origin/<name>, even though it is merged to HEAD`——分支内容确实合了，但它的远端跟踪引用停在旧位置 | 正确解是先 `git branch -rd origin/<name>` 再 `-d`；**不值得为此动别的 ref 时就留着**（一条分支成本为零）。绝不为删干净去用 `-D` |
| **同一件事派给两个 owner**（2026-11-03，我自己犯的） | `-Plan` 选不到计划这件事同时派给了两条车道（一条先拿到活、另一条隔了几轮又拿到同一个题），两支改**同一个文件同一处行为**、各自写了 exit 2 与 `[PLAN ERROR]`、各自加了回归断言。处理成本远高于当时省的那点判断时间 | **一个操作只能有一个 owner**（历史代价是两次 `git reset` 打掉一个提交）。派工前扫一眼"手上有没有车道正在做同一处"；真撞了就**内容比对后留一支为基底、另一支只挑互补部分**，并在合并正文写清"同一件事的两份实现，最终只有一处"。重复实现留两份 = 以后改一处、另一处不响 |
| **reset 打掉在跑的 agent** | 建完工作树后统一 `git reset --hard develop` 对齐基线，其中一棵里 agent 已经在写，未提交改动被清空（H60，靠主机检出里的 patch 救回） | 先建树、对齐基线、**再**派工；已派工的工作树只用 `git fetch` 看，绝不 `reset`/`checkout`/`clean` |
| 工作树基线过期 | `git worktree add -b ... origin/develop` 取的是本地缓存的 `origin/develop`，落后于刚推的提交 | 建树前 `git fetch origin`；或建完 `git -C <wt> reset --hard develop`（仅在没派工时） |

## 构建与测试

| 坑 | 表现 | 正确做法 |
|---|---|---|
| **本机构建** | 0x50 PAGE_FAULT 蓝屏，日志/状态文件损坏 | 一次都别在本机 `cmake --build` / `npm run build` / `node scripts/test.js` |
| 并发太高 | `-Jobs 12` 下 `gpu_preference.no_device.*` 撞 10 s 超时假红，`nested_return.compile_time` 撞 5 s | 用 `-Jobs 8`；判断假红前先单独重跑该计划 |
| 互斥量混用 | 编译与测试共用一个锁会互相阻塞 | 两个独立互斥量：`Global\CamelBuildSerialized` / `Global\CamelTestSerialized`；`AbandonedMutexException` 来自 `WaitOne` 说明**已获得**锁 |
| **远程 wrapper 的两把锁**（2026-11-03） | 7 条车道同时压满主机时读数不可比；更隐蔽的是**同一棵树**上两条并发：第二条的构建覆写 `out/latest`，第一条的 plan 半途 `exit 1`（看起来像用例红了） | `camel-remote.ps1` 主机侧：按树的 `/tmp/camel-tree-<路径 cksum>.lock`（**锁 build+plan**，为的是同一棵树不互相覆写）；负载由令牌池管（原先的全局 plan 锁已撤）。等锁或等令牌超 30 分钟 ⇒ `exit 4`（**与构建/测试失败的退出码分开**），**等锁本身不算失败**。实测：两条并发长跑后者 `waited 271.6 s`；加树锁前同树并发的第二条会 `exit 1` |
| 状态写失败杀掉构建 | 面板读者占用目标文件时 `Move-Item` 抛错，构建被终止 | 重试 25×40 ms 后放弃；状态发布是**非致命**的 |
| 黄金文件过期 | 合入改了语法后 `frontend.ast_snapshot` 红 | 在**合并后的树**上、主机跑 `python3 test/tools/parsecheck/check.py snapshot --update`、`scp` 回来提交。**别让每个任务只提交自己那几行**：分支的黄金是从比它树更新的构建里记的，最后攒出 77 条互不相同的偏差（2026-10 一次全量刷新 2501 文件才对上） |
| 参考页过期 | `stdlib-reference.mjs --verify-ops` 红 | 标准库声明合并后同机跑 `--write`，把 `docs/stdlib-reference.md` 一起提交；`--verify-ops` 的算子数会随迁移上下变（343→341→365），别当常量 |
| **合并后用了 `-NoBuild`** | 合入了 C++ 改动（`src/`、`include/`、`modules/`）却复用旧二进制：9 条用例假红（`-I` 全红、`native.random_*` 文案不对）；反向也成立——H139 曾拿 `-NoBuild` 证明"只移动 id 的金标变化"，其实读的是旧 `out/latest/stdlib`，**证明本身无效** | H149 之后守卫会拦：构建写 `out/<tag>/build-info.json`（9 个构建相关根、1125+ 文件的**内容**指纹，跨机器一致），`-NoBuild` 不匹配则 **exit 4** 并打印 `built from:` / `this tree:` / `changed since the build:`；只改 `docs/` `test/` `scripts/` 静默放行，`-AllowStale` 是唯一显式覆盖 |
| **旧模块产物残留** | 模块改名（`sys`→`env` 等）后 `out/latest/stdlib` 里还有 `sys.cmo`，旧名**仍能解析**，`*_old_name_rejected` 假绿/假红 | 改名类任务收尾删掉旧 `.cmo`（`out/latest/stdlib`）再跑；不以增量目录的解析结果为准 |
| 计划里写了不存在的键 | `expect_stdout_exact` 被运行器静默忽略，用例"通过"其实没比较（H52 在修） | 用 `verify_script` 做逐字节断言 |
| **slow 层从没被扫过** | 所有闸门都是 `--tier fast`，且 tier 过滤对**目录**目标生效：2026-10-20 起没有任何闸门能看见 `debugger.dap_protocol`，它红了几个月才被发布提案顺手撞见；`gpu/**`、`perf/**` 不在任何闸门目标里，**102 个 GPU 用例此前一次都没跑过**（H154 首跑 83/83 全绿） | 发布前用**计划文件清单**跑 slow（H154 报告 §3 的列表）；**不要**用 `-Plan test/plans -Tier all`——它会把策略禁止的 200 个用例（perf、GC 压力/投毒、progen、PyTorch）一起拉进共享主机 |
| 主机没有 GPU 工具链（**已过期**） | 旧笔记说主机没 nvcc、GPU 用例跑不了 | 主机是 CUDA 12.3 + 8×3090，`gpu/tiles` 等能跑（H154 实测）；仍别跑压力/GC 类 |
| **不带负载的读数不可比**（2026-11-03） | 同一棵树、同一个二进制的自检：安静时 **12.3 s CPU**（7.50+4.78 user+sys），`load 42–46` 时 **80.6 s**（22.41+58.23）—— **差 6.5 倍，全是负载**；当晚因此花了半小时 bisect 一个"8.5 倍回归"，而它只是 7 条车道各 `-Jobs 8`（32 核机上 56 个任务）压满主机 | 报任何时间/CPU 读数**必须同时给 `uptime` 的三个 load**；`camel-remote.ps1` 现在自己打 `[load] … before run` / `after run`，并在 1m > 8 时打 `WARNING: … not comparable to quiet-host readings`（**只警告，不改退出码**）。判回归前先在安静机器上量锚点与 HEAD 各一次 |

| **完整路径指向"不含计划的目录"**（2026-11-03） | `-Plan test/plans/feat/docs_links`（该目录只有 `fixture/`，计划文件是**它的兄弟** `docs_links.plan.toml`）解析成 **0 个计划**；`test/plans` 下 **74 个目录里 51 个**不含任何 `*.plan.toml`（16 个根 + 35 个嵌套 fixture）。多目标时更坏：`-Plan test/plans/feat/docs_links,feat/docs_links` 曾 **exit 0 / total: 17**，0 计划那个目标凭空消失 | 目标是 `*.plan.toml` 或**含计划的目录**；短名（`feat/docs_links`）会先试 `${base}.plan.toml`，带 `test/plans/` 前缀的直接落到已存在目录、不走兄弟回退。已修：选不到计划 ⇒ **exit 2**、不打 `[SUMMARY]`、每目标一行 `[PLAN ERROR]` 写 **stdout** 并点名同级 plan 文件；每个目标**逐个结算**，不再只看总数 |
| **远程 helper 丢 plan 的 stderr**（2026-11-03） | 同一命令在主机上直跑能读到失败原因，经 `camel-remote.ps1` 就没那几行：0 计划那次丢 1 行 `[FAIL] this run selected no test at all…`；拼错路径丢 **5 行** `ENOENT … at walkPlans` | 主机侧 `$runPlan 2>&1`（在主机上就并进 stdout）。判任何"红得没理由"前先确认不是这条；新写的诊断尽量写 stdout |
| **形状不匹配 → 未初始化内存**（2026-11-03） | 同一晚两次：数组×标量转置 `d/ds = 6.5897671638891e-310`（应 6.0）；把下标律放宽到开秩后秩一 `tangent = 6.9084636812749e-310`（应 6.0）。`e-310` 是未初始化 double 的次正规数，**读两次可能不同**，比"确定的错数"更坏 | 见到 `~1e-310` 的十进制尾巴就当"类型/形状错了"而不是"算错了"。根因之一是写答案元组/lifted pair 用 `Fill`，而 `EdgeChecker::checkNode` 覆盖 `Copy/Gate/Cast/Join/Accs/Func`**漏了 `Fill`**（2026-11-03 已补，成员下标取 payload 的 `slots` 不是成员顺序） |
| **判据用例本身就是唯一证据** | 一条律里两个答案一个对一个错时，整条律不报错：`d/dx` 对、`d/ds` 悄悄变 0 | 新律至少两个方向的判据各一条；加用例后计划总数会变（`transforms` 80→81），**写出"差的就是这一条新用例"**，不要硬说"没变" |

## 传输

| 坑 | 表现 | 正确做法 |
|---|---|---|
| CRLF | `bash: line 27: exit: 0: numeric argument required` | 脚本作为文件发送，写文件前 `-replace "\r\n","\n"`，`UTF8Encoding($false)` |
| PowerShell 重定向 | `git diff > patch` 写出 UTF-16，主机上补丁无法应用 | `git diff --output=file`；写文件用 `[System.IO.File]::WriteAllText` |
| 管道进 `ssh bash -s` | 编码被管道改写，脚本损坏 | 传文件 + `ssh "bash /path/run.sh"` |
| 每次连接 ~0.35 s | 一次运行 5 次 ssh/scp 就是 2 s | 一次 `scp -r` 送整个暂存目录；暂存目录由主机脚本自己删 |

## 合并

| 坑 | 表现 | 正确做法 |
|---|---|---|
| 多人改同一张表 | `README.cn.md` 几乎必冲 | 先取"刚对账过"的那一版，再把另一侧真正新增的行补进去。稳妥做法：删掉冲突标记后按 `H\d+` 分组，同 ID **保留最长那行**（更长 = 更新过状态）；做完断言标记数 0、ID 无重复 |
| **"两边都留"留下的是旧行** | 某表里同一项被**修改**（不是新增）：并集式合并把 HEAD 的旧行与分支的新行都留下，于是 `re` 表项重复、`crypto` 与 `hash` 并存，工具去找已删除的 `signatures/re.cml` 而 ENOENT | 并集只适用于**新增**；同一 key 的**修改**必须取新弃旧。合并后跑一遍工具（`--verify-ops` 之类）就能暴露 |
| 代码两边各加 case | JIT 后端冲突 | 保留双方，各自补 `break` |
| 合并后没跑闸门就推 | 一个语法变化就让 AST 金标红 | 合并 → 闸门 → 推送，顺序不能反 |
| **改了生成物以为是改对了**（2026-11-03） | `diagnostics` 的 `semantic.cpp` 是生成物：直接改它，下一次 `diagnostics-catalog.mjs` 一跑就被覆盖，改动消失且没有任何提示 | 新诊断走 `diagnostics/registry.tsv`（唯一事实源）→ `--write` → 用例+计划条目 → `camel-for-ai --write`，**`--write` 在计划条目之后跑**；判据是"改生成物之前先在 registry 里找到那一行" |
| **husky 的格式化钩子整篇重排**（2026-11-03） | 本机 pre-commit 的 `scripts/format.js` 与树里的格式不一致：一条"只删一行探针"的提交被放大成 **+93/−89**（对未改动的 `builder_expr.cpp` 也会重排 700 行），于是那条提交的读数不可信 | 提交前看一眼 `git show --stat`；被放大就 `git reset --soft` 重做并 `--no-verify`。凡是"只改一行"的提交出现整篇 diff，就是钩子在改你的东西 |
| **diff3 的第三个标记**（2026-11-03） | 只数 `<<<<<<<` 与 `>>>>>>>`，漏了 `\|\|\|\|\|\|\|`，`rulesets/transpose.cml` 里留一行裸标记 ⇒ 整个模块解析失败，门禁 **155 = 92 过 / 63 红** 全是它的下游 | 解冲突后立刻 `git grep -n '^<<<<<<<\|^=======\|^>>>>>>>\|^|||||||\|'` **四种子串一次扫完**；`feat/docs_links` 现已有 `docs.no_unfinished_merge_left_in_the_tree` 用例，解完冲突先跑它 |
| **两份生成文档各自漂**（2026-11-03） | `stdlib-reference.mjs --verify-doc` 只报一行"out of date"、**不列条目**，缺的是 `may_be_rearranged`；同一笔里 `camel-for-ai --write` 没跑，于是另一份 `docs/reference/camel-for-ai-stdlib-signatures.md` 继续漂 | 标准库声明一改，`--write` 与 `camel-for-ai --write` **两条都跑**；缺什么只能从重生成后的 diff 反推（实测 +18/−0 单块、无重排，`--write` 是安全的） |
| 摘要有构建内 id | `rename.*_graph_identity` 钉了整个 jgir 文档的 sha256，而节点的 `origin` 是随构建/文件变化的内部编号，十几个合并后摘要自己漂了 | 摘要前把这类**构建内部 id**（`origin`）剔掉，只留结构；否则测的是整棵树而不是改名。H139 顺手把 DOT 金标里的 `origin=` 归一成 `origin=<ORIGIN>`（仍断言"有 origin"），文本金标不再因无关文件而红 |
| **摘要里有绝对路径** | `identity.mjs` 靠 `input.case_path` 把检出路径换成占位符，而运行器传的是 `undefined`（`JSON.stringify` 直接丢键），于是**绝对路径进了摘要**：在 `verify` 里绿、在任何 agent 工作树里必红，三个人先后以为是自己的锅 | H143：payload 真的给了 `case_path`（`path.resolve(dirname(plan_path), test.case)`），脚本缺字段要**抛错**而不是哈希路径；证法是在**第二个路径的检出**上跑同一计划 |

## 资源

| 坑 | 表现 | 正确做法 |
|---|---|---|
| 工作树越攒越多 | 17 个、D: 只剩 41 GB | 合并完立刻 `worktree remove`，顺带 `-Clean` 主机侧 |
| 主机检出目录 | 每个带 130–220 MB 的 `build/` | 脚本自动删"本机已无对应工作树"的目录；`-Clean` 删单个 |
| 清 pip 缓存 | 主机上不了 pypi，清了就装不上包 | 只 `pip cache purge`（安全，~4 GB），别 `rm -rf ~/.cache/pip` |

## 协作

| 坑 | 表现 | 正确做法 |
|---|---|---|
| 空等 | 问"你在等谁" | 我是审查者：没有 agent 在跑就补派，收到结果就审、合、推 |
| 只转发 agent 自述 | 报告说绿、实际没跑 | 看 diff、看数字、必要时自己重跑一个计划 |
| 任务书自己烂掉 | 56 行状态与代码不符 | 定期对账（H53 做过一次），并保留"派工前核对"一节 |
| git 历史 | 用户的硬要求 | 只提交有意义的整组改动；不提交本机限制（测试规则包装）、不提交草稿式的半成品 |
## 协调者自己

| 坑 | 表现 | 正确做法 |
|---|---|---|
| **向用户提问**（2026-11-01，代价半天） | 一条措辞冲突本可写成一页交给作者，却用 `ask_user_question` 问用户；用户不在，整轮停在等回复 | **永远不问用户**：能靠读数回答的自己答，答不了写 `docs/handover/questions/` 一页一问，写死默认建议，**同一轮按默认开工**。见 `SKILL.md` §0 |
| 把约定写进 `AGENTS.md` | 试过在仓库 `AGENTS.md` 加协调规则，用户明确否掉：**约定属于 skill，不属于仓库** | 约定写 `SKILL.md` §0；仓库文档只写语言与工程规则 |
| 全局禁用工具 | 为了让某个会话不问，改了 profile 的 `cordis.patch.yml` 把 `dsh-tool-ask-user` 关掉——**连累了其他会话** | 不要动共享 profile；靠 skill 铁律 + 定时提醒。真要按会话生效，只改该会话自己的东西 |

| **提交钩子会静默重排版**（2026-11-04，一夜两次） | 本机 `clang-format` 是 **22.1.0-rc3**（`G:\DevEnvs\LLVM\bin`），**不是**仓库树格式化时用的版本；`scripts/precommit-staged.js` 用 PATH 上的 `clang-format` 跑 `scripts/format.js`，于是**任何一次提交都会把车道的代码重排**——有一次合并把 `gradient.cpp` 改写了约 200 行，看上去像代码改动 | ①**合并提交一律 `--no-verify`**（车道分支的字节必须原样进来），或先钉死 formatter 版本；②核对合并是否只是重排：`git diff -w <lane>:<file> HEAD:<file>`，**非零 = 重排，不是内容**；③汇报 diff 行数前先跑一次这个判断 |
| **`--citations` 跑一小时，不是 `.venv` 的错**（2026-11-04） | 在**开发检出**上跑 `check-doc-links.mjs --citations` 会跑 1 小时级（实测 2,852 s CPU 仍在走、无输出），一直被当成"挂住"。真正的主因是 `vendor/` 里第三方头文件让 symbol/line 两个正则**灾难性回溯**：`nlohmann/detail/meta/type_traits.hpp` 单文件 208 s + 206 s、`taskflow/dsl/task_dsl.hpp` 83 s + 83 s，**全部 0 匹配**，profile 里两个正则占 99.5% 采样。`.venv`(29,926 文件) + `.cache`(15,481) 只让它更慢 | 量引用一律 `git archive HEAD` 到 `D:\Temp\` 再 `--citations --root <干净树>`（2.9 s）。`vendor/` 还要跳过：它被 git 忽略，且引用的是**别人项目自己的路径**，会报 2 条假红 |
| **本机 `out/latest/bin` 与树不同版**（2026-11-04） | 本机二进制停在 `2026-11-02 23:09`，而 `develop` 之后合了六支改源码的车道；拿它跑 `feat/compile` 得 `2238 = 2110/128 + 1 timeout`，**128 条红全是陈旧二进制的噪音**。同一棵树走主机闸门是 `2238 = 2235/2/1` | 闸门**只认主机读数**；本机跑出来一片红时，先看 `out/latest/bin` 的构建时间与 `git log -1 --format=%ci` 的树时间差，别去查那 128 条红。provenance 里有 `binary built …` 行，看它是不是每次重建递增 |
| **`[System.IO.File]::WriteAllText` 的相对路径会写进主检出**（2026-11-04） | .NET 用的是**进程工作目录**，不是 PowerShell 的 `cd`（`Set-Location` 只改 Provider 位置）。一条车道因此把 `scripts/headers.mjs` 写进了 `D:\Projects\Camel` | 写文件**一律绝对路径**；`[IO.File]::*` / `Out-File` / `Set-Content` 都按这条办。被写脏了先 `git -C <主检出> checkout -- <文件>` 复原，再在工作树里重写 |
| **位置参数会把裸 token 静默吃成设置**（2026-11-04） | `camel-remote.ps1 -Path <wt> smoke` 里那个 `smoke` 会被绑到 `LocalConfig`——**一次看起来跑对了、其实读了另一个设置文件的运行** | 脚本加 `[CmdletBinding(PositionalBinding = $false)]`：按名绑定，裸 token 直接由 PowerShell 报"没有位置参数接受它"。**读起来对的运行比报错的运行危险** |
| **用 PowerShell 拼文档/代码字符串**（2026-11-03） | 双引号里写反引号包着的标识符时，反引号被当成转义（\b \f \t），标识符被吃掉几个字母（int→int、bool→ool、float→loat），肉眼几乎看不出来 | 用 `edit` 工具或单引号 here-string；写完用 `[regex]::Matches($t,[char]8).Count` 查一遍控制字符 | 含 sha/引号的句子一律用 `edit` 工具或单引号字符串（双引号里 `` `e `` 是 ESC，`e83fabea2` 会被吃成 `83fabea2`）。 还有：**给"不精确"写过理由 ≠ 不精确是对的** —— 凡是决定保留一个不精确的东西并给它写了理由，都当红灯看（2026-11-03 作者一句"a declaration is the contract"推翻一轮口径）。
