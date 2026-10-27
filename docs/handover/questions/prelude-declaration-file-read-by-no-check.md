# 前奏的声明文件不被任何程序级 `camel check` 读取

车道 `handover/check-nonzero`，起点 `origin/develop` `1b14c5c43`。本车道按 `docs/handover/reviews/questions-2026-10-26-late.md:47`（决策项 4，"前奏声明被拒，`camel check` 失败"）去找红，实测结果是：**所有文件化的"库/声明被拒"路径今天都已经非零退出**（H181 修好并被用例钉住），唯一仍"接受并继续"的是前奏，而前奏今天没有任何文件化的声明可拒。**缺陷因此不在 `camel check` 的退出码少了一条分支，而在前奏的声明文件没有任何程序级 check 读它。**

## 读数（HEAD `1b14c5c43` 的主机构建，`out/latest`）

- **这份参考文件今天本身就非法**：`camel check -O short <install>/stdlib/signatures/globals.cml`（把它当入口）→ exit **1**，6 条语法错：`264:25`、`273:27`、`282:28`、`307:7`、`339:45`、`520:25`（`SyntaxDiag::UnexpectedToken`、`SyntaxDiag::MissingToken`）。
- **没有任何程序级 check 读它**：在 `CAMEL_STD_LIB` 的副本里把 `signatures/globals.cml` 改成非法（追加 `export func probe_no_such(x: i32): i32`），`camel check` 一个不 import 任何东西的程序 → **exit 0、输出 0 行**；原样、改坏、删掉，三条读数都是 0。
- 对照（本车道 20+ 种构造，全部已非零）：`CAMEL_STD_LIB` 含真 `math.cmo` 与非法 `math.cml`（H181 形状，入口 `import math` 但未被使用）→ exit **1**（`SemanticDiag::NativeDeclarationNotLoaded`、`ImportedModuleHasErrors`、`UnresolvedReference`）；兄弟库导出的声明被拒 → exit **1**（`NativeDeclarationNotLoaded` 加上入口上的 `ImportedModuleHasErrors`）；库不解析 → exit **1**（`SyntaxDiag::MissingClosingDelimiter`、`ModuleParseFailed`）。这些由 `test/cases/tools/cli/failed_program_paths.mjs:145-166` 与 `test/plans/feat/tools/cli.plan.toml:25-35` 钉住。
- 闸门（未改码的干净树，`-Plan smoke,test/plans/feat -Tier fast -Jobs 8 -Check all`）：`total 6979, pass 6973, fail 0, skip 2, known failures 4`，`RUN_EXIT=0`，8 个 checker 全部 exit 0。

## 机制

每个用户模块构造时 `src/core/module/userdef.cpp:74-75` 无条件导入前奏（`// Automatically import the built-in module`，随后 `importAllRefsFromMod(ctx->importModule(""))`），这次导入**不记 import site**。而 `tools/camel-cli/module_failures.cpp:31` 的 `programDependencySet` 只沿 `module->importSites()`（同文件 `:43`）走；`findModuleFailures`（同文件 `:54`）只问 `ctx->allUserModules()`（同文件 `:61`）里的 `UserDefinedModule`，且要有 `diagnostics()->hasErrors()`（同文件 `:66`）。前奏是空名、C++ 注册的 `BuiltinModule`，没有 `.cml`/`.cmo`，于是它永远不在 `ModuleScope::ProgramDependencies` 里：既不进 `failures.modules`，也不进 `declarationModules`（`tools/camel-cli/module_failures.h:39-50`）。

值得注意的是，`ProgramDependencies` 的定义写的正是"入口模块与它直接或间接 import 的每个模块"（`tools/camel-cli/module_failures.h:19-23`）——前奏就是"它 import 的模块"，只是"不需要 import 的名字"这条语言规则让它走了一条不记 site 的路。今天 `stdlib/signatures/globals.cml:10` 明文写着 "For reference only: these declarations are not compiled"，所以这条不一致还没有可观测的后果。

## 请裁定（两条）

1. **决策项 4 是否等决策项 1 落地后再做？** 决策项 1（`docs/handover/reviews/questions-2026-10-26-late.md:44`，内建声明由前奏的声明文件编译，文件名以落地的为准）先让前奏成为一个"有声明文件、被拒时会报错"的模块，决策项 4 才有可观测的读数。
2. **若不等：现在是否就落"把 `importModule("")` 记为 import site"的 2 行**（`src/core/module/userdef.cpp:75` 旁），让 `programDependencySet` 收前奏？代价是它今天**无红可证**（只有决策项 1 落地后才会被证），并且 `ProgramDependencies` 从此包含一个今天不可能出错的模块。本车道的读数倾向"等"：这 2 行今天不改变任何一个读数。

## 已钉住、勿动（同一族，别顺手改）

`test/cases/tools/cli/pass_declarations_missing.mjs:47-49`（计划 `feat/opt/pass_params` 的 `declaration.missing_is_named`）：`CAMEL_STD_LIB` 里 `std.cml` 只有 `module std` 时，`camel check` exit **0**，而 `run` 拒 pass（exit **1**，点名模块与它读的声明文件）。这不是缺陷——不解析 pass 的 check 不需要 pass 声明——所以任何"让 check 因标准库不可用而失败"的改法都会把它变红。

## 这条是已记录的缺陷，不是"忽略"

按 `AGENTS.md`：设计原则与裁定是规范性的，与代码、其他文档或测试结果冲突时由它胜出，冲突记为缺陷或待办。决策项 4 已经裁定"前奏声明被拒，`camel check` 失败：报出了错误，命令就以非零退出，到处同一条规则，前奏也不例外"，而今天前奏的声明文件没有任何程序级 check 读它、该文件本身还有 6 条语法错 ⇒ 这条记为**已记录的缺陷**，不是"今天不需要"。