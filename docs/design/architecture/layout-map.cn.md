# 目录对照表（搬家脚本的输入）

依据：`docs/design/architecture/abstractions.cn.md` 第 9 节（仓库布局，已定）、
`docs/design/architecture/refactor-plan.cn.md` 第 1、2 节（阶段与车道）、
`docs/handover/plans/2026-11-22-refactor-stage0.cn.md` 0.5。

这张表是**搬家脚本唯一的输入**（`scripts/layout-move.mjs`）。脚本读它，产出 `git mv` 序列、`#include`
改写与每块的构建目标；脚本不执行，演练在临时树里跑（见 `scripts/layout-move.mjs --help`）。

表的写法有三条规矩，脚本按它们检查，缺一条就报错而不是跳过：

1. **表 A 的每一行对应工作树根下一个条目**，一个不多、一个不少。脚本拿 `readdir` 读出的顶层条目数
   与表 A 的行数相比，两者不等就退出 1（「漏了一行」和「多了一行」都是错）。
2. **表的「目标」是搬家后的路径**，`/` 结尾表示目录。`—` 表示这个路径不搬家。
   顶层的四个容器目录（`src/`、`include/`、`modules/`、`stdlib/`）在表 A 里只写一行「拆到表 B」，
   它们的子条目在表 B 里一一有行。
3. **状态只有三种**：`搬`、`留`、`不明确`。`不明确` 的行脚本**不搬、不改写、不生成目标**，
   集中列在文件末尾的表 C，由决策者逐条定；定了就把那一行的状态改掉、填上目标，脚本下一轮自动搬家。
   **表里没有第四种状态**：一个路径要么搬、要么留、要么被明确标成没定。

块名取核心抽象第 9 节的五处：`kernel`、`parse`、`compile`、`lib/<模块>`、`tools`。
一个块的构建目标只把**下层**的头文件放进包含路径；`lib/<模块>` 之间只有声明过的依赖可以互相引用。

## 表 A：顶层对照（50 行 = 工作树根的 50 个条目）

| 现有路径 | 目标 | 块 | 状态 | 备注 |
|---|---|---|---|---|
| `.clang-format` | — | — | 留 | 格式化配置 |
| `.git` | — | — | 留 | 工作树里是指针文件、主检出里是目录；git 自身，不是仓库内容 |
| `.gitattributes` | — | — | 留 | |
| `.github/` | — | — | 留 | CI 配置，归合并位 |
| `.gitignore` | — | — | 留 | |
| `.husky/` | — | — | 留 | 提交钩子 |
| `.prettierrc` | — | — | 留 | |
| `.vscode/` | — | — | 留 | 编辑器工作区设置 |
| `ABOUT` | — | — | 留 | |
| `AGENTS.md` | — | — | 留 | |
| `agents/` | — | — | 留 | 施工规则 |
| `assets/` | — | — | 留 | 品牌图 |
| `benchmarks/` | — | — | 留 | 用户程序与训练脚本，不是模块 |
| `bindings/` | ? | — | 不明确 | U18 |
| `changes.log` | — | — | 留 | |
| `cmake/` | — | — | 留 | 构建胶水与选项 |
| `CMakeLists.txt` | — | — | 留 | 构建入口；脚本只按块改写它的四条钩子行 |
| `CODE_OF_CONDUCT.md` | — | — | 留 | |
| `conanfile.txt` | — | — | 留 | |
| `CONTRIBUTING.md` | — | — | 留 | |
| `diagnostics/` | ? | — | 不明确 | U17 |
| `docs/` | — | — | 留 | |
| `editors/` | — | — | 留 | 编辑器扩展；语言服务器本身在 `tools/lang-server/` |
| `examples/` | — | — | 留 | 用户程序 |
| `exemplars/` | — | — | 留 | 用户程序 |
| `export_matrix.csv` | — | — | 留 | 生成物，索引页 |
| `export_matrix.md` | — | — | 留 | 生成物，索引页 |
| `hello.cml` | — | — | 留 | 仓库根的示例程序 |
| `include/` | 拆到表 B | 各块 | 搬 | 只留内核的公开头，其余随各自源 |
| `internal/` | — | — | 留 | 构建与闸门 helper，不是独立程序（第 9 节的 `tools/` 指命令行、调试器、语言服务器） |
| `LICENSE` | — | — | 留 | |
| `llms.txt` | — | — | 留 | |
| `logo.png` | — | — | 留 | |
| `modules/` | 拆到表 B | `lib/` | 搬 | 与 `stdlib/` 并成 `lib/<模块>/` |
| `package-lock.json` | — | — | 留 | |
| `package.json` | — | — | 留 | |
| `promo/` | — | — | 留 | 宣传片素材 |
| `pypi/` | — | — | 留 | 发布清单与打包 |
| `README.md` | — | — | 留 | |
| `REPRODUCE.md` | — | — | 留 | |
| `ROADMAP.md` | — | — | 留 | |
| `scripts/` | — | — | 留 | 构建/测试/检查脚本，同上：不是独立程序 |
| `SECURITY.md` | — | — | 留 | |
| `showcase/` | — | — | 留 | 用户程序 |
| `src/` | 拆到表 B | 各块 | 搬 | |
| `stdlib/` | 拆到表 B | `lib/` | 搬 | |
| `test/` | — | — | 留 | 测试计划、用例、金标、工具；金标归合并位 |
| `third_party/` | — | — | 留 | 最下层，被 `libcamel` 一起编译 |
| `tools/` | `tools/` | `tools` | 留 | 原地成为一个块：命令行、调试器、语言服务器等独立程序 |
| `ZEN` | — | — | 留 | |

## 表 B：四个容器目录的拆分

`<模块>` 是占位符：脚本枚举 `modules/*/` 与 `stdlib/*.cml` 的实际名字去展开，并检查每个名字都有行。

| 现有路径 | 目标 | 块 | 状态 | 备注 |
|---|---|---|---|---|
| `src/core/` | `kernel/core/` | `kernel` | 搬 | 类型、算子记录、图数据模型与存储、编辑会话、事实、重写核心、值 |
| `src/runtime/` | `kernel/runtime/` | `kernel` | 搬 | 同上；内部再按组件细分是阶段 1 内部的事，不是这张表的事 |
| `src/utils/` | `kernel/utils/` | `kernel` | 搬 | 基础工具，最下层 |
| `src/parse/` | `parse/src/` | `parse` | 搬 | 源码 → 语法树 |
| `src/compile/` | `compile/src/` | `compile` | 搬 | 语法树 → 图 |
| `src/builtin/` | ? | — | 不明确 | U1 |
| `src/execute/` | ? | — | 不明确 | U2 |
| `src/passes/` | 拆到本表以下四行 | — | 搬 | 容器 |
| `src/passes/opt/` | ? | — | 不明确 | U3 |
| `src/passes/sched/` | ? | — | 不明确 | U4 |
| `src/passes/test/` | ? | — | 不明确 | U5 |
| `src/passes/trans/` | ? | — | 不明确 | U6 |
| `src/passes/trans/rulesets/` | ? | — | 不明确 | U7 |
| `src/profile/` | ? | — | 不明确 | U9 |
| `src/service/` | ? | — | 不明确 | U8 |
| `include/camel/core/` | `kernel/include/camel/core/` | `kernel` | 搬 | 内核公开头，唯一留在包含路径根上的 `camel/` 目录 |
| `include/camel/utils/` | `kernel/include/camel/utils/` | `kernel` | 搬 | |
| `include/camel/runtime/` | `kernel/include/camel/runtime/` | `kernel` | 搬 | |
| `include/camel/parse/` | `parse/include/camel/parse/` | `parse` | 搬 | |
| `include/camel/compile/` | `compile/include/camel/compile/` | `compile` | 搬 | |
| `include/camel/common/` | ? | — | 不明确 | U10 |
| `include/camel/init.h` | ? | — | 不明确 | U11 |
| `include/camel/execute/` | ? | — | 不明确 | U2（头随其源） |
| `include/camel/passes/` | ? | — | 不明确 | U12 |
| `include/camel/service/` | ? | — | 不明确 | U8（头随其源） |
| `include/camel/profile/` | ? | — | 不明确 | U9（头随其源） |
| `include/camel/standalone/` | ? | — | 不明确 | U13 |
| `modules/<模块>/` | `lib/<模块>/` | `lib/<模块>` | 搬 | 一个 Camel 模块一处 |
| `modules/camel_module_abi.cpp` | ? | — | 不明确 | U16 |
| `modules/camel_module.cmake` | ? | — | 不明确 | U16 |
| `stdlib/<模块>.cml` | `lib/<模块>/<模块>.cml` | `lib/<模块>` | 搬 | `.cml` 声明与 C++ 实现放在一起；40 个名字 |
| `stdlib/prelude.cml` | `lib/std/prelude.cml` | `lib/std` | 搬 | 例外：它没有模块入口名，是随每个程序一起编译的内建签名表 |
| `stdlib/rulesets/` | ? | — | 不明确 | U14 |
| `stdlib/signatures/` | ? | — | 不明确 | U15 |

`stdlib/` 的展开规则是 `stdlib/<名字>.cml → lib/<名字>/<名字>.cml`，**41 个 `.cml` 里 40 个走这条规则**，
唯一例外是 `prelude.cml`（上表最后几行已写明）。脚本会核对：`stdlib/*.cml` 的个数必须等于
「被这条规则展开的」加「被例外行点名的」，不等就退出 1。

## 表 C：不明确（19 项，请决策者逐条定）

**一条都不猜。** 每一项都写出「两条路各带什么代价」，定了就改上表里那一行的状态与目标。

| 编号 | 路径 | 问题 | 路 A（代价） | 路 B（代价） |
|---|---|---|---|---|
| U1 | `src/builtin/` | 内建算子和内建执行器算内核还是算 `std`？ | `kernel/builtin/`：内核直接登记内建算子，`lib/std` 只放遍；内核因此继续认识具体算子 | `lib/std/builtin/`：内核只认算子记录，内建算子像模块一样登记；`lib/std` 要能被内核加载 |
| U2 | `src/execute/`、`include/camel/execute/` | 执行基础设施放哪一块？ | `lib/std/exec/`：总方案的车道表这么写；但 `exec_plan` 按第 13 节是**事实**，事实归内核 | 内核对半、`lib/std/exec` 另一半：要逐文件拆，`exec_plan.cpp` 与 `pass.cpp` 分居两处 |
| U3 | `src/passes/opt/` | C++ 的优化遍归哪个模块？ | `lib/std/cxx/opt/`：总方案注明重写车道拥有它 | `lib/std/opt/`：与 `stdlib/opt.cml`（`opt::` 模块）同处，改名后模块名与目录名一致 |
| U4 | `src/passes/sched/` | 三个调度后端是一个块还是三个？ | `lib/std/{nvm,fvm,jit}/`：车道表点了 `lib/std/nvm`，一个后端一个目录 | `lib/std/sched/`：一个块一个构建目标，`fastvm`/`nodevm`/`taskflow` 作子目录 |
| U5 | `src/passes/test/` | 只给测试用的遍归哪？ | `lib/test_support/test/`：与 `test_support` 模块同处 | `lib/std/test/`：测试遍也是遍，与该模块的其余遍同处 |
| U6 | `src/passes/trans/` | 变换/导出/打印的遍归哪？ | `lib/transforms/`：车道表把 `lib/transforms` 给变换车道 | `lib/std/trans/`：这些遍现在就是 `std::` 的名字，改名会动 CLI 读数 |
| U7 | `src/passes/trans/rulesets/` | 规则集（C++ 侧）的独占目录名 | `lib/rulesets/`：与 `stdlib/rulesets/` 同一个名字，两块并一处 | `lib/transforms/rulesets/`：规则随变换走；但规则车道要独占它就得跨车道改 |
| U8 | `src/service/`、`include/camel/service/` | 语义查询、代码生成、性能剖析归工具还是前端？ | `tools/service/`：第 19 节说工具经 `compile/` 的语义查询接口，`service` 是工具的底座 | `compile/service/`：语义查询是 `compile/` 该提供的那份接口的现成实现 |
| U9 | `src/profile/`、`include/camel/profile/` | 性能采样的核心归内核还是工具？ | `kernel/profile/`：工具经内核的自省接口读它 | `tools/profiler/`：只有剖析工具读它，内核不需要 |
| U10 | `include/camel/common/` | 公共小头算内核公开头的一部分吗？ | `kernel/include/camel/common/`：与 `core` 并列 | 顶层 `include/camel/common/`：所有块都要引的，留在最上层的 `include/` |
| U11 | `include/camel/init.h` | 同上（初始化入口） | `kernel/include/camel/init.h` | 顶层 `include/camel/init.h` |
| U12 | `include/camel/passes/` | 遍的头随哪个块 | 随 U3–U6 定下的块（各自块内 `include/`） | 统一放 `lib/std/include/camel/passes/` |
| U13 | `include/camel/standalone/` | C++ 导出程序用的头归哪 | `lib/std/cpp/include/camel/standalone/`：导出后端自己的公开头 | `tools/camel-cpp/`：只有 `camel-cpp` 这个工具生成并需要它 |
| U14 | `stdlib/rulesets/` | 规则集的 `.cml` 声明归哪 | `lib/rulesets/`（与 U7 同名合并） | `lib/std/rulesets/`：规则集是 `std::` 的规则库 |
| U15 | `stdlib/signatures/` | 原生函数的类型签名表归哪 | `lib/<模块>/signatures/`：按签名所属模块散到 6 处 | `lib/std/signatures/`：一处放齐，谁要看都在同一个地方 |
| U16 | `modules/camel_module_abi.cpp`、`modules/camel_module.cmake` | 模块的公共构建胶水与 ABI 桩 | `cmake/camel_module.cmake` + `lib/camel_module_abi.cpp`：胶水进 `cmake/`、源就近 | `lib/abi/`：模块的二进制接口层单独一块，第 15 节的 C 接口落在那里 |
| U17 | `diagnostics/` | 诊断码登记表（生成 `core/error` 下的码表） | `kernel/diagnostics/`：它是内核错误码表的数据源 | 留顶层：顶层 `CMakeLists.txt` 与 `scripts/diagnostics-catalog.mjs` 都按顶层路径读它 |
| U18 | `bindings/python/` | CPython 扩展（宿主 libcamel 的一方） | `lib/python/bindings/`：与 `python::` 模块同一个目录树，两个方向一处 | 留顶层：它不是 Camel 模块，是另一个语言侧的宿主 |
| U19 | `modules/verify/` | 只有 C++、没有 `.cml` 声明的模块 | `lib/verify/`：目录名就是它的入口名（`verify::`） | `lib/test_support/verify/`：它与 `test_support` 是同一类（只给测试用） |

## 这张表怎么用

```powershell
# 只生成，不执行：看 git mv 序列、包含改写与每块的构建目标
node scripts/layout-move.mjs --out D:\Temp\layout-out

# 在临时树里真的搬并编译（演练；产物不合入）
node scripts/layout-move.mjs --apply --root D:\Temp\layout-drill
```

`--apply` 只会动 `--root` 指向的树，且拒绝在仓库主干上跑。演练结果与违例清单见
`docs/handover/plans/2026-11-22-refactor-stage0-layout-violations.cn.md`；车道对目录的独占见
`layout-ownership.cn.md`。