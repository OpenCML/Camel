# 目录所有权表（搬家后生效）

依据：`docs/design/architecture/refactor-plan.cn.md` 第 2、3 节（车道按**目录所有权**划分）、
`docs/handover/plans/2026-11-22-refactor-stage0.cn.md`（阶段 0 的六条车道）。

两条规则，搬家（阶段 1）之后生效：

1. **一条车道只改自己独占的目录。** 需要改别人的目录时，向那条车道提「接口请求」（审查页上一行），
   由它改、它合；不越界改别人的文件。
2. **公共文件只归合并位。** 构建文件、测试金标、文档索引、问题页索引是冲突热点；车道不手改它们，
   合并位在合并时统一重生成。能改成自动生成的，就改成自动生成的。

## 阶段 0 的六条车道（现在生效，用的是旧路径）

| 车道 | 做哪件 | 独占的目录（搬家前） | 搬家后对应 |
|---|---|---|---|
| **合并位** | 0.1、合并、闸门、违例清单记账、修复分流 | 无独占目录；公共文件见下表 | 同左 |
| **驱动** | 0.2 调遍的驱动不认识具体的遍 | `src/execute/pass*.cpp`、`src/execute/camel_pass.*`、`src/execute/pass_declarations.*`、各遍源文件里新增的登记 | `lib/std/exec/pass*`、`lib/std/exec/camel_pass.*`（U2 定了才落地） |
| **加载** | 0.3 内核不调用解析与编译 | `src/core/module/`、`compile/` 的初始化（`src/compile/init.*`） | `kernel/core/module/`、`compile/src/init.*` |
| **规则** | 0.4 遍只认编译好的规则值 | `include/camel/core/forward.h`、`src/compile/gir/rule_templates*`、`modules/transforms/` 的规则读取、`src/passes/trans/rulesets/` | `kernel/include/camel/core/forward.h`、`compile/src/gir/rule_templates*`、`lib/transforms/`、U7 定下的规则集目录 |
| **结构** | 0.5 表、脚本、所有权表、违例清单 | **不独占任何源码目录**：只写下面三个文件 + 违例清单 | 同左 |
| **差分** | 0.6 差分对照工具骨架 | `test/tools/` 下新增的工具、`tools/graph-diff/` | `test/tools/`、`tools/graph-diff/`；对照工具的实现在 `tools/` 内新开目录 |

**结构车道不产分支以外的路径所有权。** 它只新增四个文件：`layout-map.cn.md`、`scripts/layout-move.mjs`、
`layout-ownership.cn.md`（本页）、以及 `docs/handover/plans/` 下的违例清单。它对现有源码目录的
所有权是**零**——这正是 0.5 的判据 ⑤ 要的：台账进库，代码一格不动。

## 合并位独占的公共文件

| 文件 / 目录 | 为什么归它 |
|---|---|
| `CMakeLists.txt`、`cmake/**`、`modules/camel_module.cmake`、`tools/libcamel_tool.cmake` | 构建文件：每条车道都想改它，谁改谁就成了冲突点 |
| `test/plans/**/golden/**`、`test/plans/feat/parse/golden/ast_hashes.txt` | 测试金标：重录要跟提交一起进，不能各自录各自的 |
| `docs/**/README*.md` 的索引段、`docs/handover/questions/` 的待定表 | 生成物：由生成器写，合并位重生成 |
| `diagnostics/registry.tsv` | 诊断码登记表：诊断码是全仓库共享的编号空间 |
| `export_matrix.csv`、`export_matrix.md`、`docs/stdlib-reference.md` | 生成物：`--check` 会核对它们与生成器是否一致 |

## 搬家（阶段 1）之后的八条车道

阶段 0 结束时这六条车道并进重构总方案第 2 节的八条。目录是搬家后的名字，与
`docs/design/architecture/layout-map.cn.md` 的块名一致：

| 车道 | 独占的目录 | 这条表里还没定、要它来定的 |
|---|---|---|
| **图** | `kernel/graph`、`kernel/core`、`kernel/runtime`、`kernel/utils` | U1、U9、U10、U11 |
| **算子与事实** | `kernel/ops`、`kernel/facts`、C 接口层 | U1、U16 |
| **变换** | `lib/transforms` | U6、U7、U14 |
| **重写** | `kernel/rewrite`、`lib/rewrite`、`lib/std/cxx/opt` | U3、U7 |
| **前端** | `parse/`、`compile/` | U8、U15 |
| **执行** | `lib/std/exec`、`lib/std/nvm`、`lib/std/fvm`、`lib/std/prl`、`lib/std/cpp` | U2、U4、U9、U13 |
| **工具与差分** | `tools/`、差分对照工具 | U8、U13、U19 |
| **合并位** | 无独点目录；公共文件见上一节 | U17、U18 |

**这张表怎么查**：一个路径搬到哪里，看 `layout-map.cn.md`；搬过去之后谁能改它，看这张表。
两处冲突时，以 `layout-map.cn.md` 的块名为准——块是构建边界，车道是分工边界，前者更硬。