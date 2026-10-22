# 交接任务书

> **看板读法（H153，2026-10-22）**：本表按编号升序；一条行只取一个判定，顺序 open > stale > partly > done。**done** = 报告已合并且验收无新增失败（H18 已作废，按关闭计），**partly** = 报告在但仍有未做项，**stale** = 行文已被后续合并推翻（本轮按报告改正，改动处带 `（H153 校正，依据 Hxx）`；只表示行文，不表示任务已完成），**open** = 未合入/未开工；以 `docs/handover/reports/` 的报告为准，本表只是索引。
> **H153 核对计数（135 行，含 H153）**：done 117 / partly 9 / stale 6 / open 3——stale 的 6 行（H6、H19、H38、H44、H44b、H147）已全部改正；partly 里 H51 另有一处笔误，H114 的行文也补了摘要来历（它本身是 done）。
> **H160 后计数（138 行）**：done 120 / partly 9 / stale 6 / open 3；H160 只加一行、改 H38 行文，没有动别的判定。

> **2026-10-19：设计原则已按主题重编**（15 节，只写当前有效的规则）。旧的编号（如 2.13、2.20）与原文见 [裁定记录](../design-principles-log.cn.md) 开头的对照表；本任务书中的引用已改为新编号。

本文件写给接手开发的 AI。读完就开工，不必再问"要做什么"。每个任务完成后把结论写进约定的位置（第 3 节），由协调者（与语言作者讨论设计的 AI）检查代码；检查意见也写在约定的位置。

**判断标准不是"能运行、测试通过"，而是"符合 Camel 的长期设计"。** 能跑但不优雅、绕开通用机制、按名字写特例的实现会被退回。第 2 节列出了退回最多的几类问题，提交前逐条自查。

更新：2026-10-22。

---

## 0. 开工前必读（按顺序）

1. `AGENTS.md`：构建、测试、日志、遍的用法。仓库根目录。
2. `agents/test.md`、`agents/commit.md`、`agents/refactor.md`。`agents/branching.md` 讲的是维护者的发布流程，**你不做发布，不碰 `main`、`develop`**，分支规则以本文件第 1 节为准。
3. **`docs/design-principles.cn.md`**：规范性文件，与代码、测试、其他文档冲突时以它为准。至少读第 1–3 节、第 5–13 节、第 14 节（实际发生过的误读）、第 15 节（尚未决定的问题）。
4. `docs/roadmap.cn.md`：当前顺序、谁在做什么。
5. 本次任务相关的设计稿与审查报告（各任务里写明）。审查报告在 `docs/reviews/2026-10-19/`，每条有位置、现状引文和"优雅的形式"。

不确定某个词、某条规则的意思时，先在设计原则里搜；搜不到再按第 4 节提问，不要猜。

---

## 1. 工作流程

### 1.1 分支

- 从最新的 `origin/fsh-dev` 开分支，名字是 `handover/<任务编号>`，如 `handover/H1-enum`。
- **只推送你自己的 `handover/...` 分支**：`git push origin handover/H1-enum`。不推送、不修改 `fsh-dev`、`develop`、`main` 或别人的分支；不强制推送（`--force`）；不改写已推送的历史。
- 需要同步时，把 `origin/fsh-dev` **合并**进你的分支（`git merge origin/fsh-dev`，合并提交），不要变基已推送的分支。
- 一个任务一个分支。任务很大时可以分几次推送，每次都能编译、相关测试通过。
- 协调者检查通过后，由协调者把你的分支合并进 `fsh-dev`。你不需要也不应该自己合并。

### 1.2 构建与测试

- 构建：`npm run build`（Windows PowerShell，命令之间用 `;`，不用 `&&`）。产物在 `out/latest/bin/`。
- 测试：`node scripts/test.js <计划文件或目录>`。测试分两层（H10）：
  - **日常开发跑 `npm run test`**：只跑 `fast` 层（`smoke` 加 `feat/**` 的快速测试），按处理器核数并行（`--jobs N` 可改，`--jobs 1` 为串行）。改动定下来后可用 `node scripts/test.js --changed`，按 `test/plan-selection.json` 只跑相关计划。
  - **合入 `develop` 前跑一次 `npm run test:all`**：全部计划、全部层，包括与 PyTorch 的对比、GC 压力与投毒扫描、随机程序检查和性能基准。
  - 只跑某个目标时：**目录**目标默认只跑 `fast` 层，要看慢测试加 `--tier all`；直接点名**计划文件**则整个计划都跑（点名就是意图）。`--changed` 的基线默认 `origin/develop`，可用 `CAMEL_TEST_BASE` 覆盖。分层规则见 `docs/test-plans.md`。
- **先手动跑，再写测试**（`AGENTS.md`）：新功能先用 `camel x.cml` 在命令行运行，看清输出与诊断，再把确认过的行为写成测试。
- 工作中只跑相关的测试计划；**任务结束前跑一次 `npm run test:all`**，把失败的测试名与基线比较。
- **基线**：2026-10-19 的 `fsh-dev`（5bc29823）全套 5646 项中有 19 项失败，名单如下。这些不是你造成的，不需要你修；但你的分支不能新增失败。

  ```
  grammar.reference_in_step  frontend.ast_snapshot  frontend.format_keeps_ast
  frontend.fuzz_lossless_tree  catalog.doc_is_generated  graph.default
  integrated.graph.false  integrated.graph.true  failed_no_rule.error
  pass_export_refusal_var_param  pass_export_refusal_located.request_in_arm
  pass_export_refusal_located.request_in_arm_json  poison:rewrite_opt
  poison:failed_no_rule.error  registry.signatures_match  rewrite_opt
  04_staticness  05_effect_order  portability.std_includes
  ```

  其中几项（AST 快照、诊断目录、签名一致性）会被正在进行的地基工作修掉；合并 `origin/fsh-dev` 后基线可能变短，以合并后的为准。
- 改了语法或 `.cml` 测试文件：更新 AST 快照 `python3 test/tools/parsecheck/check.py snapshot --update`。
- 改了诊断：`node scripts/diagnostics-catalog.mjs --write`。
- 每次提交前：`node scripts/check-doc-links.mjs --all`、`node scripts/check-std-includes.mjs`。

### 1.3 提交

- 小步提交，每个提交说清楚它做了什么、为什么（`agents/commit.md`）。
- 提交信息用英文，与仓库现有风格一致（`feat(...)`、`fix(...)`、`refactor(...)`、`docs: ...`）。

### 1.4 文档

改了用户可见的行为，就同步改文档：语言参考 `docs/language/`（中文与 `en/` 两份）、`docs/stdlib-reference.md`、相关设计稿。**不要改设计原则的内容**；发现设计原则与实现必须不同，按第 4 节提问。

---

## 2. 提交前的自查清单（退回最多的问题）

这些都是 2026-10-19 审查中实际发现的问题，每一类都有真实例子。

1. **核心按名字认具体的东西。** 编译器、核心、遍里出现 `if (name == "numeric")`、`uri == "tree:tree_map"`、写死的算子/模块/类型名单。反例：`@numeric(...)` 在构建语法树时比较字符串；参数树操作在建图器里按 URI 展开。**正确做法**：用已有的通用机制（算子表与它的特性、语义声明、资源声明、装饰器、模块注册的接口），按声明的性质判断，不按名字。
2. **另造一套相似的机制。** 已有装饰器却另做一种 `@name(...)` 标注；已有原生子图调用服务，却在每个后端各写一遍高阶算子。**正确做法**：先找现有机制能否承载；不够就扩展它，不要平行再造。
3. **同一事实写在多处。** 效应既看资源声明又看签名里的 `var` 又按 URI 前缀猜；"什么是叶子"写了七份且已不一致；算子签名写在三处。**正确做法**：一个事实只有一个来源，其余处读它或由它生成。
4. **写死的上限与封闭的枚举。** 8 位的位掩码已经用满、写死的常量（应当可调的上限）。**正确做法**：可以增长的东西不用固定位宽；上限做成可配置。
5. **临时裁定语义。** 设计原则没写的行为，凭感觉定一个。**正确做法**：按第 4 节提问。
6. **为了让测试通过而改。** 改期望输出掩盖行为变化、跳过测试。**正确做法**：期望输出的每处变化都要能解释，并写进报告。
7. **兼容层与过渡方案。** 语言作者明确不要"乱七八糟的兼容"。旧写法要删就直接删干净：不留别名、过渡层，也不加"已退役，请改用 X"之类的诊断或说明；删除后它就是普通的未知名字或语法错误。
8. **只做一个后端。** Camel 有 `std::nvm`、`std::fvm`、`std::jit`、并行调度器 `std::prl`、宏求值器 `std::macro`、C++ 导出 `std::cpp`，还有 ONNX 导出与自动微分。新的值或节点要在每一处都能运行，或者在不支持的地方给出带位置的明确拒绝。测试要覆盖多个后端（看现有测试计划如何对多个调度器跑同一程序）。

---

## 3. 结论放在哪里

- **你的报告**：在你的分支上写 `docs/handover/reports/<任务编号>.md`，随最后一次推送一起提交。模板：

  ```markdown
  # <任务编号> 报告
  分支：handover/<任务编号>，最后提交：<hash>
  ## 做了什么
  （按提交列出，每条一两句）
  ## 设计上的选择
  （每个技术选择：选了什么、为什么、考虑过的其他做法）
  ## 用户可见的变化
  （每条配一个前后对比的例子；期望输出改了哪些、为什么）
  ## 测试
  全套：总数 / 通过 / 失败；新增失败（应为零）；新增测试列表；覆盖了哪些后端
  ## 自查（第 2 节逐条）
  ## 待决问题
  （第 4 节格式）
  ## 没做完的
  ```

- **协调者的检查意见**：写在 `fsh-dev` 上的 `docs/handover/reviews/<任务编号>.md`。开始下一步前先 `git fetch` 看有没有新的检查意见；有"退回"条目时先改完再继续。
- 任务状态以本文件第 5 节的表为准，由协调者更新。

---

## 4. 遇到不确定的语义怎么办

- **语义问题**（用户能看到的行为、语法、类型规则、错误的种类）不要自己定。写进报告的"待决问题"，格式：

  ```markdown
  ### Q<n>：一句话的问题
  例子：（一段 Camel 代码，以及两三种可能的结果）
  建议：（你倾向哪个、为什么）
  影响：（不定下来会挡住什么）
  ```

  然后**跳过被挡住的那部分**，继续做不受影响的部分。
- **技术选择**（数据结构、文件组织、算法）可以自己定，写进报告的"设计上的选择"。
- 判断一件事是语义还是技术：如果换一种做法，用户写的同一个程序会得到不同的结果、不同的诊断或不同的语法，就是语义。

---

## 5. 任务

### 角色名（2026-10-20）

本文档中的**"协调者"只指 `fsh-dev` 一方**（语言作者的设计伙伴）。施工方内部负责分派与合并的主代理称**"施工负责人"**。施工负责人可以决定实现层面的细节，写进报告并注明"施工负责人"；**设计原则与 `design-principles-log` 只由协调者写**——语义与写法上的待决问题放在报告的"待决问题"里，等协调者在 `reviews/` 中答复。

### H21 第 3 项撤回（2026-10-20）

语言作者裁定回到原设计（设计原则 4.4、9.9 已恢复）：**权重是 `with` 参数，`grad`/`with_grad` 对第一个 `with` 参数求导**，其余 `with` 参数当作常量；普通参数是数据。由此：

1. 撤回 `f59daa72` 的语义：`with_grad(loss)<m>(x, y)` 对 `m` 求导；拒绝条件改为"第一个 `with` 参数是 `var`、没有切向量，或函数没有 `with` 参数"（诊断 `NotDifferentiable`）。
2. 已改成 `loss(m, x, y)` 的示例、测试、库代码改回 `with` 写法；**benchmarks 不必改**（它们本来就是 `with` 写法）。
3. C2 示例保持 `with <m: Mlp>`；H21 第 4 项重测时按此。
4. 以 H25 记录；放在丁车道最前。
5. **改名与推荐写法**：`value_and_grad` 改名为 **`with_grad`**（返回"值连同梯度"），旧名直接删除（不留别名或诊断）；`grad` 不变。全仓库（库、测试、示例、benchmarks、文档）一并改。推荐写法：循环外 `let loss_with_grad = with_grad(loss)`，循环内 `let (l, g) = loss_with_grad<m>(x, y)`；示例按此风格，H19 的《标准库约定》收录。

### 丙车道（图存储）的进展与对其他车道的影响（2026-10-21）

协调者一方已合入 fsh-dev：32 位编号与容量上限删除、`GraphId` 与程序快照 `Program`（遍的签名为 `apply(const Program*) -> const Program*`）、`stableId`/`lineage`/`serial` 删除、冻结图分块（`GCNodeChunk`）、草稿读者列表与 O(1) 删除、`std::opt` 的筛选与内联器提速。即将合入：图引用改用 `GraphId`、统一的执行计划（`ExecPlan`，图上的扩展槽删除）、墓碑与块共用。

**协调者一方将修改 `src/core/mm/gen.cpp` 的全量回收路径**（图空间改由 GC 标记与清扫，快照与图版本由 GC 管理）：乙车道暂不改动 `gen.cpp` 中与图空间、全量回收相关的部分；数据块记账与延迟释放队列保持不变。

**下一步是控制区域**（设计稿第 5 步）：`if`/`match` 的臂与 `LOOP` 的条件、循环体改为同一张图内共用帧的区域，前端的分支与循环建图（`builder_control.cpp`）、`loop_lowering.cpp`、`branch_arms.cpp` 将被重写或删除。**甲车道在 `builder_control.cpp` 与分支、循环相关的建图代码上只做必要的小改动，并频繁合入 develop**；需要较大改动时先在报告中提出。

### 5.0 调度

任务之间的并行与先后由施工负责人自行安排（可同时调度多个子代理）；任务书只写依赖的硬前提。下文各任务中"某车道"的字样只是历史标记，不约束调度。

**测试**：日常开发只跑快速层与相关的计划，不频繁跑 slow 层（GC 压力、投毒扫描、PyTorch 对照、性能基准）——它们拖慢开发。slow 层只在发布版本之前跑一次，或者在任务确实改动了 GC、内存管理、数值内核时跑与之相关的那几个计划。

### 派工前核对

- 状态列是任务是否开放的唯一依据；任务正文怎么写不作数。
- 派工前先核对 `git log --oneline origin/develop --grep="<id>"` 与 `docs/handover/reports/<id>*.md`；两者都没有，才是未开工。
- 某行写 **已完成**、正文却仍像派工书时，先按证据改正状态列或正文，不得直接派工。

| 编号 | 任务 | 前提 | 状态 |
|---|---|---|---|---|
|---|---|---|---|
| H1 | `enum` | 无 | **已完成**（检查意见 reviews/H1.md） |
| H2 | 标签机制与数值许可改为标签 | 无 | **已完成**（第 1–5、7、8 项完成：第 1–3、7、8 项在 `856af859`，第 4 项 `7b4147c1`（许可是标签、默认按 7.3、`#strict` 经 `effectivePermissions` 清空全部许可、删除 `@numeric(...)`），第 5 项在本分支（`@vjp` 改为 `#vjp<rule>` 声明标签、规则是图引用 `kGraphRefRule`、删除宏求值与 `attachRule`）；第 6 项已完成（`#macro` 见 reports/H97.md，删除 `inner`/`outer`/`atomic`/`shared` 见 reports/H107.md）；报告 reports/H77.md） |
| H3 | 依赖节点属性的后续 | 图存储第二版第 5 步 | **部分完成**（H3a 完成，见 H102 行与 reports/H102.md；H3b 未做） |
| H4 | 已裁定未开工的小项 | 各小项另注 | **已完成**（H4a `37e447a6`、H4b `0581d473`、H4c/H4f `e6d20306`、H4d `45bbdc14`+`638b6893`、H4e `1ad52472`；报告 reports/H4.md） |
| H5 | 命名类型 | H1 | **已完成**（`afe8ab45` 检查通过；H5b `6e54035c`、H5c `ebb57244`、H5d `2e6dcb22`；报告 reports/H5.md） |
| H6 | 地基遗留 | 无 | **部分完成**（第 3、4、6、7 项已完成 `e954fe90`；第 5 项由 H80 与 H101 完成（fast/slow/perf/gpu/GC/poison 全层），见 reports/H80.md、reports/H101.md，剩余红当时记为 H98/H102/H103；H102 已完成（reports/H102.md）、H103 已完成调查（reports/H103.md，迁移留给 H51）、H98 仍是待裁定的草稿（reports/H95.md §6）（H153 校正，依据 H102、H103）；第 1 项的前端四条与 `TypeCode::Byte` 由 H88 完成（`from_bytes::<T>` 与 `load_tree` 仍等泛型），见 reports/H88.md；第 2 项由 H85 完成 `:graph/original`、traits、`#independent`，控制边约简由 H95 收到 `std::opt::ctrl`（reports/H95.md；H153 校正，依据 H85、H95）；报告 reports/H6.md） |
| H7 | 资源与副作用 | H2 | **未开始** |
| H8 | 基础与清理工作 | 无 | **已完成**（第 1、2、4–7 项已合入 `927cdcb0`；第 3 项在 `handover/H68-operid`：`12cbd9eb`、`bc2cf5ea`、`3a2669f9`；报告 reports/H8.md、reports/H68.md） |
| H9 | 缺陷修复与平台健壮性 | 无 | **已完成**（第 1–4 项已合入；报告 reports/H9.md） |
| H10 | 测试提速 | H9 第 4 项 | **已完成**（`68933cc2`、`318c4d65`；fast 层的 2 分钟目标未在空闲 8 核机验证；报告 reports/H10.md） |
| H11 | 健壮性与小缺陷 | 无 | **已完成**（第 1–4 项已合入；报告 reports/H11.md） |
| H12 | 拆分建图器（纯搬移） | H11 之后 | **已完成**（10 个纯搬移提交；报告 reports/H12.md） |
| H13 | 官方示例第一批 | 无 | **部分完成**（C1、C2、C6、D5 已入 `40f46bda`、`707d8a12`；D4 未做；报告 reports/H13.md） |
| H14 | 反引号字符串插值 | 无 | **已完成**（`2f4e413b`；报告 reports/H14.md） |
| H15 | 复数与字面量按上下文取类型 | 无 | **已完成**（字面量 `fa67a454`；复数 `92ea1d60` 与 H15b–H15d；报告 reports/H15.md） |
| H16 | 一批小项（Json、管道实参、enum 作键、调试构建、快照核对等） | 无 | **已完成**（第 1–5 项 `e6be4d68`、`8447cc9a` 与 H16b；第 6、7 项在 `handover/H66-h16-rest`；报告 reports/H16.md、reports/H16b.md、reports/H66.md） |
| H17 | 原生多维数组与缓冲区（先交计划） | H16 | **部分完成**（第 1 项基本落地、第 3 项部分；第 2、4 项未开始；报告 reports/H17.md、reports/H17b.md） |
| H18 | 图存储第 3 步：索引与增量提交 | — | **已作废**（收回，由协调者一方与核心重构一起实施，2026-10-20） |
| H19 | 标准库重新规划的落地（先交计划） | H16 | **部分完成（步骤 0–1 已入；第 10 步 13 个模块已声明化；第 3、5、6 步与 `env`/`process`/`hash` 的声明化未做）**（`handover/H76-stdlib-step1`，报告 reports/H76.md）：约定页 `docs/stdlib-conventions.cn.md`；机制与 `bytes` 试点（C++ 不再写签名，`stdlib/bytes.cml` 是唯一来源）。**第 10 步的模块批次已开始**：H104 完成 `str`（报告 reports/H104.md），`bytes` 之后的第二个模块；H115 完成 `json`（报告 reports/H115.md），10 个名字 35 条声明，`encode`/`decode_as` 两条计算签名留在 C++；H113 完成 `math`（报告 reports/H113.md），96 条固定签名 + 8 条 `// dynamic func`；H128 完成 `random`（报告 reports/H128.md），7 条固定签名，`choice`/`sample`/`shuffle` 三条计算签名留在 C++、参考条目用 `signatures/random.cml` 加 `computed`；H135 完成 `algo` 与 `statistics`（报告 reports/H135.md），`algo` 的 `argsort` 6 条声明、7 条计算签名留在 C++，`statistics` 5 个名字 20 条声明、删掉数值数组的伞形解析器；H146 完成 `para` 与 `this`（报告 reports/H146.md），`para` 的 `rendezvous` 1 条声明、`unordered_foreach`/`unordered_reduce` 两条计算签名留在 C++，`this` 的 `zen` 1 条声明、没有计算签名；第 2–9 步不是"仍未开始"：第 2 步的删除计划由 H116（`io` 重建）与 H146（`para`/`this` 保留并声明化）取代，第 4 步的改名由 H114 完成、`io` 由 H116 重建，第 8 步的 `str::bytes` 由 H122 删除，第 9 步的资源名由 H134 改完（`io:terminal` 与 `eprintln` 未做），第 11 步由 H127 收尾；仍未做的是第 3 步（`perf`+`sim`→`bench`）、第 5 步（`algo`→`array`）、第 6 步（`statistics`→`stats`）、第 7 步的错误约定，以及 `env`/`process`/`hash` 的声明化；13 个声明模块与 17 条计算签名的逐模块账本见 `### H19` 下的《H19 模块账本》（H153 校正，依据 H114、H116、H122、H127、H134、H135、H146）。**第 1 步的写法以批复为准，不是计划里的 `native` 写法**：按设计原则第 4 节，原生实现是模块里**无函数体**的 `export func name(...): T uses ...`，**不加任何 `native` 修饰**；它按「模块名:函数名」（`bytes:len`）绑定 C++ 算子，源码里不写 URI；约定外的名字才用标签（`#op<'bytes:raw_len'>`）作例外。同目录与 `.cmo` 同名的 `.cml` 是它的声明模块，声明是类型的唯一来源；泛型无体声明按类型实例化，图上是 OPER 不是 CALL |
| H20 | H13、H14 检查后的小项 | 无 | **已完成**（第 2–5 项已入 `bdf40b4e`、`3750aefc`、`2398e598`；第 1 项在 `handover/H78-interpolation-tidy`；报告 reports/H20.md、reports/H78.md） |
| H21 | 下标与切片、lambda 参数推断、对第一个参数求导 | H15（下标部分与 H17 协调） | **部分完成**（第 1 项前半由 H83 完成：语法、类型、rank-1 的切片与 `t[idx]`、`last`、格式化器、语言服务器、文档；多维切片与步长不为 1 的视图等 H17。第 2 项由 H92 完成（形参类型与省略的返回类型从所在位置的函数类型推出；报告 reports/H92.md），泛型高阶函数（`map`/`tree_reduce`）的推断记为待决；第 3 项已撤回并按 H25 完成 `16136a3a`；报告 reports/H21.md、reports/H83.md） |
| H22 | 循环与求导的跟进 | 无 | **已完成**（`292c0479`、`d50794d5`；报告 reports/H22.md） |
| H23 | 路径赋值与递归结构体 | 无（原地部分随 H17） | **已完成**（第 1–4 项；第 2 项的经数组递归在 `handover/H23b-recursive-array`；报告 reports/H23b.md） |
| H24 | 字面量取类型的其余位置 | 无 | **已完成**（`aa71a2c6`；报告 reports/H24.md） |
| H25 | 撤回 H21 第 3 项：对第一个 `with` 参数求导 | 无 | **已完成**（`16136a3a`；报告 reports/H21.md 的 H25 一节） |
| H26 | 删除"已退役"类诊断 | 无 | **已完成**（`51c69903`；报告 reports/H26.md） |
| H27 | H24–H26 检查后的小项 | 无 | **已完成**（`2df02675`；报告 reports/H27.md） |
| H28 | 数据块：缓冲区提升为通用机制 | H17d | **已完成**（检查意见 reviews/H17-H22.md） |
| H29 | fvm/jit 上元组数组的 `reduce` 结果错误 | 无 | **已完成**（检查意见 reviews/H17-H22.md） |
| H30 | Debug 构建能链接，测试运行器增加 Debug 层，视图逃逸断言真正运行 | 无 | **已完成**（`2ec50cc4`；报告 reports/H30.md） |
| H31 | 诊断并修复 Debug 层的 `Accessing uninitialized slot`（经 `materializeCallResult`，9 项） | 无 | **已完成**（`22ba6649`；报告 reports/H31.md） |
| H33 | ONNX 导出的分支调用拆分不再产生输入环 | 协调者一方的 R2c（前端直接产生分支区域）合入后 | **已完成**（`db7d9345`；报告 reports/H33.md） |
| H35 | Windows 构建核对 `graph` 模块 C7–C9 新增的导出项（手写的修饰名，未在 Windows 上链接过） | 无 | **已完成**（`445ab37d`；报告 reports/H35.md） |
| H36 | Camel 图 API 的收尾（导入 Camel 库、遍参数、新值的根规则、区域读取等） | 无 | **部分完成**（第 1–4、6 项已合入 `134c522f`、`d33376fe`；第 5 项等 H19；报告 reports/H36.md、reports/H36b.md） |
| H37 | **缺陷（最优先）**：库程序（Camel 写的遍所在的模块）中模块级 `let` 的值读成 0 | 无 | **已完成**（修复 `5c40c184`；检查意见 reviews/H17-H22.md；报告 reports/H37.md） |
| H38 | 规则引擎的其余部分（`when`、`#requires`、分析、替换 `algebra.cpp`；先交计划） | H2（`#requires` 部分） | **部分完成**（第 1、2、3、6 项已完成：`when` `50954d05`、误差界与许可门控、`analysis` v1、`extract` 不再保留死写法的修复；第 4 项 **H100 删掉了除 `neutral_only` 模式以外的全部 C++ 定律**（`std::opt::algebra` 默认就是 `rules::simplify`/`rules::simplify_strict`，`algebra.cpp` 603→297 行），**文件本身保留**：只剩 `neutral_only` 的五条整数单位元（`x+0`、`0+x`、`x-0`、`x*1`、`1*x`）。H160 证明这五条定律早已在库里（`int_*` 18–22 行、`int32_*` 67–71 行，`check` 一直抽样验证），库缺的不是定律而是"只做单位元、不改图命名节点、不清理死常量"的模式；换成规则清单会改变 `neutral_only=true` 的图（值不变），所以五条是 (a) 有意保留（reports/H160.md）。H65 补了 int32/float32 元素定律与 f64 系数守卫（64 条），H87 又补张量定律与元素裸项、`safe_fp` 的 `strict` 参数（92 条）：元素语料与 `algebra_tensor.cml` 的 merged／merged_explicit／shared_product 等算符多重集，`check` 92/92；H87 §6 的"只差张量零定律与注册替代"按 H100 §3 更正：张量零定律两边都没有，不是删除对象；H153 的"还差五条整数单位元的规则模式"按 H160 更正为"缺的是模式，不是规则"（H160 校正，依据 reports/H160.md）；剩下：第 4 项等作者决定 `neutral_only` 的去留（保留则不删文件），第 5 项等 `where`；报告 reports/H38.md、reports/H38b.md、reports/H65.md、reports/H87.md、reports/H160.md） |
| H39 | 图存储的技术债（过期提交合并进最新快照、未绑定的编码图、`jitFnToGraph_`、节点级事实标记、草稿改为块存储） | 无 | **已完成**（复核报告 reports/H39.md；第 5 项按剖析结论不做） |
| H40 | C2 峰值内存因每步 `take` 升高 150 MB：查清数据块回收是否及时触发并修复 | 无 | **已完成**（`90163c82`；报告 reports/H40-memory.md；后续 H40b、H40c） |
| H41 | **缺陷**：`ort_loop_values`、`ort_grad_loop_values`、`lsp.editing`、`examples.ml.export_onnx_translate` 在 fsh-dev 上失败（H5 名义结构体之后） | 无 | **已完成**（`1f53d368`；报告 reports/H41.md） |
| H42 | 模块导出表改为每个程序一份，不再修改进程级的共享表（`clearExportedNamespaces`） | 无 | **已完成**（`a7d621ee`；报告 reports/H42.md） |
| H43 | 区域与循环的收尾（前端直接建循环区域、删除降低机制、JIT 的循环指令、自动微分读区域、ONNX 导出循环） | 无 | **部分完成**（第 1–3、5、6 项已完成 `48345984` 等；第 7 项已达标：`std::fvm` 一轮 28.4–30.2 ns（同批手写尾递归 28.4–32.1）、`std::jit` 一轮 7.0 ns（0.070 s / 一千万轮，与编译尾递归持平）；第 4 项未做；报告 reports/H43a.md、reports/H43b.md、reports/H43-perf.md、reports/H43-jit-loops.md、reports/H55.md、reports/H56.md） |
| H44 | ONNX 重名输出、折叠常量的 Safe 结构体 CAST（不依赖它展开）、结构体循环状态 | 无 | **已完成（三项均已落地）**（第 1 项已入 `ce16c788`；第 2 项已落地（H60：折叠把函数返回的值当作可替换锚点，`constantOf` 的 CAST 穿透已删，导出模型不变；报告 reports/H60.md）；第 3 项运行后端已完成：结构体状态在 nvm/fvm/jit/`std::opt std::jit` 一致，`loops.struct_state` 锁定；H69 补上设计原则 5.2 的"无名结构体按名字进入命名结构体"（循环体与分支臂），autodiff 穿过结构体状态与 ONNX 的 carried 结构体展开**已由 H71 完成**（`fe59f333a` 修段错误、`6336d5d96` 按叶子展开，reports/H71.md §2–§3），H69 §5 的待决 1–3 因此关闭（H153 校正，依据 H71）；报告 reports/H44.md、reports/H69.md） |
| H45 | `if` 臂里的循环在所有后端给出错值 | 无 | **已完成** |
| H46 | `std::cpp` 生成代码中 `rt::Site` 预渲染的递归深度消息损坏 | 无 | **已完成**（`60d92431`；报告 reports/H46.md） |
| H47 | Tiles 现状与差距报告（只读，可并入 H48 的第一份报告） | 无 | **已完成**（报告 reports/H47.md；H49a 更正了它对 `run` 归因：4.9 ms 中约 3.2 ms 是 `tensor:sum` 串行归约） |
| H48 | GPU G0：剖析工具与 GPU 基准 | 无 | **已完成**（`std::profile` 与训练步基准；报告 reports/H48.md；今日没有算子被放到设备上） |
| H49 | GPU G1–G3：放置标签、设备上的数组值、驻留与异步、整段编译与缓存（先交计划） | H48 的基准可用 | **部分完成**（H49a：区域/树传输合批，小往返 97.6→56.3 µs、同步点 5→2；报告 reports/H49a.md。H105：G1 的 `--devices` 已落地，报告 reports/H105.md；H117 补上无设备时的单次探测与 `@gpu` 报错位置；API 面仍是旧的 `@gpu` 偏好 + 运行期分派，`#gpu`/`#cpu` 标签、动态形状、回传图类型仍未做，`@gpu` 的保证语义待作者裁定） |
| H50 | GPU G4–G5：支持面（bf16/Tensor Core 等）、使用已授予的许可、融合、优化器多张量融合（先交计划） | H49 计划获批 | **未开始** |
| H51 | 泛型第一期：按结构展开（`match type`、`static for`、`static loop`、`t.[n]`、可变元数），`tree_map` 等改用 Camel 写 | 无 | **进行中**（计划已批 reviews/H51-plan.md，按 15 步实施；步骤 1–2 `match type` + 类型分支 + 实例返回类型推断已入，报告 reports/H51a.md；步骤 3–4 `fields T` + `static for` + `t.[n]` 已入，报告 reports/H51b.md；步骤 4 的结构体/元组推导式 `{ static for … }` / `( static for … )` 已入（含每个父一次 `ACCS` 的复用、空形状、`0x17A`），报告 reports/H79.md，验收 957/957；H79 的两个待决问题（`fields` / `match type` 之后的类型位置，`fields ()` 因此可写；推导式字段类型取自值）已由 H90 裁定并落地，报告 reports/H90.md；步骤 5 `static loop` 已入，报告 reports/H86.md；步骤 6 的 `indices`（`static for (j) in indices ts` / `static loop ... in indices ts`，新码 `SemanticDiag::StaticIndicesNotTuple` `0x000182`）已入，报告 reports/H96.md（H153 校正，依据 H96 §1；`0x17E` 是 H94 的 `NativeDeclarationOverlap`），rest 参数/`..ts`/`..e`/`constas`/`StaticTuple` 仍未做（见 H96 §6）。**写 `tree_*.cml` 前必读**：H19 计划 §1.6 更正了本计划 §1.6 的优先级——`Context::getModulePathAndKind`（`context.cpp:547`）在同一目录里先找 `xx.cmo` 再看 `xx.*`，所以与 C++ 模块同名的 `.cml` 是静默死代码而不是遮蔽它；把 `tree_*` 改成 Camel 写的那一步必须**同一个提交**里把 C++ 的 `tree` 模块改名为 `ckpt`，否则新文件永远不生效） |
| H52 | **测试基础设施缺陷**：运行器忽略 `expect_stdout_exact` 等未知字段，旧计划没有真正比较输出——未知字段报错，修复受影响的计划，列出暴露的真实失败 | 无 | **已完成**（运行器已严格、`expect_stdout_exact`/`expect_stdout_not_contains` 真正比较；暴露的 9 条真实失败全部已修，8 条循环梯度与第 9 条 `extract` 死写法都由 H59 定位并修好；报告 reports/H52.md） |
| H54 | CPU 每算子开销：小矩阵不进线程池、首次调用的 6 ms、分派成本（见 reviews/H24-H25-H26.md 末尾） | 无 | **已完成**（报告 reports/H54.md：首次 6 ms = 池构造 31 条线程，小算子首调 1.58 ms → 21 µs；惊群唤醒改为只点醒所需 worker，3 块区域 950 → 82 µs；阈值统一为 `kMinWorkPerWorkerNs = 40 µs` 一个常数；mlp_step 93.95 → 48.14 ms、transformer_step 6.63 → 5.83 ms、链 9.31 → 8.84 ms，链已贴地板。**后续**：优化器那一步紧接着由 H64 做成一次对整张量的遍历——每参数 11 → 1.56 ns，mlp_step 48.14 → 31.38 ms，阈值在 3 块区域上的边界见 reports/H64.md 3.3） |
| H55 | 循环事件在能原生降低的形状上降为原生代码（测试是比较加分支、回边是直接跳转、不保留轮计数），其余形状明确说明为什么仍走 helper | 无 | **已完成**（`d66e3238`；报告 reports/H55.md） |
| H56 | `std::fvm` 的循环一轮：就地绑定两个状态的形状、内联事件派发、轮数读写，目标是不慢于同工作量的手写尾递归 | 无 | **已完成**（`8008eaba`；一轮 44.92 → 28.4–30.2 ns，同批尾递归 30.0–32.1，报告 reports/H56.md） |
| H58 | **过期测试**：DAP 尾递归步进的 6 个断言仍按 `fda0d21f` 之前的“臂是独立影子帧”写；按 12.1 的区域模型更正断言，并补尾调用点的 `next`/`stepOut` | 无 | **已完成**（报告 reports/H58.md；无源码改动，不是缺陷修复） |
| H59 | **严格运行器暴露的真实失败**：H43b 区域化 autodiff 让复合循环状态的梯度在投影处静默丢失（8 条，首次坏提交 `914c0eda`）；`saturate_all`/`extract` 保留饱和造出的死写法（第 9 条） | 无 | **已完成**（报告 reports/H59.md；新增回归用例 `loop_grad_composite_read`） |
| H61 | 规则引擎按作者裁定的三项：`analysis` 接收多个模式变量；抽取的保留边界写进设计稿；`algebra.cpp` 对等的次序（先元素类型定律、再张量定律；`safe_fp` 就是 `#strict`） | 无 | **部分完成**（第 1、2 项完成；第 3 项只记后果、未实施；报告 reports/H61.md。§3.2 记下的抽取缺陷由 H63 修复；措辞张力按作者更正解决——裁定括注"按依赖求值只适用于纯计算"是协调者的误写、已删除，设计原则 3.1 不变，见 reports/H63.md） |
| H62 | 无 JIT 构建（CI 的 `linux-x64-nojit` 腿）在 `fastvm.cpp` 读 `jitConfig_` 而该成员只在 `#if ENABLE_FASTVM_JIT` 内声明——构建红，发布 CI 会跟着红 | 无 | **已完成**（各处按构建分开，无 JIT 时剖析后端名就是 `std::fvm`；无 JIT 全量重建后 fast 层 4769 / 4757 pass / 0 fail / 12 skip；报告 reports/H62.md） |
| H63 | **抽取的保留边界与作者裁定不符**：`DraftWritings::extract` 按"不参与同余"预选，连带保留纯节点（shape 3、GC 追踪常量等），饱和造出的死写法留在抽取结果里；按裁定改为"有副作用／承载顺序／被写入的变量"保留，纯节点只按可达性 | 无 | **已完成**（报告 reports/H63.md；回归 `rules.extract_keeps_no_unreachable_pure_node`：4 轮饱和后 33 节点、15 个死字符串常量，修复后抽取回 3 节点、0 个；`mm/writings`、`opt/algebra`、`rules`、`loops`、`autodiff`（去 PyTorch 对照）、`smoke`、`feat/opt` 共 1020/1020） |
| H70 | H44c 待决第 2 项：补一个"特化跨轮仍可达"的用例，锁住程序级特化表的用处 | 无 | **已完成**（报告 reports/H70.md；`specialization_survives_the_round.copy_is_reused_in_a_later_round`：程序级表 7 条 / 12 图 174 节点，7 份 `walk` 副本全部从根可达且其中一份被后面的轮次原样取回；会话级表 12 份副本 / 17 图 254 节点，钉住形态 10 条；`feat/opt/generic` 87/87、`feat/parse/frontend` 4/4） |
| H71 | 结构体循环状态：字段作分量（数组字段原地增长）、自动微分穿过（修段错误）、ONNX 携带结构体展开（见 reviews/H24-H25-H26.md 末尾） | 无 | **已完成**（三项一项一个提交，报告 reports/H71.md：`7eff3f329` 字段作分量——200 轮里结构体数组字段 199 → 1 块、值不变、四个后端与 `std::opt std::jit` 一致；`fe59f333a` 自动微分穿过结构体状态——切线同形、无切线字段丢弃，`loop_grad_struct_state` 与元组参照同为 24.0、五个后端一致；`6336d5d96` ONNX 把携带的结构体/元组按叶子展开进 `Loop`、两端重组，ONNX Runtime 与 NumPy/原生最差 1.19e-07。计划 loops 123、loops_in_arms 23、autodiff 161、onnx 127、smoke 10、feat/linear 712、parse 快照 4 全绿；待决问题见报告第 5 节。**注意编号**：H69 之前把慢层扫描临时记为 H71，已改名为 H80，reports/H80.md） |
| H72 | **潜在缺陷**：Tiles 的 `cast_like` 归一化只比较形状，元素类型不同时也直接返回输入（`compiler.cpp:779-785`）；`Tensor<f64>` 在类型层被归一化成 f32、Tiles 只接受 f32 张量，所以今天这条短路是空真地正确，bool/int64 张量存储一到来（H49 第 5 步）就是错值。按 `notes/core-semantics-map.md` 的既定契约改为“形状与元素类型都对上才别名，不同则 `UnsupportedComputation` 留在 CPU 由内核转换”；同类型的 `cast` 一并按“转成自身类型什么也不做”别名 | 无 | **已完成**（报告 reports/H72.md；新增 `cast_like_aliases_the_same_element_type`、`cast_between_element_types_stays_on_cpu` 与 plan_check 的 `cast_like` 计划边界检查；`feat/modules/tiles` 61/61；清掉 H49 第 5 步的一个前提） |
| H73 | **真实缺陷**：紧凑纯数据聚合数组（`items(map)`、`[...] as (int, int)[]` 等）的元素按"视图"读（`rtdata/view.h`：线程局部单元里的 Struct/Tuple 头，指向数组数据块，**不是** GC 堆对象），高阶算子把它原样绑进回调帧的槽；帧是每个安全点的 GC 根，收集器/验证器于是把这个 TLS 地址当托管对象追。H71 报的 `gc.collfuzz` 18/72 失败（全 `std::nvm`）由此而来，非移动收集器同样失败；fvm/jit 在回调里有调用时也失败，prl 在元素调用期间不轮询安全点所以幸免。修复：`Frame::set` 在"槽类型是 Struct/Tuple 且值带视图标志"时先物化——与 `Tuple::set`/`Struct::set`/`Array::set`/H29 的 `materializeCallResult` 同一条"存储即边界"约定；验证器与收集器未改 | 无 | **已完成**（报告 reports/H73.md；新增 `test/cases/feat/mm/gc/gc_view_frame_store.cml` 与 `feat/mm/gc.plan.toml` 的 nvm/fvm/jit 三条，修复前三条都失败、修复后各约 20–40 ms；`feat/mm` 166/166、`gc.collfuzz` 72/72、`gc.plan.toml` 43/43、`smoke`+`feat/linear/loops`+`feat/compile` 1052/1052、整个快层 4816（4803 通过 / 13 跳过 / 0 失败）。poison 的 `rules.*` 22/47 仍失败，与本文不同因（草稿会话的静态槽不在根集里，H74）） |
| H74 | **rules 的改写跨 safepoint 留下裸草稿指针**（H80 投毒扫描的 12 项 `poison:rules.*`；当前树因 H65 的 typed 用例是 22 项）：`RuntimeGraphDraftSession` 逐图建草稿，`when` 条件是 Camel 代码（safepoint），但 `DraftWritings` 只为正在改写的那份草稿注册根，其余草稿的静态槽在 commit 前失效 | 无 | **已完成**（报告 reports/H74.md；新增 `RuntimeGraphDraftSession::RootScope`，rewrite/saturate 两个 kernel 从第一张图到 commit 一直持有；新增 `gc.rewrite_condition_keeps_drafts_rooted`、`gc.saturation_condition_keeps_drafts_rooted`；投毒 rules 47/47、`feat/modules/rules` 54/54、`feat/opt` 685/685、`smoke` 10/10、`feat/mm/gc` 42/42；`gc.collfuzz` 归 H73、`poison:shapes.*` 是扫描 runner 伪失败，均未动） |
| H75 | **H71 扫描的 16 个 GPU/Tiles 红灯**：逐个复现并按实际失败分组，判定回归／过期期望／未实现，修小回归、其余写进计划与待决 | 无 | **已完成**（报告 reports/H75.md；8 个真回归在 `modules/tiles/src/compiler.cpp`：结构体 `CAST`（`44137514` 名义结构体后匿名值落到命名位置会留下 `CAST`，Tiles 只认张量目标）与多状态 LOOP 把 `FILL` 模板常量当捕获（`60ab7619`/`c912ea15`，`externalInputs(true)`→`false`）；`core_view_maps` 改测试形态（`grad` 绑到 wrapper 外）、`static_regions`/`multi_state_loop_boundaries` 改期望并注计划、`resident_fallback_fault` 的计划期望与 `docs/cli.md` 冲突（运行期故障在标准输出，故障未被吞）已改计划、`extrema_canonical_bits` 是负载误红（单独 9 s）；全量 GPU/Tiles 135/135。第 3 项（核心的循环降低是否也把常量排除在捕获外）已由 H91 按"常量不是捕获"完成：`LoopView`/`LoopDraftView::externalInputs` 的默认写成"构建期已知的值不算"，并有用例钉住，见 reports/H91.md） |
| H80 | 慢层全套扫描（原按 H71 记，作者把 H71 分给结构体循环状态后改号） | H6 第 5 项 | **已完成**（Linux 主机 fast/slow/perf/gpu/GC/poison 全层：fast 4788 项 0 失败、基线 19 项全转绿；新增 GC/poison/tiles 三簇转交 H73/H74/H75；报告 reports/H80.md，tiles 逐条清单 reports/H80-gpu-tiles-failures.txt） |
| H81 | 核心的 `CAMEL_*` 环境变量改为命令行选项、遍参数或 `std::profile`（见 reviews/H24-H25-H26.md 末尾） | 无 | **已完成**（报告 reports/H81.md；`CAMEL_GC`→`--gc`、`CAMEL_BLOCK_LIMIT_MB`→`--block-limit`、`CAMEL_GRAPH_GC_MB`→`--graph-gc-mb`、`CAMEL_GC_STATS`/`CAMEL_TYPE_STATS`/`CAMEL_EFFECT_TRACE`→`std::profile(...)` 参数、`CAMEL_NATIVE_STACK_GUARD`→`--native-stack-guard`、`CAMEL_PROFILE_MODE`→`--profile-mode`、`CAMEL_ENABLE_TEST_PASSES`→在流水线里写测试遍名；旧名字一律给诊断并退出 1，不再静默生效；`src`/`include`/`modules` 里已无这些 `getenv`。smoke 11/11、feat/compile 937/937、feat/mm 166/166、feat/trans/effect_trace 4/4、docs_links 1/1；feat/tools 只余 `develop` 上既有的 `grammar.tables`（`static` 关键字未进 TextMate 语法，已用基线树复现）。`CAMEL_TILES_DEVICE` 归 H49 的 `--devices`；`CAMEL_TILES_NVRTC` 保留为环境变量。H93 已删掉这张表与"旧名字给诊断"的拒绝：旧名字不再被读取，设置它没有任何效果，见 reports/H93.md） |
| H82 | Debug 构建在安全点检查重写会话打开的草稿是否都已登记为根（H74 第 9 节的强制机制） | 无 | **已完成**（报告 reports/H82.md；会话的 Debug 记录 + 每个安全点入口的 observer，消息给出安全点、会话构造点与程序根、未覆盖草稿的图号/静态槽号/槽类型；移除 `rewriteKernel` 的 RootScope 后 abort 134、恢复后通过；Release 零代价；Release `smoke`+`feat/mm`+`feat/modules`+`feat/compile` 2136 通过 / 0 失败 / 1 跳过（Windows-only）；Debug `rules`+`feat/mm` 152 条 151 通过，唯一失败 `rules.typed_element_laws_fire` 在基线 Debug 上同样失败、与检查无关；另修 develop 顶端 `advanced_tracer.cpp` 的 Debug 编译错误） |
| H87 | 张量定律（H65 的下一步）：元素族定律的张量版、`c1*t + c2*t`、`(t*c1)*c2`、`t+t`、裸项 `t*c + t`，以及元素族补裸项；让 `algebra_tensor.cml` 的 merged、merged_explicit、shared_product 在 `rules::simplify` 下与 `std::opt::algebra` 等算符多重集；两个待定项（`x*c + x`、`safe_fp` 的严格策略）按作者裁定处理 | 无 | **已完成**（报告 reports/H87.md；规则库 64 → 92 条，`check` 92/92；新增 `rules.tensor_laws_counts` 等 12 条与 `algebra.plan.toml` 五条；三例多重集相同、四个后端值相同；`feat/modules/rules` 67/67、`feat/opt` + `feat/linear` 1403/1403、`smoke` 全绿。H100 已把 `algebra.cpp` 删到只剩 `neutral_only` 的单位元；张量零定律两边都没有，见 reports/H100.md） |
| H88 | H6 第 1 项的前端四条：移除 `TypeCode::Byte`、期望类型合并为一条通道（frontend 第 6 条）、`append` 就地改写走通用 `setInPlaceForm`（第 7 条）、语言服务器的类型解析取自编译器（第 15 条） | H6 第 1 项 | **已完成**（四个提交，一件一个；报告 reports/H88.md；`from_bytes::<T>` 与 `load_tree` 仍等泛型） |
| H93 | 删除 `kRetiredEnvironmentVariables` 表、退役诊断与其用例（违反"不加指向新名的诊断"） | 无 | **已完成**（报告 reports/H93.md；`tools/camel-cli/config.cpp` 的表、`rejectRetiredEnvironmentVariables` 与 `parseArgs` 里的调用，`smoke` 的 `retired_environment_variable.diagnosed`，`docs/cli.md`/`cli.cn.md` 的退役诊断段都删了；这条消息本来就不是诊断目录里的码（`--check` 197 条、`--verify-doc` 一致）。`git grep` 九个名字在 `src`/`include`/`modules`/`tools` 无输出；旧变量现在不被读取，九个一起设也照常运行（探针用例通过）；`smoke` 10/10、`feat/mm` 169/169、`feat/compile` 958/958、`feat/tools` 19 通过 10 跳过、`feat/docs_links` 1/1，另 `feat/trans/effect_trace` 4/4、`feat/consistency/limits` 110/110。编号更正：作者记为 H83，但 H83 已被下标与切片占用并合入，此小项改记 H93） |
| H94 | H76 的六条裁定：`.cml` 与 `.cmo` 是同一个模块（C++ 导出的类型/常量在声明模块里可见，`export let`/`export type` 进同一实体）；泛型声明之间按固定签名一样检查重叠、歧义在声明处报错；无 `export` 的无体函数是模块私有的原生算子；入口模块的声明用 `module` 名作 URI 前缀；一个模块只能声明自己的算子、`#op` 只能指本模块实现的算子 | 无 | **已完成**（报告 reports/H94.md；六条裁定各一个提交：`5bc873fd6` `.cml`/`.cmo` 同一模块（`Module::inheritExportsFrom`/`replaceEntity`，C++ 导出拷进声明模块，同名算子组以声明为准），`da208fc48` 泛型重叠（`NativeDeclarationOverlap` `0x17E`，固定的比泛型的更具体、排在前），`146030126` 私有声明（进 `ModuleUnit::nativeOperatorGroups`，不进模块实体，`ImportNameNotExported` 拒绝导入），`34378bd82` 入口前缀用 `declaredName()`，`57e49f97d` 只能声明自己的算子（`NativeDeclarationForeignOperator` `0x17F`，`bytes_tag` 的 `#op<'bytes:size'>` 由正例改成拒绝用例），第 6 条只改文档。`feat/compile`/`feat/modules`/`feat/linear`/`smoke` 合计 2735 条（1 条 Windows 跳过）全过；声明计划 14/14；`stdlib-reference --verify-ops` 343 条一致） |
| H98 | 草稿（**未实施**）：`var`/`let` 声明不进入同步链，声明与它首次使用之间的语句因此与声明里的构造无序 | 无 | **草稿，待作者裁定**（H95 的测量：`var v = [...]` 的构造节点不进同步链（`lastSyncedNode_` 只由读、调用、分支、循环设置），而 `let s0 = std::mem::blocks()` 这类调用节点在图上没有任何输入，于是“先构造还是先测量”只由 `computeExecutionOrder` 从出口出发的 DFS 后序决定；字面量的构造除了那一次分配没有可观察效应，所以图不违反 3.3/3.4，但每个分配窗口都因此脆弱，程序读者也看不出这种无序。若要让声明进链，要改的是：声明构造完成后把 `lastSyncedNode_` 指向构造节点，并给非调用的初值补一条来自前一条同步语句的控制边；代价是所有“声明后接语句”的程序的 `std::jgir` 都会换边或多边，这是语义改动，应单独裁定并单独跑全套，不是为了让某个用例通过。见 reports/H95.md 第 6 节） |
| H99 | **同一规则用例在 Debug 与 Release 下重写计数不同**（H82 §6.2 的 `rules.typed_element_laws_fire`）：查清构建配置为何改变规则库落点，并让配置不再参与 | 无 | **已完成**（报告 reports/H99.md；根因是 f32 字面量只写 `slot_t` 低 4 字节、高 4 字节未初始化（Debug 残留 `0x00007382`，Release 为零），而模式常量、`constantOf` 与 `NodeKey` 按整槽比较；`float32_fold` 算出的 `1.0` 高位为零，`float32_mul_one` 只在 Release 触发。修复：`makeSlotFromPrimitiveData` 零初始化，并在重写边界按类型规范化常量位（保留 `-0.0`/NaN 位型）；不改规则库与期望。Debug/Release `feat/modules/rules` 均 67/67，同一用例完整输出 `diff` 为空；Release `smoke` 10/10、`feat/opt` 768/769，唯一失败 `fold_struct_cast.in_return_position` 在基线 Debug/Release 上同样失败，与本任务无关） |
| H100 | 让代数遍就是规则库：把 `std::opt::algebra` 注册成 `rules::simplify` / `rules::simplify_strict`，`std::opt` 通过同一个遍调用；删掉 C++ 的定律表，只留 `neutral_only` 的整数单位元 | H87 | **已完成**（报告 reports/H100.md；H65 元素多重集 `8 mul_d, 2 add_d, 2 mul_l, 1 mul_i, 1 mul_f, 1 dtof` 不变；张量三例不变；四后端值一致；新增 `library.laws.*` 证明 `x*0`/`x-x`/`x/1`/`-(-x)` 在触发；`feat/opt` 696/697，唯一非 H100 失败 `fold_struct_cast` 归 H106；`algebra.cpp` 603 → 297 行） |
| H101 | 慢层全套扫描（H6 第 5 项）：在 `45520f228` 上按目录跑 `-Tier slow` 与 `perf/**`，按 H80 的四种性质给每个失败定性，只修便宜且无歧义的；回归/缺陷给最小复现并上板 | 无 | **已完成**（报告 reports/H101.md；首轮 2412 项 / 2404 pass / 8 fail；慢层修 3 处（`gc_pacing.churn_prl` 单独跑 `0ecdc2f5a`；`perf.fib` 两个 nvm 15→60 s `883b75554`；`asian_option_mc.nvm` 120→300 s `76bda038c`），另修 fast 层的 `ast_hashes`（`c5a23d3ed`）；转交 H98/H102/H103；最终慢层 2412 项 / 2408 pass / 4 fail（3 个 `poison:shapes.*` 与 `progen_check`），fast 层 4913 项 / 4901 pass / 1 fail / 11 skip（仅 `fold_struct_cast.in_return_position`），`smoke` 10/10；主机负载 81–108 时的 3 个 perf 超时在降载复跑后全绿） |
| H102 | H3a：融合不得改变算子身份（`matmul_add`/`matmul_blas` 消失，实现选择与融合组成为属性） | 无 | **已完成**（报告 reports/H102.md；图上保持 `matmul → add → relu`，`tensor.kernel`/`tensor.fusion` 为属性，核心只加通用 `ArgsView::attrs()` 通道；四条执行路径逐字节一致；`matmul_blas`/`matmul_add`/`matmul_add_relu` 的 OpDef、内核、VJP、ONNX lowering 全部删除） |
| H103 | H6 第 1 项剩下的两条：`from_bytes::<T>` 与 `load_tree`（“运行时读取一个由编译期实参命名的类型”）——查清能否在现有机制上建，能就建 | 无 | **已完成（查清，迁移未做）**（报告 reports/H103.md；`from_bytes::<T>` 已经是按类型分 URI、编译期选定（`tensor:from_bytes_f32/_i64/_bool`），无需再建；`load_tree` 的编译期展开机制已够用（`match type`/`static for`/`::<F>` 用例可跑），但把它改成展开 = H51 计划里 C++ `tree`→`ckpt` 改名 + 用 Camel 重写 `tree.*` 的那一步迁移，建议单开任务；真正的语言缺口是“按类型分 URI 的算子不能在 `.cml` 里声明”，新增 `native.declaration_type_argument`、`native.declaration_per_result_type` 两个诊断用例钉住，另修 `valueNameExists` 看不见本模块声明的缺陷（`native.unselected_case_sees_declaration`）、加 `static_expansion.type_arguments_from_fields` 证明展开侧够用；顺手改掉 `07_statements.md` 里“不能显式给类型实参”的过期半句） |
| H104 | H19 第 10 步的下一批模块声明化（第 1 批 `bytes` 之后）：迁移 `str`，`stdlib/str.cml` 写全部 16 个声明（含 7 个 `with` 参数、`int?`/`string!`），删掉 C++ 的 `OperatorGroup`/`StaticFuncTypeResolver` 与签名文件，导出组来自声明；`str::bytes`/`from_bytes` 的删除属第 8 步，本批不做 | 无 | **已完成**（报告 reports/H104.md；`stdlib/str.cml` 16 条无体 `export func`，`modules/str/module.cpp` 只剩执行器、内核绑定与 16 条纯度；`replace` 的 `from` 是关键字，改名 `from_`；声明计划 17/17（新增 3 条：导入方看到声明的类型、错误个数、错误类型），`stdlib_values` 59/59；`feat/modules`+`feat/compile`+`feat/linear`+`smoke` 2769 条、2768 通过、1 跳过、0 失败；`--verify-ops` 343 条一致、`--verify-doc` 最新；`feat/parse` 的 `format_keeps_ast` 通过，AST 快照按工具 `--update` 刷新（含基线上 H87/H91/H94 之后未刷新的条目，报告 §6 逐条列出来源）） |
| H105 | H49 G1 第一项：`--devices`——运行时选择流水线可以使用的设备；点名的设备缺失时点名报错而不是静默回退；`CAMEL_TILES_DEVICE` 退役 | 无 | **已完成**（报告 reports/H105.md；核心 `include/camel/execute/devices.h`/`src/execute/devices.cpp` 解析并保存选择，`RuntimeDiag::DeviceUnavailable` `0x04000070`，Tiles 后端校验每个点名的序号、`--devices cpu` 只跑主机且需要设备的操作报错；4 条新用例（选中的设备/不存在的设备/主机/非法值），26 条计划从旧环境变量改用 `--devices cpu`；`feat/modules/tiles` 64/64、`feat/modules`、`smoke` 见报告第 5 节；`@gpu`/`#gpu` 的保证语义与其余待决问题见报告第 7 节） |
| H106 | **`feat/opt` 一直红的 `fold_struct_cast.in_return_position`**：`mk` 的返回位置声明为具名结构体 `P`，`std::opt` 折出的常量却带着匿名形状的类型；判断期望错还是编译器错，找出移动它的改动并修在对的一边 | 无 | **已完成**（报告 reports/H106.md；按设计原则 5.1/5.2 判断是**编译器错**：具名结构体的身份是名字加模块，值进入具名类型时它的类型就是那个名字，转换不移动值。H88 的 `a87b57d4b` 把 `visitExitNode` 里自 H5 `44137514` 起就有的"返回位置结构体显式 `coerceTo`"收进了 `adaptExitValue`，而它的判据 `assignableFrom` 对"具名目标 + 同序匿名源"为真，转换因此被跳过；H5 的 `named_structs` 只覆盖字段异序的返回（走拷贝分支），H88 的验收没跑 `feat/opt`。修复：`adaptExitValue` 改为"位置类型不是值已有的类型就套用"（与 `checkValue` 同一条规则），`coerceTo` 自己决定 CAST/拷贝/拒绝；两个具名类型之间仍要显式 `as`。bisect：H87 合并 `cef7324b4` 87/87 绿，H88 点 `0d122fae2`（H88 合并自身不能编译，其直接子提交补回 `builder.h` 的静态展开缓存）86/87，唯一失败就是本用例。证据：`std::gir` 优化前是 `CAST` 且 `value_type="P"`，`std::opt` 后 0 个 CAST、常量 `value_type="P"`，`std::jgir` 的 `outputType` 也是 `P`；`feat/opt` 692/692、`feat/compile` 988/988、`feat/linear` 723/723、`smoke` 10/10、`feat/trans` 39/39） |
| H107 | H2 第 6 项剩下的部分：删除关键字 `inner`、`outer`、`atomic`、`shared`，标准库签名里的 `inner func` 改为无函数体的声明 | 无 | **已完成**（报告 reports/H107.md；四个词都没有替代标签——原生实现是模块里无函数体的 `export func`，模块私有辅助函数是不导出的 `func`，外部实现由模块系统表达，“整体不展开”是 `#kernel` 一类标签、`atomic` 没有对应标签，数据共享由不可变数据取代；词法表 45→41，`ImplMark` 与 `Modifier::Atomic/Shared` 整个删除，产生式 121→120；`stdlib/signatures` 381 条 `inner func`（381 条带占位 URI 函数体、0 条真辅助函数）→ 381 条无函数体 `export func`，204 条 `#op`、177 条默认 URI、34 条 `with`，`inner` 计数 0；不新增诊断，旧写法就是 `SyntaxDiag::UnexpectedToken`；`feat/parse` 57/57、`feat/compile` 988/988、`feat/modules` 1040 通过 + 1 跳过、`feat/linear` 723/723、`smoke` 10/10、`stdlib-reference --verify-ops` 343 条一致；`math_functions.cml` 的 `std::jgir` 前后 SHA-256 相同） |
| H109 | **`loops.onnx_refused` / `loops.onnx_refused_prl` 在 develop 上变红**（分支 `handover/H109-onnx-refused`）：查清这两条用例真正测的是什么（导出器对 LOOP 的拒绝，还是 `std::macro` 的编译期求值），并让"导出含循环的函数被拒绝、且指出循环位置"重新成为可观察的结论 | 无 | **已完成**（报告 reports/H109.md；用例测的是导出器的拒绝，不是 `std::macro`：pre-H97 `72bf10c05` 与 pre-H100 `9f5620a4c` 上 loops 都是 124/124，无 pass 的运行同样得到 `onnx.export_model: ... (at ...:8:13)`，该程序无宏声明，`std::macro` 一直是空操作。变红来自 H100 而非 H97：导出器在自己的私有副本上化简时，`rules::simplify` 的 checked commit 拿副本与运行中的 program 比较，被改过版本的图报 `RewriteInvalid`，LOOP 到不了翻译。修复：导出器在化简前用 `collectReachableGraphs` 扫描 LOOP 节点、以同一段拒绝文本和节点 origin 报出（`refuseLoops`，与 `evalNode` 共用 `loopRefusalText`）；计划去掉空操作的 `std::macro`，两条用例断言 `onnx.export_model:` + 拒绝句 + `onnx_refused.cml:8:13`，并禁止 `RewriteInvalid`/`rules::simplify`。loops 124/124、smoke 10/10） |
| H110 | 控制输入不该让子项失去规则匹配（H100 §4 的后续项）：查清 `shapeTakesPart`/`shapeOf` 的形状 3 到底保护什么，把"定律不再读取的每个算子子项"的等待搬到替换写法上，删掉守卫并用计数与顺序用例证明 | H100、H95 | **已完成**（报告 reports/H110.md；守卫不是形状推断也不是定律可靠性，而是顺序——`rewrite` 只搬根的等待。`shapeOf` 只留 1/2，`canBeRoot` 删除，`matchFrom` 收集每个算子子项的 `ctrlInputs`，`rewrite` 把它们挂到新写法上（已存在写法带等待时不改写，避免环）。计数：`integrated.graph.false` 15 → 10（−4 两次系数合并，−1 `underflow_sensitive` 的带控制边 `t+t` 由 `float_combine` 合成 `x*0.2`），`.true` 15 → 14（`float_double` 的精确加倍，不需许可），`gradient.*` 保持 5（与控制边无关：`x*c` 不匹配常量在左边的 `c*x`，`float_add_self` 先吃掉和，记为开放点）。新增 `algebra_order.cml` 与 `order.graph`/`order.execution`：改写后的乘积仍带原控制边（指向 `println` 与彼此），四后端打印顺序不变。`feat/opt/algebra` 184/184；`feat/opt`+`feat/modules/rules`+`feat/linear`+`feat/trans`+`smoke` 1543/1546，3 个失败（`fold_struct_cast` = H106；`loops.onnx_refused*` 在 develop `6487151b` 的二进制上逐字复现，均非本轮）；`feat/mm/writings`+`feat/modules/graph` 71/71） |
| H111 | **运行期 `onnx.export_model` 在所有程序上都失败**（H109 发现，H100 引入）：导出器在私有副本上做 `std::opt` 化简时，`rules::simplify` 的 checked commit 把副本与运行中的 program 比较，前几轮改过版本的图被报成冲突；不只是循环，最小程序 `f(x) = x * 2.0` 的导出也失败 | 无 | **已完成**（报告 reports/H112.md；根因就是 H109 看到的：导出器在私有副本上开会话，`rules::simplify` 的 checked commit 无条件把副本与上下文当前程序比较，而两份程序的图 id 不是同一批版本。修在会话层：只有能替换上下文程序的会话（打开在它上面，或工作在 owner 装上的程序，且根图就是程序根图）才比较版本；私有程序的 `commitChecked()` 与 `commit()` 相同，真正的冲突仍由 `opt.edges` 诊断。`feat/modules/onnx` 130/130，新增 `export_model_minimal`。另修 H100 按需编译 `stdlib/rules.cml` 带出的两处：库构建覆盖程序的 entry origin、标准库自己的 import 被入口目录同名文件遮蔽） |
| H113 | H19 第 10 步的下一批模块声明化（`str` 之后）：迁移 `math`，`stdlib/math.cml` 写全部可声明的固定签名（96 条，41 个名字，逐个确切的 `#op` URI），删掉 C++ 的 96 条 `StaticFuncTypeResolver` 规则与 `stdlib/signatures/math.cml`，导出组来自声明；`sqrt`/`shl`/`shr` 的计算签名、`pi`/`e`/`inf`/`nan` 的值仍留 C++ | 无 | **已完成**（报告 reports/H113.md；`stdlib/math.cml` 96 条无体 `export func` + 8 条 `// dynamic func` + 4 条 `// value`；`modules/math` 删 28+68=96 条静态规则，保留 `sqrt`/`shl`/`shr` 的动态组、执行器、纯度、语义与值；math 99 个注册算子不变，`--verify-ops` 343、`--verify-doc` 最新、`docs/stdlib-reference.md` 无变化；声明计划 24/24、`stdlib_values` 59/59、`stdlib_registry` 4/4、`operator_semantics` 4/4；`feat/compile` 1000/1001（唯一红 `permissions.strict_algebra` 在 H112 基线上同样失败）、`feat/linear` 723/723、`smoke` 10/10；`feat/modules` 951/1051，97 个 ONNX 归 H112、2 个 `graph_facts` 归 H121；另把 `test/cases/compile/tags/graph.cml` 改名为 `tags_nodes.cml` 以避开与标准库 `graph` 模块的搜索名冲突，`feat/compile/tags` 30/30） |
| H114 | H19 第 4 步：`sys`→`env`、`os`→`process`、`crypto`→`hash`；每个模块一个提交，只改拼写，旧名不再是模块，也没有别名或指向新名的诊断（导入旧名是普通的 `SemanticDiag::ModuleNotFound`）；终端五个算子在 H116 的 `io`，`process` 只留 `exit`/`sleep`/`whoami`/`cpu_count` | 无 | **已完成**（报告 reports/H114.md；`bf1506ac4`、`91ff0faec`、`409ad0ab4` 三个改名提交，`1c692c6b7` 合并 `dd3314984`，`31a1ce0de` 按规范删除 0x185；目录、CMake 目标、`BuiltinModule` 名、执行器工厂、`*` URI、签名文件、测试与所有导入处同步改名；`sleep`/`exit` 仍是全局内建但 URI 为 `process:*`；三份 `std::jgir` 样本规范化后 SHA-256 不变：env `31e343f4…`、process `ef0d12bc…`、hash `e24d1896…`；`rename` 6/6、`rename_identity` 3/3（H147 门禁同一组三条红：`a210…`/`aa79…`/`5d54…`，摘要哈希了 checkout 绝对路径；H143 定位为 `case_path` 未填并重录为 `8c4c5bb6…`/`8c8b9948…`/`60047e7f…`，验收 0 红；H153 校正，依据 H143、H147 §3.3–§3.4）、`io` 4/4、`stdlib_registry` 4/4、`frontend` 4/4；验收 2807 条中 2801 通过、3 条既有基线红、3 条 skip，无新增失败（报告 §6）） |
| H115 | H19 第 10 步的模块声明化：迁移 `json`，`stdlib/json.cml` 写 35 条声明（`decode`/`kind`/`is_null`、`get` 的两组尖括号参数各 4 条、六个访问器各 4 条），删掉 C++ 的 10 个 `exportEntity` 与 3 个签名辅助，模块导出组改由声明回答；`encode`/`decode_as` 是计算签名，逐条报告后留在 C++ | H19 | **已完成**（报告 reports/H115.md；`--verify-ops` 367 条一致，参考页 265 条；发现并修正 H16 的两层拼写：`Json!?` 不是 `Json?!`） |
| H116 | **`io` 模块**：把终端从 `os` 拆到 `io`，给出声明模块与 C++ 侧；查明 `str::bytes`/`from_bytes` 是否应迁入 | 无 | **已完成（`io` 重建；`str::bytes` 未动）**（报告 reports/H116.md；按 H19 第 4 步把 `set_terminal_raw_mode`/`has_input`/`get_char`/`get_chars`/`clear_input_buffer` 从 `os` 移到 `io`（URI `io:`，内核 `__io_*__`；资源仍 `io:stdin`/`io:terminal`，待第 9 步改名；五个算子全部 effectful），`stdlib/signatures/io.cml` → `stdlib/io.cml` 为声明模块，C++ 只留 kernel/executor/纯度与资源，`os` 只剩 `exit`/`sleep`/`whoami`/`cpu_count`；`str::bytes`/`from_bytes` **没有**迁入：H19 第 8 步是删除它们（`bytes::encode`/`decode` 是唯一转换处），与工单前提/H104 的转述相反，留待第 8 步或作者改计划；新增 4 条 `feat.modules.io`（管道 stdin 的读写往返、非终端的 raw mode 故障、`std::jgir` 的 `effect` 角色、`inspect --ops` 的资源）与 2 条编译负例（错元数/错类型在声明处诊断，`0x0300000F`）；`feat.modules.io` 4/4、`native_declarations` 22/22、`stdlib_registry` 4/4、`resources` 3/3、`std` 25/25、`feat/linear` 723/723、`smoke` 10/10、`--verify-ops` 343 条一致；`feat/modules` 958/1058（97 条 ONNX 基线红 H111/H112、2 条 H121 facts 回归、1 跳过）、`feat/compile` 997/999（`read_by.present`/`permissions.strict_algebra` 基线红）；无新诊断） |
| H117 | H105 遗留：无设备时只探测一次（不逐次重试 `cuInit`），无设备/点名缺失的报错给出位置 | 无 | **已完成**（报告 reports/H117.md；`sharedBackend()` 的探测结果按进程缓存（`ProbeOutcome`：成功 / 能力缺失 / 点名的设备缺失 / 普通故障），首次调用 7 次构造 → 1 次，`--devices cpu` 一次都不探测；`@gpu` 包装图的 `tiles:prefer_gpu` 节点取被包装函数的图起点，`--devices 63` 的报错从 `(add):?:?:` 变为 `gpu_decorator.cml:5:1`；新增 2 条计划（探测 1 次、`cpu` 0 次），`missing_device` 补位置断言；无新诊断码）。缓存与测试钩子的取舍、动态调用点为何拿不到，见报告第 5 节。**注意**：分支点 `f200920c0` 上 `feat/modules` 已有 99 条红（`feat.modules.onnx` 的导出路径经 `rules::simplify` 报 `RewriteInvalid`、`feat.modules.graph_facts` 两条 FACTS 计数不符），本项未引入，报告 4.1 节有改前/改后的同集合对照；合并闸门若要 `feat/modules` 全绿需另派一项） |
| H118 | 交换律匹配（H110 §6 的后续项）：定律只写一种操作数顺序时，程序写反了该不该命中——由算子语义声明的事实决定，还是让定律写两遍；匹配器先按写下顺序、没有可接受的匹配才对调二元算子的两个输入，替换仍按定律右边；用 H110 的 `3.0*(x+1); t+t` 与 `(x+1)*3.0; t+t` 证明两张图相同 | H110、H85 | **已完成**（报告 reports/H118.md；事实在 `sem::OperatorSemantics::binary` 的 `BinaryLaw.commutative`，按算子与元素类型声明：标量 `add`/`mul` 对 Int32/Int64/Float32/Float64/Complex64/Complex128 声明，`sub` 不声明；`tensor:add`/`multiply` 只对 Float32 元素声明，整数张量故意没有声明，匹配器因此不对调。`declaredCommutative` + `matchFrom` 先自然顺序、无接受匹配再交换；`rewrite` 允许，`saturate` 对 `<=>` 传 false（反向已是规则；否则 `rules::library_saturate_all` 从约 0.5 s 涨到 >20 s 超时）。H110 那对现在都是 `(x+1)*6.0`（默认 `float_combine`、严格 `float_double`，右边同形）；`gradient.*` 5 → 4，`3.0` 被 `6.0` 取代；其余计数不变。新增 `algebra_commutative.cml`、3 张图/1 条非交换守卫（`0.0-x`、`1.0/x`）/4 后端执行；`feat/opt/algebra` 184 → 192，五个计划 1554/1554） |
| H119 | **回归（H101 慢层发现）：`T?` 的期望类型漏进 `\|\|` 的右操作数与 `??` 的臂** | 无 | **已完成**（报告 reports/H119.md；泄漏点是 H88 `a87b57d4b` 的单一期望类型通道：`let o: bool? = …` 的 CAST 把 `bool?` 留在 `expectedValueType_`，`&&`/`\|\|` 的臂、`??` 的两侧、块里值没人要的语句都在它下面构建，`checkValue` 把 `false` 装进 `bool?`，`requireBool` 于是报 `LogicalOperandNotBool`；第二形态同理，`let n` 的 `??` 与它的主语拿到 `float?` → `BranchReturnTypeMismatch`。修复（`gir: an operand's type comes from its construct…`）：逻辑分支的操作数 = `bool`，`??`/`!!` 主语无位置、右臂 = payload，非尾语句无位置（`gct->operands()` 的降级作用域除外），`if` 条件 = `bool`、`match` 主语无位置；`!`/比较/算术走调用、实参取形参类型，未改；`if`/`match` 的臂按 5.1 仍取整表达式的位置。`LogicalOperandNotBool` 未削弱（新 negative 用例钉住 9:13）。bisect：`git log -S"checkValue"/"visitInPosition"` 都指向 `a87b57d4b`。验收：tagged 54/54（含两个新用例）、`feat/opt -Tier all` 704/704（`progen_check` 1025 格 0 wrong；另有 H97 后生成器仍写 `macro` 的一行修正）、`feat/linear` 723/723、`smoke` 10/10、`feat/compile` 997/999（两红是预先存在的模块遮蔽）、`feat/parse` 56/57（基线独有的 stale AST 金标，上游 `68f7d252c` 已重记，合并 forward 即绿），详见报告 §7） |
| H120 | **测试基础设施缺陷**（H101 慢层）：poison 扫描把两个都叫 `shapes` 的源计划展开成同名副本，`poison:shapes.fvm/jit/prl` 报 `reference back end did not run` | 无 | **已完成**（报告 reports/H120.md；副本身份改为 `<sweep>:<源计划 id>:<测试名>`，`__backendReference` 同改，同一 sweep 内重复列出的源计划去重；源计划测试名未动，`[PASS]` 行与 summary 格式未变；运行器自测新增 `test/tools/selftest/sweep_names/`：两个源计划各有一个 `shapes`，修复前 3/4（`differs from poison:shapes.nvm`）、修复后 4/4，同一源测试在两个 sweep 下两个身份且各查各自的 nvm 4/4；同一提交 `28073beda` 的 Release 构建：`gc_poison_sweep` 全量 2163 项 7→4 fail（3 条 shapes 伪失败转绿，余 4 条是 H116 行记的 `read_by.present`/`permissions.strict_algebra` 与 H121 的 `rewrite_opt`/`rewrite_macro_simplify`），`feat/mm -Tier slow` 2252→2253 项、2243→2249 pass（+1 项是 enum 的 `shapes.nvm` 现在被自己的后端引用带进 slow 层；另两条 mnist 前置条件波动导致 −2 skip），`feat/mm` fast 171/170/1（`saturate.deterministic` 既有红，与 sweep 无关），`feat/compile` 999/997/2（两条基线红不变），`smoke` 10/10） |
| H121 | **回归（H113 发现）：`feat.modules.graph_facts` 的 fact 失效范围变小**——`test/cases/facts/tensors_and_loops.cml` 期望 `FACTS graph layer valid=6`/`total valid=4`/`count valid=2`，实际 `3`/`1`/`1` | 无 | **已完成**（报告 reports/H121.md；逐 merge 二分 18 点、每点一次构建 + 一次计划，6→3 发生在 H100 merge `6f02b8c65`：`std::opt` 的代数阶段改走 `rules::simplify`，运行时才懒加载 `rules`，其 import 的 `math` 声明 99 条算子语义，`declarationEpoch` 166→265；layer/total/count 的图修订号未变，但 `core.instance`/`core.reorder`/`core.effects`（默认 `readsDeclarations=true`）按 op-semantics §10.3 过期，`core.shape`（`Dependency::Node`，false）与 `core.loopDomain`（false）仍有效；这是正确的保守失效，不是重算路径坏：§10.4 过期读作没有，`computeFacts` 重算 245 条，`verify equal=43 differ=0`。修在用例层：`test::facts_seed/check` 增打印纪元、修订号、per-kind、重算后可读性，`rewrite_opt`/`rewrite_macro_simplify` 改断言“未改图上读声明的 kind 过期、不读的 kind 不过期”+重算后 `stale=0 differ=0`，`rewrite_inline` 保留“纪元不变、未改图零过期”对照；`graph_facts` 8/8、`feat/opt` 710/710、`feat/trans` 44/44、`smoke` 10/10（合计 772/772）。**观察项（未修，报告 §7）**：`declarationEpoch` 全局单调是当前规范允许、最保守的读法，但比“我读过的那条声明变了”粗；按 owner 纪元或按 fact 的声明依赖集的粒度、内存/失效正确性/跨模块改名代价见报告） |
| H122 | **H19 第 8 步：删除 `str::bytes`/`str::from_bytes`**，`bytes::encode`/`decode` 是字符串与 UTF-8 字节之间唯一的转换 | 无 | **已完成**（报告 reports/H122.md；`stdlib/str.cml` 两条声明、`modules/str` 两个内核/绑定/纯度删干净，不留别名或过渡层；调用方 `utf8_string`/`str_chars`/`str_bytes`（改名 `str_unicode`）迁到 `encode`+`to_array` / `from_array`+`decode`，`fault_from_bytes*` 拆成 `bytes_fault_from_array`（同一 `InvalidArgument`，仍第 7 行）与 `bytes_invalid_utf8`（`EncodingError` 错误值，程序继续、退出 0）；非 UTF-8 的差别：旧 `from_bytes` 是故障，新 `decode` 是错误值；旧名按设计原则第 1 节成为普通未知名字（导入后调用是 `ImportNameNotExported` `0x03000015`，定位在使用处 `:10:13`；不导入是 `UnresolvedReference`），**没有**指向新名的诊断（任务书第 3 条与规范冲突，按规范执行；未加 `0x03000185`，诊断目录仍 202 条）；`--verify-ops` 343 → **341** 条一致；参考页 265 → 263 条，AST 快照只记本任务 11 个文件的 13 行（快照另有 77 条既有红，见报告第 6 节）；`feat/modules` 952/1052（99 条基线红：97 ONNX H111/H112、2 H121 facts；跳过 1，Windows 专用）、`feat/compile` 999/1001（2 条基线红）、`feat/linear` 723/723、`smoke` 10/10） |
| H123 | **文档缺陷（H115 发现）**：两层标记的顺序写反了——`T?!` 是 fallible(nullable(T))、`T!?` 是 nullable(fallible(T))，H16 报告与多份文档把“可空套可错”写成 `R?!`；按实现（解析器 `visitTypeExpr` 与类型打印 `tupleParts`）定出规则，逐处更正中英文文档，并用 `match` 的穷尽性锁定两种拼写 | H115 | **已完成**（报告 reports/H123.md；改 20 处拼写 + 7 处规则说明，H16 报告只加更正注记；新增 1 条行为用例（四后端）与 2 条负例，`feat/compile/tagged` 58/58；`smoke` 10/10、`feat/parse` 57/57、`feat/compile` 1006/1008（仅 H115/H116 已记的 `read_by.present`/`permissions.strict_algebra` 基线红）；doc links 353 篇无断链；AST 快照刷新含基线条目，报告 §6） |
| H124 | **模块解析冲突（H119 发现）：用例目录里的 `graph.cml` 被当成 `std::opt` 依赖的 `graph` 模块编译**——`stdlib/rules.cml` 做 `import graph`，于是同一目录下的测试文件顶替了模块，`std::opt` 的遍退出 1 且无输出 | 无 | **已完成（由 H112 覆盖 + 本批补性质）**（报告 reports/H124.md；H112 的 `2dafce471` 让每个从文件加载的模块先在自己目录里解析 import，入口程序自己的 import 仍按入口目录优先，本批未再改优先级。四条用例：`short_circuit.passes`/`.simplify`、`tags.read_by.present` 已绿；`strict_algebra` 的遮蔽已消失，但仍停在 H112 §6.1 的 CSE 形状断言（H125 范围）。新增 `imports.entry_dir_wins`/`imports.library_dir_wins`：同一目录里程序的 `graph.cml` 归程序，`std::opt` 的 `rules` 仍取标准库 `graph`；`module_imports` 25/25。把 H113 的 `tags_nodes.cml` 还原为 `graph.cml`（内容不变），更新 `tags.plan.toml` 与 AST 快照，保住真实同名夹具。计划：`smoke` 10/10、`feat/compile` 1009/1010、`feat/consistency` 988/988、`feat/opt` 710/710、`feat/linear` 723/723；`feat/parse` 56/57 为基线 AST 漂移；两条红均非本批） |
| H125 | **`permissions.strict_algebra` 的期望与 7.4 冲突**（H112 §6.1 转交）：`#strict` 下 CSE 把 `x*0.1 + x*0.1` 合成 `p + p`，规则库的 `float_add_self`（`p + p => p*2.0`，无 `#requires`）再把它精确翻倍；旧断言却要求“1 个乘 + 1 个加” | 无 | **已完成**（报告 reports/H125.md；按 7.4 推导：`#strict` 只拿走许可，不禁止逐位等价的改写；`float_add_self` 对 ±0、次正规、最大有限值、溢出、±inf、NaN 都精确，16 行逐位对照无一不等，所以 H112 的诊断成立、旧期望陈旧，规则库无需改。用例改为断言属性：拒绝需要 `reassociate` 的 `x*0.2`，接受 CSE 与精确加倍（2 乘 1 加、1 乘 1 加、或 2 乘且有一个乘数 2.0），并用 `tightPair`（不同系数）证明权限仍有牙齿；新增无遍执行的 16 行精确性用例。`feat/compile` 1009/1009、`feat/opt/algebra` 192/192、`feat/modules/rules` 67/67、`smoke` 10/10；提交 `26938ce5b`） |
| H126 | H19 第 10 步的下一批模块声明化（`bytes`/`str`/`math`/`json` 之后）：迁移 `re`，`stdlib/re.cml` 写唯一一条声明 `test(pattern: string, text: string): bool`，删掉 C++ 的 `OperatorGroup`/`StaticFuncTypeResolver`、`exportEntity`、`stdlib/signatures/re.cml`，导出组来自声明；`re` 没有计算签名、值、资源、原地形式或泛型，全部签名都可迁移 | 无 | **已完成**（报告 reports/H126.md；`stdlib/re.cml` 一条无体 `export func`，`modules/re/module.cpp` 只剩执行器工厂、内核与 `test` 的纯度，`git grep '"re:'` 在 `src`/`include`/`modules`/`tools` 无结果；参考页无 diff；声明计划 32/32（新增 3 条：导入方看到声明、错误个数、错误类型），`--verify-ops` 367 条一致、`--verify-doc` 最新；`feat/modules`+`feat/compile`+`feat/linear`+`smoke` 2805 条、2801 通过、3 失败、1 跳过（`permissions.strict_algebra` 基线红、2 条 `graph_facts` 归 H121、`modules.unicode_paths` 仅 Windows），ONNX 126/126 全绿；`feat/parse/frontend` 3/4，`ast_snapshot` 的 13 条差异全是基线漂移，无一与 `re` 有关） |
| H127 | **H19 第 11 步的收尾**：删除文档与工具里旧的“函数体写 `return '<URI>'`”签名写法，确认编译器不再特判它，并给 `stdlib/signatures/*.cml` 加“条目不得带函数体”的门禁 | H19 | **已完成**（报告 reports/H127.md；`docs/technical/09_operator_registration.md` §8 与 `docs/stdlib-conventions.cn.md` 第 31 行改为无函数体的 `export func`，写明旧写法已删除、写了函数体就是普通函数、声明模块里报 `DeclarationModuleHasBody`；`scripts/stdlib-reference.mjs` 的 `--verify-ops`/`--verify-doc` 在解析前扫描 `stdlib/signatures/*.cml`，单行 `{ return '...' }`、`sync { ... }` 与下一行开体的声明都拒绝，结构体结果类型不误报；故意加回 `fake_size` 时 exit 1，撤销后 367 条算子一致；编译器无特判可删：`nativeDeclaration()` 只看 AST 有无函数体，`nativeOperatorUri()` 只读 `#op`，手工用例 `func f(): int { return 'str:len' }` 报 `ReturnTypeMismatch`（`0x03000011`），`func f(): string { return 'bytes:size' }` 打印 `bytes:size` 并 exit 0；`feat/compile` 1013/1014（1 红 `permissions.strict_algebra` 是 H112 §6.1/H113 §7.1 的 CSE 断言基线）、`feat/modules` 1058/1061（2 红 `graph_facts.rewrite_opt`/`rewrite_macro_simplify` 归 H121，1 跳过）、`feat/parse` 57/57、`smoke` 10/10、`check-doc-links --all` 357 篇 0 断链、`diagnostics-catalog --check` 202 个码；无新诊断） |
| H128 | H19 第 10 步的下一批模块声明化（`json` 之后）：迁移 `random`，`stdlib/random.cml` 写 7 条固定签名（`seed`/`rand`/`randn`/`randint`/`key`/`split`/`permutation`，逐条 `#op<'rand:...'>`），删掉 C++ 的 7 条静态组与签名文件里的对应条目，导出组来自声明；`choice`/`sample`/`shuffle` 的计算签名留在 C++，参考条目用 `computed: 'signatures/random.cml'` | 无 | **已完成**（报告 reports/H128.md；`stdlib/random.cml` 7 条无体 `export func`，`stdlib/signatures/random.cml` 只留 3 条计算签名；`modules/random` 删 7 条 `StaticFuncTypeResolver` 与 `tuple.h`，保留执行器、10 条纯度、7 条资源、`Key` 类型导出；声明计划 32/32（新增 3 条：导入方看到声明的类型、错元数、错类型）、`modules.random` 11/11、`modules.std` 25/25、`stdlib_registry` 4/4、`feat/parse/frontend` 4/4；`feat/compile` 1016/1017（`permissions.strict_algebra` 归 H125）、`feat/modules` 1064/1070（2 条 `graph_facts` 归 H121，3 条 `rename.*_graph_identity` 是 H114 记录的摘要过期，1 跳过）、`feat/linear` 723/723、`smoke` 10/10；`--verify-ops` 367 条一致、参考页 265 条、无新诊断） |
| H129 | **`-I` / `--include` 被解析后从未加入模块搜索路径（H124 §6 第 3 条）**：标志被接受、被静默忽略，只有模块恰好位于既有根下时才能编译 | 无 | **已完成**（报告 reports/H129.md；搜索顺序按 `docs/language/05_modules.md` §5.2：入口文件目录 → 按给出顺序的每个 `-I` 目录 → `CAMEL_PACKAGES` → 旧的 `CAMEL_MODULE_PATH` → 标准库；设计原则 §10/§15 没有这一节，`docs/cli.md` / `docs/cli.cn.md` 已补成同一顺序。库自己的 import 先看自己的目录（H112），再回退到这条列表，因此 `-I` 对库内 import 同样生效；相对路径按入口目录解析；不存在的 `-I` 根在 import 处由 `ModuleNotFound` 的搜索尝试点出并说明路径不存在，不静默通过。`-I` 在 clipp 之前从 argv 取出，否则 `values` 会把目标文件吃成第二个根。新增 8 条用例（只经 `-I` 可达、入口目录胜出与反向、两次 `-I`、不存在的根、`-I` 先于 `CAMEL_PACKAGES`、包根单独、库内 import）：`feat/compile/module_imports` 33/33。验收：`feat/compile` 1023/1024（`permissions.strict_algebra` 为 H125 基线红）、`feat/modules` 1064/1070（H121 facts 2 条、`rename_identity` 摘要 3 条基线红，1 条 Windows-only skip）、`smoke` 10/10；提交 `ebf001f51`、`60684d24e`） |
| H130 | **模块在、遍不可用的诊断（H124 的 A/C）：`module::pass` 解析不到时只报 “pass not found” 加全部遍，模块自己的原因被丢掉**——文件在但加载/编译失败也被记成“不存在”，模块在、遍不在时也不说它提供了哪些遍 | 无 | **已完成**（报告 reports/H130.md；三类回答分开：模块在但加载/编译失败仍为 `InvalidPassParameter`，编译错误照旧以 `note:` 链上文件、行、列，加载失败把加载器原话写进消息；模块在、提供不了这个遍用新增 `RuntimeDiag::PassNotProvidedByModule`（`0x04000186`）/`PassNotExportedByModule`（`0x04000187`），只写模块名和它提供的遍，不再列全部遍；模块不存在仍是 `UnrecognizedGraphPass` 加列表。解析顺序与按需 import 未动，只改报告；`docs/diagnostics.md` 重新生成（204 条）。`feat/modules` 新增 7 条用例、更新 `unknown_camel_pass`；`feat/compile` 1022/1022、`feat/consistency` 987/988、`feat/modules` 1062/1071（+2 跳过）、`smoke` 10/10，8 条红在还原到 `16a784009` 的同机复跑中逐条相同（基线）；提交 `0cba7132e`、`8e9027f46`） |
| H131 | H19 第 10 步的下一批模块声明化（`bytes`/`str`/`math`/`json`/`re` 之后）：迁移 `fs`，`stdlib/fs.cml` 写 15 条声明（签名的唯一来源），删掉 C++ 的 `OperatorGroup`/`StaticFuncTypeResolver`、`exportEntity`、`stdlib/signatures/fs.cml`，导出组来自声明；`fs` 每个算子都有副作用，效果与资源的事实留在 C++ 的纯度与 `declareResources`，不写进声明 | 无 | **已完成**（报告 reports/H131.md；`stdlib/fs.cml` 15 条无体 `export func`，`modules/fs/module.cpp` 只剩执行器工厂、内核、纯度与资源，`git grep '"fs:'` 在 `src`/`include`/`modules`/`tools` 只剩一句注释；参考页 263 条、`--verify-doc` 最新、`--verify-ops` 365 条一致；`inspect --ops` 的 15 条 fs 记录只多了参数名，纯度/资源/语义逐条不变（8 条 read、7 条 rmw，全部 ambient external）；`std::jgir` 三份样本把 origin id 归一后逐字节相同（419fc92a/cd5f0e9d/e4d82a53）；声明计划 37/37（新增 3 条：导入方看到声明、错误个数、错误类型）；验收 `feat/modules`+`feat/compile`+`feat/linear`+`smoke`+`feat/examples` 2933 条、2926 通过、5 失败、2 跳过（2 条 `graph_facts` 归 H121；3 条 `rename.*_graph_identity` 是 H114 的过期摘要，在 `f9b796586` 与把 fs 还原的 `16a784009` 上同样失败，与 fs 无关；`modules.unicode_paths` 仅 Windows、`nn.mnist_py` 缺前置条件）；`feat.examples` 111/111；另修掉起点 `scripts/stdlib-reference.mjs` 里 H126 合并留下的重复 `re` 与已删的 `crypto` 两项（起点 `--verify-*` 直接 ENOENT）；AST 快照只记本批 4 行） |
| H132 | **`feat/mm` 快层的 `saturate.deterministic` 红**：期望 `integer_chain ... matches=26`，实际 33——判断是计数移动还是确定性破坏，并把用例改成性质 | 无 | **已完成**（报告 reports/H132.md；实际行 `SATURATE-INFO integer_chain rounds=3 matches=33 writings=6 unions=6 saturated=1 erased=10`；只有 `matches` 26→33，`rounds/writings/unions/saturated` 与期望相同，三次运行逐字节相同、nvm/fvm 相同 → **计数移动，不是确定性**；原因 H118 `f037fbbf7` 的交换律匹配回退在快照 `70e1e7d3b` 之后落地，H118 跑过的五个计划不含 `feat/mm`。不保留数字：新增 `test::saturate_props`，断言终止（`SATURATE-PROPS targets=10 saturated=10`）、不动点（`SATURATE-AGAIN ... writings=0 unions=0`）、两次会话/两个后端/两个进程一致（`stable=10`、`backends_agree`、differential），并断言十条抽取后的算子（少一条规则/预算不够会红）；`feat/mm` 175/175、`feat/opt` 710/710、`feat/modules/rules` 67/67、`smoke` 10/10、`feat/mm/writings` 22/22。另修 H120 遗留的 `rename identity` sweep 副本取键与摘要 `origin` 漂移（`identity.mjs`），`rename_identity` 3/3；合并到当前 develop 时三条摘要取 develop 的值、保留取键。提交 `90169c7bd`、`f465ff396`） |
| H133 | H19 第 10 步的下一批模块声明化（`random` 之后）：迁移 `time`，`stdlib/time.cml` 写三条固定签名（`now`/`strftime`/`strptime`），删掉 C++ 的三条 `StaticFuncTypeResolver`、`exportEntity` 与 `stdlib/signatures/time.cml`，导出组来自声明；`time` 没有计算签名、值、泛型、原地形式或核心前缀，三条全部迁移 | 无 | **已完成**（报告 reports/H133.md；`stdlib/time.cml` 3 条无体 `export func`，`modules/time/module.cpp` 只剩执行器工厂、内核、三条纯度与三条资源（`time:clock`/`process:environment` 按 H19 第 9 步再改名）；声明计划 40/40（新增 3 条：导入方看到声明的类型、错元数、错类型），`feat/parse/frontend` 4/4、`stdlib_registry` 4/4；`feat/compile` 1036/1036、`feat/linear` 723/723、`smoke` 10/10、`feat/modules` 1058/1064（2 条 `graph_facts` 归 H121，3 条 `rename.*_graph_identity` 是 H114 的旧摘要，1 跳过），总 2833 条 2827 通过，`permissions.strict_algebra` 已转绿；`--verify-ops` 365 条、参考页 263 条无 diff；无新诊断；`smoke` 的 `rgir.dot` 只更新 24 行 `origin=`（声明模块的 token 使 origin 后移 44，结构不变）） |
| H134 | **H19 第 9 步的资源名**：把已声明化模块的资源名改成原则 3.3 的名字（`io:stdout`→`stdout`、`io:stdin`→`stdin`、`io:stderr`→`stderr`、`time:clock`→`clock`、`process:environment`→`env`、`rand:generator`→`rng`），`rand:generator` 是 H128 留下的债；`io:terminal`→`stdin` 是资源集变化，不做，留给作者 | 无 | **已完成（6 个资源改名；1 个资源集变化未做）**（报告 reports/H134.md；声明处 `builtin_semantics.cpp`、`modules/{env,io,process,random,tensor,time,tree}`；`resources.plan` 的期望逐条改并新增 `resource_names` 用例，用 `inspect --ops` 钉住 14 个环境/外部资源名，`io:set_terminal_raw_mode` 仍是 `io:terminal`；`docs/technical/41`、设计稿与 `docs/design/stdlib` 口径同步；无新诊断（202 条，`--check` 过）；`smoke` 10/10、`feat/modules` 1061 过/1065（2 条 `graph_facts` 归 H121，2 跳过）、`feat/compile` 1033/1033、`feat/linear` 723/723、`feat/opt` 710/710） |
| H135 | H19 第 10 步的下一批模块声明化（`random` 之后），一次两个模块、一个模块一个提交：`algo` 的 `argsort` 6 条声明（结果恒 `int[]`，六种键类型原就是 C++ 解析器的六条规则），7 个结果类型取自数组元素的名字（`sort` 族、`merge_sorted_arrays`、`flatten`、`unique`）留 C++ 并用 `computed:`；`statistics` 5 个名字 20 条声明全部可声明（`float`/`float?` 不随元素类型变），删掉 C++ 的伞形 `isNumberArray`/`numbers()` 解析器与整个 `stdlib/signatures/statistics.cml` | 无 | **已完成**（报告 reports/H135.md；`argsort` 的组从 `modules/algo/sequence.cpp` 删掉、执行器/内核/原地形式未动；`modules/statistics/module.cpp` 130→62 行，只剩执行器工厂与 5 条纯度；生成器把两模块改成 `source:`+`declaration:`，`algo` 另读 `computed: 'signatures/algo.cml'`；参考页 263 条，唯一 diff 是 `quantile` 四条重载各带 `<q: float>`；声明计划 43/43、`feat/compile` 1039/1039、`feat/linear` 723/723、`smoke` 10/10、`feat/parse/frontend` 4/4；`feat/modules` 1057/1064，5 红 = 2 条 `graph_facts`（H121）+ 3 条 `rename.*_graph_identity`（查清是 H114 的 identity 脚本拿不到 `case_path`、摘要里留下 checkout 路径，见报告 §8），2 跳过 = `mnist_py.simplify`（MNIST 前置）+ `unicode_paths`（仅 Windows）；`--verify-ops` 385 条一致、`--verify-doc` 最新、无新诊断） |
| H136 | **文档扫尾（H114/H122/H107/H127 的遗留）**：把 `docs/`、`editors/`、`tools/`、`scripts/`、`examples/`、`README*` 与 `src`/`modules`/`include` 注释里仍把退役名字当作现行写法的页面改掉；历史报告只加注记；新增门禁，页面再写旧名时失败 | 无 | **已完成**（报告 reports/H136.md；改 4 页 5 处：`docs/technical/30…` 的 `os.exit`→`process:exit`、`design/module-root` 的 `inner func`、`design/stdlib` 的 `sys`/`os`、`design/training` 的 `str::from_bytes`；两份日期化记录只加注记；`docs/handover/`、`docs/reviews/` 是记录不扫描；测试负例保留并列在报告 §5；新增 `scripts/check-retired-names.mjs` 并接入 `feat/docs_links`（`docs.retired_names_absent`），干净树 409 篇 0 命中，故意加回 `sys:get_env` 时 exit 1；`docs_links` 2/2、`smoke` 10/10、`feat/compile` 1036/1036、`feat/parse` 57/57；`feat/modules` 1057 通过 / 5 失败 / 2 跳过（2 条 H121 `graph_facts`、3 条 H114 过期摘要，均基线）；另发现 `test/tools/selftest/.../reuse.cml` 仍 `import … from sys`，`feat/consistency/runner` 因此红，报告 §8） |
| H137 | **已迁移的声明模块回归门禁**：H19 的声明化（`bytes`/`str`/`math`/`json`/`io`/`re`/`random`/`fs`，以及后来的 `time`/`algo`/`statistics`/`ckpt`）只做一次迁移，没有检查“迁过的模块还留在迁移状态”——后来的改动可以重新加回 `StaticFuncTypeResolver`、留下旧签名文件，`--verify-ops` 仍绿 | 无 | **已完成**（报告 reports/H137.md；新增 `scripts/stdlib-declarations.mjs --check`，从树里认声明模块（`stdlib/<m>.cml` 的 `module <m>` + C++ `BuiltinModule("<m>")`），四条检查：声明名无静态解析器、签名文件只在 `computed:` 命名时存在、每条声明有算子（复用 `--verify-ops`，365 条）、导出组只有计算签名；接入 `feat/modules/stdlib_registry` 的 `registry.migrations_stay_migrated`。给定起点 8 个声明模块；合并 `origin/develop`（H121/H132/H133、`time`）后 9 个，0 个问题。例外 8 条计算签名：`math` 的 `sqrt`/`shl`/`shr`（`// dynamic func`）、`random` 的 `choice`/`sample`/`shuffle` 与 `json` 的 `encode`/`decode_as`（`computed:` 签名文件）。红证：给 `re` 加回 `test` 的 `StaticFuncTypeResolver` → `modules/re/module.cpp:44 ... 're' ... 'test'`；留下 `stdlib/signatures/re.cml` → 点名该文件；撤销后 5/5。验收：`smoke` 10/10、`feat/compile` 1039/1039、`feat/modules` 1061/1065（3 条 `rename.*_graph_identity` 是 H114 旧摘要，起点即红；1 条仅 Windows），总 2114 条 2110 过；无新诊断（202 个码，0x185 保留、未用 0x186+） |
| H138 | **构建缺陷：改名后的旧 `.cmo` 留在源码树与 `build/modules/<旧名>/<配置>/`，构建把它复制回 `stdlib/`，安装再复制进 `out/latest/stdlib/`，旧名仍然可解析**——H114 §6.2 只手工删了安装里的三个文件，所以 `rename.{env,process,hash}_old_name_rejected` 的红绿取决于有没有人手删；让 `.cmo` 不能比它的模块活得久 | 无 | **已完成**（报告 reports/H138.md；把 `.cmo` 写进源码树的是 `scripts/common.js` 的 `copyBuildArtifacts()`（`290eb3fce` 的 `:343-361`，写动作 `:356-357`），复制进安装的是 `scripts/collect-out.js:273-294`（`:274` 整体复制 `stdlib/`、`:289-290` 扫 build）；这棵树里没有编译 `stdlib/*.cml` 的 CMake target，`.cmo` 全部来自 `add_camel_module`。修复：CMake 在 configure 时为每个配置写 `build/camel-module-outputs-<配置>.txt`（`$<TARGET_FILE>`，`CMakeLists.txt:237-258`、`modules/camel_module.cmake:26`），构建与安装只安装清单上的 `.cmo`，并删除 `stdlib/`、`out/<tag>/stdlib/` 与 `build/{libs,modules}/<目录>/<配置>/` 里清单外的 `.cmo`，搜索顺序不变。证明：放置 5 个陈旧 `.cmo`（含 `stdlib/sys.cmo`、`out/latest/stdlib/sys.cmo`、`build/modules/sys/Release/sys.cmo`）后重建，全部删除，`import sys` 得到定位 `env_old.cml:6:1` 的 `SemanticDiag::ModuleNotFound`，`env`/`process`/`hash` 正例仍通过；用例与计划一行未改，三条反例在新构建、紧接着再跑、放置后重建三次都 6/6。验收：`feat/compile` 1036/1036、`smoke` 10/10、`rename` 6/6，`feat/modules` 1064 条中 3 红是既有基线 `rename.*_graph_identity`（在本分支基点 `290eb3fce` 上就是红的，H129 §5 已转交图侧） |
| H139 | **测试缺陷（H133 暴露）**：编译器的 origin 编号是构建内部标识符，随“在被断言程序之前建了多少 AST 节点”整体平移，文本 golden 却把它当结构 pin——H133 迁移 `time` 后 smoke `rgir.dot` 的 24 个 `origin=` 偏移 44、只能重生成；`rename.*_graph_identity` 早已在算摘要前丢掉它。按设计原则 2.5（名字、源码位置不计入图结构）在比较时归一化：扩展 `normalize_origins` 的 DOT 形式（`origin=63`→`origin=<ORIGIN>`），应用到 3 个 `text_exact` 快照，`gir_dot_isomorphic` 不动；`span`/`srcfile` 仍逐字比较 | 无 | **已完成**（报告 reports/H139.md；`normalize_origins` 覆盖 DOT 与 jgir JSON 两种形式并补进 `docs/test-plans.md`；golden 用 `--update-golden` 在主机重生成：smoke `rgir.dot` 24 行、trans `boundaries.agir`/`argir` 各 23 行，取回文件与提交前差异只有 origin 行；最终 smoke+feat/parse+feat/compile+annotated_dump 1108/1108；证明一：fib 多一个 `+ 0` 仍红（`node count mismatch actual=36, expected=34`，开规范化器的 rgir 快照同样红）；证明二：`stdlib/time.cml` 加一个 `type` 使 24 个 ids 271–309→275–313、计划仍 10/10，撤销后恢复（记录：注释行不加 origin，origin 按 AST 节点发放；`-NoBuild` 读到的是 `out/latest/stdlib` 旧副本）；证明三：错行诊断由 `feat/parse/core` 的 `syntax.missing_then`（line 8 col 21）与 `feat/compile/native_declarations` 的 `time_arity.cml:10:21`/`time_type.cml:11:21` 覆盖；没有 golden 需要保留编号，规范化器按计划点名） |
| H140 | H19 第 10 步的下一批模块声明化：`ckpt`（H108 由 `tree` 改名）。把 `stdlib/signatures/ckpt.cml` 搬成声明模块、删掉 C++ 里重复的签名事实、导出组来自声明；遍历（`save_at`）是 H108 的另一个问题，不碰 | 无 | **已完成（调查结论：不迁移）**（报告 reports/H140.md；`ckpt` 的 7 个算子没有一个能写成声明：五个 intrinsic 没有执行器，声明在加载时报 `0x0300017C`（探针 3.1）；`save_tree` 能声明但丢掉编译期叶子检查，`unsupported_leaf_is_a_compile_error` 从 `0x0300001C` + `tree leaf 'weights'` 退化成运行期 `0x04000036`（探针 3.2）；`load_tree` 的结果类型取自 `::<T>`/期望类型，即 H103 的 `native.declaration_type_argument`。因此没有搬文件、没有删 C++、没有新增用例（没有声明可测），`stdlib/signatures/ckpt.cml` 与 `modules/tree` 原样。效果与资源证据：同一份 `std::jgir` 样本在"`save_tree` 声明化"前后把 origin 归一后逐字节相同（SHA-256 `3f68c7a5…75b`），`inspect --ops` 的 purity/resources/semantics 逐字不变，只有签名文本多出参数名、`with/norm` 从 `-2` 变成 `0/2–3`。H108 的阻塞状态不变（作者裁定未下），本轮补的是"声明迁移也被同一批缺口挡住"；`--verify-ops` 365 条一致、参考页无 diff、诊断 202 条；验收 2836 条 2832 通过 / 3 失败（H114 的 `rename.*_graph_identity` 旧摘要，基线同红）/ 1 跳过（Windows-only），与改动前的基线一致） |
| H141 | **H19 第 10 步：`graph` 声明化，并调查 `globals` 能否同样声明**。`graph` 的算子是图值的算子（`Program`/`Graph`/`Node`/`Op`/`Region`/`TypeRef`/`Tag`/`Txn`）：声明里能命名的都写成 `stdlib/graph.cml` 的无体声明，C++ 只留算不出的签名；`globals` 是语言全局而不是模块，结论与理由见报告 | 无 | **已完成（`graph` 已声明化；`globals` 不迁移，留待作者裁定）**（报告 reports/H141.md；`stdlib/signatures/graph.cml` → `stdlib/graph.cml`：45 条 `export func`（37 条具名，含 `id` 的 3 个重载；8 条 `operator ==`），6 条计算签名以 `// dynamic func` 留在 C++（`constant`/`add_constant` 取 `sample` 的类型，`open_function`/`to_function`/`program_of`/`graph_of` 取函数值的类型），`!=` 按设计原则 4.3 不能声明、由 C++ 的 `__neq__` 提供（编译器只否定 Camel 的 `==` 重载，原生 `set.ops` 不否定），H137 检查器为 `__neq__` 加一条注明原因的例外；`modules/graph` 删掉 30 组 `StaticFuncTypeResolver`/`exportEntity` 与 `factGroups()`，`ReadOp`/`TxnOp`/`FactOp` 只剩 `{name, kernel}`，纯度的 43 名平表、资源、执行器与原地形式原样保留，`Effects` 结构类型由 C++ `exportType` 命名；修了 `exportOperatorOverloads` 把“已由 `exportNativeOperators` 导出的无体算子组”误报 `UnresolvedReference` 的一处（`src/compile/gir/builder_operators.cpp`）；`--verify-ops` 365 → **367**、`inspect --ops` 57 → 59（只多 `id` 的 `Graph`/`Op` 两个重载，其余名字/URI/元数/纯度逐条不变）、参考页 263 条只 +2 行、`std::jgir`（`graph_types.cml`）origin 归一并 972 行逐字节相同（`eb2e85ae…`，原始 31 行差异全是 origin id）、无新诊断；声明计划 43 → **46/46**（导入方看到声明类型、错元数 `10:17`、错类型 `11:17`，均 `0x0300000F`），`feat.modules.graph` 51/51、`feat/compile` 1042/1042、`feat/linear` 723/723、`smoke` 10/10、`feat/parse` 57/57 且快照 2510 文件 0 未记录；`feat/modules` 1061/1065（3 条 `rename.*_graph_identity` 是 H114 旧摘要，起点即红；1 条仅 Windows）；`globals`：`BuiltinModule("")`、不可导入（`import globals` 是 `ModuleNotFound`）、93 条签名中 43 条通配 URI、`print`/`println`/`with` 变参、`std::mem::blocks` 非标识符、`sleep`/`exit` 绑 `process:` 前缀（在 `globals` 里声明即 `NativeDeclarationForeignOperator`），`stdlib/signatures/globals.cml` 未动，建议由作者裁定全局的声明形态） |
| H142 | **H19 声明化的启动开销**：在 Linux 主机上量 `camel --version`、一行无 import 的程序、`camel inspect --ops`（导入全部 22 个标准库模块）以及同一个程序的运行，当前 develop 对迁移前的基线 `fea0c9d8e`（H76 合并 `c08ff3a3b` 的第一父；`git ls-tree fea0c9d8e stdlib/` 没有 9 个声明模块） | 无 | **已完成**（报告 reports/H142.md；主机 `88zjwei` 不空闲，负载 35–215；`hyperfine` 未装，用 101 次交替循环 + `getrusage` 的子进程 CPU，中位数与 p10/p90 都记了。启动路径无回归：`--version` 5.27→5.39 ms CPU（+2.3%），无 import 的 7.80→8.10 ms（+3.8%），阶段报告里没有任何标准库模块的 parse/cmo_load。导入路径有：`inspect --ops` 18.83→24.96 ms CPU（+32.6%），`imports_all` 18.12→23.92（+32.0%），只 `import math` 8.39→10.83（+29.1%）；墙钟中位数 40.59→53.35 / 38.98→51.45 ms（负载 148–215，配对差 +11.5/+12.1 ms）。归因：9 个声明模块的 parse 2.38 + gct 1.37 + declare 0.82 = 4.4–4.6 ms（`math` 一个模块 2.2 ms，96/182 条声明），基线这 9 个模块的 `cmo_load` 2.35 ms、当前 2.71 ms，所以 C++ 侧没有变便宜，净增约 4.9 ms；文件读/调度尖峰和注释都被排除（去 60% 注释不降）。决定：不实施修改；最便宜的缓存（读 9 个源实测 0.19 ms，省约 4.4 ms）与惰性声明都改变磁盘/错误语义，作为作者问题 Q1/Q2；新增 `perf.startup`（`test/tools/startup-cost.mjs` + 一行 fixture），天花板 40/45/100/90 ms（后两条约为安静中位数的 4 倍，前两条 5–8 倍：5–8 ms 低于主机调度噪声，负载 215 时 20/36.5 ms），`-Jobs 8` 1.4–1.6 s PASS，但共享主机的墙钟分辨不了 4.6 ms，这个数按报告记录） |
| H143 | **测试基础设施：`verify_script` 的 `case_path` 是 `undefined`，三条 `rename.*_graph_identity` 的摘要含 checkout 绝对路径**（H131/H135 都踩过，只在集成检出 `verify` 上绿）：修 payload，让摘要与检出位置无关，并用第二个路径的检出证明 | 无 | **已完成**（报告 reports/H143.md；两个缺陷：`run-tests.mjs` 在 `validateTest` 返回后才把 `casePath` 加到结果上，payload 的 `case_path: result.casePath` 拿到 `undefined`、`JSON.stringify` 丢键；`identity.mjs` 用 `input.case_path` 替换 jgir 的 `source.file`，键缺失时替换等于没做，绝对路径留在摘要里。修：`casePath` 在构造 result 时设置，payload 与 `meta.json` 都拿到 `path.resolve(计划目录, case)`；`docs/test-plans.md` 写明 `case_path` 及 `repo_root`/`test_root`/`plan_path`/`test_name`/`test_tmp*`；`identity.mjs` 优先用 `case_path`，缺键时按 case 文件名剥掉任意绝对前缀（POSIX/Windows，保留文件名），规范化后仍有绝对路径就报错拒算；新增 `payload.case_path` runner 自测。新记录值 env `8c4c5bb6…`、process `8c8b9948…`、hash `60047e7f…`。证明：`h143-payload` 与另一路径的 `h143-second` 两次独立构建各 3/3，六份原始 jgir 文档本地重算与记录值逐一相等；验收 `smoke`+`feat/modules`+`feat/compile`+`feat/consistency` 3108 条 3105 过 0 红 3 跳过。取证：同一份文档把路径换成 `~/camel-work/verify` 得 develop 的记录值 `e25ec979…`，换成 `CASE` 得 H135 的 `9f63f49b…`——旧摘要按检出路径记录，所以只在 `verify` 上绿；H131 的“换提交重建仍红”没换路径，才误判成图变了） |
| H144 | **`compile_bench.smoke` 红：`chain-3/passes` 期望 4 个 user modules，实际 6**——`counters.modules` 数的是本次运行编译过的每个 Camel 模块，H100 之后 `std::opt` 的代数步按需编译 `stdlib/rules.cml`，`rules` 的 `import math` 又带出声明模块 `stdlib/math.cml`，于是 4 个程序自己的模块变成 6；H121 已证明按需加载正确，所以错的是“user modules”这个测量，不是编译器。不删数：从 `gct` 阶段的 `detail` 取出完整模块名单，报告同时给 user（程序自己的模块，chain-3 仍为 4）与 total（6），库模块单独列出并单独钉住 | 无 | **已完成**（报告 reports/H144.md；`bench.py` 的 `module_names()`/`classify_modules()` 从阶段报告得到 loaded/user/library，链-3 的 own 固定 `main, bm_0000, bm_0001, bm_0002`，`passes` 的库模块固定 `rules, math`（`PASS_LIBRARY_MODULES` 带“库重构时允许改”的注释），`compile`/`fvm` 固定无库模块，并断言 `counters.modules` 等于名单长度；阶段表加 `modules (user / total)` 与 `library modules` 两行，规模表加 `user`/`total` 两列，旧列一个未删；失败信息给出实际模块、期望与哪些是程序自己的。负例：把库模块期望改成只剩 `rules`，check 退出 1 并指出 `['rules','math']` 与 own 名单。验收：`compile_bench.smoke` 1/1、单独 1.15 s；`feat/tools` 30 条中 19 通过、1 失败、10 跳过，唯一失败 `lsp.editing` 在 develop 的 `verify`（`9d1edc06e`）上逐字复现，是既有基线红；主机构建 625 targets 通过） |
| H145 | **迁移后的声明模块在 `::` 后不再列出成员**：`lsp.editing` 的 `the exports of a module after ::` 与 `with their signatures` 红，`import math` 后打 `math::` 补全为空；H19 把 11 个模块的签名搬进 `stdlib/<m>.cml` 后，语言服务器看不到这些导出（`bytes`、`str`、`math`、`json`、`io`、`re`、`random`、`fs`、`time`、`algo`、`statistics`） | 无 | **已完成**（报告 reports/H145.md；洞在 `src/service/semantic/analyze.cpp:991`（基点 `e5544f744`）的 `if (!mod \|\| !mod->loaded()) continue;`：声明模块停在 `Declared`，`loaded()` 为假，整个命名空间被跳过；枚举成员的两处（`programExports().forEachExportedEntity` 与 `nativeEntityNS()`）本身已是并集。加载器返回的一个模块对象上，C++ 计算签名经 `Context::loadModuleFrom` 的 `inheritExportsFrom` 继承，声明经 `declareModule` 的 `exportEntity` 注册，因此无需第二份签名或 LSP 自己的 `.cml` 解析。修复：`analyze.cpp:990-1013` 对未加载模块先 `declareEntities()`（与 `ProgramExports::importedEntity`、`camel inspect --ops` 同一条既有 API），每模块 `try/catch`；另修 `queries.cpp:398-435` 运算符调用的 `external` 为空，hover 不再把 `math::sqrt` 说成 built-in。用例覆盖两种形态：`math`（`sqrt` 计算签名逐字保留 `(val: float \| double) => float \| double`，`abs` 声明 6 重载、`(+5 overloads)`）、`re`（只有 `test`，声明签名逐字）；补全、hover、签名帮助都断言，95→105 条。`lsp.editing` 105/105、`tools.lsp` 9/9；验收 2151 条、2135 通过、4 红、12 skip，4 红均为起点即红（`compile_bench.smoke` 的 chain-3 计数 4→6，H112 §5；`rename.{env,process,hash}_graph_identity` 3 条，H131/H135/H137 板行）；`feat/compile` 1045/1045、`feat/modules` 1061/1066、`smoke` 10/10；未改编译器声明处理，无新诊断，未改 `.cml` 与 AST 快照） |
| H146 | H19 第 10 步的下一批模块声明化：`para` 与 `this`（最靠近语言本身的两个模块；一个模块一个提交，`para` 在前）。`para` 的 `rendezvous` 一条固定签名可声明；`unordered_foreach`/`unordered_reduce` 的签名（包括 `unordered_foreach` 接受的函数类型）取自数组元素类型，按计算签名留在 C++。`this` 的 `zen` 没有参数、没有计算，整个签名文件搬成声明模块 | 无 | **已完成**（报告 reports/H146.md；`stdlib/signatures/para.cml` → `stdlib/para.cml` 只留 `rendezvous` 一条无体声明，`signatures/para.cml` 只留两条 `#op<':mark/...'>` 计算条目、由模块项的 `computed:` 命名；`modules/para` 删掉 `rendezvous` 的 `OperatorGroup`/`StaticFuncTypeResolver`，保留两个动态组、导出循环、执行器、纯度与 `para:rendezvous` 资源。`stdlib/signatures/this.cml` → `stdlib/this.cml`（`zen` 一条），删掉 `modules/this` 的 `exportEntity`/静态组与 `resolver.h`，保留执行器与纯度。`unordered_foreach` 不按 `(item: T) => void` 声明：C++ 解析器不看回调返回类型，`test/cases/para/core/map_chunks.cml:42` 的 `(x: int) => x` 会被更严的声明拒绝。效果与资源逐条不变（`inspect --ops`：`para` 3 条、`this` 1 条，只有 `rendezvous` 的签名文本多了参数名；`rendezvous` 仍 effectful + `rmw ambient para:rendezvous; external`，`zen` 仍 pure）；两份 `std::jgir` 样本归一 origin 后 SHA-256 不变（`b6199b19…9741a`、`b0579b5d…c1222`，原始差异只有 origin 行）；参考页无 diff、`--verify-doc` 最新、`--verify-ops` 385 条一致、`stdlib-declarations --check` 13 个声明模块 0 问题、诊断 202 条无新增；声明用例 55/55（新增 6 条：两模块的导入方看到声明类型、错元数、错类型；`this` 无值参数，错类型与错个数重合，报告 §5 写明）；验收 `feat/compile` 1051/1051、`feat/linear` 723/723、`feat/modules` 1062 通过 / 3 失败 / 1 跳过（3 红是 H114 的 `rename.*_graph_identity` 旧摘要，基线同红）、`smoke` 10/10，合计 2850 条 2846 通过 / 3 失败 / 1 跳过，与改动前基线 2844 条相比只多 6 条新用例；`feat/parse/frontend` 4/4，AST 快照只改本批 9 行新增、2 行删除；提交 `62e40db21`、`a25d6ad47`） |
| H147 | **v0.2.24 发布提案**：发布版本与下一个开发版本、按用户可见变化重写 `changes.log`、就绪清单、快层门禁 | 无 | **已完成（未发布）**（报告 reports/H147.md、reports/H147-release.md；发布版本 **v0.2.24**（树里五个版本文件已是 `0.2.24`，bump 提交 `74394322f`），下一个开发版本 **v0.2.25**；`changes.log` 现有的 22 条草案重复 `[0.2.23]`、写进内部重构、漏掉整个 H19 迁移，提案给 20 条替换文本并逐条附依据，文本只在报告的围栏里，**没有改树**；门禁快层 5046 条：**5028 通过 / 7 失败 / 11 跳过**（`-Jobs 8`，构建 716.6 s、计划 98.9 s），`--verify-ops` **385** 条一致、`--verify-doc` 一致、诊断目录 **202** 条、文档链接 **377** 篇 0 断链；当时判定 **不建议直接发**：`lsp.editing`（`math::` 的补全为空）与 `compile_bench.smoke`（模块数 4 → 6）是声明化留下的两条真回归，3 条 `rename.*_graph_identity` 是过期摘要（改名正例全绿），`debugger.dap_protocol` 是 slow 层红（80 个检查里 4 个）；上述三条阻断（3 条摘要、`compile_bench`、`lsp.editing`）其后分别由 H143、H144、H145 修掉，本行的门禁数字是当时那份树上的记录，是否发布仍由作者决定（H153 校正，依据 H143、H144、H145）；没有 tag、没有 bump、没有合 `main`、没有 push） |
| H148 | **语言作者待答问题汇总**：把 H91/H92/H95/H96/H87/H103/H105/H108/H112/H118/H121/H124/H134/H140 以及 `origin/develop` 的 H142/H146 报告与看板里的待决项合成一页（16 个问题 + 4 个文档缺陷 + 已答项），按解锁任务数排序，标 Q1..Q16 可引用 | 无 | **已完成**（报告 reports/H148.md；不裁定任何语义、不改报告/代码；Q1–Q3 是 ckpt/tree 的三把锁；Q11 回调返回类型，Q12/Q13 是 H142 的缓存/惰性质量项；文档缺陷 D1 §3.10 捕获表、D2 §3.3 按对象资源自相矛盾、D3 H19 第 10 步 ckpt 范围、D4 看板 H19/H44/H69 记录过期；“已答”节含 H96 rest/constas、H118 整数张量交换律、H140 五个 intrinsic、H108 选项 2 等） |
| H5b | 在声明类型的模块里找算子（设计原则 5.7，按 H5 报告第 5 节的计划） | 无 | **已完成**（`6e54035c`；报告 reports/H5b.md） |
| H15b | 复数：`c64`、`c128`、虚数字面量；数据块布局与 `slotShaped` 判定；H28 评审第 1、2 项 | 无 | **已完成**（`92ea1d60`、`52c084eb`；报告 reports/H15b.md） |
| H15c | 复数收尾：格式说明作用于两部分、数值契约条目、`^`、`std::cpp` 降低（或先定位拒绝）、DLPack 往返测试 | 无 | **已完成**（报告 reports/H15c.md） |
| H15d | 带符号 NaN 在格式说明下打印为 `nan`；复数入口参数 | 无 | **已完成**（`3b34ed35`；报告 reports/H15d.md） |
| H15g | `i32`/`f32` 入口参数按声明类型读取；`let x: int = '42'` 的诊断改为类型不符 | 无 | **已完成**（`04bf4b6b`；报告 reports/H15g.md） |
| H15h | 结构体字段的文本诊断定位、转换为自身类型通过、`string = 42` 的类型不符诊断、入口与源码同一数字文法 | 无 | **已完成**（`4bb60b1a`；报告 reports/H15h.md） |
| H15i | 下溢一致、`inf`/`nan` 数据文本、`09` 与 `077` 的词法、元组字面量分支、删除不可达诊断 | 无 | **已完成**（`584ef85c`；报告 reports/H15i.md） |
| H17c | 追查小程序变慢（红线）；循环状态原地写推广到多状态 | 无 | **已完成**（`adb7f914`；报告 reports/H17c.md） |
| H17d | 查清 fvm/jit 上约 5% 的残留（按存储类别特化热循环）；H17c 评审第 1、2 项 | 无 | **已完成**（`cba1d32b`；报告 reports/H17d.md） |
| H149 | **`camel-remote.ps1 -NoBuild` 静默复用宿主机的 `out/latest`，而它可能是另一个 revision 构建的**：H129 因此丢 9 条（`imports.include_root_*`、`native.random_*` 在旧二进制上红）、H132 丢 1 条（新注册的 `test::saturate_props` 不在旧二进制里），H139 更糟——`-NoBuild` 读 `out/latest/stdlib` 这份构建时的副本，`stdlib/` 的改动根本看不到，于是产出一次没有意义的绿；两个方向都不出声。让构建把"这份安装来自哪份源码"写进 `out/<tag>/build-info.json`，`-NoBuild` 在主机上比较，内容不同就退出 4 并打印两边 revision、两边指纹和变动的文件 | 无 | **已完成**（报告 reports/H149.md；`scripts/common.js` 新增 `sourceFingerprint()`/`gitState()`/`writeBuildInfo()`，`scripts/collect-out.js` 在每次构建末尾写戳（Release/Debug/每个 release 镜像 tag 各一份，本地构建同样写），新增 `scripts/build-info.js`（`check`/`show`/`stamp`）；指纹 = 构建会读的 9 个路径（`src/ include/ modules/ tools/ stdlib/ third_party/ cmake/ CMakeLists.txt conanfile.txt`）上 1125 个文件的 `相对路径\0SHA-256(内容)` 排序聚合（同一内容在 Windows 与 Linux 上都是 `edd3ba1b8fe7`），`docs/ test/ scripts/ agents/ editors/ pypi/` 不在内（用例在跑计划时读，不需要重建；改 `collect-out` 这类安装配方要自愿重建一次，报告 §5.3），`stdlib/` 在内（H139 的假绿）；`camel-remote.ps1 -NoBuild` 在 plan 之前对计划**实际会用**的安装（`-Tier debug` → `out/debug`，其余 `out/latest`）跑 `check`，不匹配退出 4 并打印 `built from:` / `this tree:` / `changed since the build:`，没有戳（旧安装）同样拒绝，`-AllowStale` 是显式覆盖（仍打印不匹配并声明结果不描述这棵树），分支没有 `scripts/build-info.js`（守卫之前）只告警，git 不可用只把 revision 记成 null（指纹仍有效）。四段记录（Linux 主机，`-Jobs 8`）：改 `src/` 一行注释 → **拒绝**（exit 4；两边 HEAD 同为 `4579d211d6`，脏 4 vs 5，指纹 `edd3ba1b8fe7` vs `1c9e74e56cda`，点名 `src/core/source/manager.cpp`）；只改 `test/cases` → 放行且 smoke 10/10；重建（`9582e32d69 (clean)`）后 `-NoBuild` **静默通过**；只改 `docs/`（本报告与看板）→ 放行。另两条：提交前后内容相同（`4579d211d6 (dirty: 4)` → `9582e32d69 (clean)`）放行，证明比的是内容不是 commit；`-AllowStale` 打印全部不匹配后继续。验收：首次构建 129.9 s；`smoke` + `feat` 快层 5046 条 5033 过 / 1 红 / 12 skip，红是 `feat.tools.lsp/lsp.editing`（develop 既有，H144 记录在同一主机 `verify` 上复现），skip 是 10 条 `grammar.scopes`（缺 VS Code 依赖）+ `modules.unicode_paths` + `modules.nn.mnist_py`；没有新增红。提交 `9582e32d6`（构建戳与 check）与本报告所在提交（报告 + 看板）；`camel-remote.ps1` 在仓库外，改动见报告 §7） |
| H27b | 复合字面量作实参进入形参类型；编辑器语法补 `constas` | `builder_operators.cpp` 的在改项合入后 | **已完成**（`cc87c464`；报告 reports/H27b.md） |
| H27c | 实参元素的诊断位置；部分应用的上下文实参；管道额外实参的缺陷 | 无 | **已完成**（`d2633786`；报告 reports/H27c.md） |
| H27e | 字面量进入后多个候选：普通调用与部分应用都报歧义（去掉声明顺序兜底） | 无 | **已完成**（`6a50167d`；报告 reports/H27e.md） |
| H40b | 块压力请求全量收集、重写 `gc_pacing`、回归用例、更正 C2 叙述 | 无 | **已完成**（`d1a3f40d`；报告 reports/H40b.md） |
| H40c | 块压力按块身份只计一次；晋升后接管块时登记（H40b 评审第 1、2 项） | 无 | **已完成**（`3c1a1802`；报告 reports/H40c.md） |
| H44b | 特化缓存放在程序一级、跨会话保留；之后落地结构体 CAST 折叠并删除导出绕行 | 无 | **已完成（第 2 项由 H60 落地）**（第 1 项已入，H44c 按裁决改为“缓存不拥有图”；第 2 项当时未落地，后由 H60 让折叠把函数返回的值当作可替换锚点、删掉 `constantOf` 的 CAST 穿透（reports/H60.md；H153 校正，依据 H60）；`unended_unrolling` 的导出拒绝 96 s → 0.015 s；报告 reports/H44b.md、reports/H44c.md） |
| H44c | H44b 的两个待决问题：缓存不使图存活（Q2）；内联历史（Q1） | 无 | **已完成**（第 1、2 项已入；H44 第 2 项仍未落地，证据见 reports/H44c.md；待决第 2 项"特化跨轮仍可达"的用例由 H70 完成，见 reports/H70.md） |
| H150 | **`debugger.dap_protocol` 的 slow 层红（80 条里 4 条）与 `dap_pipeline` 的时序红**：前者不是 `std::opt` 改了图，也不是期望陈旧——调试器把 `std::opt` 代数步**为规则库自己**执行的 `stdlib/rules.cml` 当成被调试程序索引了一次，`calls.cml` 的断点全被判成“不在运行的图里”，真正的 `std::fvm` 开始时 `runIndexed_` 已为真、不再重新解析，于是第 16 行给出“被某一遍删掉”、第 17 行根本不停（`waitStop()` 为 `null`，用例取 `stop.top` 抛出）；`std::jgir` 显示优化后第 16 行 `DATA "10"`（origin 179）与第 17 行 `:io/println`（origin 181）都还在，所以第 16 行 `ConstantOnly`、第 17 行可执行这两条期望是对的。后者是适配器收到 `finished` 就杀 worker，而 `continue`/`nextPass` 的回答可能还在 worker 的服务器线程上没写完，客户端收到“被调试程序已经结束”（24 个忙循环下 100 次红 7 次） | 无 | **已完成**（报告 reports/H150.md；修法：`ExecutionObserver` 新增 `passStarting(schedules, root)`，`applyPassesDetailed` 在每遍 `apply` 前通知，`DebugCore` 只在调度遍索引运行并保留帧（改写/翻译遍的规则库、宏函数执行不产生帧、不触发断点重算）；适配器新增 `workerCallMutex_`，`core()` 全程持有、`finished` 杀 worker 前先取，回答不会被截断；用例加守卫“存活断点从未被撤回”，并让 `dap_pipeline` 失败时打印命令回答。`std::opt` 源码与期望未动。验收：连续五次完整调试器计划 12/12（`dap_protocol` 1.18–1.26 s、`dap_pipeline` 282–326 ms），修复前 `dap_protocol` 61 s 红；负载下 `dap_pipeline` 修复前 100 次红 7、修复后 120 次 0 红；`feat/opt` 710/710；`feat/tools` 19 通过、1 失败（既有基线 `lsp.editing`）、10 跳过） |
| H151 | **H19 第 10 步：`tensor` 的声明迁移**——把 `stdlib/signatures/tensor.cml` 与 `modules/tensor` 的每个算子分类成可声明／计算／阻塞并落表，可声明的搬进 `stdlib/tensor.cml`（C++ 交出静态解析器与 `exportEntity`），算子的执行器、纯度、资源、原地语义不动 | 无 | **已完成**（报告 reports/H151.md；64 个公开算子 + 18 个内部名字 + 79 条 URI 逐条分类：**10 个名字 13 条声明**迁出（`shape`、`numel`、`dim`、`to_bytes`、`load_npy`、`save_npy`、`num_threads`、`set_num_threads`、`gemm_backend`、`seed`；`dim`/`set_num_threads`/`seed` 各有 `int` 与 `i32` 两条），**54 个名字 56 条计算签名**留在 C++（每个名字都写明是元素类型、形状、轴、联合类型、可选参数还是类型实参让它计算），阻塞名单 2 条（`range` 三个可选整型位置各接受 `int`/`i32`，精确写要 14 条；`sum`/`mean` 的轴形式带可选 `keepdims`，一个名字不能拆成两个组）。`Tensor.shape` 这类别名不能写进声明，因此别名不新增语法：`Module::replaceTypeQualifiedAliases` 在模块写下一个普通名字的算子组时，把同名、同 URI 的限定名指向同一个实体（`exportEntity`/`replaceEntity` 与 `inheritExportsFrom` 两处都调用，谁最后写都是声明赢），规则对任何模块通用、无 tensor 特判；十个已迁移模块没有限定名算子行，别名修复对它们逐个不触发，`inspect --ops` 的 154 条 tensor 基线行（0 条精确）在改动后仍是 154 条、字段逐字节相同、只有 26 条变成精确（13 条声明 + 13 条别名）。参考页 327 条、tensor 一节 64 条按 10 个小节分组、声明与计算同名小节合并、别名不重复成节。验收：`feat/modules` 1073（1072 通过、1 跳过）、`feat/compile` 1048/1048、`feat/linear` 723/723、`smoke` 10/10、`feat/opt` 710/710，`--verify-ops` 与 **451** 条重载一致、`--verify-doc` 一致、`stdlib-declarations.mjs --check` 12 个声明模块 69 条计算签名 0 问题；`std::jgir` 样本规范化后与起点逐字节相同；无新诊断、未改效果与资源条目） |
| H152 | **H142 量出的声明模块 4.6 ms 启动开销，在不改语义的前提下拿掉**：起点 `origin/develop` `4122a4ce0`，九个声明模块 182 条声明；H142 的基线是 4.6 ms（`math` 2.2 ms / 96 条） | 无 | **已完成（结论：不改编译器，交付度量与门禁）**（报告 reports/H152.md；主机 `88zjwei` 负载 12–47；`inspect --ops` 里九模块 parse+gct+declare **3.59–4.07 ms**（H142 的 4.4–4.6 ms 是负载 148–215 下的同一份工作），**每条声明 19.6–19.9 µs**，占整次编译工作 **18.3–19.7%**；parse 55% / gct 28% / declare 17%，parse 的 92% 是建 AST（`camel-parsecheck bench stdlib/math.cml 101`：lex 18 µs、lex+cst 44、ast 571；AST 每条声明 15 个节点，1445 行 dump / 96），AST 阶段 22.3% 是 `shared_ptr` 引用计数、12.6% 分配、11.4% malloc（allocshim 1041 样本、200 次运行，system CPU 45% 是页错误），GCT 是 malloc 16.1% + origins 11.2%，GIR 是算子名哈希 18.0%。三个候选逐个量：**(a) 每模块一套前端没有可共享 setup**（`SourceContext`/`GIR::Builder`/`OperatorTable` 本来就共享，其余是模块状态；`math` 与 `time` 两点拟合 18.4 µs/条 + 约 45 µs/模块，固定部分合计 0.3–0.5 ms，残差 ±30 µs）；**(b) 签名建两遍不是白做**：declare 侧的重建是 interning 命中（C++ 模块注册时已 intern），提前到 GCT 只是搬家——子阶段 `gct.functype` 0.157→0.288 ms、declare 签名 0.059→0.020 ms，两次构建 A/B 是 gct +0.089 / declare −0.089；**(c) 96 行相似声明是 AST 成本**，子节点 `reserve(2)` 的同一二进制 A/B 是 parse **+0.134 ms（更慢）**，减节点/共享 TYPE 节点会改 dump 并撞 `parent_` 断言。顺带量的读文件少两次拷贝（`parse.read` 0.030→0.019 ms = −37%，但只占一次运行 0.2%）、算子组索引（declare +0.012 ms）、`reserve(2)` 都回退，**编译器源码零改动**，三者都验证 `inspect --ops` 逐字节不变。交付：`perf.startup` 新增 `declaration.cost`（读编译器自己的 `--time-phases`，182 条声明求和除以条数）——安静主机 **19.6 µs/条**（p10 18.9 / p90 20.4，同窗口两次 19.6/19.6，负载 47 时 22.3），**share 18.3–19.7% 不随负载移动**；两个天花板 60 µs 与 **30%**（H142 的 4.6 ms 回来读 45%），都做了阈值 5 的负例并以退出码 1 报错；这回答了 H142 §6.1 的 Q3——这条成本不再需要子进程 CPU 计时。验收：`feat/compile` **1045/1045**、`feat/parse` 57/57、`perf.startup` 1/1、`docs.links` 2/2、`--verify-ops` **385** 条一致；Q1 磁盘缓存、Q2 惰性声明仍是作者问题，Q3（AST 分配横切改动）作为新线索留给独立任务） |
| H153 | **看板整理（只改看板，不改代码）**：逐行核对原有 134 行声明是否仍成立（`git log --oneline origin/develop --grep="H<n>"` 加报告里的门禁数字），按 done/partly/stale/open 定性；把行按编号升序排回；重建 H19 的模块账本；标题下写读法与计数 | 无 | **已完成**（报告 reports/H153.md；排序只动行、不删不并，H143 的孤儿行并回表内；改正 8 行——H6（第 2 项归 H85/H95、剩余红归 H102/H103）、H19（第 2–9 步的真实状态）、H38（第 4 项的剩余部分按 H100 更正：文件未删，只剩五条整数单位元）、H44 与 H44b（第 2、3 项由 H60/H71 落地）、H51（`0x17E` 笔误）、H114（3 条摘要的 red→green 经过）、H147（三条阻断已修），每处带 `（H153 校正，依据 Hxx）`；H6/H19 各有一对重复的"剩下什么"，各合并为一条；H145 的裸 `\|\|` 按 H119 的写法转义；账本 13 个声明模块（其中 5 个留 17 条计算签名）、3 个仍 C++（`env`/`process`/`hash`；`globals` 是内建）、1 个阻塞（`ckpt`），见 `### H19` 下；核对计数 done 117 / partly 9 / stale 6 / open 3） |
| H154 | **slow 层全面清扫**：`npm run test` 就是 `--tier fast`，目录目标把 slow 用例逐条滤掉（只有命名计划文件才整计划跑），`gpu/**` 与 `perf/**` 连闸门的目标目录都不在；H147 只是顺手撞见 `debugger.dap_protocol` 一条。按 tier 列清 slow 层（21 个整计划 314 条 + 9 个快层计划里的 20 条 = 334 条、41 个 `tier = "slow"` 标记），排除 stress / progen / GC 压力 / PyTorch / benchmark 共 200 条并逐条记名，其余 134 条在主机上按计划文件、`-Jobs 8` 跑完 | 无 | **已完成**（报告 reports/H154.md；主机构建 625 targets 216.0 s；已跑 407 条 = 慢层 134 + 命名计划连带的快层 273：**406 通过 / 1 失败 / 0 跳过**，计划 wall 合计 152 s；唯一红 `debugger.dap_protocol` 4/80 检查（折叠出的常量被报成 Removed 而不是 ConstantOnly、优化后存活的 `println` 不停），**9 轮 9 红**、61 s 是 3 个 20 s 事件超时不是负载、期望字符串在代码里仍在（`debug_core.cpp:70-84`）而分诊走的是 `resolveLine()` 的 Removed 分支，是行为缺陷不是旧期望，**归 H150**；两条抖动 `debugger.dap_pipeline`（9 轮 1 红，H43/H76/H147 历史上同）与 `debugger.dap_session`（9 轮 1 红，适配器回 "The program being debugged has ended."），同属适配器↔worker 的 HTTP 通道、不是超时，已让前者的检查打印适配器原话（提交 `test(debugger): …`），未做猜测性修复；`gpu/tiles` 83/83、`gpu/tiles_rows` 2/2、`gpu/tiles_transfer` 3/3 **第一次跑绿**（主机有 CUDA 12.3 与 8×RTX 3090，"主机没有 nvcc"的旧记法已过期），`feat/mm/gc_pacing` 15/15、`gc_soak` 22/22；排除的 200 条（perf 的 10 个计划、mm/gc 的 3 个压力计划、progen、autodiff/onnx 的 PyTorch 对照、jit_framed 的 2 条 gc_stress、tiles_regions/tiles_decorator 的 14 条 GC 压力）如实记为**未知而非绿**；未挪计划、未删计划、未改期望值；另：H147 §3.5 推荐的 `-Plan test/plans -Tier all` 会把禁止在共享主机上跑的 200 条全跑一遍，发布前应按报告 §3 显式列计划文件） |
| H155 | **文档片段门禁**：面向读者的页面里的 `cml`/`camel` 片段必须通过解析 + GCT + GIR；声明了命令与文本块的片段要真的运行并比对输出；故意不完整或错误的片段必须带显式标记与理由，反例还要在页面上可见。发现过期就改文档，不改检查器 | 无 | **已完成**（报告 reports/H155.md；新增 `scripts/check-doc-snippets.mjs`，接入 `feat/docs_links` 的 `docs.snippets_compile`，与 `docs.retired_names_absent` 并列；干净树 142 篇、211 个片段编译、123 个运行比对、42 个带标记的不完整片段、102 个设计/技术页节选、3 个生成/日期化参考页允许、2 个记录目录不扫描；成本检查器 2.0 s、计划用例 1.98 s（暖跑 0.98 s），`-Jobs 8`，每片段一个进程因为各自可有模块、入口与多文件目录。改 20 篇：10 个顶层语句片段（中英各 5 个）包进 `func main`，6 个输出块（中英各 3 个）按当前格式化器改成 `5.0`/`10.0`/`1.4142135623730951`/`20.0`，其余是标记与“反例”标签，未删片段、未放宽期望。红证：`len(xs)`→`len(xs, 1)` 时 `docs/language/en/03_data_model.md:90` 报 `SemanticDiag::NoMatchingFunction`，计划 2/3 红；撤销后 3/3 绿，`check-doc-links --all` 385 篇 0 断链。提交 `c584e751b`） |
| H156 | **H19 第 10 步：`tiles` 的声明化与账本「只算签名、H19 不动」的核对**：模块导出 12 个算子，其中 `available`（`() => bool`）与 `device`（`() => string`）是固定签名，账本把它们归进「计算签名（动态/可选，H19 不动）」是过时结论，所以按前几批的形状正常迁移；其余 10 条留在 C++。要回答任务的两个问题：账本的说法对不对（不对，`tiles` 不是只算签名）、以及有没有签名类型依赖设备或编译期 tile 形状（没有——`available`/`device` 是运行时探测、类型固定；`Device<...>` 的主机类型来自实参） | 无 | **已完成**（报告 reports/H156.md；清单：12 个导出 = **2 可声明 + 10 计算 + 0 阻塞**，另有 8 个只在执行器表里的名字（`run_compiled`/`run_compiled_device`/`prefer_gpu`/`retain_host`/`retain_preferred`/`run_host_resident`/`test_call`/`test_compiled_call`），由区域 pass、测试 pass 与 `gpu(fn)` 包装图插入，无算子组也不需要签名。10 条计算签名的理由：`gpu` 结果就是实参函数类型（还有「无 with、≥1 普通参数、叶子 f64/f32 张量」的谓词）；`upload`/`download` 把实参每个叶子包进/剥掉 `Device<...>`；`run`/`run_device`/`run_preferred_resident` 结果 = 函数退出类型（后两者再取 `residentType`）；`source`/`inspect`/`benchmark` 结果固定 `string` 但普通参数 = 函数的参数表；`regions` 的 with 参数 = 任意函数值。做法：新增 `stdlib/tiles.cml`（两条无体声明）与 `stdlib/signatures/tiles.cml`（10 条计算条目，模块项 `computed:` 命名），C++ 删掉两个 `StaticFuncTypeResolver` 与它们的 `exportEntity`，10 组收进 `getOperatorGroups()`（字面名字，声明检查器能指认），内核/执行器/纯度/资源/`setInPlaceForm` 未动。数字：`--check` **14 → 15 个声明模块、23 → 33 条计算签名、0 问题**；`--verify-ops` **387 → 399** 条重载（`registry_all.cml` 加 `import tiles`，`tiles` 第一次进入覆盖）；`inspect --ops` 715 → 727 行，12 条签名逐字节相同；参考页 263 → 275 条；AST 快照 6 行新增、1 行替换。无设备也能导入：`CUDA_VISIBLE_DEVICES=-1 camel inspect --ops` 仍列出 12 条 `tiles` 算子、退出 0。三条用例（导入 + `available()` 在无可见设备下打印 `false`；`available(1, 2)` 的 `NoMatchingFunction`/`0x0300000F`；`let x: int = available()` 的 `DeclaredTypeMismatch`/`0x03000110`，消息里的 `bool` 来自声明），`native_declarations` 55 → 58。验收：`feat/compile` 1054/1054、`feat/modules` 1071 通过 / 0 失败 / 2 跳过、`smoke` 10/10、`feat/parse` 57/57、`feat/docs_links` 3/3，合计 **2195 通过 / 0 失败 / 2 跳过**。账本按报告修正（`tiles` 行移出「计算签名（动态/可选）」组、计数改为 15/33）；没有新增诊断码。提交 `3b5869420`） |
| H157 | **H19 账本「计算签名、非迁移目标」九行的核对（只盘点，不迁移）**：对 `rewrite`、`nn`、`onnx`、`autodiff`、`pyplot`、`python` 与（H156 尚未开工的）`tiles` 逐个用主机 `inspect --ops` 枚举算子，按「可声明 / 计算签名 / 阻塞」归类并给出可声明数；`graph` 已由 H141 迁移，`tensor` 归 H151；不改代码、计划、用例 | 无 | **已完成**（报告 reports/H157.md；6 个模块 47 个名字：**18 可声明 / 20 计算签名 / 9 阻塞**，其中 11 条今天就能装载（`rewrite` 8、`nn` 1、`onnx` 1、`autodiff` 1），另 7 条（`pyplot` 1、`python` 6）只差模块补 `OperatorTable::declare` 的纯度；阻塞为任意函数类型 3、变参 4、`PyObject<T>` 不接受类型变量 2；结论是这些行没有一行成立。`tiles` 开工时无 H156 提交、收尾时 H156 已完成迁移，按任务书归 H156、不计入合计（旁证见报告 §7，两边的“计算/阻塞”分桶有 5 个名字口径不同）。账本 `graph` 行改为「已迁移（45 声明 + 6 计算）」，`tensor` 行标注归 H151，`rewrite` 行改判，原六模块合并行按模块拆成五行、`tiles` 行归 H156（未删模块/依据）；主机 `-Plan smoke -Jobs 8` 10/10，未跑 GPU/PyTorch，未迁移任何算子） |
| H158 | **H148 Q16 的成本数字**（H121 §7 的观察项）：`std::opt` 的代数阶段懒加载 `rules`（经它加载 `math` 的 99 条声明）会让所有读声明的 fact 按全局 `declarationEpoch` 失效；H121 只证明这是“正确、但比实际依赖更粗”，没有数字，Q16 无法按重算成本裁定 | H148 Q16 | **已完成**（报告 reports/H158.md；两个构造负载——64 个单循环函数的内建程序、24 组 matmul/loop/sum 的张量程序——外加未提交的 4 倍同构负载；`test::facts_load_rules` 在种子前用 `registerCamelPass` 编译 `rules` 作为对照（`epoch 166 -> 265`），让 bump 与 control 只差“纪元”这一项。计数：585 条种子里 369 过期，其中 **192 条只由纪元失效**（72 `effects` + 72 `instance` + 48 `reorder`，全在 `std::opt` 未改写的 72 张函数图上），重算条数差也正好 192；4 倍负载 768 条。时间（干净 Release 构建 `4bd13d759`，101 轮交错、`RUSAGE_CHILDREN`）：进程内 `computeFacts` 中位数差 **+0.36 ms**（192 条，p10 −0.22 / p90 +0.65）、**+0.64 ms**（768 条），折合 **0.8–1.9 µs/条**；整进程 CPU 差 +1.5 / −0.2 / +1.9 ms，区间全部跨零；占 82 ms 的 0.2–0.4%（4 倍负载 0.11%）。备选算术：B（owner 纪元）与 C（声明锥）在本次负载上都只省这 192 条、**0.15–0.36 ms**，C 相对 B **多省 0**（变化的 99 条全是种子时没读过的 `math` 新键，`inspect --ops` 里 `math:` 为 0）；B 的内存代价 16–48 B/条（种子 585 条 → 9–28 KB）。判词：**成本可以忽略**，现状 A 没有为性能而改的理由；今日流水线里没有跨过这次加载的 fact 消费者，端到端约为 0，量到的是“一旦有这种消费者”的代价。不改失效语义、`KindSpec`、`OperatorTable`，数字属于 Q16，未改 H148；`graph_facts` 8/8） |
| H159 | **停止态下 `continue`/`nextPass` 的回答丢在适配器与 worker 之间（H154 那两次抖动的复查）**：前提先纠正——那两条红是 H154 在**没有 H150** 的树上量的（`git merge-base --is-ancestor 929c551f0 37079f9f5` 为假；H150 合并 09:38:41、H154 基点 09:21:20），不是“H150 之后还有第二条路”。机制（探针 trace）：worker 的 `command()` 已返回 `ok`、程序已经 `endRun`，适配器在“回答已产生、还没写回 socket”的 0.5 ms 里杀了 worker，客户端收到“被调试程序已经结束”，而程序照这一步跑完并打印了输出。当前树上的自然率 **0 红 / 686 次用例**（206 轮 harness + 40 轮计划），把排序关掉（探针）**3/3 轮全红**、消息与检查逐字一致。H150 的锁只盖住了最宽的那个窗口：终点先判后做（`programExit_` 在锁外写、空的锁作用域出锁之后才 kill）、`camel/passGraphs`/`camel/passGraph` 两条快照 GET 完全不进锁、`terminate` 宽限 / `giveUp` / `disconnect` 三条结束路径也不进锁；更要紧的是 `core()` 把**任何** POST 失败都说成“程序结束”，分不清“跑完了 / worker 崩了 / launch 没起来 / 连接断了”——这正是任务书点名的那个 bug | 无 | **已完成**（报告 reports/H159.md；修法：适配器新增 `EndReason`（Finished / Terminated / LaunchFailed / WorkerGone / Disconnected）与 `endSession()`——**一个临界区**里等在飞命令、记原因、杀 worker，五条结束路径统一走它；`core()` 与新的 `workerGet()` 进锁后先看 `endReason_`，非空就按原因回答、不碰 socket，POST 失败也按已知原因回答；`onWorkerExit` 在没决定终点时记 `WorkerGone`（带 signal / exit code）；五句分类消息，`The program being debugged has ended.` 只在真的结束时说。未加长任何超时（5 / 30 / 320 s 原样）、未动编译器。测试：`dap_protocol` 加“没起来的 launch 之后请求必须说没起来、不能说已经结束”——**修复前确定性红**（HEAD 的适配器 `1 of 93 checks failed`，实际回的就是 “The program being debugged has ended.”），修后绿；另加“跑完但失败的流水线必须说结束”与 `dap_session` 结束后的 `camel/passGraphs`。数字：调试器计划 12/12（wall 2.2 s）、24 个忙循环下 `dap_pipeline`+`dap_session` **200/200 轮全绿**（122.5 s，轮内 load1 24–60）、修复前同一 harness 0/206 轮；`feat/tools` 30 条 19 通过 / 1 失败 / 10 跳过，唯一红 `compile_bench.smoke` 是既有基线（`PASS_LIBRARY_MODULES` 期望 `['rules','math']`、实际 `['rules','graph','math']`），它只跑 `camel --time-phases`、从不启动 `camel-db`，与本次改动无关，未改它以凑绿；探针（`trace.h`、全部 `TRACE`、两个环境变量开关）提交前已删，保留 `test/tools/debugger/race-harness.mjs` 可复跑） |
| H160 | **`algebra.cpp` 剩下的五条整数单位元**：查清 `neutral_only` 的五条（`x+0`、`0+x`、`x-0`、`x*1`、`1*x`，int 与 i32）在规则库里有没有对应、两边会不会给出不同结果；按证据定性 (a)/(b)/(c)；只在 (b)/(c) 时删代码并证明图不变，只在 (a) 时记录缺口并加测试 | 无（H38 第 4 项、H100 之后） | **已完成**（结论：五条都是 (a)，`algebra.cpp` 不删；库早有这十条规则（`int_*` 18–22 行、`int32_*` 67–71 行，`check` 抽样验证），缺的是"只做单位元、不改图命名节点、不清理死常量"的**模式**；删除会让 `neutral_only=true` 的图改变——`anchor` 的命名加号被规则引擎重定向、死常量被 `eraseUnused` 清掉、端口丢 `origin`/`span`——值不变；新增 `neutral_identities.cml`/`neutral_kept.cml` 与 10 条测试钉住五条、模式边界与命名节点；`feat/opt` 710/710、`graph` 61/61、`rules` 67/67；报告 reports/H160.md） |
| H161 | **变更选择规则的盲区**：H154 发现 `gpu/**`、`perf/**` 不在闸门目标里也没有选择规则（102 条 GPU 用例从没被闸门跑过），但没点破目录规则先短路——旧的 onnx/nn/autodiff/tensor 文件规则因此都是死代码；tier 过滤又只对目录目标生效，选中 gpu 目录等于一条也不跑 | 无 | **已完成**（报告 reports/H161.md；文件规则 25 → **36** 条（68 个路径前缀、102 条计划项，原 64 条），3 条目录规则不变；`modules/` 27/27、`src/passes/` 4/4 有映射；文件规则改为与目录规则**取并集**，并新增 `layer: true`（命名文件但跟随 `--tier`）；`modules/tiles/**` → 6 个计划 **182 条**（含 94 条 GPU），主机 `-Tier fast` 实测 **182/182 绿、84.5 s**，GPU slow 真的执行（`gpu/tiles_transfer` 单跑 3/3，`gpu/tiles` 83/83（`gpu/tiles_rows` 的 `strict_sum_bits` 19.19 s））；`stdlib/rules.cml` / `src/passes/opt/**` → 4 个计划 **788 条**（opt 710），**788/788 绿、6.4 s**；不匹配仍回退整个快层，`--changed --list` 可只打印选择；perf 10 个计划、GC 压力 5 个、progen 1 个、autodiff/onnx 的 9 条 PyTorch/ORT slow **一律不接**，`benchmarks/`、`test/cases/perf/` 以及直接编辑这些计划文件都走回退；验收 smoke + feat/consistency + feat/docs_links **1001/1001**；主机首次构建 625 targets、139.2 s；未改任何计划的 tier、未挪/删计划） |
| H162 | **发布门禁重跑，并把门禁交回仓库**：在 `origin/develop` `87a5a1455` 的当前树上重跑快层 + slow 层 + 静态检查；slow 层按 H154 §3 的显式计划文件列表跑（不用 `-Tier all`，也不给目录），逐条分诊新红；新增 `npm run release-gate`（`scripts/release-gate.mjs`）：快层 + slow 文件列表 + 八项静态检查，把 19 个禁止计划 / 200 条按名字打印为未知，拒绝按 tier/目录选（exit 2 并点名 `agents/test.md`），校验构建戳，任一阶段失败即非零 | 无 | **已完成**（报告 reports/H162.md；`8439c65a7` 上快层 5067 条 5055 通过 / **0 失败** / 12 跳过、slow 层 **407/407**、静态 **8/8**；唯一新红 `compile_bench.smoke` 分诊为过期期望：H144 修的是用户模块计数，H141 的 `graph` 声明化让 `passes` 流水线按需编译的库模块变成 `rules`/`graph`/`math`，pin 没跟上，在 `788a179b2` 修好（含 `docs/technical/38_compile_pipeline_cost.md` 的 6→7、131→132）并单独复跑 1/1 通过；H147 的七条红（3 条摘要、`lsp.editing`、`compile_bench.smoke`、`dap_pipeline`、`dap_protocol`）现全绿，H147 §7 追加 gate re-run；19 个计划文件 / 200 条策略禁止用例仍未跑，如实记为未知；写作期间 `origin/develop` 前进到 `e82f41c99`（H151–H164 陆续合入），发布候选上要再跑一次 `npm run release-gate`；未打 tag、未 bump、未合 `main`、未改 `changes.log`） |
| H164 | **v0.2.24 的发布说明写实**：H147 的 20 条草案逐条对照代码与报告，`changes.log` 的 `[0.2.24]` 节重写为 24 条用户可见的变化（标准库改名与 16 个声明模块、`io`/`str::bytes`、资源名、ONNX 恒等 CAST、许可标签与 `when`/`#requires`、`PassNotProvidedByModule`、`-I`、声明化启动成本、`x86-64-v3`），并核对版本与下一版本；不改代码、不打 tag、不 bump | 无 | **已完成**（报告 reports/H164.md；发布版本 **`v0.2.24`**、下一版 **`v0.2.25`**，未打 tag、未 bump、未合 `main`/`develop`、未 push；H147 的"`onnx.export_model` 恢复可用"是 v0.2.23 之后才出现的回归（H100→H112），v0.2.23 用户没有见过，已改写为恒等 CAST 的净变化；期望类型/两层标记/事实入口三条标为澄清而非行为变化，是否只留净变化留作者定；新增遍诊断（`PassNotProvidedByModule`/`PassNotExportedByModule`，目录 202→204 条）、放不下的数字文本与 `IntegerLiteralTruncated` 的删除、声明化启动 +32%（约 4–5 ms）三条；只读树的检查 `diagnostics-catalog --check` 204 条与 `check-doc-links --all` 396 篇通过，`--verify-ops`/`--verify-doc`/`stdlib-declarations --check` 需要构建、留给 Linux 主机；`[0.2.23]` 及以下未动、节数仍 70） |

| H165 | **H19 第 10 步：`nn` 与 `autodiff` 的声明化**：H157 账本把两个模块标为「`nn` 1 可声明 / 18 计算」「`autodiff` 1 可声明 / 2 计算」，逐条核对 `OpDef`/`OptimizerResolver`/`GradResolver` 与主机 `inspect --ops` 后成立；把两个固定签名迁进 `stdlib/nn.cml`、`stdlib/autodiff.cml`，C++ 交出 resolver 与 `exportEntity`（`nn` 还加 `kDeclaredOperators` 跳过表），其余 18 + 2 条计算签名留在 C++、参考条目写进 `stdlib/signatures/`；执行器、纯度、资源、原地语义未动 | 无 | **已完成**（报告 reports/H165.md；`nn` 19 个名字 / 21 条注册 = **1 声明 + 18 计算名字（20 条 URI）**，`autodiff` 3 / 3 = **1 声明 + 2 计算**；`--check` **16 → 18 个声明模块、87 → 107 条计算签名、0 问题**，`--verify-ops` **465 → 489** 条重载（`registry_all.cml` 加两个导入），参考页 **338 → 361** 条，AST 快照 **2540 → 2550** 个文件，`native_declarations` 61 → 67；六条新用例（`nn` 导入/arity/type，`autodiff` 导入/arity/结果类型——`typeas T` 没有错类型用例，按任务书用结果类型代替）；`std::jgir` 混合样本（`save_params`/`sgd`/`adam_state`/`adam`/`stop_gradient`/`grad`）在 `b66edb7b5` 与本分支上规范化 `origin` 后 18910 字节逐字节相同，`inspect --ops` 的 24 条里只有 `save_params`/`stop_gradient` 的签名与参数计数变化，其余字段逐字节相同；验收 `feat/modules` + `feat/compile` + `feat/linear` + `smoke` 完整环境 **2869 条 2868 过 / 0 失败 / 1 跳过**，不带 PyTorch/ORT 的环境 **2869 条 2845 过 / 0 失败 / 24 跳过**（跳过全是 PyTorch/ORT 对照与 Windows-only 的 `unicode.paths`）；没有跑 PyTorch/GPU 计划，没有新增诊断码；`adam_state` 的理由改正：参数域是「有切线的树」而不是「结果取自参数」） |
| H166 | **H19 第 10 步：`pyplot`、`python` 的纯度注册与声明迁移**：H157 判定 7 个名字只差 `OperatorTable::declare`；本任务复现拒绝、定位代码路径、补齐注册并迁移 | 无 | **已完成**（报告 reports/H166.md；拒绝是 `NativeDeclarationNotLoaded` `0x0300017C`，发生在 `Builder::declareNativeOperator` 的纯度检查 `src/compile/gir/builder_modules.cpp:385`（执行器检查在 377 已通过），13 条纯度条目与 H157 的估算完全一致；两个模块各补 `declare`（全部 effectful），无声明时 `std::jgir` SHA-256 前后相同、`inspect --ops` 只有 purity 变化。`pyplot` 8 条声明进 `stdlib/pyplot.cml`（4 种元素类型 × 2 种参数个数，0 计算签名），`python` 6 条进 `stdlib/python.cml`、5 个名字（6 个 URI）留 C++ 记入 `stdlib/signatures/python.cml`（2 个 `PyObject<T>` 为待作者裁定的声明语法缺口，3 个变参按既有约定记计算签名）。树内 18 个声明模块、92 条计算签名、485 条重载、参考页 352 条；`--check`/`--verify-ops`/`--verify-doc` 绿，`smoke` + `native_declarations` + `stdlib_registry` + `cleanup` **118/118**；参考页与 AST 快照在主机重新生成，无新诊断码；提交 `721af86ff`、`cb2eebda7`、`556ca8c70`） |

| H167 | **perf 层的分类与一个手动性能闸门**：H154 认定 `perf/**` 被 `--changed` 与 `npm run test` 排除（这是对的：又长又重），代价是性能可以无限回归而无人发现；分类 10 个计划，只对成本允许重复的两个建闸门，其余按政策拒绝 | 无 | **已完成**（报告 reports/H167.md；新增 `scripts/perf-gate.mjs`：`--list` / `--update` / `--reps` / `--warmup` / `--threshold` / `--baseline` / `--json`，交错重复、中位数 + p10–p90，子进程 CPU（Linux `/proc/self/stat` 的 `RUSAGE_CHILDREN`，≥250 ms 的用例按它判定）与墙钟两种数，分布重叠不判回归，墙钟出负载带判 inconclusive；`test/perf-baseline.json` 播种在主机 `4df0be103` 的干净构建上（commit、build 指纹 `3840393569ab`、5 次重复 + 1 轮预热、负载 37.45→29.19 / 最差 37.84）；分类：`perf.startup` 2.08 s、`perf.fib` 10.8 s **可门禁**，`perf.para` 一次重复 **178.7 s**（常驻 4–12 GB）只可测，`perf.gc` / `perf.collections_fuzz` / `perf.training_steps` / 4 个 showcase 按 `agents/test.md` 政策**不跑**、只按计划文件分类；证明：连跑两次 no regression（10/10 可比），把基线压到 60% 后 4 条 REGRESSION（+61%…+67%，exit 1），恢复后 sha256 逐字节一致；`docs/test-plans.md` 新增「Performance measurement」；smoke + feat/docs_links **13/13**、2.0 s） |
| H168 | **声明语法缺口的盘点与提案（只盘点，不迁移）**：把 `tensor`/`rewrite`/`nn`/`onnx`/`autodiff`/`pyplot`/`python`/`tiles`/`ckpt` 九个模块的 130 个公开名字按“已声明／不加语法可声明／被语言机制挡住／被标准库规则挡住／缺执行器／计算签名”逐条归类，给出每个缺口的名字清单、文档依据（已有裁定还是只有作者能答）、解锁名字数与风险，写出最小提案集（`docs/technical/47_declaration_syntax.md`） | 无（引用 H141/H151/H156/H157/H140 的探针与 H148 Q1/Q3） | **已完成**（报告 reports/H168.md；**17/130 被机制挡住**：任意函数类型 9（独占 6）、变参 4（**独占 0**）、`PyObject<T>` 2、类型实参 2、类型变量谓词 3（独占 1）；可声明的 `show` 2 条 + `python` 6 + `pyplot` 8 共 16 条声明不加任何语法（`python`/`pyplot` 那 14 条已由 H166 在 `develop` 上迁完，基点尚无）；可选参数/整数宽度/联合参数/类型限定别名各挡住 **0** 个，只是规则条数（`range` 14 条、`plot` 8 条）；先关的一条是 Q-M `PyObject<T>`（与 `Device<T>` 同形），第一步是先写那 16 条声明；未构建、未迁移，记 6 条文档缺陷） |
| H169 | **`std::opt` 为什么编译 `graph`，这笔钱是多少**：H159 的红报出 `['rules','graph','math']`，但 H144 的 `("rules","math")` 在 09:05 为真、09:32 H141 把 `graph` 变成声明模块、10:12 H151 又把 `tensor` 变成声明模块，实际集合是 **`['rules','graph','math','tensor']`**——名单在一天内两次过期，H162 的三名字 tuple 在合入时也已过期。来路：`stdlib/rules.cml:11-14` 显式导入 `graph`/`math`/`tensor`；`std::opt` 的代数步走 `rules::simplify`，`rules` 作为遍的工具程序编译时，有 `.cml` 声明文件的导入模块各走一次 parse/GCT/declare；`graph` 不注册任何遍，所以不是遍依赖，是"真实 import + 声明急切加载"。成本：十行无 import 程序跑 `std::opt`，61 轮交错，`graph` 的 parse+gct+declare **中位数 0.844 ms**（p10 0.796、p90 0.931），math 1.789、tensor 0.282、rules 22.683；删掉安装里的 `graph.cml` 后 `std::jgir` 输出 SHA-256 逐字节相同（5→4 模块），但 `graph_types.cml` 会红在只在 `graph.cml` 声明的 `current`/`nodes`/`id` 上，所以省法只能是 H142 Q2 的惰性声明，不是删 `rules` 的 import | 无（H144 的名单、H142 Q2） | **已完成**（报告 reports/H169.md；`bench.py` 删除 `PASS_LIBRARY_MODULES`，新增 `pass_loaded_modules()`（`library` 阶段或在其 `gct` 内编译的模块）、`stdlib_declaration()`（`stdlib/<name>.cml` 必须写 `module <name>`）、`module_phase_costs()`；`check` 断言"数到的库集合 == 阶段树证明的集合"、"每个库模块有自己的声明"、`passes` 仍加载库，并逐个打印 parse/gct/declare/body 与总价；没有声明文件的导入（`rewrite`）只有 `cmo_load`，不进集合也不计价；`results.md` 新增 `Library modules compiled on demand` 表；文档改为 chain-3/passes 8、chain-128/passes 133（H162 的 132 少算 tensor），新增 §5.1 记录 0.84 ms 与 H142 Q2。负例：给集合加 `rewrite` → 同时报"阶段树没有它"与"不是标准库声明模块"；从阶段树删 `graph` → 报集合不一致；均退出 1。正例：`compile_bench.smoke` PASS（630.90 ms）；主机 `smoke + feat/opt + feat/trans + compile_bench` **765/765 通过、0 失败**（构建 10.2 s、计划 7.1 s、`-Jobs 8`）；不改编译器/流水线/`rules.cml` 的 import，不 push） |
| H176 | **文档缺陷的分类与收尾**：把 H148 D1–D4、H168 §9 的六条、H129/H134/H140 与全库报告里已记录的文档缺陷合成一张表，按 handover / technical / design principles / language reference 分 owner；修 handover/technical，normative 的交作者 | 无（H148、H168 的记录） | **已完成**（报告 reports/H176.md；16 条记录 = **6 条已修**（H151 的 `show` 计数与 §3.4 标题、H157 的 `with` / `py_print` 结果 / 3 与 8 作用域、H19 第 10 步的 `ckpt` 注；提交 `a2b98c0e9`）、**4 条交作者**（设计原则 §3.10 捕获表、§3.3 句柄类型、语言参考 7.5 的 `typeas` 模块类型实参、7.4.3 的泛型同名；逐条给原文 / 代码证据 / 一句替换，接受或拒绝）、**5 条已修**（`0b50f7bd2` 看板 H19/H44/H69、`5a6dc71c0` `signatures/tensor.cml`、`c2280f01d` 设计原则裁定记录、`0fabf2fe2` `case_path`、`40ef85ebd` NaN 符号）、**1 条报告间口径**（H156/H157 的 `tiles` 计算/阻塞标签，交协调者）；未改设计原则、语言参考、`changes.log`、版本文件与生成页） |

| H170 | **声明化迁移的残留清查（H19 的 16 个模块作为整体审计）**：每个已迁移模块的 C++ 里是否还有“声明已经给出的签名”留下的静态解析器、`exportEntity`、只为它存在的 helper/表、旧的 `stdlib/signatures/<m>.cml` 条目；并回答整个仓库层面 `StaticFuncTypeResolver`、`foldFromTypes`、别名发布是否还需要 | 无 | **已完成**（报告 reports/H170.md；16 个模块逐个查 (a) 声明名的静态解析器、(b) 声明名的 `exportEntity`、(c) 孤立 helper/表、(d) 陈旧签名条目：**(a)(b)(d) 均为 0**——`--check` 16 个声明模块 / 87 条计算签名 / 0 问题，`--verify-ops` 465 条重载；**(c) 也没有孤立项**——主机全量构建在 16 个模块里没有 `-Wunused-function`/`-Wunused-const-variable`/`-Wunused-variable`（全树 4 条 `unused` 诊断都在 `src/` 与 `modules/onnx`），调用点扫描的 ≤2 次定义都是“定义 + 1 处调用”的 helper 或内核表条目/同名标准库函数，没有只有定义、没有调用者的项。真正的残骸只有 3 行 `#include`：`bytes` 的 `type/resolver.h` 与 `type/bytes.h`（H76 静态签名块留下，`f50fa8c69`）、`graph/facts.cpp` 的 `type/resolver.h`（H141 删 `factGroups()` 后留下，`aa518693d`）；每个提交都跑主机构建、`--check`、`--verify-ops`、与基线逐字节相同的 `inspect --ops` 及模块计划（bytes 71/71、graph 135/135）。跨模块结论是**都不删**：`StaticFuncTypeResolver` 全树 210 处（183 在语言全局 `globals.cpp`，8 在未迁移的 `env`/`hash`/`process`，4 在 `onnx`/`rewrite`/`sim`，11 在 builtin `profiler`/`tagged`/测试 pass，1 在 `builder_operators` 对 `FunctionType` 的局部构造，1 是 `introspect` 的 `dynamic_cast` 探测，1 是类定义，16 个已迁移模块里只剩 graph 的 `__neq__` 1 处——`!=` 按设计原则 4.3 不能声明）；`foldFromTypes`/`setTypeFolder` 是 `std::opt` 的常量折叠（`generic.cpp:550/644`、`builder_operators.cpp:2204`、`screen.cpp:142`），不是签名；`replaceTypeQualifiedAliases` 是 `tensor` 的 `Tensor::<name>` 别名发布（`module.cpp:341/355/420` 调用，`modules/tensor/module.cpp:92/100` 写），仍需要；tensor 的 `kDeclaredOperators` 10 名与声明 10 名逐字一致。`std::jgir` 的 771 行 16 模块样本连 `origin` 都与基线逐字节相同。验收：`feat/modules` + `feat/compile` + `smoke` 在最终树上 **2150 条、2149 通过、0 失败、1 skip**（`unicode.paths` 是 Windows 专用；改动前 2147 通过、3 skip 里的两条 `mnist_py` 是首次运行前置命令 `exit null` 的偶发跳过，最终运行通过）；未跑 stress/progen/GC-stress/PyTorch，未改纯度/资源/效果/诊断码） |
| H171 | **文档里的数字**：面向读者的页面里有一类断言是数字（模块/声明/算子重载/诊断码/测试/文件/计划数、版本、计时），链接、旧名字、片段三道闸门都不读；盘点可核对的数字，逐条核对真值、改错，并给有唯一来源的子集加一道闸门，接入 `feat/docs_links`；生成页不改、既有闸门不放宽、日期化快照按页允许并写理由 | 无 | **已完成**（报告 reports/H171.md；新增 `scripts/check-doc-counts.mjs` 与 `docs.counts_current`：145 篇、13 条在用断言、8 条日期化快照（6 页，带理由）；12 条规则覆盖 CMake 最低版本、导出矩阵、`RuntimeDiag::RuntimeError` 引用、`SyntaxDiag` 错误码、签名文件、遍别名、`.cml` 文件、AST 快照、模块子目录、发布闸门的计划/静态检查数、`modules/tiles/**` 选计划。修正 **6 条断言、10 个数字、5 个文件**：CMake `3.20`→`3.25` ×4、导出 `106/66`→`119/65`、兜底码 `81`→`89`（`147` 标成“原本”）、语法码 `12`→`16`、签名文件 `十五`→`11`、遍别名 `35`→`37`。红证：把 `119` 改成 `118` 后 `ROADMAP.md:35` 报声明/真值/来源、退出 1，撤销后绿。顺带发现并修好：三道既有 `feat/docs_links` 用例把 `${case}`（脚本自己的路径）当文档，扫描 0 篇而“通过”；改为 `${case} --default-roots`（三道脚本新增该开关）后真实扫描 31/409/142 篇。成本：闸门 0.40–0.60 s（主机，145 篇；Windows 0.70–0.74 s），`feat/docs_links` 4/4、冷 1.3 s / 暖 1.6 s，`smoke` 10/10，合计 14/14；主机 `-Jobs 8`，未 push、未改 `changes.log`/版本文件） |
| H172 | **`inspect --ops` 的静默失败（H166 记录、未修）**：声明模块装载失败时列表照常打印、退出 0，H166 只能人工数名字才发现注册缺口；复现、定位、修到"有错就不出列表"，并核对 `--verify-ops` 是否也漏 | H166（发现） | **已完成**（报告 reports/H172.md；修复前：`CAMEL_STD_LIB` 副本追加坏声明后 `inspect --ops` 退出 0、356 行、stderr 0 字节，而同一声明 `camel check` 退出 1 报 `NativeDeclarationNotLoaded` `0x0300017C`；诊断写在声明模块与主模块的 `Diagnostics` 里，`--ops` 分支 `tools/camel-cli/main.cpp:433` 没有消费者，也不看 `compile(Done)` 的返回值。修法：先扫 `allUserModules()`，任一用户模块有错误就不打印列表，stderr 给 `camel: the declaration module '<m>' failed to load from '<file>'` 再 dump 诊断，退出 1；未导入模块不加载、计算签名在 C++ 的模块（`env` 等）无 `.cml`，均不报错（用例四次运行验证）。无新诊断码。测试 `registry.declaration_load_failure_is_an_error`，红证明修复前 5 通过 / 1 失败、修复后 6/6。`--verify-ops` 对仓库声明能红（1 处差异即退出 1），但只覆盖 `MODULES` × probe 导入的模块、看不到副本里追加的声明；修复后它把 `inspect --ops` 的 stderr 作为原因带出。`smoke` + `feat/compile` + `feat/modules` **2157 例、0 失败、3 跳过**；提交 `f346ffd89`） |

| H173 | **标准库注册覆盖：`registry_all.cml` 漏掉的模块**：H166 发现 `python`/`pyplot` 不在探针里，`--verify-ops` 从不看它们；查清还有哪些模块在同一个洞里，逐个补进注册表或带可核对事实排除，并加检查让洞不可能再出现 | 无 | **已完成**（报告 reports/H173.md；27 个 C++ 模块注册算子，改前只有 22 个被 `--verify-ops` 看见：`sim` 被 `registry_all.cml` 导入却不在 MODULES（H166 同形），`rewrite`/`nn`/`autodiff`/`onnx` 连导入都没有。补 `sim`（2 条签名）与 `rewrite`（10 条声明覆盖 8 个可调用名字，3 个保留组无调用名不列）进 MODULES，`registry_all.cml` 加 `import rewrite`；补覆盖暴露 `rewrite:analysis` 纯度从未声明，按 effectful 补上（未声明本就等同非 pure，只改 `inspect --ops` 报出的 purity）。排除 `nn`/`autodiff`/`onnx`（26 个注册行，张量栈、签名由 C++ 计算、无声明/签名文件，`registry_tensor.cml` 仍查它们的纯度），排除项要求理由、无签名来源、`covered_by` 真的 import 它。`stdlib-declarations.mjs --check` 新增第 5 项：从 `modules/*` 的 `BuiltinModule` + 声明文件枚举算子模块，既不在探针也不在排除表即失败（红证明：删 `import sim` 点名 `sim`，删 `nn` 排除项点名 `nn`，均恢复）。`--verify-ops` 485→495，参考页 352→362（`--write`），AST 快照在主机重生成（`registry_all` `02afa2029ef65f3f`→`aa0f7c9fec2d803a`）；`smoke`+`feat/modules`+`feat/compile`+`feat/parse/frontend` **2160 条 2159 通过 / 0 失败 / 1 skip**。提交见报告 §7；注意报告 §8：更新的 develop 上 H165 已给 `nn`/`autodiff` 签名，合入时删掉这两个排除项） |
| H174 | **把 H140 给 `ckpt` 留的三行障碍写成可判定、可复核的陈述**：每个名字今天由谁给签名、被哪条机制挡住、重测结果、解锁它的最小改法与代价；只在发现 H140 之后落地的机制已经解决某条障碍时才迁移那一部分 | 无（H140、H151、H166、H103、H148；H168 在 `origin/develop`，不在本分支基点） | **已完成（调查结论：不迁移，0/7）**（报告 reports/H174.md；在 `8bb46f120` 的构建上逐条重测：五个 `tree_*` 声明仍被 `NativeDeclarationNotLoaded` `0x0300017C` 拒绝，位置是 `builder_modules.cpp:377` 的执行器检查（纯度已由 `module.cpp:67-73` 登记），且展开先于解析、只认 URI（`builder_operators.cpp:1878-1883`、`tree.cpp:41-46`），所以补执行器也不会被调用；`save_tree` 的两条声明能装，但 `unsupportedLeaf`（`checkpoint.cpp:351-357,362`）的编译期拒绝退化成运行期 `RuntimeDiag::FileSystemError` `0x04000036`（对照 C++ 解析器是 `SemanticDiag::ArgumentsRejected` `0x0300001C`），`load_tree` 的 `typeas T!` 在 `::<T>` 与期望类型两种写法下都是 `NoMatchingFunction`（`resolver.h:105-110`、`resolver.cpp:86-101`、`checkpoint.cpp:420-426`），且 `load_tree` 也检查叶子谓词（`:395`），所以是 2+3 而不是只缺 Q1；三条障碍今天都成立，H140 之后 143 个提交里 `builder_modules.cpp`/`tree.cpp`/`resolver.*` 与语法文件零改动，语言里没有 `where`。决定：声明路线 Q1+Q3 的上限是 2/7（`save_tree`/`load_tree`），五个 intrinsic 仍需要 H51 第 2 项的 Camel 重写；最小解锁是 H51 第 2 项 + H108 选项 1，不是声明语法。门禁：`--check` 18 个声明模块/92 条计算签名/0 问题，`--verify-ops` 485，计划 2156 条（2154 通过/0 失败/2 跳过：`unicode.paths` 仅 Windows、`mnist_py` 缺 `${python}` 前置）；`stdlib/`、`modules/`、用例零改动） |

**按队列顺序连续做下去**（队列顺序以 `docs/roadmap.cn.md` 与依赖为准，不是本表的编号顺序）：一项完成、报告写好、合入 `develop` 后，接着做下一项，不必等检查意见；检查意见到了再回头处理。遇到需要设计判断的地方，按第 4 节写进报告的"待决问题"，跳过被挡住的部分继续做后面的。H2、H7 是语言表面的较大改动，规格已写全，但涉及的判断较多，宁可多问。

前提是否满足，看 `docs/roadmap.cn.md` 第 1 节：地基项标为"已合并"即满足。**正在进行的地基工作**会改动这些区域，H1、H4 期间尽量不要大改它们，必要时小改并在报告里说明：遍与流水线（`src/execute/pass.cpp`）、优化遍与调度器（`src/passes/opt/`、`src/passes/sched/`）、装饰器与标注的解析和建图（`src/parse` 中的 `funcAnno`、`src/compile/gir/builder.cpp` 的装饰器部分）、图存储的许可列、字符串与 `Bytes`、参数树（`src/compile/gir/tree.cpp`）。

---

### H1：`enum`

**依据**：设计原则 5.3"和类型按 Rust 的 `enum` 做"与"退掉预留的 `T ? U`"、5.3（不在运行时按类型判断）、5.5。读这几段原文。

**已有的半成品**：`docs/handover/assets/enum.h.draft.txt` 是一份早期草稿头文件（从未编译、未接入），设计了 `EnumDecl`（一个声明，带参数名与变体）、`EnumType`（一个实例，按参数驻留）、`VariantShape { Unit, Tuple, Struct }`、泛型实例未定参数用的"洞"（`Option<_>`，与 `Option<int>` 统一）。可以采用，也可以改，改了在报告里说明原因。**基于最新的 `fsh-dev` 从头接入**，不要去找或合并旧分支。

**要做的语言功能（语义已定）**：

```camel
type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
    Empty,
}
type Option<T> = enum { Some(T), None }
type Tree<T> = enum { Leaf, Node(Tree<T>, T, Tree<T>) }

let a = Shape::Circle(1.0)
let b = Shape::Rect { w: 2.0, h: 3.0 }
let c = Shape::Empty
let area = match b {
    case Shape::Circle(r)     => 3.14159 * r * r
    case Shape::Rect { w, h } => w * h
    case Shape::Empty         => 0.0
}
```

1. **声明**：`type 名字 = enum { 变体, ... }`，可以带类型参数（`type Option<T> = enum {...}`），可以递归（变体的字段类型引用自身）。变体三种形状：无数据、元组（一个或多个位置字段）、结构体（具名字段）。同一个 `enum` 中变体名不能重复；结构体变体内字段名不能重复。末尾逗号可有可无。
2. **构造**：`Shape::Circle(1.0)`、`Shape::Rect { w: 2.0, h: 3.0 }`、`Shape::Empty`。结构体变体构造必须给全字段，顺序不限。泛型实例的参数由实参推出；推不出时（如 `Option::None`）由期望类型推出（`let x: Option<int> = Option::None`），仍推不出是编译错误。
3. **`match`**：
   - 臂的写法 `case Shape::Circle(r)`、`case Shape::Rect { w, h }`、`case Shape::Empty`、`case _`。
   - 元组变体的模式必须给出全部位置，不要的写 `_`。结构体变体的模式写出要绑定的字段，未写的字段忽略；可以改名 `{ w: width }`。
   - **第一版不做嵌套模式**（如 `case Shape::Node(Tree::Leaf, x, _)`）；遇到时给出明确的"暂不支持"诊断，并在报告里记下。
   - **穷尽**：列全了所有变体就不需要 `case _`；漏了变体且没有 `case _` 是编译错误，诊断要列出漏掉的变体名。多余的臂（变体已被覆盖）给警告。
   - `match` 的值也可以是一个返回 `enum` 的表达式；各臂结果类型的统一规则与现有 `match` 相同。
4. **值的其余操作**（协调者已定，按此实现）：
   - **相等**：同一 `enum` 实例类型的两个值可以用 `==`、`!=` 比较，先比变体再逐字段比较；任一字段类型不支持 `==` 时，这个 `enum` 也不支持，编译错误。
   - **打印**：`Shape::Rect { w: 2.0, h: 3.0 }`、`Shape::Circle(1.0)`、`Shape::Empty`，与源码写法一致；泛型实例不打印类型参数。
   - **不能直接取字段**：`b.w` 是编译错误，诊断提示用 `match`。
   - **不按值的类型区分变体**（5.3）：没有 `is`、`typeof` 之类的运行时判断。
5. **图上不增加节点种类**（5.3）：构造是"装入一个标签和数据"，`match` 是"取标签、分支、在臂里取出数据"，与现有的 `T?`、`T!` 相同。先读 `T?`、`T!` 在 GCT/GIR 中如何构造与匹配（`src/compile/gct/builder.cpp`、`src/compile/gir/builder.cpp`、运行时 `src/core/rtdata/tagged.cpp`），照同一模式实现。取标签、取字段如需新的内部算子，作为普通算子注册（带纯度、资源与语义声明），不要在调度器里按名字特判。
6. **每个后端都要支持**：`std::nvm`、`std::fvm`、`std::jit`、`std::prl`、`std::macro`（编译期求值，`enum` 值可以是宏的结果并物化为常量）、`std::cpp`（C++ 导出）。ONNX 导出与自动微分遇到 `enum` 给出带位置的明确拒绝。垃圾回收要能追踪 `enum` 值中的引用（含递归的 `Tree`），在两种回收器下都测（看现有 GC 压力测试）。
7. **工具**：调试器能显示 `enum` 值（同打印格式）；语言服务器的悬停显示类型、补全 `Shape::` 后的变体；格式化器保持写法；`std::gir`/`std::rgir` 打印正常；编辑器语法文件（`tools/` 下）识别 `enum`。
8. **退役 `A | B`**（5.3）：类型位置上的 `A | B`（联合类型）删除，`|` 只留给逻辑或。涉及 `src/parse/syntax/parser_type.cpp`、`src/core/type/composite/union.cpp`、`include/camel/core/type/storage.h` 的 `Union`、`src/parse/ast/builder.cpp` 等。删除前先搜索所有用到联合类型的地方（标准库签名、测试、文档）：
   - **注意**：`stdlib/signatures/json.cml` 的访问器写作 `get(j: Json | Json?)` 等。退役后改为接受 `Json?`，并按 11.2 同时接受 `Json!`（访问器接受 `Json!`，使解析失败也能链式取值）。`Json` 能否直接传给 `Json?` 形参取决于现有的可空转换规则——先手动验证；如果需要为此改类型规则，按第 4 节提问。
   - 写 `A | B` 的旧代码给出诊断：说明联合类型已退役，建议改用 `enum`。
9. **退役 `T ? U`**（3.9）：`?` 只是可空标记，`T ?` 后接类型的特殊读法去掉。搜索解析器中的这条读法与相关测试，删除并给诊断。
10. **测试**：每条语义一个小程序，在多个后端上跑；穷尽检查的正反例；泛型与递归（用 `Tree<int>` 做一次插入与遍历）；`==` 与打印；宏求值；GC 压力；ONNX、自动微分的拒绝诊断；`A | B` 与 `T ? U` 的退役诊断。
11. **文档**：语言参考新增 `enum` 一节（中英两份），写清以上全部规则与例子；删除联合类型与 `T ? U` 的描述；标准库参考更新 `json` 签名；`docs/roadmap.cn.md` 不用你改。

**可能遇到的待决问题**（遇到就按第 4 节写，不要自己定）：变体可见性与模块导出（`enum` 跨模块导入时变体是否随类型一起导出——建议随类型导出）；`enum` 能否作为映射的键或集合的元素；`T?`、`T!` 是否改由 `enum` 实现（**本任务不改**，只在报告里写你的看法）。

---

### H2：标签机制与数值许可改为标签

**前提**：地基"装饰器通用机制"已合并（已满足）。不再依赖 H1。

**依据**：设计原则 **第 6 节**（全文照做）、7.5（许可的规则）；节点标签的底层已有：`include/camel/core/node_attrs.h`、`OperatorTable::declareGrant`。

1. **`tag` 声明**：解析 `export tag 名 <参数> on 位置列表` 与别名组 `tag 名 = [ ... ]`；标签是与函数、类型同一命名空间的名字，可导入、可限定。C++ 模块用同一套登记接口声明标签（把现有的 `declareGrant`、`attr::registerKey` 收为"登记一个标签"）。
2. **`#` 写法**：`#名`、`#名<参数>`、`#[a, b<v>]`，可写在函数、类型声明、结构体字段、代码块前；贴到 `on` 未列出的位置是编译错误；参数是编译期的值。`#` 不再是注释（见 H6 第 6 项）。
3. **节点标签**（`on nodes`）用现有的节点属性实现：区域上的事实累积，同名带参的内层为准。**声明标签**（`on func`、`on type`、`on field`）存在声明上，C++ 查询接口可读；编译期代码中的读取（`match type`、`fields`）随泛型第一期接入，本任务只提供存储与 C++ 查询。
4. **数值许可改为标签，默认值按设计原则 7.4**：`reassociate`、`contract` 默认授予，其余用标签授予；新增标签 `#strict`（不使用任何许可，含默认许可）；核心的 `NumericPolicy` 以 7.3 为默认策略，安全模式参数即严格策略。标准库把 `reassociate`、`contract`、`approx_math`、`change_precision`、`no_nans`、`no_infs`、`no_signed_zeros`、`flush_subnormals` 声明为 `on nodes` 的标签，`finite_math` 为别名组。删除编译器前端的 `numeric` 分支与名单、`@name(...)` 写法（旧写法给出指向 `#[reassociate]` 的诊断）、一字节许可列；核心语义层通过节点标签读取许可（标签与含义的对应只在一处）；所有读者（算式优化、`tensor::fuse`、`inspect`、`std::jgir`、Tiles）改为同一个辅助函数。Tiles 只改到能编译。
5. **`@vjp` 改为 `#vjp<rule>`**（声明标签，值为规则函数的图引用），自动微分直接读取，删除靠宏求值与 `attachRule` 的做法。
6. **`macro` 改为标准标签 `#macro`**（不用导入）：`std::macro` 读标签，含义按设计原则第 8 节不变；求值失败按运行时故障处理，与任何遍运行中的故障相同；删除函数类型中的 `Modifier::Macro`。删除关键字 `inner`、`outer`、`atomic`、`shared`（标准库签名中的 `inner func` 改为无函数体的声明）。**已完成**（`#macro` 见 reports/H97.md；关键字删除与签名迁移见 reports/H107.md）。
8. **标签可声明读取它的遍**：`tag 名 ... read_by 遍名`（写法可调整，报告中说明）；流水线中没有该遍时给出"标签没有效果"的警告。
7. 许可、宏、自定义导数的原有测试改为新写法后全部通过；语言参考、标准库参考、诊断目录同步更新。

**剩下什么**：第 6 项已完成（`#macro` 见 reports/H97.md，关键字删除见 reports/H107.md）。第 4 项由 `7b4147c1` 完成，第 5 项由本分支后续提交完成（报告 reports/H77.md）；第 1–3、7、8 项早已完成（`856af859`）。

### H3：依赖节点属性的后续

**前提**：地基"装饰器通用机制"已合并。

**H3a 优化不改变图上的算子身份**（设计原则 9.6 第五条；审查 `modules.cn.md` 第 4 条）。现在 `tensor::fuse` 会把 `matmul` 换成 `matmul_blas`，把 `matmul` 加偏置、激活换成 `matmul_add`、`matmul_add_relu` 等新算子，这些新算子没有语义声明与定义图。改为：
- 图上保留原算子（`matmul`、`add`、`relu`……），融合与库实现的选择记在节点属性上（键由 `tensor` 模块注册，如"这几个节点作为一组用某实现执行"）；执行时按属性选内核。
- 选择规则读核心的统一判定：7.3（CPU 上按库实现的 f32 矩阵乘默认可用，条件是结果可复现；安全模式下用规范算法；安全模式由遍参数给出——遍参数是地基工作的一部分，用它的接口）。现在 `matmul_blas` 要求 `reassociate` 与 `change_precision` 两项许可，按 7.3 改为默认可用。
- 删除 `matmul_blas`、`matmul_add` 等作为图上算子的注册。ONNX 导出与 Tiles 看到的是原算子。
- 测试：融合前后结果在安全模式下逐位相同；默认模式下两次运行逐位相同。

**H3a 已完成**（分支 `handover/H102-fusion-keeps-operator`，报告 reports/H102.md）：图上的算子保持
`matmul`/`add`/`relu`，`tensor::fuse` 只写 `tensor.kernel`（"library"/"canonical"，按 7.3 的有效许可
判定，`#strict` 与 `tensor::fuse(safe_fp=true)` 得到 canonical）与 `tensor.fusion`（"linear"/
"linear_relu"，组内每个节点都写）；`matmul_blas`、`matmul_add`、`matmul_add_relu` 的注册、内核、
VJP 与 ONNX lowering 全部删除，ONNX 导出与 Tiles 的区域分析看到的就是程序写的算子。逐位证据在
`fuse_bits`（四种运行逐字节相同）与 `gemm_canonical.fused`；安全模式下融合与不融合相同是构造性的，
因为遍不改算子、CPU 上也不换内核。

**H3b 内联来源**（图存储第二版 3.3）：内联把被调函数体放进一个标注区域，记录被内联函数与调用处，嵌套内联即嵌套区域，被内联函数的声明标签挂在区域上（设计原则 9.4 的警告据此给出）。**调试器不为内联函数虚构帧**（裁定记录 2.14 该条已撤回）：内联进来的代码显示在调用者的帧里，标注来自哪个函数、哪次调用；GIR 可视化把区域画成可展开的框。同时核实 12.1 的"从分支的臂回到汇合处时不在同一行停第二次"，写测试确认，不符合就修。区域机制本身属于图存储第二版第 5 步；若该步尚未完成，本项先不做。

---

### H4：已裁定、尚未开工的小项

互相独立，可以分开提交，也可以分成 H4a、H4b…… 各开分支。

- **H4a 张量命名维度 `constas`**（设计原则 5.4 原文有例子）：`Tensor<f32, const [constas n, 3]>` 在形参类型里第一次出现时绑定 `n`，之后直接写 `n`；只出现在返回类型里的名字是错误；维度可以做加法和乘以常数（线性式，判断相等），其余结果为未知；调用处实参维度未知时编译期放过，在调用处做运行时检查（与 `as` 相同）；不产生新的实例。前提：先读张量类型与形状推断的现有实现（`modules/tensor`、类型解析器），弄清"形状常量"如何表示。
- **H4b `for` 遍历字符串、映射、张量**（设计原则 3.10 的循环条目）：字符串按码点（11.1：`string` 保证 UTF-8）；映射按什么顺序遍历**是语义问题**，按第 4 节提问（建议：插入顺序，如果映射实现保持插入顺序；否则提问）；张量沿第一维。**前提**：地基"前端清理"已合并（它改字符串）。
- **H4c ONNX 导出 `ArgMax`**：按 7.7 的规则（有 NaN 时返回第一个 NaN 的位置；并列取第一个，+0 大于 -0）。ONNX 的 `ArgMax` 不保证这些，导出时要么组合出等价的子图，要么在无法证明输入没有 NaN 和 -0 时拒绝导出并给出带位置的诊断。用 ONNX Runtime 测试（看现有 `ort_` 开头的测试）。
- **H4d 自动微分支持 `LOOP`**：现在遇到 `LOOP` 节点拒绝。读 `docs/design/loops` 与 `camel/runtime/loop.h` 的 `LoopView`。先写一份短设计（反向时如何保存每轮的状态、轮数未知时怎么办）放在报告里，**先不实现**，等协调者检查设计后再做。
- **H4e 数学模块的数值声明**（审查 `modules.cn.md` 第 15 条）：`math` 模块的算子补上数值语义声明与 ulp 上界，照张量算子已有的声明写法。
- **H4f ONNX 导出器的切片规则**（审查 `modules.cn.md` 第 14 条）：导出器不再自己重述切片越界的规则，改为读核心的索引语义声明。

不在这里的审查条目，要么属于正在进行的地基工作，要么归 Tiles（见 `docs/design/tiles-route/README.cn.md` 第 11 节），不要做。

---

### H5：命名类型（设计原则 5.2）

**前提**：H1（`enum`）已合并，因为 `enum` 也是命名类型，两者共用"命名类型的身份"。

**现状**：`type Point = { ... }` 是别名；`StructType::equals`（`src/core/type/composite/struct.cpp`）按字段名比较、不看顺序，所以 `{ a, b }` 与 `{ b, a }` 是同一个类型。

**要做的**：
1. **先统计影响面，写进报告再动手**：仓库里（标准库、测试、示例、模块）有多少处把一种有名字的结构体直接当另一种用，或依赖"结构相同即相同"；列出清单与改法。
2. 有名字的结构体成为命名类型：身份 = 名字 + 声明模块（泛型实例的身份还包括类型实参）。打印、诊断显示名字。
3. 无名结构体值可直接用作结构相同的命名类型（字段按名字对应）；两个命名类型之间要 `as`，字段按名字一一对应，结构不同是编译错误。
4. 字段顺序：命名类型按声明顺序；无名结构体类型的写出顺序属于类型，`equals` 相应修改。检查参数树、检查点、`tree_paths` 都按规范顺序。
5. 重载与泛型中的查找按 5.7：对依赖类型变量的值做运算时，另在类型声明所在的模块里找重载。这条若牵涉的改动大，先在报告里写方案，等协调者检查。
6. 泛型类型不变（5.8）：检查现有的隐式转换有没有穿透泛型参数的情况，有则改为编译错误并列出受影响的代码。
7. 所有后端、模块缓存、检查点的结构说明、ONNX、Tiles 中用到类型身份的地方都要核对。语言参考的数据模型一章（`docs/language/03_data_model.md` 与英文版）写清楚。

---

### H6：地基遗留的条目与声明上的标签

地基工作在额度用尽前收尾，下列条目没有完成（详见各条地基的合并提交说明与 `docs/reviews/2026-10-19/` 的对应条目）：

1. **前端**（审查 frontend）：第 6 条，期望类型合并为一条通道；第 7 条，`append` 的就地改写改走通用的 `setInPlaceForm`（牵涉 `std::cpp` 发射器、ONNX 导出器与测试）；第 15 条，语言服务器的类型与命名空间解析取自编译器（编译器先要记录类型名的声明来源）；`TypeCode::Byte` 的移除（会移动类型码与转换表，单独做）。`from_bytes::<T>` 按类型分 URI、`load_tree` 运行时读类型，等泛型（设计稿 `docs/design/generic-structure`）实现后再改，不在本任务。
2. **运行时**（审查 runtime）：第 9 条 `:graph/original` 改为引用表中的关系；第 10 条控制边约简移出编码器与建图器；`OperatorTraits` 的位由语义记录推出。用户函数声明"元素互不依赖"（3.6）用节点上的标签实现。（H85 部分完成：`:graph/original` 已在 `43286534f` 随循环 lowering 删除，不是改成关系；traits 由 `OperatorTraits::traitsOf` 唯一推出；`#independent` 标签与运行时查询已实现；控制边约简由 H95 收到 `std::opt::ctrl`：H85 因 `path_assigned.allocs` 从 `1 0 0` 变成 `2 0 0` 撤回，H95 测量证明复制次数不变，多出的块是数组字面量构造落进测量窗口，用例已把窗口锚定在构造之后，四个后端在有/无 `std::opt::ctrl` 下都是 `1 0 0`，见 `reports/H95.md`。）
3. **遍参数**：`--timeit` 不加入模块登记的默认遍；`CAMEL_OPT_GROWTH_BUDGET` 改为 `std::opt::inline` 的参数；调试用的跟踪变量是否改为参数，按第 4 节提问。
4. ~~声明上的标签~~ 已并入 H2。原文：在节点标签机制（`include/camel/core/node_attrs.h`）之上，为函数、类型声明、结构体字段增加声明上的标签，登记方式相同；`fields T` 与 `match type` 能读到（随泛型第一期一起做时再接入）；**`@vjp` 改为函数声明上的标签**（值为规则函数的图引用），自动微分直接读取，删除 `modules/autodiff/operators.cpp` 中靠宏求值与 `attachRule` 的做法；测试覆盖跨模块使用与模块缓存。
5. **未跑完全套测试**：三条地基（遍参数、效应统一、前端清理）合并时只跑了相关计划与冒烟测试。先跑一次全套，把与第 1.2 节基线相比新增的失败逐个修掉，写进报告。**这一项最先做。**（H80 在 Linux 主机上跑了 fast/slow/perf/gpu/GC/poison 全层：fast 层 0 失败、基线 19 项全转绿；新增的 GC、poison、tiles 三簇已转交 H73/H74/H75，见 `reports/H80.md`。H101 续跑慢层（`-Tier slow` 与 `perf/**`，2412 项）：修掉 4 个过期窗口/快照，转交 H98（`progen_check` 期望类型回归）与 H102（poison 同名副本的扫描伪失败），fast 验收又发现 H103（返回位置结构体类型回归）；见 `reports/H101.md`。）
6. **`#` 不再是注释**（4.1）：词法只保留首行 `#!`；仓库中所有 `#` 注释改为 `//`（`.cml` 文件、测试、文档示例、编辑器语法文件）；与 H2 同时合并。
7. **撤回默认流水线登记**（设计原则 9.2 已改）：删除 `registerDefaultPipelinePass`、`--no-module-passes` 及相关测试与文档；遍参数语法与模块登记遍名保留。需要某个遍的库（如自动微分需要 `std::macro`）在前提不满足时给出说明要加哪个遍的诊断。

**剩下什么**：第 1 项的前端四条与 `TypeCode::Byte` 由 H88 完成（`from_bytes::<T>` 已按类型分 URI，H103 查清无需再建；`load_tree` 等 H51 的 `ckpt` 迁移）；第 2 项的 `:graph/original`、traits、`#independent` 由 H85 完成，控制边约简由 H95 收到 `std::opt::ctrl`（reports/H95.md）；第 3、4、6、7 项已完成；第 5 项的全套测试由 H80/H101 收尾（H153 校正，依据 H88、H85、H95、H101、H103）。

---

### H7：资源与副作用（设计原则 3.3）

**前提**：无（与 H2 的标签工作相互独立；原生算子的资源从标签改为签名中的 `uses`）。

1. **`resource` 声明**与分组、默认可见的基本资源；资源与其他名字共用命名空间。
2. **函数类型带资源集合**：`uses` 列表与 `pure`；资源的推导（不动点）、承诺的检查、函数类型参数的资源泛型（按实例化确定）、资源的子类型关系。现有的资源声明（阶段 4 的 `rt::resourcesOf` 等）作为原生算子 `uses` 的存储，统一到这一处。
3. **`var` 作为资源**：每个变量、`var` 形参、闭包捕获。
4. **建图**：同一函数体内同一资源的执行者之间按书写顺序生成控制边；`sync` 块的语义不变；按需执行的规则不变。
5. **删除 `wait` 与前缀 `sync func`**：解析器不再认识它们（不留专门的诊断）；仓库中全部用法改写；`IgnoredSideEffect`、`CapturedWriteInAsyncFunction` 等诊断按新模型重新审视（多数应消失）。
6. **文档**：语言参考第 4 章（4.4 重写）、设计原则 3.4、裁定记录 2.6 的旧文字加注"由 2.24 修订"、第 14 节相关误读条目、诊断目录。
7. **测试**：同一变量、同一资源的自动有序；不同资源互不约束（用可观察的方式验证，如两个文件的写入）；按需执行不变；`pure` 与 `uses` 承诺的编译错误；高阶函数的资源泛型；在各后端运行。

---

### H8：基础与清理工作（2026-10-19 指派，优先于 H2–H7）

说明：下一轮大规模重构（[总纲](../design/blueprint/README.cn.md)）由协调者在设计定稿后亲自主导。在那之前，请先做下面这些具体、边界清楚的工作。**按顺序做，每项一个或几个提交，每项完成后在报告 `reports/H8.md` 里追加一节**（做了什么、测试结果、遇到的问题）。遇到需要设计判断的地方，停下来写进报告的"待决问题"，不要自己定。H2–H7 暂缓。

1. **清理基线失败**：全套测试中既有的失败（当前清单在第 1.2 节，合并 `develop` 后以实际为准）逐个修掉，或查明原因写进报告。包括：AST 快照（需要 Python 3：`python3 test/tools/parsecheck/check.py snapshot --update`；本机没有就先安装或在报告中说明环境）、`frontend.format_keeps_ast` 与 `frontend.fuzz_lossless_tree`、`registry.signatures_match`（补齐签名文件中缺的算子）、`portability.std_includes`（补头文件）、几项图检查与导出拒绝测试、`04_staticness`、`05_effect_order`。**不许为通过而改期望输出**：每一处期望输出的改动都要在报告中说明为什么新的输出才是对的。
2. **格式化器的崩溃**：`case 1`、`case Shape::Circle(r)` 一类 `match` 臂使 `camel-format` 崩溃（H1 报告"没做完的"第 3 条）。找到原因修好，并加测试。
3. **内部算子按编号引用**：建图器与其他地方用字符串名引用内部算子的（如 `__enum_make__`、`__enum_tag__`、`__enum_field__`，以及用 `grep -rn 'Operator("__' src` 能找到的同类用法），改为在登记时取得 `OperId` 保存、按编号引用。行为不变。
4. **删除 `UnionType`**：类本身以及 `storage.h`、`typevar.cpp`、`tuple.cpp`、ANTLR 生成头文件中的分支；语言入口已经只剩退役诊断。单独一个提交。
5. **`enum` 的收尾**：跨模块使用的测试（H1 Q1）；`std::cpp` 支持 `enum`（在 `camel/standalone/runtime.h` 给出运行时表示与 make/tag/field/print/equal，`cpp_types.cpp` 与算子规则接上；四个调度器与 `std::cpp` 输出逐字相同的测试）；语言服务器在 `Shape::` 后补全变体（语义查询层增加"类型的成员"查询）。
6. **建立性能与内存基线**（为图存储重构做准备，**只测量，不优化**）：选 5–8 个有代表性的程序（`test/plans/perf` 中的、一个大一点的训练示例、一个递归很深的例子如 fib、一个大参数树），在 `std::nvm`、`std::fvm`、`std::jit` 上记录：编译（建图）时间、`std::opt` 的时间、峰值内存、图占用的字节数（每节点）、执行时间。写一个脚本可重复地产生这些数字，结果写进 `docs/performance/graph-store-baseline.md`（表格加测量方法与机器信息）。
7. **列清单（只调查，不修改）**：在 `docs/handover/reports/H8.md` 中列出：(a) 代码里仍按名字字符串判断算子、模块、类型的地方（`grep` 字符串比较与 URI 前缀），每处一行：位置、做什么、按什么判断；(b) 仍然扫描整图或整个程序去找引用的地方。这两份清单供重构时使用。

**剩下什么**：无。第 3 项（内部算子改为按 `OperId` 引用）在 `handover/H68-operid` 上完成，
提交 `12cbd9eb`、`bc2cf5ea`、`3a2669f9`，见 `reports/H68.md`；第 1、2、4–7 项见 `reports/H8.md`。

---

### H9：缺陷修复与平台健壮性（2026-10-20 指派）

与 H8 相同的做法：按顺序做，每项一个或几个提交，报告写在 `reports/H9.md`，每项一节；遇到需要设计判断的地方停下来提问。

1. **直接调用的 FUNC 节点记录整个调用表达式的源码范围**（H8 报告第 1 节 `04_staticness`）：现在记成实参表 `(x * 0.5)`，应当是 `f(x * 0.5)` 整个调用。修好后 `04_staticness` 应恢复通过。顺带检查其他由调用产生的节点（算子调用、方法式调用 `x->f()`、管道写法）是否有同样的问题，一并修好；涉及的期望输出逐一说明。
2. **`alloc_small` 的偶发访问冲突**：`camel --timeit std::nvm/5 test/cases/perf/gc/alloc_small.cml --arg reps=200000 --output-format json` 偶发以 `0xC0000005` 结束。先写一个能稳定复现的循环脚本（重复运行到出现为止，记录出现频率），用调试构建拿到调用栈，查明原因（GC、计时、输出路径都有可能）并修好；加一个压力测试，在两种回收器下运行。查不出原因时，在报告中写下已排除的可能与证据。
3. **Windows 上的非 ASCII 路径与环境**：用户名或 `PATH` 含中文时 `sys.basic` 失败。Windows 上读取环境变量、命令行参数、当前目录、文件路径，一律使用宽字符接口（`GetEnvironmentVariableW`、`GetCommandLineW`/`wmain`、`_wgetcwd`、`std::filesystem::path` 的宽字符形式等），再转换为 UTF-8；打开文件时把 UTF-8 路径转回宽字符。范围包括 CLI 入口、`io`/`fs`/`env` 模块、模块搜索路径（`CAMEL_HOME`、`CAMEL_PACKAGES`）。测试：在一个含中文名的临时目录里运行程序、读写文件、读取环境变量。
4. **测试的残留文件**：测试写入 `tmp/test/...` 的产物，在下一次运行时会影响结果（如"拒绝时不得写出模型"的断言被上一次留下的文件干扰）。让会写文件的测试在运行前清理自己的输出，或使用每次运行唯一的目录；找出所有这类测试一并处理。

---

### H10：测试提速（2026-10-20 指派，H9 之后做）

**问题**：全套测试拖慢开发。以 2026-10-19 的一次全套为例（5,667 项）：各测试耗时之和约 2,424 秒（40 分钟），而且运行器逐个串行执行。耗时极不均匀：

- 超过 5 秒的 72 项占了 2,060 秒（85%）；
- 最慢的两项是与 PyTorch 对比梯度的 ResNet 测试（`resnetbn_gradient_relu_kinks_vs_torch64` 530 秒、`resnet_gradient_relu_kinks_vs_torch64` 248 秒），合计 778 秒（32%）；
- `poison:` 前缀的 GC 投毒扫描 1,627 项，合计 607 秒（25%）；
- GC 压力（`gc.collection_trees_*`）、`progen_check`、`agree_with_fvm`、`export_tree_recursion`、`benchmark_gradients_vs_torch` 等各数十秒。

**要做的**（按顺序，每项一节报告，记录前后的耗时）：

1. **并行执行**：`test/tools/run-tests.mjs` 支持 `--jobs N`（默认为处理器核数），以测试为单位并行，每个测试使用独立的临时目录（依赖 H9 第 4 项）。输出顺序保持确定（按计划与测试的顺序汇总），失败信息完整。先找出不能并行的测试（共享文件、端口、全局状态），标记为串行执行而不是放弃并行。
2. **分层**：利用已有的 `tier` 字段，把测试分为 `fast`（默认，`npm run test` 只跑这一层）与 `slow`。归入 `slow` 的：与 PyTorch 的对比、GC 压力与 GC 投毒扫描、随机程序检查、性能基准、单项超过约 5 秒的其他测试。目标：`fast` 层在 8 核机器上并行后 2 分钟以内。`npm run test:all`（或 `--tier all`）跑全部。在 `docs/test-plans.md` 写明分层规则：新测试默认进 `fast`，超过 5 秒的必须标为 `slow` 并说明理由。
3. **投毒扫描只扫一部分**：现在投毒扫描把大量普通测试各重跑一遍。改为：`fast` 层对每个计划只扫少量有代表性的用例（例如每个计划一个），完整的投毒扫描放进 `slow`。不得删掉 GC 相关测试本身。
4. **缩小最慢的测试**：与 PyTorch 对比的 ResNet 测试，检查能否在保持检验能力（同样覆盖 ReLU 拐点、BN 等情形）的前提下缩小网络与批量；做不到就保留在 `slow`。
5. **按改动选择计划（可选）**：提供 `node scripts/test.js --changed`，根据与 `origin/fsh-dev` 相比改动的文件（如 `src/passes/sched/fastvm/` → 调度器相关计划；`docs/` → 文档检查），选出相关计划运行。映射写成一个简单的配置文件，选不出时退回 `fast` 全层。

**约定变化**：完成后，日常开发跑 `fast`；合入 `develop` 前跑一次 `all`。交接任务书第 1.2 节同步修改。

---

### H11：健壮性与小缺陷（2026-10-20 指派）

1. **按内容比较相等改为迭代**（`src/core/rtdata/equality.cpp`）：现在是递归，比较很长的递归 `enum`（如由 `Node(..., next)` 串起的百万个节点）会耗尽原生栈。改为显式栈；同时排查其他会随数据深度递归的地方并一并改为迭代或给出可控的诊断：打印（`appendText` 对递归 `enum`、嵌套数组）、`json` 的编码与解码、GC 的追踪、`std::cpp` 生成的打印与相等、类型的 `toString`/`equals`/`mangle`（深嵌套类型）。每处加一个"很深的数据"的测试（如 100 万层的链表式 `enum`、10 万层嵌套数组），在四个调度器上运行。
2. **`match` 的诊断与覆盖检查的测试补全**：漏掉变体、重复的臂、多余的 `case _`、单元变体写成绑定名（`case Empty`）等情形各有正反例测试，诊断带位置并列出漏掉的变体名。
3. **H8 报告中记下、未处理的小问题**：逐一核对报告"未做完"与各节中提到的遗留，能直接修的修掉，不能的写明原因。
4. **`docs/performance/graph-store-baseline.md` 的基线脚本进 CI 可选作业**：只是让它能被一条命令完整跑完并输出表格；不加阈值判断。

---

### H12：拆分建图器（纯搬移，2026-10-20 指派）

`src/compile/gir/builder.cpp` 已有约 8,800 行，`src/compile/gct/builder.cpp` 约 3,800 行，难以维护。

- **只搬移，不改逻辑**：按功能把成员函数移到同目录下的多个文件（例如：表达式与运算符、调用与重载、控制流与 `match`、`enum`、装饰器与标签、模块与导入、类型与泛型实例化、诊断辅助），类的声明保持一个；每个新文件开头一句话说明它包含什么。
- 每次搬一组，单独提交，提交信息写明"纯搬移"；用 `git diff --color-moved` 自查确实只有搬移；相关计划通过。
- 不改名字、不改签名、不顺手重构。发现想改的地方记进报告，留给重构。
- 构建系统（`CMakeLists.txt`）同步；`node scripts/check-std-includes.mjs` 通过。

---

### H13：官方示例第一批（2026-10-20 指派）

计划见 [官方示例计划](../design/showcase/README.cn.md)。先做第一批：**C1、C2、C6、D4、D5**，按第 3 节的组织方式（`showcase/` 下每个示例一个目录：Camel 实现、PyTorch 对照、数据脚本、运行与比较脚本、README；小规模配置进 `slow` 层测试）。

- Camel 实现是展示品：按当前设计原则写，追求清楚可读；遇到写不顺的地方（语言缺什么、哪里别扭），**不要绕开**，记进该示例 README 的"遇到的问题"，并在报告中提问。
- PyTorch 对照按社区惯用写法，不故意写差；结果按设计原则第 7 节比较。
- 度量：代码行数与并排对照、吞吐、峰值内存、编译时间，由脚本生成表格。
- 第一个示例（C2，MLP 训练 MNIST）做完后先停下，在报告中给出目录结构与度量方式，等协调者确认后再做其余四个。

**剩下什么**：D4（MCTS 示例）未做，卡在 Q6/Q7（MCTS 写回与运行期数组 → 张量）；C1、C2、C6、D5 已完成。

---

### H14：反引号字符串插值（2026-10-20 指派，插在队列最前面）

依据设计原则 11.5：`` `step ${i}: loss ${l:.4f}` ``。

1. 词法：反引号字符串已识别为 `FString`（现在报"暂不支持"）。`${` 开始一个插值，里面是任意表达式，可选 `:格式说明`（与 `format` 的格式说明相同），到匹配的 `}` 结束；表达式内可以嵌套字符串与花括号。普通的 `{`、`}` 不需转义；`\${` 表示字面的 `${`。可以跨行，换行原样保留。
2. 语义：编译为 `'...'->format<表达式...>`（同一语义，2.18 的简写）；格式说明与类型不符在编译期报错（沿用 `format` 的检查），错误位置指向那个插值。
3. 工具：格式化器、语法高亮（编辑器语法文件）、语言服务器（插值表达式内的悬停与补全）。
4. 测试：各种表达式、嵌套、格式说明、跨行、转义、错误的格式说明；四个调度器与 `std::cpp` 输出逐字一致。
5. 文档：语言参考 2.x（词法）与 6.x（表达式），中英两份；把仓库示例与文档中合适的 `format` 用法改为插值（不必全部改）。

---

### H15：复数与字面量按上下文取类型（2026-10-20 指派，紧接 H14）

依据设计原则 5.1。

1. **字面量按上下文取类型**（Go 的无类型常量）：期望的类型 → 二元运算另一操作数的类型 → 默认类型；字面量之间的运算在编译期按精确值计算；放不进目标类型是编译期错误；变量之间不做隐式转换。核对现有测试中因此改变的诊断（例如以前要求写 `2.0` 的地方），每处说明。
2. **复数**：核心类型 `c64`、`c128`（类型表、运行时表示、数组中的紧凑存储、DLPack 的复数编码）；虚数字面量 `4i`、`4.0i`、`1e-3i`（词法：数字紧跟 `i` 且其后不是标识符字符；十六进制、二进制不可带）；算术、比较（只有相等）、`abs`、`re`、`im`、`conj`、`exp` 等基本函数（放在 `math` 或新的 `complex` 模块，数值声明按设计原则第 7 节写）；打印 `3+4i`；格式化说明。
3. 四个调度器与 `std::cpp` 输出逐字一致；语言参考与标准库参考（中英）。

### H16：一批小项（2026-10-20 指派，紧接 H15）

1. **`Json` 访问器接受 `Json!`**，错误原样传下去（设计原则 11.2）；**`JsonTooDeep`** 错误种类。
2. **管道调用带额外实参**：`x->f(a, b)` 即 `f(x, a, b)`（4.4）；解析、类型检查、格式化器、文档。
3. **`enum` 作映射的键与集合的元素**（11.3）：所有字段可比较时允许；顺序先按变体声明顺序，再逐字段。
4. **源码范围**：部分应用 `inc<1>` 取表达式本身；`error(...)` 取整个调用表达式。
5. **调试构建**：关闭 `WINDOWS_EXPORT_ALL_SYMBOLS`，改为显式导出，使 `npm run debug` 能链接。
6. **`--timeit` 失败时在 JSON 中记录失败的迭代号**。
7. **语法树快照 91 处既有差异**：逐一核对，属于设计变更的更新快照，属于缺陷的修掉，报告中列出。

**剩下什么**：无。第 1–5 项见 reports/H16.md、reports/H16b.md；第 6、7 项见 reports/H66.md（第 6 项给出 `--timeit` 失败轮的 JSON `"iteration"` 与同号文本行；第 7 项在 `043ac0eb` 上重新生成后为 0 处差异，91 处已由后续任务的快照再生成消化，逐批理由见该报告 §2.2）。

---

### 先交计划的任务（H17、H18 适用）

这类任务的设计已定，但实现上有结构性的选择。**动手之前先写实施计划**，放在 `reports/H17-plan.md`（或 H18；H17 的计划在 [arrays-memory/plan.cn.md](../design/arrays-memory/plan.cn.md)），内容：改哪些数据结构与接口（列出文件与类型）、数据怎样流动、旧结构怎样过渡与删除、按什么顺序提交（每次提交都能构建、相关测试通过）、怎样测试（含性能基线的对比）、有哪些拿不准的地方。**协调者在 `reviews/` 中批复计划后再实施**；等待批复期间可以做队列中的其他项。

### H17：原生多维数组与缓冲区（2026-10-20 指派，先交计划）

依据：[原生多维数组、内存管理与互操作](../design/arrays-memory/README.cn.md) 第 9 节第 1–3 步，设计原则 5.4、10.5。

1. **缓冲区对象与引用计数**：张量改为"数组值 + 缓冲区对象"；缓冲区只放数字（红线）；视图共用缓冲区并计数；并行处用原子计数；DLPack 交换改为零拷贝（进与出两个方向，释放回调）。
2. **原生多维数组类型** `T[维度...]`：类型规则、维度绑定（形参中首次出现自动绑定、已有名字为引用、只出现一次警告、`?`、`..s`、线性运算）、形状检查从 `tensor` 模块的解析器移入核心；`Tensor<T, [dims]>` 作为别名；现有 `T[]` 即 `T[?]`，行为不变；`map`/`filter`/`sort` 对多维数组按第一维。
3. **原地更新的保证**：计数为 1 时，"返回新值"的数组操作原地进行；以分配计数断言的测试覆盖每种操作。
4. 性能基线前后对比（小型逻辑计算不变慢；张量程序的时间与峰值内存不升）。

**剩下什么**：第 2 项（`T[维度...]`、维度绑定、`Tensor` 别名）未开始，第 1 项还差三个证明缺口、第 3 项只覆盖排序/追加/下标/循环状态，第 4 项按计划的 P7 未做。

### H18：图存储第二版第 3 步——索引与增量提交（2026-10-20 指派，先交计划）

依据：[图存储与图 API（第二版）](../design/graph-store/README.cn.md) 4.3、4.5 与第 1 节列出的扫描式算法；H8 报告第 7 节清单 (b)。

1. 使用者索引按图版本建一次并缓存、事务中增量维护；程序级引用索引随提交更新。
2. 提交只解码、校验、编码改过的图与受影响的直接依赖者；`std::opt` 与内联改为局部改写；内联用调用索引维护工作列表。
3. 删除扫描式算法：`eraseNode` 全表扫描、连边时重建对端尾块、内联后重新收集闭包、提交时解码整个闭包、从节点重新推导引用。
4. 本步**不改**图的身份（`GraphId`、快照）与布局（32 位、分块），那是第 1、2 步，由协调者主导。
5. 性能基线前后对比：建图与 `std::opt` 的时间、峰值内存应当下降。

### H19：标准库重新规划的落地（2026-10-20 指派，先交计划）

依据：[标准库的重新规划](../design/stdlib/README.cn.md) 第 2、3 节与 [盘点](../design/stdlib/inventory.cn.md)。按"先交计划的任务"的规则执行。

1. **一个事实只写一处**：原生函数以 `.cml` 中无函数体的声明为唯一来源（签名、纯度、资源、故障），C++ 按名字绑定实现；删去 C++ 注册与算子表中重复的声明；`stdlib/signatures` 参与编译。计划中说明绑定机制与各后端（四个调度器、`std::cpp`）怎样取得这些事实。
2. **统一约定**：写一页《标准库约定》（命名、参数顺序与尖括号、错误、资源名、纯度），放在 `docs/`。
3. **按模块分批迁移**：去重（`sleep`、排序、`has`/`contains`、`min`/`max`/`sum` 等）、改名与拆分（`stats`、`env`、`process`、`io`）、错误约定（`fs`、`env`、`time`）、资源名、能用 Camel 写的改为 Camel（`array`、`stats`、`random` 的组合部分、`Map`/`Set` 的组合操作、排序之外的 `algo`）；工具类移出（`test`、`bench`、`profiler`；`this`、`rendezvous` 进测试设施）。每批一次提交，能构建、相关测试通过；测试与示例随之改写（内测阶段不考虑兼容）。
4. 不在本项：网络、流、键式随机数、优化器、扩展库（另行指派）。

**剩下什么**：第 0、1 步已入（`handover/H76-stdlib-step1`，报告 reports/H76.md）；H94 的六条裁定解决了第 1 步报告第 6 节的大部分待决问题（`.cml` 与 `.cmo` 是同一个模块、泛型声明之间的重叠检查、私有声明、入口前缀、只能声明自己的算子）。第 10 步的模块批次已由 H104 开始（`str`，报告 reports/H104.md）；`io` 的终端部分已由 H116 从 `os` 拆出并声明化（`stdlib/io.cml`，5 个算子；报告 reports/H116.md）；`math`/`fs`/`time`/`json`/`re`/`random` 已按同一做法迁完；`graph` 由 H141 声明化（45 条无体声明，6 条计算签名与 `__neq__` 留在 C++；报告 reports/H141.md）。`globals` 不是模块（`BuiltinModule("")`、不可导入、通配 URI、变参、非标识符名、`sleep`/`exit` 绑 `process:` 前缀），H141 只做了调查，**要不要给语言全局一个声明宿主需要作者裁定**。第 4 步其余的 `exit`/`sleep`（全局内建，URI 已是 `process:*`）、第 8 步的 `str::bytes`/`from_bytes` 删除（已由 H122 完成），以及第 2–3、5–9 步仍未开始。
**剩下什么**：第 0、1 步已入（reports/H76.md）；H94 的六条裁定解决了第 1 步报告第 6 节的大部分待决问题。第 10 步已声明化 13 个模块（`bytes`、`str`、`math`、`json`、`io`、`re`、`random`、`fs`、`time`、`algo`、`statistics`、`para`、`this`）；第 4 步的改名由 H114 完成、`io` 由 H116 重建，第 8 步的 `str::bytes` 由 H122 删除，第 9 步的资源名由 H134 改完（`io:terminal` 与 `eprintln` 未做），第 11 步由 H127 收尾；第 2 步的删除计划由 H116/H146 取代。未做：第 3 步（`perf`+`sim`→`bench`）、第 5 步（`algo`→`array`）、第 6 步（`statistics`→`stats`）、第 7 步的错误约定，以及 `env`/`process`/`hash` 的声明化与 `ckpt`（阻塞，H140）。逐模块账本见下。（H153 校正，依据 H114、H116、H122、H127、H134、H135、H146）

#### H19 模块账本（H153 重建，2026-10-22）

判定口径：**声明** = 全部签名在 `stdlib/<m>.cml`；**声明 + 计算签名（n）** = 固定签名在声明里、n 条由 C++ 按实参算（参考条目在 `signatures/<m>.cml`，模块项写 `computed:`）；**仍 C++** = 签名文件还在、未迁移；**计算签名（不在 H19）** = 按 H19 计划批复 Q2(a) 留在 resolver/`OpDef`；**阻塞** = 机制边界，见报告；**N 可声明 / M 计算** = 该模块 N 个名字的固定签名能写成无体声明、M 个名字的类型由 C++ 按实参算（H157 起用于不在 H19 的模块；装载条件见 reports/H157.md §0）。

| 模块 | 状态 | 依据（报告 / 问题） |
|---|---|---|
| `bytes` | 声明 | reports/H76.md（第 1 步试点） |
| `str` | 声明 | reports/H104.md |
| `math` | 声明 + 计算签名（3：`sqrt`/`shl`/`shr`） | reports/H113.md |
| `json` | 声明 + 计算签名（2：`encode`/`decode_as`） | reports/H115.md；H103 §3 |
| `re` | 声明 | reports/H126.md |
| `random` | 声明 + 计算签名（3：`choice`/`sample`/`shuffle`） | reports/H128.md |
| `fs` | 声明 | reports/H131.md |
| `time` | 声明 | reports/H133.md |
| `io` | 声明 | reports/H116.md |
| `statistics` | 声明 | reports/H135.md |
| `algo` | 声明 + 计算签名（7：`sort` 族、`merge_sorted_arrays`、`flatten`、`unique`） | reports/H135.md |
| `para` | 声明（`rendezvous`）+ 计算签名（2：`unordered_foreach`/`unordered_reduce`） | reports/H146.md §2.2 |
| `this` | 声明（`zen`） | reports/H146.md |
| `tiles` | 声明（`available`/`device`）+ 计算签名（10：`gpu`、`upload`、`download`、`run`、`run_device`、`run_preferred_resident`、`source`、`inspect`、`benchmark`、`regions`） | reports/H156.md（**H156 校正**：H19-plan §1.5/§2/§11 把 `tiles` 归进「不动」是工作归属的安排，不是机制判断；实测 12 个导出里 2 条固定、10 条计算、0 阻塞） |
| `env` | 仍 C++（`stdlib/signatures/env.cml`）；改名已由 H114 完成 | reports/H114.md；H19-plan §3 第 10 步 |
| `process` | 仍 C++（`stdlib/signatures/process.cml`）；改名已由 H114 完成 | reports/H114.md；H19-plan §3 第 10 步 |
| `hash` | 仍 C++（`stdlib/signatures/hash.cml`）；改名已由 H114 完成 | reports/H114.md；H19-plan §3 第 10 步 |
| `globals` | 仍 C++（内建；`signatures/globals.cml` 只供参考，不是声明模块） | H19-plan §3 第 10 步；H137.md §2 |
| `ckpt` | **阻塞**：5 个 intrinsic 无执行器、`save_tree` 的叶子谓词写不出、`load_tree` 的结果类型取自 `::<T>` | reports/H140.md §0/§3；reports/H174.md（在 `8bb46f120` 上重测三条仍成立；声明路线 Q1+Q3 上限 2/7，最小解锁是 H51 第 2 项 + H108 选项 1）；H103 Q1–Q3（参数化声明）；H51 第二期（`where`/类型函数）；H108 §3（作者裁定） |
| `tensor` | 计算签名（`OpDef`；参考页只收 `from_bytes`/`to_bytes`） | H19-plan §2；H19 计划批复 Q2(a) |
| `graph` | 计算签名（resolver） | H19-plan §2；H19 计划批复 Q2(a) |
| `rewrite` | 计算签名（resolver） | H19-plan §2；H19 计划批复 Q2(a) |
| `nn`、`onnx`、`autodiff`、`pyplot`、`python` | 计算签名（动态/可选，H19 不动）；**H166 校正**：`pyplot`、`python` 已拆出并迁移，见下面两行 | H19-plan §1.5、§2；H166 |

- **H156 校正**：声明模块与计算签名的树内数字在主机上实测（`camel inspect --ops` 与 `node scripts/stdlib-declarations.mjs --check`）。H156 改动前是 **14 个声明模块、23 条计算签名**（`graph` 的 45 条声明与 6 条 `// dynamic func`、`math` 的 3 条计算条目都算在内），迁移 `tiles` 后是 **15 个声明模块、33 条计算签名、0 问题**（`--verify-ops` 399 条重载、参考页 275 条）。旧条目里的「13/17」是 H146 §6.1 的数；H153 在 Windows 上只复现了枚举、没跑 `camel inspect --ops`，本机没有构建。（依据 reports/H156.md §7、§8）
- 同表 `graph` 行仍写「计算签名（resolver）」，与 H141 的 45 条声明不符；H156 只改 `tiles` 行与计数，没有替 H141 改它的行（依据 reports/H156.md §8）。
| `tensor` | **未审**：H151 独立任务（本任务开始时该分支无提交）；本行不再断言「计算签名」 | H19-plan §2；H151（H157 校正，依据 H157 §8） |
| `graph` | **已迁移**：45 条声明 + 6 条计算签名（`// dynamic func`） | reports/H141.md（H157 校正，依据 H141） |
| `rewrite` | **8 可声明 / 0 计算**（3 阻塞：`__rule`/`__rule_when` 的任意函数类型、`__analysis` 变参） | reports/H157.md §2（H157 校正，依据 H157） |
| `nn` | **H165 已迁移**：1 条声明（`save_params`）+ 18 个名字 / 20 条计算签名（15 个张量算子与 3 个优化器由 `OpDef`/resolver 算） | reports/H165.md §2–§4（H165 校正，依据 H165） |
| `onnx` | **1 可声明 / 0 计算**（`supported_operators`；`export_model` 阻塞：任意函数 + 结构示例类型） | reports/H157.md §4（H157 校正，依据 H157） |
| `tiles` | **H156 已迁移**：`available`/`device` 进 `stdlib/tiles.cml`，其余 10 条留 C++；H157 开工时 H156 无提交、收尾时已有迁移，未计入 H157 合计 | reports/H156.md（H157 注，依据 H156） |
| `autodiff` | **1 可声明 / 2 计算**（`stop_gradient`；`grad`/`with_grad` 的梯度函数类型由实参算） | reports/H157.md §5（H157 校正，依据 H157） |
| `pyplot` | **已迁移**：`plot` 的 8 条声明进 `stdlib/pyplot.cml`（4 种元素类型 × 2 种参数个数，0 条计算签名）；补上 `plot` 的纯度后装载成功 | reports/H166.md（**H166 校正**，依据 H166） |
| `python` | **已迁移**：6 条固定签名进 `stdlib/python.cml`；5 个名字（6 个 URI：`py_call` 两条、`py_print`、`py_println`、`py_wrap`、`py_unwrap`）留 C++，记入 `stdlib/signatures/python.cml` | reports/H166.md（**H166 校正**，依据 H166） |
| `autodiff` | **H165 已迁移**：1 条声明（`stop_gradient`）+ 2 条计算签名（`grad`/`with_grad` 的梯度函数类型由实参算） | reports/H165.md §5（H165 校正，依据 H165） |
| `pyplot` | **1 可声明 / 0 计算**（`plot` 是固定签名；需先补 `plot` 的纯度，补后 8 条声明） | reports/H157.md §6.1（H157 校正，依据 H157） |
| `python` | **6 可声明 / 0 计算**（6 个固定签名，需先补纯度；`py_wrap`/`py_unwrap` 的 `PyObject<T>` 与 3 个变参名字（4 个 URI）阻塞） | reports/H157.md §6.2（H157 校正，依据 H157） |

- H157 计数口径：按**名字**计（不是注册条数；`python:py_call` 与 `python:py_call_kw` 只算一个名字）；「可声明」包含“签名可写、只差模块补 `OperatorTable::declare` 纯度”的 7 条（`pyplot` 1、`python` 6），其中**今天就能装载的 11 条**见 reports/H157.md §0。六个模块合计 47 个名字 = 18 可声明 + 20 计算签名 + 9 阻塞；`graph` 已由 H141 迁移，`tensor` 归 H151，`tiles` 归 H156，三者不在 47 里。原六模块合并行按模块拆成五行（`tiles` 归 H156），未删模块或依据（H157）。
- **H166 校正**：`pyplot`、`python` 迁移后主机实测 **18 个声明模块、92 条计算签名、0 问题**（`--verify-ops` **485** 条重载、参考页 **352** 条）；H157 的「13 条纯度条目」「`plot` 补纯度后 8 条声明」「7 个名字只差纯度」三个数都成立。`pyplot` 与 `python` 的纯度全部按 effectful 注册，注册前后 `std::jgir` 逐字节相同、`inspect --ops` 只有 `purity` 字段变化；`python` 的 `py_wrap`/`py_unwrap`（`PyObject<T>`）作为声明语法缺口留给作者裁定，3 个变参名字继续记计算签名（依据 reports/H166.md §2–§6）。
- 13 个声明模块、17 条计算签名与 `node scripts/stdlib-declarations.mjs --check` 的树内判定一致（H146 §6.1 记录“13 个声明模块、17 条计算签名、0 问题”；H153 在 Windows 上只复现了枚举，没跑 `camel inspect --ops`，本机没有构建）。
- **H165 校正**：迁移 `nn`、`autodiff` 后 `--check` 是 **18 个声明模块、107 条计算签名、0 问题**，`--verify-ops` **489** 条重载，参考页 **361** 条，AST 快照 **2550** 个文件；两个模块的计数口径见 reports/H165.md §0（H165）。
- `para` 的两条不是阻塞：H146 §2.2 判定它们可以声明，但声明写不出“函数类型取自数组元素类型”，按约定留在计算签名（与 H115/H128 同形）；`ckpt` 才是机制边界上的阻塞（H140 §3）。
- 第 5 步（`algo`→`array`）与第 6 步（`statistics`→`stats`）未做，所以两者仍是独立模块；`array`、`rules`、`tensor_definitions` 是 Camel 源码/定义库，没有 C++ 签名文件可迁。
- 工具与测试设施：`perf`（`module util`）+`sim`→`bench`（第 3 步）与 `test_support`（第 2 步的计划，已由 H146 的保留并声明化取代）未做。

### H20：H13、H14 检查后的小项（2026-10-20 指派）

依据 `reviews/H13-H14.md`。

1. **插值收尾**（甲车道）：插值中格式说明不合法且含 `=>` 时提示给 lambda 加括号；所有字符串用同一张转义表（普通字符串的 `` \` `` 也解码）；GCT 的 FString 分支改为内部错误断言；`${}` 给专门的"空插值"诊断；`grammar.tables` 补上 `constas` 关键字。
2. **张量的取行与标签**（乙车道）：`take(t, indices)` 沿第一维取行（纯算子，四个调度器、`std::cpp`、自动微分的反向、ONNX `Gather`）；`one_hot(labels, classes)`；`softmax_cross_entropy` 按目标的类型重载，接受整数下标。
3. **键式随机数的最小集**（乙车道）：`random::key(seed)`、`split(key)`、`permutation(key, n)`，Philox，纯函数，各后端逐位一致。
4. **检查点与删除**（戊车道）：`save_tree` 改为整体替换（临时目录 + 原子替换，失败时保留旧目录），与签名注释一致；`fs::remove`、`fs::remove_dir` 返回 `T!`。
5. 完成后 C2 示例改用 `take` + `permutation` 每轮重新打乱、用整数标签（去掉 one-hot 文件）、用反引号插值与解构，重测一次（standard 配置，空载时）。

**剩下什么**：第 1–5 项均已完成；第 1 项（插值收尾）见 `reports/H78.md`，第 2–5 项见 `reports/H20.md`。

### H21：C2 暴露的写法（2026-10-20 指派）

依据设计原则 5.4、5.9、9.9、3.10（语言作者已定）。

1. **下标与切片**（甲车道，H15 之后；与乙车道的 H17 协调：H17 计划中若改变数组表示，切片按视图实现放到 H17 之后，语法与类型检查先做）：`t[a:b]`、`t[a:b:s]`、省略端点、多维 `t[:, 0]`、`t[a:b, c]`、按整数数组下标 `t[idx]`（降低为 `take`）；负数下标是故障；`last(t)`；越界规则见 3.9。四个调度器、`std::cpp`、自动微分的反向（切片的反向是散射）、ONNX（`Slice`、`Gather`）；格式化器、语言服务器、文档。
2. **lambda 参数类型从上下文推出**（甲车道）：期望函数类型的位置（实参、`with` 实参、带标注的绑定）推出未标注的参数与返回类型；泛型高阶函数（如 `tree_reduce`）需要双向推断时先推出能推出的部分，推不出时诊断要求标注。
3. **（已撤回，见 H25）对第一个参数求导**（丁车道）：`grad(f)`、`with_grad(f)` 对 `f` 的第一个参数求导，模型参数作为普通参数；现有经 `with` 参数求导的测试与示例改写（内测阶段不考虑兼容），`nn` 等库中同类写法一并改。
4. 完成后把 C2 改为新写法（`loss(m, x, y)`、`train_x[order]`、lambda 不写类型），与 H20 第 5 项合并重测。

**剩下什么**：第 1 项的**前半**由 H83 完成（`t[a:b]`、`t[:b]`、`t[a:]`、`t[:]`、步长为 1、`t[idx]`、`last(t)`：语法、类型、rank-1 的下界、格式化器、语言服务器、文档，报告 reports/H83.md）；**多维切片 `t[:, 0]`、`t[a:b, c]` 与步长不为 1 的视图**要等 H17 的数组表示，现在写它们是带位置的诊断且点名 H17（多维切片是编译期 `ConstructNotSupported`，数组的步长是运行时 `InvalidArgument`，张量的步长与 rank≥2 的切片是编译期 `ArgumentsRejected`）；槽数组（元素是对象）的切片现在复制，随 H17 改视图。第 2 项（lambda 参数类型从上下文推出）由 H92 完成（reports/H92.md：实参、`with` 实参、带标注的绑定、赋回该变量、返回位置都推出形参类型与省略的返回类型；推不出时报 `SemanticDiag::LambdaParameterWithoutType` 并点名形参；泛型高阶函数 `map`/`tree_reduce` 的推断记为待决）；第 3 项已撤回，由 H25（`16136a3a`）完成。

### H22：循环与求导的跟进（2026-10-20 指派，丁车道）

依据 `reviews/H4d-H13b.md`、设计原则 3.10、9.9。

1. **（优先）不得静默丢失梯度**：依赖被求导参数、却没有梯度通道的值（含循环中的数组状态），一律带位置拒绝。然后做**追加识别为收集输出**：循环规范化改写中，初值为空数组、下一值为"追加一个值"、循环体其他处不读的状态，改写为收集槽，`FILL` 还原用户可见的元组；自动微分与 `std::opt` 之前运行。用 `loop losses = [] for … do losses->append(l)` 的求导与分配计数用例覆盖。
2. **数组的切向量是元素切向量组成的数组**（9.9、5.4）：`tangentTypeOf(T[...])` = 元素切向量的数组，元素没有切向量时为"无"；循环捕获可求导的数组时梯度照常累加，下标读取的反向为散射加；循环处的拒绝收窄为"没有切向量的数组"。数字数组的部分随 H17 的表示落地，可以先在现有槽数组上做（结构体数组等）。
3. **空数组字面量按上下文取类型**（5.1 的同一规则）：`[]` 出现在期望 `T[]` 的位置（含递归类型的数组字段）时取该类型，不再需要 `[] as Node[]`。

### H23：路径赋值与递归结构体（2026-10-20 指派，甲车道）

依据设计原则 3.5、5.2（语言作者已定）。

1. **路径赋值**：目标可以是以 `var` 开头的路径（字段、下标、它们的任意组合），含复合赋值。**实现方法**：在建图前把它降低为整体重绑——`v[i].f = e` 降低为 `v = set(v, i, set(v[i], f, e))`（`set` 是"返回改了一处的新值"的纯算子，字段与下标各一个），之后的一切（资源、`wait`、各后端）都只看到对 `v` 的普通赋值，不需要新的节点种类。**原地**：降低出来的 `set` 链套用现有的"不共享"判据（`holdsUnsharedArray`、`inPlaceRebindingOf`），`v` 无人共享时整条链原地写，对槽数组现在就能做，紧凑数组随 H17。用分配计数断言"`var` 数组上的路径赋值不复制""`let a = v` 之后修改 `v` 时复制且 `a` 不变"。越界是故障。
2. **递归结构体**：经数组、映射、集合、`enum` 的递归允许；直接包含自身报错（指出环）。
3. **空数组字面量按上下文取类型**（与 H22 第 3 项是同一件事，二者合并做一次）。
4. 文档：语言参考 6.11（删去"目标必须是变量本身"）、3.7；删除 `AssignmentTargetNotVariable` 中已不成立的部分，`field_assigned`、`element_assigned` 两个用例改为正例。
5. 完成后 D4（MCTS）可以开工（还需 H17）。

**剩下什么**：H23 的四项都已完成。第 2 项的经数组递归（`type Node = { children: Node[] }`）在 `handover/H23b-recursive-array` 做完：声明、构造、遍历、路径赋值与移动回收都有用例；经映射/集合的递归仍按设计保留 `FeatureNotSupported`，泛型递归结构体仍未支持（均见 reports/H23b.md）。第 1、3、4 项早在 `1e3d20fa` 合入。

### H24：字面量取类型的其余位置（2026-10-20 指派，甲车道）

依据 `reviews/H15-H20-H21.md` 与设计原则 5.1：分支臂里 `return` 的期望类型改为臂的位置（种子 16 移回 progen 批次）；数组字面量的元素、结构体字面量的字段、`match` 的模式按期望类型取；无期望类型时数组内字面量取最宽的一种；整数字面量的位运算编译期精确计算；折叠表达式越界的诊断指向整条表达式；`LiteralOutOfRange` 与 `LiteralStaticCastFailed` 合并为 `LiteralNotRepresentable`（消息引用源码文本、说明原因）；语言参考醒目说明 `0.1 + 0.2` 与 `let a = 0.1; a + 0.2` 的差别。另：H21 的 `NotDifferentiable` 诊断、`nn-autograd.md` 清理、benchmarks 改写按评审执行；H20 的 `.previous` 改名与 macOS 的 `renamex_np` 按评审执行；`fs::list_dir`、`fs::remove_all`、错误种类目录并入 H19。

### H26：删除"已退役"类诊断（2026-10-20 指派，甲车道）

依据设计原则第 1 节（已修订）：旧写法删除后不留任何指向新写法的诊断、别名或说明。

1. 找出代码中所有专为旧写法设置的诊断与分支（如联合类型 `A | B`、`T ? U`、`wait`、前缀 `sync func`、形状位置的 `constas`（H17 计划中的 `ConstasRetired` 不再添加）、`@name(...)` 圆括号形式等），连同它们的消息、诊断目录条目、专门的测试用例一起删除；删除后这些写法按普通的语法错误或未知名字报告。
2. 文档中"已退役""请改用"一类的说明一并删除（语言参考、技术文档、诊断目录）；历史性的报告与评审记录不动。
3. 报告中列出删除的诊断清单。

### H27：H24–H26 检查后的小项（2026-10-20 指派，甲车道）

依据 `reviews/H24-H25-H26.md`。

1. 复合字面量在建图时的二次舍入：精确值带到确定类型的那一步，用一个会暴露二次舍入的用例锁住。
2. `if`/`match` 各臂的值取整个表达式的期望类型（5.1）。
3. VS Code 语法：装好 `editors/vscode` 的依赖，删去 `|` 作类型运算符的范围与快照中的联合类型，重新生成 `.scopes` 金标。
4. 删除 `AnyNotAllowed` 及其测试与语言参考中的替代写法表（`any` 是普通的未知名字）。
5. 语言参考 8.6 改正 `enum`；命令行未知选项报"未知选项"。

### H28：数据块——缓冲区提升为核心内存层的通用机制（2026-10-20 指派，乙车道，紧接 H17c）

依据设计原则 10.5、5.4（已修订）。

1. **位置与名字**：`camel::core::array::Buffer` 与 `bufferSpace()` 移到核心内存层，改名为数据块（`camel::core::mm::Block`、`blockSpace()`，计数器、`std::mem::buffers()` 相应改为 `std::mem::blocks()`）；数组只是它的一个使用者。不留旧名。
2. **判据改为"纯数据"**：类型系统提供一个判定（数字、布尔、复数，以及只由它们组成的结构体与元组；递归判定，结果随类型驻留缓存），取代 `arrayStorageFor` 中的"数字元素"判据。纯数据结构体的数组紧凑存储（按结构体的布局逐元素排列），`getSlot`/`setSlot` 与字段访问照常可用；DLPack 只导出数字元素的数组，结构体数组的导出留到 Arrow 一项。
3. **`Bytes` 改用数据块**：读文件、`encode`、与 Python `bytes` 的互换走零拷贝；小于阈值内联。
4. 本项不做：长字符串、内存映射、表格的列、显存——随各自的任务接入，接口按本项预留（所有者、释放回调、设备字段已有）。
5. 性能红线：三个小程序前后对比（同 H17b 的方法）。

### H29：fvm/jit 上元组数组的 `reduce` 结果错误（2026-10-20 指派，乙车道，最优先）

协调者一方在图存储工作中发现（当前 fsh-dev 与 develop 均复现）：

```camel
[(1, 30), (2, 6), (3, 20)]->reduce<(a: (int, int), b: (int, int)): (int, int) => if b.1 > a.1 then b else a, (0, 5)>
```

nvm、taskflow 得 `(1, 30)`（正确），fvm、jit 得 `(3, 20)`。`feat/examples` 的 `parallel_map.fvm/.jit` 与 exemplars 01/02/05/08 因此失败。

可疑点：H28 的元素视图——`slotShaped` 视图复用同一个线程局部单元、普通视图走 128 格环；`reduce` 的累加器 `a` 若是一个元素视图，下一轮读取 `b` 时会覆盖同一个单元，`a` 与 `b` 指向同一处（视图逃逸进了累加器）。H17d 的按存储类别特化的循环也在这条路径上。要求：找到根因并修复；累加器、返回值、任何跨元素存活的值都不得是视图（这正是 H28 评审第 1 项要求的检查应当拦住的情形——一并核对开发构建的视图逃逸检查为什么没有发现它）；用上面的程序与 `reduce`/`foldl` 的多种元组、结构体形状在四个后端上锁住。

### H33：ONNX 导出的分支调用拆分不再产生输入环（原误编为 H32，与 Debug 栈的报告重号）（2026-10-21 指派，丁车道，R2c 合入后）

协调者一方补齐了提交时的校验（边上的类型兼容、无环、冲突检测；`validate(graph, GraphState)`）。唯一产生非法图的是 ONNX 导出的分支调用拆分（`enableBranchedCallSplit`，`draft_inline.cpp` 的 `devirtualizeBranchedCallInDraft`）：数据依赖分支（如 `if s > 1 then y*2 else y`）的反向是对一个从分支 JOIN 中取出（ACCS）的闭包的间接调用；ONNX 没有闭包，拆分把它改写为每条臂一次 FUNC 调用、每个取出的值一个 JOIN，这些兄弟 JOIN 读同一个 BRCH。结果形如 `OPER x ← JOIN ← JOIN ← ACCS ← FUNC(每臂的 pullback 调用) ← 余切的 OPER 链 ← x`：喂给每臂调用的余切由读取这些调用的结果 JOIN 的节点算出——作为数据依赖图是一个环。现在以 `GraphState::NonExecutable` 临时放行，且这种会话永不安装进流水线的程序。

要求：拆分为每个取出的值生成一个 If，其各臂的调用把余切作为**在分支之外、只依赖前向 JOIN 算出的捕获**传入，使没有任何分支之外的节点在余切算出之前读梯度 JOIN——即没有输入环。四个用例：`export_train_step_branch_tensor`、`export_train_step_branch_disjoint`、`export_gated_gru_step`、`export_loop_values`。**验收**：删除 `enableBranchedCallSplit` 会话上的 `GraphState::NonExecutable` 设置后，这四个用例与全部 ONNX 计划通过。在协调者一方的 R2c（分支臂成为区域）合入后做，届时分支的表示会变。

### H36：Camel 图 API 的收尾（2026-10-21 指派，丙车道交给施工方）

协调者一方已完成 `graph` 模块 C0–C9（读图、事务、`commitChecked`、Camel 写的遍在流水线上运行、函数到函数、事实、文档；见 `docs/technical/45_graph_module.md`）。交接说明：[graph-api.md](../design/graph-store/handover/graph-api.md)（剩余工作、入口、陷阱），方案：[graph-api-plan.md](../design/graph-store/handover/graph-api-plan.md)。

1. **遍所在的模块能导入 Camel 库**（如 `array`）：交接说明给出两种做法与入口，选一种并说明理由。
2. **命令行遍参数**：`mymod::p(k=v)` 映射到函数的 `with` 参数（配置，设计原则 4.4）。
3. **事务中新建值的根规则**：编辑可以创建常量等新值（常量折叠类改写需要），草稿的静态槽在 NodeVM 安全点不得被移动或回收。
4. **区域的读取**（`region_of`、种类、父区域）与校验器的区域规则（2）（3）：在协调者一方的 R2c/R2b 合入后。
5. H19 落地后把 `constant(n, sample)` 改为 `constant::<T>(n)`。
6. 测试：每项在 `modules.graph` 计划中加用例；差分测试（Camel 遍对 C++ 遍逐字节）保持通过。

**剩下什么**：第 5 项 `constant::<T>(n)` 仍等 H19；第 1–4、6 项已合入。

### H37：库程序中模块级 `let` 的值读成 0（2026-10-21 指派，最优先）

协调者一方在规则引擎工作中发现：作为库编译的程序（`camel_pass` 运行 Camel 写的遍时所用的程序、`compileAsLibrary` 的路线）没有模块级的值——遍所在模块里写 `let two = 2`，遍中读到的是 `0`。这是**静默的错误值**，不是诊断。要求：找到根因（库程序的模块级 root 图是否被执行、模块级捕获端口如何绑定），修复使模块级 `let` 在库程序中取得正确的值；用一个 Camel 遍读模块级常量与由函数调用算出的模块级值的用例锁住；同时检查运算符定义程序（同一路线）是否受影响。详细复现见协调者一方的交接说明（`docs/design/graph-store/handover/` 中 identity 的报告，随后加入）。

### H38：规则引擎的其余部分（2026-10-21 指派，先交计划）

协调者一方已完成第 8 步的前半：`rule` 声明（解析、格式化、编译成函数）、原生模块 `rewrite`（`rewrite`、`saturate`、`extract`、`check`）、随机检验工具、`stdlib/rules.cml`（26 条精确规则，均通过检验）与 `rules::simplify`、`rules::saturate_all`；见 `docs/technical/46_rules.md`。交接说明（"HAND-OVER BRIEF" 第 A 部分）：[store-and-rules.md](../design/graph-store/handover/store-and-rules.md)；方案：[rules-plan.md](../design/graph-store/handover/rules-plan.md)。

1. **`when`**：按协调者已定的方案 (a)——在 `graph` 模块的句柄上加草稿引用，`when` 中的模式变量是指向正在改写的草稿的 `graph::Node`。
2. **`#requires<...>`**（等 H2）：需要许可的规则（`x*c1 + x*c2`、`(x*c1)*c2`、整数倍的 `x*c + x*c`），检验按许可对应的误差界（数值契约）。
3. **分析 `analysis`**（8.7）：语法、编译成函数与事实种类，`when` 中按名字读取。
4. **追平后删除 `algebra.cpp`**：在代数语料与规模程序上与 `std::opt::algebra` 结果一致后删除，`std::opt::algebra` 改为由标准规则库实现。
5. 泛型规则等 `where`（裁定 2.18）与泛型第一期，之前写单态规则。
6. 给 `rewrite` 模块新调用的核心函数补 Windows 导出项。

**剩下什么**：第 2 项 `#requires<...>`、第 3 项 `analysis`、第 4 项删除 `algebra.cpp`、第 5 项泛型规则只有计划；第 1 项 `when` 与第 6 项导出清单已完成。

### H39：图存储的技术债（2026-10-21 指派）

交接说明第 B 部分（不变式与陷阱：函数值随被调图重新定版本、闭包的广度优先顺序决定优化结果、每个修改函数都要递增 `mutations_`）：[store-and-rules.md](../design/graph-store/handover/store-and-rules.md)。

1. 没有冲突的过期提交合并进最新快照（设计稿 4.3.1），而不是另开分叉。
2. 编码了但没有绑定进任何程序的图，随 GC 释放。
3. `jitFnToGraph_` 中已释放图的条目清除。
4. 节点级的事实标记（事实按节点版本判断有效性）。
5. 草稿改为直接从借来的块读取，使打开与编码的工作量与改动成正比——先用剖析证明值得。

### H41：H5 之后四项失败（2026-10-21 指派，优先）

协调者一方在 fsh-dev `64b6df08`（只加了 Linux 链接所需的一行 `-fPIC`）上确认，下列测试在不含协调者改动的基线上同样失败，来自 H5（命名结构体改为名义类型）之后：

- `ort_loop_values`、`ort_grad_loop_values`：ONNX 导出器拒绝"把依赖模型的值 `CAST` 成名义类型"（`cast of a model-dependent value to 'Run'`）——函数返回命名结构体时，H5 加入的恒等 `CAST` 被导出器当成数据转换。按 H5 的规则，布局相同的匿名值进入命名类型是**恒等**的，导出器应把它当作无操作。这两项需要 Python 的 onnx/onnxruntime；施工方机器若没有，请装上或在报告中说明。
- `lsp.editing`、`examples.ml.export_onnx_translate`：请查明原因。
- 另：Linux 上 `libcamel_objects` 需要 `POSITION_INDEPENDENT_CODE ON` 才能链接 `libcamel.so`（导出列表改为生成之后），协调者一方已在 fsh-dev 中加上；请确认 Windows 不受影响。

### H43：区域与循环的收尾（2026-10-21 指派，丙车道交给施工方）

协调者一方已完成：分支臂成为区域（前端直接建、各后端与调试器读区域、锚点删除、嵌套排布线性）；循环成为区域并在所有后端于函数的帧内原生执行（fvm 新指令 `LBEG`/`LTST`/`LEND`，`std::cpp` 写 `for (;;)`）。交接说明：[regions-and-loops.md](../design/graph-store/handover/regions-and-loops.md)；方案：[regions-plan.md](../design/graph-store/handover/regions-plan.md)。

1. ~~前端直接建循环区域~~：已由协调者一方完成并合入（含 H22 的 `collectLoopAppends`/`inferLoopStateFromAppend` 移植到区域形式；`loop_lowering`、`:graph/original`、`native_loop` 已删除）。另：`failed_no_rule.error` 的宏诊断位置从 `grad` 调用处（15:13）移到 `f` 内部（10:12），疑与 develop 的求导活跃起点改动有关，请丁车道核对。
2. ~~JIT 编译含循环的图~~：x64 后端实现 `LBEG`/`LTST`/`LEND`（原先含循环的图解释执行，被调函数照常编译）。**已完成**（`48345984` 把三个循环事件编译成有帧代码；本分支在合并 develop 后的树上验证，并让含循环的图在默认阈值下第一次调用就编译，否则只进入一次的入口函数仍会被解释）：[H43-jit-loops.md](reports/H43-jit-loops.md)。
3. **自动微分读区域**：删除 `outlineLoopRegions`/`regionizeLoops`/`raiseBranchArms`/`lowerBranchArms` 这些过渡层与 `LoopView` 的子图模式。
4. **ONNX 导出循环**（读区域，映射到 `Loop`）。
5. 删除 NodeVM 的 `evalLoop` 回退（`0e88d09d`）：前端已直接建循环区域，确认不再有子图形式的 `LOOP` 后删除。
6. 改掉仍带"降低"字样的名字（`loops.*_lowered`、`OperatorTraits::inspectsGraphs`）。
7. **性能目标**（`docs/technical/39_loops.md`，1000 万轮两个状态的 `while`）：原为 nvm 1.96 s、fvm 1.02 s、jit 0.99 s，目标 nvm ≤ 0.7 s、fvm/jit ≤ 手写尾递归 0.49 s 的一半。已做两处：多值状态保留分量（不再每轮造元组）、`%` 成为指令 `IMOD`/`LMOD`；H43 实测 nvm 1.571 s、fvm 0.436 s、jit 0.435 s（尾递归 0.310 s），当时**未达标**：fvm/jit 的差距是一轮六条字节码的派发价格，nvm 的差距是每节点 23 ns 的执行器本身，jit 缺的是第 2 项。**JIT 那一半由 H55 补齐**：一轮从 35.6 ns 降到 7.0 ns（一千万轮 0.070 s），与同一份工作的编译尾递归持平，见 [H55.md](reports/H55.md)。**解释器那一半由 H56 补齐**：作者的口径是“解释执行的一轮不比同样工作量的手写尾递归慢”，H56 只做解释器一侧（状态按形状就地绑定并内联、不收集的一轮就地写下一状态、`profile` 开关内联读、循环事件按常量偏移取 `ExecLoop`），不改字节码与计划：fvm 一轮 28.4–30.2 ns，同批尾递归 30.0–32.1 ns，五个成对批次里一轮都不更慢，0.436 s → 0.28–0.30 s，见 [H56.md](reports/H56.md)。H43 原定的 0.245 s 仍要靠“一轮一条超级指令”（估计 3–5 ns），是下一步。
8. `feat/trans/cpp.agree_with_fvm`（slow 层）在里程碑时运行。

**剩下什么**：第 4 项 ONNX 导出循环（`modules/onnx/exporter.cpp` 仍拒绝 `LOOP`）未做；第 7 项的解释器一侧已由 H56 达到复议口径（fvm 28.4–30.2 ns 对尾递归 30.0–32.1 ns；nvm 没有目标），H43 原来的 0.245 s 需要"一轮一条超级指令"（估计 3–5 ns）或 `std::jit` 的原生降低；第 8 项在里程碑跑 slow 层时核对；第 1–3、5、6 项已完成。

### H45：`if` 臂里的循环在所有后端给出错值（2026-10-21 指派，最优先）

H43a 报告发现（协调者一方的区域改动引入）：

```camel
func maybe(flag: bool, xs: int[]): int {
    if flag then loop acc = 0 for x in xs do acc + x else -1
}
```

`maybe(true, [1, 2, 3])` 在 nvm、fvm、prl、jit 上都得 5（应为 6）；前面多几个节点时得 3；同一程序再调用两次得 `-1 -1`。值随程序中其他节点变化，像是臂区域内循环的端口或状态槽与别的节点共用了位置，或者执行计划对"臂里的循环"排布不成立（循环的条件、循环体与 `LOOP` 节点应是臂区域内连续的一段；臂的入口与末尾应包住整段循环）。

要求：
1. 先写失败的用例（这个形状、`match` 臂里的循环、臂里的嵌套循环、循环里的 `if` 再嵌循环），四个后端与 `std::cpp`。
2. 查根因：`ControlRegions`（`include/camel/runtime/control_regions.h`）的 `within`/`armOf` 对"区域套区域"是否正确；`computeExecutionOrder` 排布臂时是否把内层循环的两个区域与 `LOOP` 节点放进臂的范围；`ExecLoop` 的端口槽与臂的结果槽是否冲突；`branchArmEntries`/`Tails` 是否落在循环之内。
3. 修复后上面的用例全部给出正确值；区域校验在提交时对"臂里的循环"再加一条一致性检查（循环的两个区域的父区域是该臂）。

### H47：Tiles 现状与差距报告（2026-10-21 指派，只读）

Tiles（`modules/tiles`）从现在起由协调者设计、施工方实施。先交一份**只读**报告，写到 `reports/H47.md`，不改代码。依据：[外部性能报告](../design/gpu/perf-report-2026-10-21.md)、`modules/tiles/README.md` 与 `IMPLEMENTATION.md`、`docs/design/tiles-route`、`docs/design/op-semantics`。

1. **现状**：Tiles 现在能把什么下放到 GPU（算子清单、区域如何划分、谁决定下放——显式 `run_device`/区域标注还是自动）；编译缓存、内存池、流与事件、主机设备传输的现状；与执行计划、区域（分支与循环已是图内区域）、数据块（DLPack、设备字段）的接口现状。
2. **实测**：在本机（或有 GPU 的机器）用报告中的依赖链微基准与一个小型训练步（C2 的 MLP 即可），给出每步算子总数、其中下放到 GPU 的个数、主机设备传输次数与字节数、同步点个数、每算子的主机开销（CPU 路径与 GPU 路径分开），以及编译启动时间按阶段的拆分。
3. **差距**：对照外部报告的五个假设与五个请求，逐条写"现在如何、差多少、在哪个文件"。
4. 列出 Tiles 代码中已过时、依赖旧图形式（子图形式的分支与循环、`GCGraph*` 身份、`stableId`）或与现行设计原则冲突的部分。

### GPU 执行（H48–H50，2026-10-21 指派）

设计稿：[GPU 执行与性能](../design/gpu/README.cn.md)（已定）；外部性能报告：[perf-report](../design/gpu/perf-report-2026-10-21.md)。Tiles（`modules/tiles`）由协调者设计、施工方实施。H47（只读现状报告）可以并入 H48 的第一份报告。

**H48（G0，最优先）：先能看见。**
1. 剖析：一个流水线参数或遍（如 `std::profile`），报告每个算子的设备与耗时、每步 kernel 个数、主机设备传输的次数与字节、同步点、编译时间按阶段（解析、类型检查、`std::macro`、建图、各遍、Tiles 编译）。对"主机上对张量逐元素循环"给出提示。
2. 基准：外部报告的依赖链微基准、C2 训练步、一个小型 Transformer 训练步（含注意力、rmsnorm、Adam、EMA），进入 `test/plans/perf` 与性能基线；在有 GPU 的机器上与 PyTorch 同精度对照。
3. 用它们给出第一份数字：每步算子总数、下放到 GPU 的个数、每算子主机开销（CPU 与 GPU 路径）、编译启动的阶段拆分。

**H49（G1–G3，先交计划）**：`#gpu`/`#cpu` 标签（函数、块、模块）与 `--devices`；`#gpu` 范围里不支持的算子编译期报错并指出算子；Tiles 改读图内区域（分支与循环）；设备上的数据块与数组值（DLPack 的设备字段、引用计数、原地写），驻留由图推出、主机观察时下载；异步提交；整段编译、按形状签名缓存（内存与磁盘）；自适应显存池；删除 `@gpu`、`run`、`run_device` 与 Tiles 私有的驻留存储。

**H50（G4–G5，先交计划，H49 计划获批后）**：bf16/f16 与 Tensor Core 矩阵乘（`change_precision` 许可下）、批量矩阵乘、`take`/散射加、规范化、键式随机数、设备上的分支与循环；默认授予的 `reassociate`/`contract` 用于并行归约与 FMA（`#strict` 退回精确算法）；融合（能写成规则的写成规则）；优化器对整棵参数树的多张量更新。

验收：外部报告的扩散模型训练步、C2、小型 Transformer 在 3090 上与 PyTorch 对照；Tiles 现有的逐位与 ulp 差分测试保持。

### H51：泛型第一期——按结构展开（2026-10-21 指派，先交计划）

设计稿：[泛型按结构展开](../design/generic-structure/README.cn.md)（已定，第 6 节的 11 项与设计原则 5.5–5.10）。

1. **值位置的结构展开**：`match type T { case struct / tuple / #leaf / _ }`、`static for (n, F) in fields T => …`、`static loop`、编译期字段访问 `t.[n]`、推导式 `{ … => [n]: e }` 与 `( … => e )`、可变元数 `..ts` 与展开调用；含静态构造的实例自动内联；实例化链诊断与 `static error`；路径作为运行时字符串（由常量折叠）。
2. **用 Camel 重写**今天由核心硬编码展开的 `tree_map`、`tree_reduce`、`tree_unzip`、`tree_leaves`、`tree_paths`（`modules/tree`），输出与现在逐字节相同；删除核心里的硬编码展开。
3. **Optax 式优化器库**（`optim`：`sgd`、`adam`、`adamw`、学习率调度，纯函数 `init`/`update`）用 Camel 写在 `tree` 上；C2 与其他示例改用它，删除 `nn` 中的 C++ 优化器。
4. 类型位置的同一套写法（类型函数）与 `where` 约束属于第二期，本项只预留位置。
5. 性能：`tree_map` 展开后与现在的内联结果相同，自动微分与 Tiles 看到的图不变。