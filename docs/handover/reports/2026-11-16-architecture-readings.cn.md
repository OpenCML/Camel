# 架构读数（2026-11-16，周一）

与 `2026-11-13-architecture-audit.cn.md` 对比。量法：`src/`、`include/`、`modules/` 下受版本管理的 C/C++ 文件，`wc -l`；包含关系按带引号的 `#include` 解析到目录所属的层。基于 `gh/develop` `2bc0ed775`。

| 读数 | 本周 | 上周 |
|---|---|---|
| C++ 行数 | 36.0 万（src 22.1 万、modules 8.9 万、include 5.1 万），1144 个文件 | 约 35.0 万 |
| 超 2500 行的文件 | 20 个 | 未计（规矩二：中心文件不超 2500 行） |
| core → runtime 的包含 | 21 | 21 |
| execute → passes 的包含 | 62（其中 `pass.cpp` 60，测试遍 39） | `pass.cpp` 38 |
| modules → src 内部 | 3 | 约 10 |
| src → modules | 0 | builtin → math 3 |
| 后端 → 后端 | 1（taskflow → fastvm `hosted.h`） | 未列 |
| compile 里按算子名的特例 | 8 个 URI 字面量 + 12 处 `name() == "..."` | 约 15 |
| GCT 与 GIR 两层都报的诊断 | 22 | 21 |

增长最多的大文件：`runtime/graph.cpp` 3119 → 3795，`execute/pass.cpp` 3310 → 3494，`gct/builder.cpp` 8398 → 8519；GIR 建图器有 4 个文件超 2500 行（`builder_control.cpp` 5203、`builder_operators.cpp` 4923）。

**结论**：模块对内部的依赖与 builtin → math 改善了；分层（core → runtime、`context.h` 126 个包含者、`pass.cpp` 直接包含全部遍）一周没动；中心文件继续长。原因明确：本周排期收窄到三件优先级一，架构收敛（第十一节路线一至五）排在其后，是有意的。三件做完后，收敛先做两件代价最低、收益最大的：测试遍移出 `libcamel`（`pass.cpp` 的遍登记改成生成的列表），`entry_args.h` 下移到 compile 不再依赖 execute。边的种类与"有没有效应"的判断仍有多处各写一份（`syncPredsOf` 在语义层外 10 个文件、`!isPure || hasExternalEffect` 6 处），归 R3 统一分析。
