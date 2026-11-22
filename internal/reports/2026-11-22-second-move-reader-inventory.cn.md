# 第二次搬家：读者清单（基线 `develop = 539d67324`）

这一页只做一件事：为 `internal/plans/2026-11-22-root-cleanup.cn.md` 的第二次搬家，把**所有会因路径规则失效的地方**按读者类列出来，每条给「文件:行 → 原文 → 搬完该是什么 → 怎么验」。**没有搬任何文件，没有提交。**

规矩与上一轮不同：这次的每条规则都多一层 `src/`，所以上一轮那 13 处修正一处都不能照抄。

## 0. 规则编号

| # | 规则 |
|---|---|
| R1 | `kernel/ parse/ compile/ tools/ bindings/` → `src/…` |
| R2 | `lib/<模块>/<模块>.cml` → `src/stdlib/<模块>.cml` |
| R3 | `lib/<模块>/…`（C++）→ `src/stdlib/<模块>/…` |
| R4 | `lib/<模块>/signatures/<模块>.cml` 删除 |
| R5 | `hello.cml` → `examples/hello.cml` |
| R6 | `logo.png` 删除（引用改指 `assets/`） |
| R7 | `export_matrix.csv/.md` 不入库（生成到 `out/`） |
| R8 | `changes.log` → `CHANGELOG.md` |
| R9 | `ROADMAP.md` → `internal/ROADMAP.md` |
| R10 | `assets/` 留根（不改，但要确认没有读者以为它走） |

## 1. 按读者类的分组计数（互斥，合计 3,223 行）

| 类 | 处数 | 说明 |
|---|---|---|
| ① 重写规则 | **0** | 见 §3，`lib/**/*.cml` 里没有一处把路径当规则操作数 |
| ② `scripts/`（非闸门、非生成器、非打包） | **67** | `setver.js`、`diagnostics-catalog.mjs`、`check-operator-uri-literals.mjs`、`migrate-pack-dots.mjs` 等 |
| ③ 六道闸门 + `check-ast-hashes` + `check-operator-panic` | **171** | `scripts/check-*.mjs` 113 + `test/tools/` 里 8 个 `check-*`/`tree-guard` 54 + 跨规则 4 |
| ④ `test/tools/`（非闸门） | **47** | `run-tests.mjs`、`check-plan-fields.mjs`、`plan-fields-baseline.json`、`cppexec`、`benchmark-tiles.py` |
| ⑤ `cppexec`/编译期 include 根 | **311** | `cmake/layout-blocks.cmake` 263 + 各 `CMakeLists.txt` 27 + `cppexec/run.py` 等 21 |
| ⑥ AST 快照与期望字面量 | **83** | `ast_hashes.txt` 55 + 计划里的 `path#symbol` 与 `expect_stdout` 28 |
| ⑦ docs 生成器 | **119** | `stdlib-reference.mjs`、`stdlib-declarations.mjs`、`camel-for-ai.mjs`、`gen_export_matrix.py` 等 |
| ⑧ `docs/` 正文 + 代码注释里的 `path#symbol` | **2,366** | `docs/**` 1,775（含六条非根规则 63）、代码注释 343、`test/cases` 123、根 `*.md` 29 等 |
| ⑨ 打包与安装 | **46** | `collect-out.js` 的 stdlib 组装、`pypi-rollup.js`、`source-package.mjs`、`pypi/` |
| ⑩ CI 工作流 | **13** | **全是误命中**，见 §3 |
| ⑪ 新读者（`src/` 撞名） | **6 处硬冲突** + 2,724 行陈旧 `src/` 引用 | 见 §4 |

复算命令（每一行都是这一条）：

```
git grep -n -E '(^|[^A-Za-z0-9_./-])(kernel|parse|compile|bindings|tools|lib)/|(PROJECT_SOURCE_DIR|CMAKE_SOURCE_DIR|CMAKE_CURRENT_SOURCE_DIR)\}/(kernel|parse|compile|bindings|tools|lib)/|hello\.cml|logo\.png|export_matrix|changes\.log|ROADMAP\.md' -- .
```

## 2. 每条规则 → 读者类 → 文件:行 → 原文 → 搬完 → 怎么验

### R1（五个块进 `src/`）

| 类 | 文件:行 | 现在写的是 | 搬完该是 | 验 |
|---|---|---|---|---|
| ⑤ | `CMakeLists.txt:350` | `add_subdirectory(tools)` | `add_subdirectory(src/tools)` | 配置通过 |
| ⑤ | `CMakeLists.txt:322` | `add_subdirectory(bindings/python)` | `add_subdirectory(src/bindings/python)` | 同上 |
| ⑤ | `cmake/layout-blocks.cmake:42` | `${PROJECT_SOURCE_DIR}/kernel/utils` | `${PROJECT_SOURCE_DIR}/src/kernel/utils`（本文件 263 行同形） | 重新生成或逐行改 |
| ⑤ | `cmake/camel_module.cmake:47` | `${CMAKE_SOURCE_DIR}/kernel/abi/camel_module_abi.cpp` | …`/src/kernel/abi/…` | 模块 `.cmo` 仍能构建 |
| ⑤ | `test/tools/cppexec/run.py:83` | `CPP_INCLUDE_ROOTS = ("lib/std/cpp/include", "kernel/include", "include")` | 前两项加 `src/`，第三项（pre-move 兜底）删 | `cppexec` 计划 |
| ⑤ | `lib/std/builtin/modules/profiler.cpp:11` | `#include "../../../../tools/profiler/advanced/advanced_tracer.h"` | `"../../../../../src/tools/profiler/advanced/advanced_tracer.h"`（层级 +1） | 编译 |
| ③ | `scripts/check-std-includes.mjs:45` | `['kernel','parse','compile','lib','tools'].filter(existsSync)` | 加 `src`（见 §4：现在会静默空扫） | `node scripts/check-std-includes.mjs` |
| ③ | `scripts/plan-inventory.mjs:312` | `NAMING_ROOTS = [... 'kernel/','parse/','compile/','lib/','src/', ...]` | 换成 `src/` 下的真实前缀 | `node scripts/plan-inventory.mjs --check` |
| ② | `scripts/setver.js:23` | `fs.readFileSync('tools/camel-cli/config.h')` | `'src/tools/camel-cli/config.h'`（第 25 行同） | `npm run setver` |
| ② | `scripts/check-operator-uri-literals.mjs:75` | `'kernel/core/operator_table.cpp'`（76–78 同形） | 加 `src/` | 该闸门 |
| ③ | `scripts/check-operator-panic.mjs:17` | 注释 `kernel/ops/include/camel/ops/operator_record.h` | `src/kernel/...` | `--citations` |
| ④ | `test/tools/check-pass-declarations.mjs:434` | `['kernel/execute/camel_pass.cpp', 1]` | 加 `src/` | 该计划 |
| ④ | `test/tools/check-doc-pass-forms.mjs:456` | `read('kernel/execute/pass.cpp')` | 加 `src/` | 该计划 |
| ② | `scripts/headers.mjs:95` | `BLOCK_DIRECTORIES = [... 'src','include','modules']` | 去掉旧布局名，改 `src/<块>` 判定（见 §4） | `node scripts/headers.mjs --check` |

### R2 / R3（`lib/` → `src/stdlib/`）

| 类 | 文件:行 | 现在写的是 | 搬完该是 | 验 |
|---|---|---|---|---|
| ④ | `test/plan-selection.json:43` | `"paths": ["lib/std/"],` | `"paths": ["src/stdlib/"],` | `node test/tools/selftest/run.mjs` |
| ④ | `test/plan-selection.json:48` | `"paths": ["lib/"],` | `"paths": ["src/stdlib/"],` | 同上（另有 28 条 `"paths": ["lib/…"]`，行 252–437） |
| ④ | `test/plan-selection.json:602` | `"paths": ["assets/", "logo.png"]` | 去掉 `logo.png` | 同上 |
| ③ | `scripts/stdlib-declarations.mjs:470` | `const where = (module) => \`lib/${path.basename(module.dir)}\`` | `src/stdlib/…` | `--check` |
| ⑦ | `scripts/stdlib-reference.mjs:76,90` | 走 `lib/`（旧 `stdlib/`）；:1064 输出 `(lib/**/*.cml)` | `src/stdlib/`，页头常量同改 | 重生成后逐字节比对 |
| ⑨ | `scripts/collect-out.js:407` | `const libRoot = path.join(BASEDIR, 'lib')` | 整段 402–433 组装删掉，改「拷 `src/stdlib/`、跳 C++、放 `.cmo`」 | `npm run build` + wheel |
| ⑨ | `scripts/collect-out.js:471` | `path.join(BASEDIR,'build','lib','python',config)` | 构建目录随源码走（`build/src/stdlib/python`），两处都试 | 桥接 `.dll` 仍在 `libs/` |
| ② | `scripts/check-operator-uri-literals.mjs:85` | `const HOME_TREE = 'lib/std/builtin/'` | `'src/stdlib/std/builtin/'` | 该闸门 |
| ⑤ | `CMakeLists.txt:299` | `add_subdirectory(lib/${_module})` | `add_subdirectory(src/stdlib/${_module})` | 配置通过 |
| ⑤ | `CMakeLists.txt:246,248` | `add_subdirectory(lib/python)` / `lib/pyplot` | 加 `src/stdlib/` 前缀 | 配置通过 |
| ⑥ | `test/plans/feat/parse/golden/ast_hashes.txt:472` | `6fd58de989977cfe ok lib/array/array.cml`（共 52 行 `lib/`） | 路径改 `src/stdlib/…`；若摘要含路径则 52 行重算 | `node scripts/check-ast-hashes.mjs` |

### R4（`signatures/` 删除）

| 类 | 文件:行 | 现在写的是 | 搬完该是 | 验 |
|---|---|---|---|---|
| ⑦ | `scripts/stdlib-declarations.mjs:124-126` | `lib/<name>/signatures/<name>.cml` 是签名来源之一 | 去掉该来源与 `computed` 规则 | `--check` |
| ⑦ | `scripts/stdlib-reference.mjs:910` | `relative.startsWith('stdlib/signatures/')` | 删这一支 | 重生成 |
| ⑨ | `scripts/collect-out.js:426` | `sub.name === 'signatures'` 合并进安装 | 删 | `npm run build` |
| ③ | `scripts/law-cotangent-ports.mjs:101` | `'lib/tensor/signatures/tensor.cml'` | 改指声明文件或删 | 该闸门 |
| ⑥ | `test/plans/feat/parse/golden/ast_hashes.txt` | 10 行 `lib/*/signatures/*.cml` 的摘要 | 删这 10 行 | `check-ast-hashes` |
| ⑧ | `docs/stdlib-conventions.cn.md:27` | 那条「signatures 单独记录」的规则 | 删/改写 | `check-doc-links` |

### R5–R9（单文件规则）

| 规则 | 类 | 文件:行 | 现在写的是 | 搬完该是 |
|---|---|---|---|---|
| R5 | ⑥ | `ast_hashes.txt` 3 行 | `… ok hello.cml` | `examples/hello.cml` |
| R5 | ⑧ | `tools/debugger/src/repl.cpp:222,223,302` | 帮助文本 `camel-db hello.cml` | `examples/hello.cml`（散文，不阻断） |
| R6 | ④ | `test/plan-selection.json:602` | `["assets/", "logo.png"]` | `["assets/"]` |
| R6 | ⑧ | `docs/**` 2 处 | `logo.png` | `assets/logo.png` |
| R7 | ② | `scripts/gen_export_matrix.py:122,123` | 写 `REPO/export_matrix.csv`、`.md` | 写 `out/` |
| R7 | ② | `scripts/gen_export_matrix.py:134,142` | `--check` 比对仓库根的两个文件 | `--check` 删除或改判 `out/` |
| R7 | ⑧ | `benchmarks/mechanism/counts.py:7` 等 10 行 | 读 `export_matrix.csv` | 读 `out/` 或改「先跑生成」 |
| R8 | ② | `scripts/setver.js:23` 附近 5 行 | 读写 `changes.log` | `CHANGELOG.md` |
| R8 | ③ | `scripts/check-doc-links.mjs:576` | `rel !== 'changes.log'`（历史文件不扫） | `!== 'CHANGELOG.md'` |
| R8 | ③ | `scripts/check-references.mjs:152` | `entry.name !== 'changes.log'` | 同上 |
| R8 | ④ | `test/tools/check-doc-pass-forms.mjs` 4 行 | `changes.log` | `CHANGELOG.md` |
| R9 | ③ | `scripts/check-doc-links.mjs:63` | `'ROADMAP.md',`（SCOPE） | `'internal/ROADMAP.md',` |

## 3. 找不到读者的规则（每条写搜过的模式）

| 规则 | 搜过的模式 | 结论 |
|---|---|---|
| ① 重写规则 | `git grep -n -E '\b(kernel\|parse\|compile\|bindings\|tools\|lib)/' -- 'lib/**/*.cml'` 再去掉注释行 → 0；`git grep -n 'lib/' -- lib/rewrite/` → 全注释 | **真的没有**：规则按算子 URI 与类型匹配，模块名由「`stdlib` 根之下的相对路径」推出（`kernel/core/context/context.cpp#findUnder`），R2 把 `<模>/<模>.cml` 摊平也没有改变相对名 |
| R10 `assets/` | `git grep -n '"assets/"' -- .` → 只有 `test/plan-selection.json:602` 一处规则名；`git grep -n 'assets/' -- scripts tools pypi cmake` → 0 | **真的没有**：它不走，所以没有读者要改（但 §2 R6 那一行要跟着 `logo.png` 改） |
| ⑩ CI 工作流 | `git grep -n -E '\b(kernel\|parse\|compile\|bindings\|tools\|lib)/' -- .github` → 13 行 | **13 行全是误命中**：`.github/workflows/build-pypi.yml:459,532,553` 的 `hello.cml` 是 `$RUNNER_TEMP` 里现写的程序；`.github/workflows/build-pypi.yml:468` 的 `lib/python*/site-packages` 是安装出来的目录 |
| R1 的 `parse/` | `git grep -n -E '(^\|[^A-Za-z0-9_./-])parse/' -- parse` → 0 | **块内零读者**：`parse/` 只在 `scripts/check-grammar.mjs`、`scripts/camel-for-ai.mjs` 与文档里被引用（include 根是 `parse/include`，靠 CMake 改，不靠源码改） |
| R5 在测试里 | `git grep -n 'hello\.cml' -- test` → 只有 `ast_hashes.txt` 3 行 | **没有用例加载它** |
| R6 `logo.png` | `git grep -n 'logo\.png' -- scripts tools pypi cmake test` → `test/plan-selection.json:602` 一处 | **没有代码读它**（打包也不拷它） |

## 4. 新读者（今天没出现、这一次会出现的）

1. **`CMakeLists.txt:42` 的 `"${PROJECT_SOURCE_DIR}/src/*.cpp"` 是睡着的雷。** 今天 `src/` 不存在，`file(GLOB_RECURSE _ALL_SRC …)` 只吃到 `third_party/*.cpp`；搬家当天它开始吃 `src/**/*.cpp`，`LIBCAMEL_SRC` 会把每个块的源码再收一遍 → 重复符号。**这是本次最可能把构建打断的一处。**
2. **`test/tools/check-no-algo.mjs:62` 与 `scripts/check-std-includes.mjs:45` 的「探到才算」写法会静默空扫。** 前者探 `lib/algo`、`stdlib/algo.cml`、`modules/algo`，搬家后三处都不是标准库所在；后者 `.filter(existsSync)` 在四个根都不存在时返回 `['tools']`，再搬一次就是空集，**退出码仍是 0**——「什么都没查」当成了「查过没问题」。
3. **`scripts/plan-inventory.mjs:319-330` 的 `catch { return }`** 同病：注释写的是「a naming root this checkout does not have」，于是根消失不报错。`gate.plan_inventory` / `gate.case_ledger` 今天正是死在这个根上，改成跳过之后，下一次的「根写错」不再现形。
4. **`pypi/camel-lang/src/camel/` 是今天树里唯一的 `src/`**，`pypi/camel-lang/MANIFEST.in:1` 写 `recursive-include src/camel/.cmlenv`。任何「`src/` 已存在吗」的判断、或全树 `src/` → `src/src/` 的改写，都会在这里翻车。
5. **`scripts/camel-for-ai.mjs:92-94` 的三对候选**（`'parse/include/…'` 与 `'include/…'`、`'src/parse/syntax/parser_expr.cpp'`）是「两代布局都试」的写法；搬家后三个候选**一个都不存在**（真路径是 `src/parse/include/…` 与 `src/parse/src/…`），这种"再补一个前缀"的修法必错。
6. **全树 2,724 行已经在说 `src/…`**，且指的是**第一次搬家之前**的布局（例：`compile/include/camel/compile/gir/graph_read.h:335` 引 `src/compile/gir/graph_encode.cpp`）。`--citations` 今天全绿（`2640 path citation` / `1091 symbol citation`）是因为它们没有反引号、不被当引用；搬家后它们长得像新树而指不到任何文件，**下一次 `--citations` 会把它们与被改坏的引用混在一起**。

**会咬人的是第 1 与第 5**：一个在配置期就炸（好查），一个在生成器里悄悄用错来源（不好查）。

## 5. 验收顺序（建议）

1. `node scripts/check-doc-links.mjs`（默认 SCOPE）→ 0；
2. `node scripts/check-doc-links.mjs --citations` → 今天 `2640`；搬家后应仍是 `2640` 减去被删文件（`logo.png`、`export_matrix.*`、`lib/**/signatures/**`）的引用数，**不是重跑一遍就绿**；
3. `node scripts/check-ast-hashes.mjs` → 55 行（52 `lib/` + 3 `hello.cml`）改写/重算后 0 红；
4. `node scripts/plan-inventory.mjs --check`、`node test/tools/check-pass-declarations.mjs`、`node scripts/check-std-includes.mjs` → 三个都会在根写错时**静默变绿**，要同时看它打印的扫描文件数；
5. 从零构建 + `npm run out` → 安装里的 `stdlib/` 与 `.cmo` 集合和搬家前逐名一致。