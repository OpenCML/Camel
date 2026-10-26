# 公开仓库的组织（设计稿）

状态：已定（协调者 2026-10-22，语言作者授权"可以大刀阔斧"）。目标：仓库公开时，目录、文档与历史看起来是一个专业、现代的语言仓库——读者五分钟内知道它是什么、怎么装、怎么写第一个程序、代码在哪里、怎么贡献；看不到内部协作的过程。

---

## 1. 两条边界

1. **公开与内部分开。** 内部协作的一切（交接说明、评审、设计轮次、裁定记录、施工报告、论文评估）不进公开仓库。从现在起它们集中在**一个**顶层目录 `internal/`，公开时整体删除，不需要逐文件甄别。
2. **公开仓库的历史从一个干净的快照开始。** 公开时新建公开仓库，第一个提交是清理后的树（`Initial public release`），此后在公开仓库上开发；现在这个仓库保留为私有的开发档案。不对现有历史做逐提交改写——一千多个提交里的任务编号、机器路径与协作措辞改写不干净，也没有价值。公开之后的提交遵守第 5 节。

## 2. 目标目录

```
README.md  LICENSE  CONTRIBUTING.md  CODE_OF_CONDUCT.md  SECURITY.md  CHANGELOG.md
ABOUT  ZEN                                         Camel 的标识：`camel --about` 的横幅与语言的八条禅（保留在根目录）
CMakeLists.txt  conanfile.txt  package.json        构建入口（package.json 只承担脚本）
cmake/                                             构建辅助
include/camel/                                     libcamel 的公开头文件（模块 ABI）
src/                                               编译器与运行时（按层分目录，见 3）
stdlib/<模块>/                                     每个标准库模块一个目录：<模块>.cml 声明 + native/ 的 C++ 实现
tools/                                             camel（CLI）、lsp、fmt、debugger、profiler 等可安装的工具
bindings/python/                                   Python 包（原 pypi/）
editors/vscode/                                    编辑器支持
examples/                                          由浅入深的示例；examples/models/ 是旗舰模型（原 showcase/）
tests/                                             测试计划、用例与测试工具（原 test/；parsecheck 等测试工具移入）
benchmarks/                                        对外有意义的基准（与 PyTorch 的对照、启动与循环的基准）
docs/                                              公开文档（见 4）
internal/                                          内部协作，公开时删除（见 1）
```

**根目录只留上面这些。** 删除或移走：`hello.cml`（进 `examples/`）、`export_matrix.*`（生成物，不入库）、根目录的 `logo.png`（`assets/` 已有，`assets/` 移入 `docs/assets/`）、`changes.log`（改为 `CHANGELOG.md`，Keep a Changelog 格式）、`.vscode/`（个人设置）、`AGENTS.md` 与 `agents/`（移入 `internal/`）、`ROADMAP.md`（公开版只留一页方向，不含任务编号）。

## 3. 源码

- **一个标准库模块一个目录**：`modules/<m>/`（C++）与 `stdlib/<m>.cml`（声明）合并为 `stdlib/<m>/`。模块的声明、实现、该模块专属的测试辅助在一起；安装布局不变。`tiles` 是 `stdlib/tiles/`。
- **`src/` 按层分目录**，每层一个职责、依赖只向下：`parse`（词法、语法、AST）→ `compile`（GCT、GIR 建图）→ `passes`（优化、导出、调度器）→ `runtime`/`execute`（帧、执行计划、执行器）→ `core`（类型、值、内存、图存储）。`service`、`profile`、`utils` 这类不表达层次的目录拆入对应的层；`builtin` 并入 `core` 或对应层。具体映射由施工方在计划里给出。
- 文件与目录名统一小写下划线；头文件与源文件同层对应。

## 4. 文档（负责人 2026-10-24 补充：对照语言类开源项目重组，不延续编号文档）

**参照**：Rust（The Book、The Reference、rustc-dev-guide）、Zig、Go、Python 的文档分层——给学的人一本书，给查的人一份参考，给改编译器的人一份内部指南，设计取舍单独成篇。公开文档以英文为主，中文为译本（`*.zh-CN.md`，同目录）。

```
docs/
  README.md                  文档入口：四类读者各从哪里开始
  install.md                 安装与构建（合并 setup、build-options、npm-scripts、pypi-packaging 中面向用户的部分）
  book/                      教程：由浅入深，按主题成章（getting-started、values-and-types、functions、
                             effects-and-sync、arrays-and-tensors、streams、rules-and-passes、training、gpu ……）
  reference/                 语言参考（规范性）：按主题成篇，不编号——lexical、types、expressions、statements、
                             effects、modules、generics、streams、rules、tags；以及生成的 stdlib 参考、
                             CLI 参考、诊断目录
  internals/                 编译器与运行时：每个模块一篇（见下）
  design.md                  设计取舍：语言的定位与原则（设计原则的公开版，不含裁定记录与内部措辞）
  reference/camel-for-ai.md  给 AI 的精炼语言参考（必读的一页：规则、惯用法、常见错误；根目录 `llms.txt`
                             指向它）；生成的诊断目录与标准库签名各成一个查阅文件，与它同目录：
                             reference/camel-for-ai-diagnostics.md、reference/camel-for-ai-stdlib-signatures.md
  assets/
```

**内部指南（`internals/`）每个模块一篇，短而权威**：parse、compile（GCT/GIR 建图）、graph-model（节点、三种边、区域、不变量）、passes（遍即函数、流水线）、rewrite（规则引擎、等价饱和）、runtime（帧、执行计划、调度器）、memory（三层与原地更新）、backends（nvm、fvm、jit、prl、cpp）、gpu（Tiles）、modules（模块 ABI、`#impl` 内核）、diagnostics、debugger。

- **每篇只写不变的东西**：这个模块负责什么、核心数据结构、必须保持的不变量、对外接口、代码在哪个目录。不写实现细节的流水账，不写历史（"原来……后来改为……"），不写任务编号与日期化叙述。篇幅以一屏到几屏为宜。
- **权威**：它说的就是代码的行为；代码改了而文档没改是缺陷。引用符号（`GraphDraft::setRegionOwner`），不引用行号。
- **与代码同提交更新**：改变模块行为或不变量的提交，同时改对应的 internals 篇与 reference 篇（`agents/refactor.md` 的标准里写明）。设计原则的改动落地时，reference 相应段落在同一任务里改完。
- **编号文档全部改写**：今天的 `docs/technical/NN_*.md`（约 50 篇）与 `docs/language/NN_*.md` 不搬家，按上面的主题**合并、改写**进 `internals/` 与 `reference/`；只记录过一次排查或一次度量的篇（如 import 成本审计、某次性能基线）不进公开文档——结论有价值的并进对应模块篇，其余随 `internal/` 留存。

设计原则（`docs/design-principles.*`）仍是规范，留在 `internal/`；公开的设计取舍页（重组后新建，现在还不存在，见第 6 节 I5）是它面向读者的精简版，随规范更新。

## 4.5 文件头

现在的文件头有好几种（一千多个 C++ 文件里至少三种措辞），还带着作者、创建与修改日期和资助信息。统一为两行，用 SPDX 标识许可证：

```cpp
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
```

- **去掉**：许可证正文的复述（根目录 `LICENSE` 已有）、`Author`、`Created`、`Updated`、`Supported by`。作者与修改时间由 git 记录；不写年份，免得每年改一遍。
- **其后空一行**，接文件职责的简短说明（`agents/refactor.md` 要求的那段，原样保留）。
- **哪些文件带头**：手写的源码——C/C++（`src/`、`include/`、`tools/`、`stdlib/*/native/`）、标准库的 `.cml`、脚本（`.js`、`.mjs`、`.py`、`.cmake`，用各自的注释符号）。**不带**：测试用例与期望输出（加头会移动诊断的行号）、示例与教程里的程序、文档、生成的文件、`third_party/`。
- **脚本** `scripts/headers.mjs`：`--fix` 把任意旧格式改成上面的两行并保留职责说明，幂等；`--check` 不改文件、有不一致即失败，接入 `npm run check` 与 CI。它核对的是真实的一致性，不是计数。

## 5. 公开之后的约定（现在开始执行，公开时已成习惯）

- **提交信息**：Conventional Commits（`feat(parse): …`、`fix(runtime): …`），英文，说明改了什么与为什么；**不写任务编号、不写"交接""评审"等协作措辞**，不提 AI。合并用 squash 或 rebase，保持主线线性。
- **代码与文档中不出现**：机器路径、个人信息、任务编号（`H` 加数字的内部编号）、"施工方/协调者"、日期化的过程叙述。
- 分支：`main`（发布）与 `develop`；贡献者从 `develop` 拉分支、向 `develop` 提 PR。

## 6. 实施顺序（施工方先交计划）

1. **I1 `internal/`**：把交接、评审、设计稿、裁定记录、`AGENTS.md`/`agents/`、论文评估（`benchmarks/` 中的 e*/d* 评估脚本、`exemplars/`）移入 `internal/`。纯移动，一个提交。
2. **I2 根目录与文件**：第 2 节的根目录清理；`CHANGELOG.md`；`bindings/python/`；`tests/`；`examples/models/`。
3. **I3 标准库按模块成目录**（第 3 节第 1 条）。
4. **I4 `src/` 分层**（第 3 节第 2 条，先交映射表）。
5. **I5 文档重组**（第 4 节）：英文为主的 README、安装、教程、参考；公开的设计原则页。
6. **I6 文件头与公开前检查**：按 4.5 统一文件头（`scripts/headers.mjs --fix`，一次机械提交）；机器路径、个人信息、密钥、任务编号、内部措辞的扫描（一次性的检查，不作常驻守卫）；生成干净快照的脚本放在 `internal/`。

每一步都是独立的提交：移动与修改不混在一个提交里（移动用 `git mv`，便于审查）。与进行中的主线任务冲突时，由施工负责人安排在合适的间隙做。
