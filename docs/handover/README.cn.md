# 交接任务书

> **2026-10-19：设计原则已按主题重编**（15 节，只写当前有效的规则）。旧的编号（如 2.13、2.20）与原文见 [裁定记录](../design-principles-log.cn.md) 开头的对照表；本任务书中的引用已改为新编号。

本文件写给接手开发的 AI。读完就开工，不必再问"要做什么"。每个任务完成后把结论写进约定的位置（第 3 节），由协调者（与语言作者讨论设计的 AI）检查代码；检查意见也写在约定的位置。

**判断标准不是"能运行、测试通过"，而是"符合 Camel 的长期设计"。** 能跑但不优雅、绕开通用机制、按名字写特例的实现会被退回。第 2 节列出了退回最多的几类问题，提交前逐条自查。

更新：2026-10-19。

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
|---|---|---|---|
| H75 | **H71 扫描的 16 个 GPU/Tiles 红灯**：逐个复现并按实际失败分组，判定回归／过期期望／未实现，修小回归、其余写进计划与待决 | 无 | **已完成**（报告 reports/H75.md；8 个真回归在 `modules/tiles/src/compiler.cpp`：结构体 `CAST`（`44137514` 名义结构体后匿名值落到命名位置会留下 `CAST`，Tiles 只认张量目标）与多状态 LOOP 把 `FILL` 模板常量当捕获（`60ab7619`/`c912ea15`，`externalInputs(true)`→`false`）；`core_view_maps` 改测试形态（`grad` 绑到 wrapper 外）、`static_regions`/`multi_state_loop_boundaries` 改期望并注计划、`resident_fallback_fault` 的计划期望与 `docs/cli.md` 冲突（运行期故障在标准输出，故障未被吞）已改计划、`extrema_canonical_bits` 是负载误红（单独 9 s）；全量 GPU/Tiles 135/135） |
| H73 | **真实缺陷**：紧凑纯数据聚合数组（`items(map)`、`[...] as (int, int)[]` 等）的元素按"视图"读（`rtdata/view.h`：线程局部单元里的 Struct/Tuple 头，指向数组数据块，**不是** GC 堆对象），高阶算子把它原样绑进回调帧的槽；帧是每个安全点的 GC 根，收集器/验证器于是把这个 TLS 地址当托管对象追。H71 报的 `gc.collfuzz` 18/72 失败（全 `std::nvm`）由此而来，非移动收集器同样失败；fvm/jit 在回调里有调用时也失败，prl 在元素调用期间不轮询安全点所以幸免。修复：`Frame::set` 在"槽类型是 Struct/Tuple 且值带视图标志"时先物化——与 `Tuple::set`/`Struct::set`/`Array::set`/H29 的 `materializeCallResult` 同一条"存储即边界"约定；验证器与收集器未改 | 无 | **已完成**（报告 reports/H73.md；新增 `test/cases/feat/mm/gc/gc_view_frame_store.cml` 与 `feat/mm/gc.plan.toml` 的 nvm/fvm/jit 三条，修复前三条都失败、修复后各约 20–40 ms；`feat/mm` 166/166、`gc.collfuzz` 72/72、`gc.plan.toml` 43/43、`smoke`+`feat/linear/loops`+`feat/compile` 1052/1052、整个快层 4816（4803 通过 / 13 跳过 / 0 失败）。poison 的 `rules.*` 22/47 仍失败，与本文不同因（草稿会话的静态槽不在根集里，H74）） |
| H70 | H44c 待决第 2 项：补一个"特化跨轮仍可达"的用例，锁住程序级特化表的用处 | 无 | **已完成**（报告 reports/H70.md；`specialization_survives_the_round.copy_is_reused_in_a_later_round`：程序级表 7 条 / 12 图 174 节点，7 份 `walk` 副本全部从根可达且其中一份被后面的轮次原样取回；会话级表 12 份副本 / 17 图 254 节点，钉住形态 10 条；`feat/opt/generic` 87/87、`feat/parse/frontend` 4/4） |
| H72 | **潜在缺陷**：Tiles 的 `cast_like` 归一化只比较形状，元素类型不同时也直接返回输入（`compiler.cpp:779-785`）；`Tensor<f64>` 在类型层被归一化成 f32、Tiles 只接受 f32 张量，所以今天这条短路是空真地正确，bool/int64 张量存储一到来（H49 第 5 步）就是错值。按 `notes/core-semantics-map.md` 的既定契约改为“形状与元素类型都对上才别名，不同则 `UnsupportedComputation` 留在 CPU 由内核转换”；同类型的 `cast` 一并按“转成自身类型什么也不做”别名 | 无 | **已完成**（报告 reports/H72.md；新增 `cast_like_aliases_the_same_element_type`、`cast_between_element_types_stays_on_cpu` 与 plan_check 的 `cast_like` 计划边界检查；`feat/modules/tiles` 61/61；清掉 H49 第 5 步的一个前提） |
| H74 | **rules 的改写跨 safepoint 留下裸草稿指针**（H80 投毒扫描的 12 项 `poison:rules.*`；当前树因 H65 的 typed 用例是 22 项）：`RuntimeGraphDraftSession` 逐图建草稿，`when` 条件是 Camel 代码（safepoint），但 `DraftWritings` 只为正在改写的那份草稿注册根，其余草稿的静态槽在 commit 前失效 | 无 | **已完成**（报告 reports/H74.md；新增 `RuntimeGraphDraftSession::RootScope`，rewrite/saturate 两个 kernel 从第一张图到 commit 一直持有；新增 `gc.rewrite_condition_keeps_drafts_rooted`、`gc.saturation_condition_keeps_drafts_rooted`；投毒 rules 47/47、`feat/modules/rules` 54/54、`feat/opt` 685/685、`smoke` 10/10、`feat/mm/gc` 42/42；`gc.collfuzz` 归 H73、`poison:shapes.*` 是扫描 runner 伪失败，均未动） |
| H63 | **抽取的保留边界与作者裁定不符**：`DraftWritings::extract` 按"不参与同余"预选，连带保留纯节点（shape 3、GC 追踪常量等），饱和造出的死写法留在抽取结果里；按裁定改为"有副作用／承载顺序／被写入的变量"保留，纯节点只按可达性 | 无 | **已完成**（报告 reports/H63.md；回归 `rules.extract_keeps_no_unreachable_pure_node`：4 轮饱和后 33 节点、15 个死字符串常量，修复后抽取回 3 节点、0 个；`mm/writings`、`opt/algebra`、`rules`、`loops`、`autodiff`（去 PyTorch 对照）、`smoke`、`feat/opt` 共 1020/1020） |
| H62 | 无 JIT 构建（CI 的 `linux-x64-nojit` 腿）在 `fastvm.cpp` 读 `jitConfig_` 而该成员只在 `#if ENABLE_FASTVM_JIT` 内声明——构建红，发布 CI 会跟着红 | 无 | **已完成**（各处按构建分开，无 JIT 时剖析后端名就是 `std::fvm`；无 JIT 全量重建后 fast 层 4769 / 4757 pass / 0 fail / 12 skip；报告 reports/H62.md） |
| H61 | 规则引擎按作者裁定的三项：`analysis` 接收多个模式变量；抽取的保留边界写进设计稿；`algebra.cpp` 对等的次序（先元素类型定律、再张量定律；`safe_fp` 就是 `#strict`） | 无 | **部分完成**（第 1、2 项完成；第 3 项只记后果、未实施；报告 reports/H61.md。§3.2 记下的抽取缺陷由 H63 修复；措辞张力按作者更正解决——裁定括注"按依赖求值只适用于纯计算"是协调者的误写、已删除，设计原则 3.1 不变，见 reports/H63.md） |
| H58 | **过期测试**：DAP 尾递归步进的 6 个断言仍按 `fda0d21f` 之前的“臂是独立影子帧”写；按 12.1 的区域模型更正断言，并补尾调用点的 `next`/`stepOut` | 无 | **已完成**（报告 reports/H58.md；无源码改动，不是缺陷修复） |
| H52 | **测试基础设施缺陷**：运行器忽略 `expect_stdout_exact` 等未知字段，旧计划没有真正比较输出——未知字段报错，修复受影响的计划，列出暴露的真实失败 | 无 | **已完成**（运行器已严格、`expect_stdout_exact`/`expect_stdout_not_contains` 真正比较；暴露的 9 条真实失败全部已修，8 条循环梯度与第 9 条 `extract` 死写法都由 H59 定位并修好；报告 reports/H52.md） |
| H94 | H76 的六条裁定：`.cml` 与 `.cmo` 是同一个模块（C++ 导出的类型/常量在声明模块里可见，`export let`/`export type` 进同一实体）；泛型声明之间按固定签名一样检查重叠、歧义在声明处报错；无 `export` 的无体函数是模块私有的原生算子；入口模块的声明用 `module` 名作 URI 前缀；一个模块只能声明自己的算子、`#op` 只能指本模块实现的算子 | 无 | **排队中**（迁移 `tensor` 的前提是第 1 条） |
| H93 | 删除 `kRetiredEnvironmentVariables` 表、退役诊断与其用例（违反"不加指向新名的诊断"） | 无 | **已完成**（报告 reports/H93.md；`tools/camel-cli/config.cpp` 的表、`rejectRetiredEnvironmentVariables` 与 `parseArgs` 里的调用，`smoke` 的 `retired_environment_variable.diagnosed`，`docs/cli.md`/`cli.cn.md` 的退役诊断段都删了；这条消息本来就不是诊断目录里的码（`--check` 197 条、`--verify-doc` 一致）。`git grep` 九个名字在 `src`/`include`/`modules`/`tools` 无输出；旧变量现在不被读取，九个一起设也照常运行（探针用例通过）；`smoke` 10/10、`feat/mm` 169/169、`feat/compile` 958/958、`feat/tools` 19 通过 10 跳过、`feat/docs_links` 1/1，另 `feat/trans/effect_trace` 4/4、`feat/consistency/limits` 110/110。编号更正：作者记为 H83，但 H83 已被下标与切片占用并合入，此小项改记 H93） |
| H82 | Debug 构建在安全点检查重写会话打开的草稿是否都已登记为根（H74 第 9 节的强制机制） | 无 | — |
| H98 | 草稿（**未实施**）：`var`/`let` 声明不进入同步链，声明与它首次使用之间的语句因此与声明里的构造无序 | 无 | **草稿，待作者裁定**（H95 的测量：`var v = [...]` 的构造节点不进同步链（`lastSyncedNode_` 只由读、调用、分支、循环设置），而 `let s0 = std::mem::blocks()` 这类调用节点在图上没有任何输入，于是“先构造还是先测量”只由 `computeExecutionOrder` 从出口出发的 DFS 后序决定；字面量的构造除了那一次分配没有可观察效应，所以图不违反 3.3/3.4，但每个分配窗口都因此脆弱，程序读者也看不出这种无序。若要让声明进链，要改的是：声明构造完成后把 `lastSyncedNode_` 指向构造节点，并给非调用的初值补一条来自前一条同步语句的控制边；代价是所有“声明后接语句”的程序的 `std::jgir` 都会换边或多边，这是语义改动，应单独裁定并单独跑全套，不是为了让某个用例通过。见 reports/H95.md 第 6 节） |
| H81 | 核心的 `CAMEL_*` 环境变量改为命令行选项、遍参数或 `std::profile`（见 reviews/H24-H25-H26.md 末尾） | 无 | **已完成**（报告 reports/H81.md；`CAMEL_GC`→`--gc`、`CAMEL_BLOCK_LIMIT_MB`→`--block-limit`、`CAMEL_GRAPH_GC_MB`→`--graph-gc-mb`、`CAMEL_GC_STATS`/`CAMEL_TYPE_STATS`/`CAMEL_EFFECT_TRACE`→`std::profile(...)` 参数、`CAMEL_NATIVE_STACK_GUARD`→`--native-stack-guard`、`CAMEL_PROFILE_MODE`→`--profile-mode`、`CAMEL_ENABLE_TEST_PASSES`→在流水线里写测试遍名；旧名字一律给诊断并退出 1，不再静默生效；`src`/`include`/`modules` 里已无这些 `getenv`。smoke 11/11、feat/compile 937/937、feat/mm 166/166、feat/trans/effect_trace 4/4、docs_links 1/1；feat/tools 只余 `develop` 上既有的 `grammar.tables`（`static` 关键字未进 TextMate 语法，已用基线树复现）。`CAMEL_TILES_DEVICE` 归 H49 的 `--devices`；`CAMEL_TILES_NVRTC` 保留为环境变量。H93 已删掉这张表与"旧名字给诊断"的拒绝：旧名字不再被读取，设置它没有任何效果，见 reports/H93.md） |
| H71 | 结构体循环状态：字段作分量（数组字段原地增长）、自动微分穿过（修段错误）、ONNX 携带结构体展开（见 reviews/H24-H25-H26.md 末尾） | 无 | **已完成**（三项一项一个提交，报告 reports/H71.md：`7eff3f329` 字段作分量——200 轮里结构体数组字段 199 → 1 块、值不变、四个后端与 `std::opt std::jit` 一致；`fe59f333a` 自动微分穿过结构体状态——切线同形、无切线字段丢弃，`loop_grad_struct_state` 与元组参照同为 24.0、五个后端一致；`6336d5d96` ONNX 把携带的结构体/元组按叶子展开进 `Loop`、两端重组，ONNX Runtime 与 NumPy/原生最差 1.19e-07。计划 loops 123、loops_in_arms 23、autodiff 161、onnx 127、smoke 10、feat/linear 712、parse 快照 4 全绿；待决问题见报告第 5 节。**注意编号**：H69 之前把慢层扫描临时记为 H71，已改名为 H80，reports/H80.md） |
| H54 | CPU 每算子开销：小矩阵不进线程池、首次调用的 6 ms、分派成本（见 reviews/H24-H25-H26.md 末尾） | 无 | **已完成**（报告 reports/H54.md：首次 6 ms = 池构造 31 条线程，小算子首调 1.58 ms → 21 µs；惊群唤醒改为只点醒所需 worker，3 块区域 950 → 82 µs；阈值统一为 `kMinWorkPerWorkerNs = 40 µs` 一个常数；mlp_step 93.95 → 48.14 ms、transformer_step 6.63 → 5.83 ms、链 9.31 → 8.84 ms，链已贴地板。**后续**：优化器那一步紧接着由 H64 做成一次对整张量的遍历——每参数 11 → 1.56 ns，mlp_step 48.14 → 31.38 ms，阈值在 3 块区域上的边界见 reports/H64.md 3.3） |
| H51 | 泛型第一期：按结构展开（`match type`、`static for`、`static loop`、`t.[n]`、可变元数），`tree_map` 等改用 Camel 写 | 无 | **进行中**（计划已批 reviews/H51-plan.md，按 15 步实施；步骤 1–2 `match type` + 类型分支 + 实例返回类型推断已入，报告 reports/H51a.md；步骤 3–4 `fields T` + `static for` + `t.[n]` 已入，报告 reports/H51b.md；步骤 4 的结构体/元组推导式 `{ static for … }` / `( static for … )` 已入（含每个父一次 `ACCS` 的复用、空形状、`0x17A`），报告 reports/H79.md，验收 957/957；H79 的两个待决问题（`fields` / `match type` 之后的类型位置，`fields ()` 因此可写；推导式字段类型取自值）已由 H90 裁定并落地，报告 reports/H90.md；下一步是计划步骤 5 `static loop`。**写 `tree_*.cml` 前必读**：H19 计划 §1.6 更正了本计划 §1.6 的优先级——`Context::getModulePathAndKind`（`context.cpp:547`）在同一目录里先找 `xx.cmo` 再看 `xx.*`，所以与 C++ 模块同名的 `.cml` 是静默死代码而不是遮蔽它；把 `tree_*` 改成 Camel 写的那一步必须**同一个提交**里把 C++ 的 `tree` 模块改名为 `ckpt`，否则新文件永远不生效） |
| H50 | GPU G4–G5：支持面（bf16/Tensor Core 等）、使用已授予的许可、融合、优化器多张量融合（先交计划） | H49 计划获批 | **未开始** |
| H49 | GPU G1–G3：放置标签、设备上的数组值、驻留与异步、整段编译与缓存（先交计划） | H48 的基准可用 | **部分完成**（H49a：区域/树传输合批，小往返 97.6→56.3 µs、同步点 5→2；报告 reports/H49a.md；动态形状、回传图类型、无设备时不重试 `cuInit` 按 author 裁定仍未做） |
| H47 | Tiles 现状与差距报告（只读，可并入 H48 的第一份报告） | 无 | **已完成**（报告 reports/H47.md；H49a 更正了它对 `run` 归因：4.9 ms 中约 3.2 ms 是 `tensor:sum` 串行归约） |
| H48 | GPU G0：剖析工具与 GPU 基准 | 无 | **已完成**（`std::profile` 与训练步基准；报告 reports/H48.md；今日没有算子被放到设备上） |
| H59 | **严格运行器暴露的真实失败**：H43b 区域化 autodiff 让复合循环状态的梯度在投影处静默丢失（8 条，首次坏提交 `914c0eda`）；`saturate_all`/`extract` 保留饱和造出的死写法（第 9 条） | 无 | **已完成**（报告 reports/H59.md；新增回归用例 `loop_grad_composite_read`） |
| H1 | `enum` | 无 | **已完成**（检查意见 reviews/H1.md） |
| H8 | 基础与清理工作 | 无 | **已完成**（第 1、2、4–7 项已合入 `927cdcb0`；第 3 项在 `handover/H68-operid`：`12cbd9eb`、`bc2cf5ea`、`3a2669f9`；报告 reports/H8.md、reports/H68.md） |
| H14 | 反引号字符串插值 | 无 | **已完成**（`2f4e413b`；报告 reports/H14.md） |
| H15 | 复数与字面量按上下文取类型 | 无 | **已完成**（字面量 `fa67a454`；复数 `92ea1d60` 与 H15b–H15d；报告 reports/H15.md） |
| H16 | 一批小项（Json、管道实参、enum 作键、调试构建、快照核对等） | 无 | **已完成**（第 1–5 项 `e6be4d68`、`8447cc9a` 与 H16b；第 6、7 项在 `handover/H66-h16-rest`；报告 reports/H16.md、reports/H16b.md、reports/H66.md） |
| H17 | 原生多维数组与缓冲区（先交计划） | H16 | **部分完成**（第 1 项基本落地、第 3 项部分；第 2、4 项未开始；报告 reports/H17.md、reports/H17b.md） |
| H18 | 图存储第 3 步：索引与增量提交 | — | **已作废**（收回，由协调者一方与核心重构一起实施，2026-10-20） |
| H19 | 标准库重新规划的落地（先交计划） | H16 | **步骤 0–1 已入**（`handover/H76-stdlib-step1`，报告 reports/H76.md）：约定页 `docs/stdlib-conventions.cn.md`；机制与 `bytes` 试点（C++ 不再写签名，`stdlib/bytes.cml` 是唯一来源）。按第 0–11 步实施。**第 1 步的写法以批复为准，不是计划里的 `native` 写法**：按设计原则第 4 节，原生实现是模块里**无函数体**的 `export func name(...): T uses ...`，**不加任何 `native` 修饰**；它按「模块名:函数名」（`bytes:len`）绑定 C++ 算子，源码里不写 URI；约定外的名字才用标签（`#op<'bytes:raw_len'>`）作例外。同目录与 `.cmo` 同名的 `.cml` 是它的声明模块，声明是类型的唯一来源；泛型无体声明按类型实例化，图上是 OPER 不是 CALL |
| H21 | 下标与切片、lambda 参数推断、对第一个参数求导 | H15（下标部分与 H17 协调） | **部分完成**（第 1 项前半由 H83 完成：语法、类型、rank-1 的切片与 `t[idx]`、`last`、格式化器、语言服务器、文档；多维切片与步长不为 1 的视图等 H17。第 2 项未开始；第 3 项已撤回并按 H25 完成 `16136a3a`；报告 reports/H21.md、reports/H83.md） |
| H25 | 撤回 H21 第 3 项：对第一个 `with` 参数求导 | 无 | **已完成**（`16136a3a`；报告 reports/H21.md 的 H25 一节） |
| H17c | 追查小程序变慢（红线）；循环状态原地写推广到多状态 | 无 | **已完成**（`adb7f914`；报告 reports/H17c.md） |
| H17d | 查清 fvm/jit 上约 5% 的残留（按存储类别特化热循环）；H17c 评审第 1、2 项 | 无 | **已完成**（`cba1d32b`；报告 reports/H17d.md） |
| H28 | 数据块：缓冲区提升为通用机制 | H17d | **已完成**（检查意见 reviews/H17-H22.md） |
| H29 | fvm/jit 上元组数组的 `reduce` 结果错误 | 无 | **已完成**（检查意见 reviews/H17-H22.md） |
| H15g | `i32`/`f32` 入口参数按声明类型读取；`let x: int = '42'` 的诊断改为类型不符 | 无 | **已完成**（`04bf4b6b`；报告 reports/H15g.md） |
| H15h | 结构体字段的文本诊断定位、转换为自身类型通过、`string = 42` 的类型不符诊断、入口与源码同一数字文法 | 无 | **已完成**（`4bb60b1a`；报告 reports/H15h.md） |
| H5b | 在声明类型的模块里找算子（设计原则 5.7，按 H5 报告第 5 节的计划） | 无 | **已完成**（`6e54035c`；报告 reports/H5b.md） |
| H15i | 下溢一致、`inf`/`nan` 数据文本、`09` 与 `077` 的词法、元组字面量分支、删除不可达诊断 | 无 | **已完成**（`584ef85c`；报告 reports/H15i.md） |
| H45 | `if` 臂里的循环在所有后端给出错值 | 无 | **已完成** |
| H44c | H44b 的两个待决问题：缓存不使图存活（Q2）；内联历史（Q1） | 无 | **已完成**（第 1、2 项已入；H44 第 2 项仍未落地，证据见 reports/H44c.md；待决第 2 项"特化跨轮仍可达"的用例由 H70 完成，见 reports/H70.md） |
| H44b | 特化缓存放在程序一级、跨会话保留；之后落地结构体 CAST 折叠并删除导出绕行 | 无 | **部分完成**（第 1 项已入，H44c 按裁决改为“缓存不拥有图”；第 2 项未落地；`unended_unrolling` 的导出拒绝 96 s → 0.015 s；报告 reports/H44b.md、reports/H44c.md） |
| H46 | `std::cpp` 生成代码中 `rt::Site` 预渲染的递归深度消息损坏 | 无 | **已完成**（`60d92431`；报告 reports/H46.md） |
| H44 | ONNX 重名输出、折叠常量的 Safe 结构体 CAST（不依赖它展开）、结构体循环状态 | 无 | **部分完成**（第 1 项已入 `ce16c788`；第 2 项已落地（H60：折叠把函数返回的值当作可替换锚点，`constantOf` 的 CAST 穿透已删，导出模型不变；报告 reports/H60.md）；第 3 项运行后端已完成：结构体状态在 nvm/fvm/jit/`std::opt std::jit` 一致，`loops.struct_state` 锁定；H69 补上设计原则 5.2 的"无名结构体按名字进入命名结构体"（循环体与分支臂），autodiff 穿过结构体状态仍崩溃、ONNX 的 carried 结构体展开仍未做，见 reports/H69.md 待决问题；报告 reports/H44.md、reports/H69.md） |
| H40b | 块压力请求全量收集、重写 `gc_pacing`、回归用例、更正 C2 叙述 | 无 | **已完成**（`d1a3f40d`；报告 reports/H40b.md） |
| H40c | 块压力按块身份只计一次；晋升后接管块时登记（H40b 评审第 1、2 项） | 无 | **已完成**（`3c1a1802`；报告 reports/H40c.md） |
| H55 | 循环事件在能原生降低的形状上降为原生代码（测试是比较加分支、回边是直接跳转、不保留轮计数），其余形状明确说明为什么仍走 helper | 无 | **已完成**（`d66e3238`；报告 reports/H55.md） |
| H56 | `std::fvm` 的循环一轮：就地绑定两个状态的形状、内联事件派发、轮数读写，目标是不慢于同工作量的手写尾递归 | 无 | **已完成**（`8008eaba`；一轮 44.92 → 28.4–30.2 ns，同批尾递归 30.0–32.1，报告 reports/H56.md） |
| H43 | 区域与循环的收尾（前端直接建循环区域、删除降低机制、JIT 的循环指令、自动微分读区域、ONNX 导出循环） | 无 | **部分完成**（第 1–3、5、6 项已完成 `48345984` 等；第 7 项已达标：`std::fvm` 一轮 28.4–30.2 ns（同批手写尾递归 28.4–32.1）、`std::jit` 一轮 7.0 ns（0.070 s / 一千万轮，与编译尾递归持平）；第 4 项未做；报告 reports/H43a.md、reports/H43b.md、reports/H43-perf.md、reports/H43-jit-loops.md、reports/H55.md、reports/H56.md） |
| H42 | 模块导出表改为每个程序一份，不再修改进程级的共享表（`clearExportedNamespaces`） | 无 | **已完成**（`a7d621ee`；报告 reports/H42.md） |
| H41 | **缺陷**：`ort_loop_values`、`ort_grad_loop_values`、`lsp.editing`、`examples.ml.export_onnx_translate` 在 fsh-dev 上失败（H5 名义结构体之后） | 无 | **已完成**（`1f53d368`；报告 reports/H41.md） |
| H40 | C2 峰值内存因每步 `take` 升高 150 MB：查清数据块回收是否及时触发并修复 | 无 | **已完成**（`90163c82`；报告 reports/H40-memory.md；后续 H40b、H40c） |
| H38 | 规则引擎的其余部分（`when`、`#requires`、分析、替换 `algebra.cpp`；先交计划） | H2（`#requires` 部分） | **部分完成**（第 1、2、3、6 项已完成：`when` `50954d05`、误差界与许可门控、`analysis` v1、`extract` 不再保留死写法的修复；第 4 项 `algebra.cpp` 因规则库未达等价而未删。H65 已补 int32/float32 元素定律与 f64 系数守卫（规则库 64 条，元素语料与 `std::opt::algebra` 等算符多重集），张量定律与 `safe_fp` 策略接口仍未做，故 `algebra.cpp` 保留；第 5 项等 `where`；报告 reports/H38.md、reports/H38b.md、reports/H65.md） |
| H39 | 图存储的技术债（过期提交合并进最新快照、未绑定的编码图、`jitFnToGraph_`、节点级事实标记、草稿改为块存储） | 无 | **已完成**（复核报告 reports/H39.md；第 5 项按剖析结论不做） |
| H37 | **缺陷（最优先）**：库程序（Camel 写的遍所在的模块）中模块级 `let` 的值读成 0 | 无 | **已完成**（修复 `5c40c184`；检查意见 reviews/H17-H22.md；报告 reports/H37.md） |
| H36 | Camel 图 API 的收尾（导入 Camel 库、遍参数、新值的根规则、区域读取等） | 无 | **部分完成**（第 1–4、6 项已合入 `134c522f`、`d33376fe`；第 5 项等 H19；报告 reports/H36.md、reports/H36b.md） |
| H35 | Windows 构建核对 `graph` 模块 C7–C9 新增的导出项（手写的修饰名，未在 Windows 上链接过） | 无 | **已完成**（`445ab37d`；报告 reports/H35.md） |
| H33 | ONNX 导出的分支调用拆分不再产生输入环 | 协调者一方的 R2c（前端直接产生分支区域）合入后 | **已完成**（`db7d9345`；报告 reports/H33.md） |
| H31 | 诊断并修复 Debug 层的 `Accessing uninitialized slot`（经 `materializeCallResult`，9 项） | 无 | **已完成**（`22ba6649`；报告 reports/H31.md） |
| H30 | Debug 构建能链接，测试运行器增加 Debug 层，视图逃逸断言真正运行 | 无 | **已完成**（`2ec50cc4`；报告 reports/H30.md） |
| H15d | 带符号 NaN 在格式说明下打印为 `nan`；复数入口参数 | 无 | **已完成**（`3b34ed35`；报告 reports/H15d.md） |
| H15c | 复数收尾：格式说明作用于两部分、数值契约条目、`^`、`std::cpp` 降低（或先定位拒绝）、DLPack 往返测试 | 无 | **已完成**（报告 reports/H15c.md） |
| H15b | 复数：`c64`、`c128`、虚数字面量；数据块布局与 `slotShaped` 判定；H28 评审第 1、2 项 | 无 | **已完成**（`92ea1d60`、`52c084eb`；报告 reports/H15b.md） |
| H27e | 字面量进入后多个候选：普通调用与部分应用都报歧义（去掉声明顺序兜底） | 无 | **已完成**（`6a50167d`；报告 reports/H27e.md） |
| H27c | 实参元素的诊断位置；部分应用的上下文实参；管道额外实参的缺陷 | 无 | **已完成**（`d2633786`；报告 reports/H27c.md） |
| H27b | 复合字面量作实参进入形参类型；编辑器语法补 `constas` | `builder_operators.cpp` 的在改项合入后 | **已完成**（`cc87c464`；报告 reports/H27b.md） |
| H27 | H24–H26 检查后的小项 | 无 | **已完成**（`2df02675`；报告 reports/H27.md） |
| H26 | 删除"已退役"类诊断 | 无 | **已完成**（`51c69903`；报告 reports/H26.md） |
| H24 | 字面量取类型的其余位置 | 无 | **已完成**（`aa71a2c6`；报告 reports/H24.md） |
| H23 | 路径赋值与递归结构体 | 无（原地部分随 H17） | **已完成**（第 1–4 项；第 2 项的经数组递归在 `handover/H23b-recursive-array`；报告 reports/H23b.md） |
| H22 | 循环与求导的跟进 | 无 | **已完成**（`292c0479`、`d50794d5`；报告 reports/H22.md） |
| H20 | H13、H14 检查后的小项 | 无 | **已完成**（第 2–5 项已入 `bdf40b4e`、`3750aefc`、`2398e598`；第 1 项在 `handover/H78-interpolation-tidy`；报告 reports/H20.md、reports/H78.md） |
| H9 | 缺陷修复与平台健壮性 | 无 | **已完成**（第 1–4 项已合入；报告 reports/H9.md） |
| H10 | 测试提速 | H9 第 4 项 | **已完成**（`68933cc2`、`318c4d65`；fast 层的 2 分钟目标未在空闲 8 核机验证；报告 reports/H10.md） |
| H11 | 健壮性与小缺陷 | 无 | **已完成**（第 1–4 项已合入；报告 reports/H11.md） |
| H4 | 已裁定未开工的小项 | 各小项另注 | **已完成**（H4a `37e447a6`、H4b `0581d473`、H4c/H4f `e6d20306`、H4d `45bbdc14`+`638b6893`、H4e `1ad52472`；报告 reports/H4.md） |
| H6 | 地基遗留 | 无 | **部分完成**（第 3、4、6、7 项已完成 `e954fe90`；第 5 项由 H80 完成，见 reports/H80.md；第 2 项由 H85 与 H95 完成（traits 与 `#independent` 已做；控制边约简已收到 `std::opt::ctrl`，H85 的“多一次复制”归因被 H95 的测量推翻，见 reports/H95.md），见 reports/H85.md；第 1 项未做；报告 reports/H6.md） |
| H12 | 拆分建图器（纯搬移） | H11 之后 | **已完成**（10 个纯搬移提交；报告 reports/H12.md） |
| H2 | 标签机制与数值许可改为标签 | 无 | **部分完成**（第 1–5、7、8 项完成：第 1–3、7、8 项在 `856af859`，第 4 项 `7b4147c1`（许可是标签、默认按 7.3、`#strict` 经 `effectivePermissions` 清空全部许可、删除 `@numeric(...)`），第 5 项在本分支（`@vjp` 改为 `#vjp<rule>` 声明标签、规则是图引用 `kGraphRefRule`、删除宏求值与 `attachRule`）；第 6 项 `#macro` 未做；报告 reports/H77.md） |
| H7 | 资源与副作用 | H2 | **未开始** |
| H5 | 命名类型 | H1 | **已完成**（`afe8ab45` 检查通过；H5b `6e54035c`、H5c `ebb57244`、H5d `2e6dcb22`；报告 reports/H5.md） |
| H13 | 官方示例第一批 | 无 | **部分完成**（C1、C2、C6、D5 已入 `40f46bda`、`707d8a12`；D4 未做；报告 reports/H13.md） |
| H3 | 依赖节点属性的后续 | 图存储第二版第 5 步 | **未开始** |
| H80 | 慢层全套扫描（原按 H71 记，作者把 H71 分给结构体循环状态后改号） | H6 第 5 项 | **已完成**（Linux 主机 fast/slow/perf/gpu/GC/poison 全层：fast 4788 项 0 失败、基线 19 项全转绿；新增 GC/poison/tiles 三簇转交 H73/H74/H75；报告 reports/H80.md，tiles 逐条清单 reports/H80-gpu-tiles-failures.txt） |

**按队列顺序连续做下去**：一项完成、报告写好、合入 `develop` 后，接着做下一项，不必等检查意见；检查意见到了再回头处理。遇到需要设计判断的地方，按第 4 节写进报告的"待决问题"，跳过被挡住的部分继续做后面的。H2、H7 是语言表面的较大改动，规格已写全，但涉及的判断较多，宁可多问。

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
6. **`macro` 改为标准标签 `#macro`**（不用导入）：`std::macro` 读标签，含义按设计原则第 8 节不变；求值失败按运行时故障处理，与任何遍运行中的故障相同；删除函数类型中的 `Modifier::Macro`。删除关键字 `inner`、`outer`、`atomic`、`shared`（标准库签名中的 `inner func` 改为无函数体的声明）。
8. **标签可声明读取它的遍**：`tag 名 ... read_by 遍名`（写法可调整，报告中说明）；流水线中没有该遍时给出"标签没有效果"的警告。
7. 许可、宏、自定义导数的原有测试改为新写法后全部通过；语言参考、标准库参考、诊断目录同步更新。

**剩下什么**：只剩第 6 项（`#macro` 尚未成为标准标签）；第 4 项由 `7b4147c1` 完成，第 5 项由本分支后续提交完成（报告 reports/H77.md）；第 1–3、7、8 项早已完成（`856af859`）。

### H3：依赖节点属性的后续

**前提**：地基"装饰器通用机制"已合并。

**H3a 优化不改变图上的算子身份**（设计原则 9.6 第五条；审查 `modules.cn.md` 第 4 条）。现在 `tensor::fuse` 会把 `matmul` 换成 `matmul_blas`，把 `matmul` 加偏置、激活换成 `matmul_add`、`matmul_add_relu` 等新算子，这些新算子没有语义声明与定义图。改为：
- 图上保留原算子（`matmul`、`add`、`relu`……），融合与库实现的选择记在节点属性上（键由 `tensor` 模块注册，如"这几个节点作为一组用某实现执行"）；执行时按属性选内核。
- 选择规则读核心的统一判定：7.3（CPU 上按库实现的 f32 矩阵乘默认可用，条件是结果可复现；安全模式下用规范算法；安全模式由遍参数给出——遍参数是地基工作的一部分，用它的接口）。现在 `matmul_blas` 要求 `reassociate` 与 `change_precision` 两项许可，按 7.3 改为默认可用。
- 删除 `matmul_blas`、`matmul_add` 等作为图上算子的注册。ONNX 导出与 Tiles 看到的是原算子。
- 测试：融合前后结果在安全模式下逐位相同；默认模式下两次运行逐位相同。

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
5. **未跑完全套测试**：三条地基（遍参数、效应统一、前端清理）合并时只跑了相关计划与冒烟测试。先跑一次全套，把与第 1.2 节基线相比新增的失败逐个修掉，写进报告。**这一项最先做。**（H80 在 Linux 主机上跑了 fast/slow/perf/gpu/GC/poison 全层：fast 层 0 失败、基线 19 项全转绿；新增的 GC、poison、tiles 三簇已转交 H73/H74/H75，见 `reports/H80.md`。）
6. **`#` 不再是注释**（4.1）：词法只保留首行 `#!`；仓库中所有 `#` 注释改为 `//`（`.cml` 文件、测试、文档示例、编辑器语法文件）；与 H2 同时合并。
7. **撤回默认流水线登记**（设计原则 9.2 已改）：删除 `registerDefaultPipelinePass`、`--no-module-passes` 及相关测试与文档；遍参数语法与模块登记遍名保留。需要某个遍的库（如自动微分需要 `std::macro`）在前提不满足时给出说明要加哪个遍的诊断。

**剩下什么**：第 1 项（前端四条与 `TypeCode::Byte`）、第 5 项（三条地基合并后的全套测试收尾）未做；第 3、4、6、7 项已完成；第 2 项的 `:graph/original`、traits、`#independent` 由 H85 完成，控制边约简由 H95 收到 `std::opt::ctrl`（`reports/H95.md`），前端部分仍属第 1 项。

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

**剩下什么**：第 0、1 步已入（`handover/H76-stdlib-step1`，报告 reports/H76.md）；第 2 步起（按模块分批迁移）未开始。第 1 步的待决问题（声明模块用不到实现模块导出的类型、泛型声明之间不做重叠检查等）见报告第 6 节，其中"声明模块里的 `Tensor` 从哪来"必须在迁移 `tensor` 前定。

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

**剩下什么**：第 1 项的**前半**由 H83 完成（`t[a:b]`、`t[:b]`、`t[a:]`、`t[:]`、步长为 1、`t[idx]`、`last(t)`：语法、类型、rank-1 的下界、格式化器、语言服务器、文档，报告 reports/H83.md）；**多维切片 `t[:, 0]`、`t[a:b, c]` 与步长不为 1 的视图**要等 H17 的数组表示，现在写它们是带位置的诊断且点名 H17（多维切片是编译期 `ConstructNotSupported`，数组的步长是运行时 `InvalidArgument`，张量的步长与 rank≥2 的切片是编译期 `ArgumentsRejected`）；槽数组（元素是对象）的切片现在复制，随 H17 改视图。第 2 项（lambda 参数类型从上下文推出）未开始；第 3 项已撤回，由 H25（`16136a3a`）完成。

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