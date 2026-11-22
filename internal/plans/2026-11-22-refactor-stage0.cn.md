# 重构阶段 0 施工计划：搬家前的准备

依据：`docs/design/architecture/refactor-plan.cn.md`（已定）第 1 节阶段 0；核心抽象 `abstractions.cn.md` 第 9 节（仓库布局、依赖反转、遍只能看图）。本阶段六件事彼此独立，同时开工；新特性冻结，修复按总方案第 5 节分流。完成的标志写在每件后面，不看行数。

## 0.1 CI 四条腿转绿（合并位）

接着现有的四类做（`internal/reports/2026-11-22-the-four-red-legs-are-four-things.cn.md`）：`timeit` 改写与 100 层 `concat` 链留作回归用例；`gpu_scope` 的另一条路径；ASan 腿为什么被跳过。
**完成**：develop 头上 CI 四条腿全绿，ASan 腿跑起来；此后每次合并不得出现新的红。

## 0.2 依赖反转一：调遍的驱动不认识具体的遍

**现状**：`src/execute/pass.cpp` 有 107 条包含，其中 64 条指向具体的遍（`std` 的各个遍、`test::*`、解析器）；模块的遍已经走 `registerModulePass`（`include/camel/execute/pass/base.h:96`、`params.h:117`）。
**做法**：`std` 与 `test` 的每个遍照模块的做法，在它自己的源文件里登记（名字、参数描述、实现）；驱动只按名字从登记表里取。"遍是函数"：登记表就是"Camel 函数名 → C++ 实现"，以后普通的原生函数也走同一张表。
**完成**：`pass.cpp` 与 `camel_pass.cpp`、`pass_declarations.cpp` 不再包含任何具体遍或解析器的头文件；`std::default` 与全部现有流水线读数不变（差分对照，0.6）。

## 0.3 依赖反转二：内核不调用解析与编译

**现状**：`src/core/module/userdef.cpp` 包含 `parse/parse.h`、`compile/gct/builder.h`、`compile/gir/builder.h`；`include/camel/core/module/userdef.h` 包含 `compile/gct.h` 与 `parse/parse.h`。
**做法**：内核定义"源码模块加载器"接口（给出源码路径与上下文，交回编译好的模块）；`compile/` 实现它，初始化时登记给内核。`userdef` 只调这个接口。
**完成**：`core/` 下没有任何文件包含 `parse/` 或 `compile/` 的头文件；加载、导入、错误报告的读数不变。

## 0.4 依赖反转三：遍只认编译好的规则值

**现状**：`include/camel/core/forward.h:28` 包含 `parse/ast/rule_head.h`；变换的 7 处直接读规则头（`internal/reviews/` 里的规则头审计页）。
**做法**：编译器把规则编成内核里的规则对象（`Rule` 的内容），规则头里变换要用的信息（名字、根算子、元数、右侧被调者、左侧绑定的变量）全部进这个对象；规则集按核心抽象第 6 节是编译期给定的值，变换从交来的规则集里按根算子编号取规则，不按名字查族、不读语法树。本步只做"不读语法树"；变换骨架的重组属于后面的变换一步。
**完成**：内核与 `modules/` 下没有任何文件包含 `parse/ast/` 的头文件；求导、转置、批处理的全部用例读数不变。
**已验收**（作者 `f0756385f`）。完成标志可数：`git grep -l 'parse/ast/' -- include/camel/core modules` 无输出
（基线 8 个文件，其中 6 处是包含、2 处是注释提及，注释也一并改写）。规则头由 `camel::core::collectRuleHead`
（`src/core/rule_index.cpp:34`）填进 `camel::core::RuleObject`（`include/camel/core/rule_index.h:48`），五事实
各是一个成员；`forward.h` 改为包含 `camel/core/rule_index.h`。差分 `output_differ=0 graph_differ=0`。
**留给变换一步的三处**（作者同上，进变换一步的施工计划，不在 0.4 返工）：
1. 索引是全局的、按族名与算子拼写取规则；核心抽象第 6 节要的是显式交来的规则集值、按根算子编号取。
2. `condition` 与 `parameterTypes` 曾存源码文字、判重靠比字符串（类型的第二份真相）。`parameterTypes`
   已编译化并留两条用例各钉一个错误方向（`87a8b5749`）；`condition` 无读者，已从 `RuleObject` 删掉
   （作者 `4d85c528c`，`1ee54d446`），随统一规则值回到匹配处。
3. `RuleObject` 与重写模块的规则值是两样东西，要不要合成一个由变换一步定。**作者已答**：一条规则就是
   一个值；`RuleObject` 加全局索引是过渡形态，**不再加字段、不再改键**，第 1 项不在它上面做
   （`internal/questions/2026-11-22-one-rule-value-or-two.md`）。

## 0.5 搬家脚本与违例清单（结构车道）

**做法**：
1. 把对照清单（scratchpad 里 `layout-inventory.md` 的方法）定成一张提交进仓库的表：每个现有路径 → 目标位置（`kernel/`、`parse/`、`compile/`、`lib/<模块>/`、`tools/`），按核心抽象第 9 节与总方案的车道划分；清单里标为"不明确"的十几项由决策者逐条定。
2. 脚本读这张表，生成 `git mv` 与包含路径的改写，并为每块生成构建目标（只把下层的头文件放进包含路径）。
3. 在当前 develop 头上演练：生成、编译、列出越层引用（编译错误或包含扫描），整理成违例清单，每条标明归哪一步重构清掉。演练结果不合入。
4. 同时写一份目录所有权表（每条车道独占的目录），搬家后生效。

**完成**：脚本在任一 develop 头上可重跑；演练能编译过（越层引用先以违例清单的形式保留，用一个过渡的包含路径兜住，随各步重构清掉）；违例清单与所有权表提交。

## 0.6 差分对照工具骨架（工具与差分车道）

核心抽象第 18 节：一个工具，配置"拿什么和什么比"；比输出（数值按声明的误差界）、规范形的图、耗时（只报告）。阶段 0 先做两种配置：同一程序在两个提交上比（给 0.2–0.5 验收用），参考执行器 nvm 对其余后端。
**完成**：对测试计划里的全部可执行用例，能在两个提交之间出一份差异清单；0.2–0.4 的合并以"无差异"为条件。

## 车道与冲突

| 车道 | 做哪件 | 动的文件 |
|---|---|---|
| 合并位 | 0.1，合并与闸门 | 公共文件、CI 配置 |
| 驱动 | 0.2 | `src/execute/pass*.cpp`、各遍源文件里新增的登记 |
| 加载 | 0.3 | `src/core/module/`、`compile/` 的初始化 |
| 规则 | 0.4 | `include/camel/core/forward.h`、`compile/gir/rule_templates*`、`modules/transforms/` 的规则读取、`src/passes/trans/rulesets/` |
| 结构 | 0.5 | 新增的表、脚本、所有权表（不动现有代码） |
| 差分 | 0.6 | `test/tools/` 下新增的工具 |

驱动与规则两条都碰 `src/passes/trans/rulesets/`：驱动只在它的文件末尾加登记，规则改它的读取逻辑；先合的那条之后，另一条在合并时让位重排。其余互不相交。0.2–0.4 全部合入、0.1 转绿之后，进入阶段 1 的搬家窗口。
