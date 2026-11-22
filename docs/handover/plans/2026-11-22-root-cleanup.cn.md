# 根目录整理（讨论稿，暂缓，未派工）

依据：公开仓库的组织（`docs/design/public-repo/README.cn.md`，已定 2026-10-22）第 2 节与第 6 节 I1、I2——当时定了却一直没做；源码布局以核心抽象第 9 节为准（`kernel/ parse/ compile/ lib/ tools/`，取代该稿第 2、3 节里的 `src/ include/ stdlib/`）。负责人 2026-11-22："根目录确实太混杂了……需要进一步精简"；"倾向于 src 包裹我们自己的源码，因为有针对这个的代码行数统计"。

## 整理后的根目录

```
README.md  LICENSE  CONTRIBUTING.md  CODE_OF_CONDUCT.md  SECURITY.md  CHANGELOG.md
ABOUT  ZEN  AGENTS.md  llms.txt
CMakeLists.txt  conanfile.txt  package.json  package-lock.json      （及 .github .husky .vscode 与点文件）

src/                                                              我们自己的源码，统计代码量就数这一个目录
    kernel/  parse/  compile/  tools/  bindings/        （tools/ 只放随 Camel 发布的工具）
    stdlib/                                    随发布附带的 Camel 标准库，与安装后的 stdlib/ 一一对应
    lib/<模块>/                                各模块的 C++ 原生实现，编出 .cmo
third_party/  cmake/                                              第三方源码与构建辅助
packaging/                                                        发布配置（PyPI 等）
examples/  benchmarks/  test/  docs/  scripts/                    示例、基准、测试、文档、脚本
internal/                                                         内部协作，公开时整体删除
```

## 逐项

| 现在 | 去处 | 说明 |
|---|---|---|
| `kernel/`、`parse/`、`compile/`、`bindings/` | `src/` 下同名目录 | 每块内部结构不变；包含根、构建目标、所有权表、违例清单、文档引用跟着加 `src/` 前缀 |
| `tools/` 里随 Camel 发布的七个：`camel-cli`、`debugger`、`format`、`profiler`、`lang-server`、`codegen`、`camel-cpp` | `src/tools/` | 产品源码，计入代码量（负责人 2026-11-22） |
| `tools/` 里开发用的三个：`parsecheck`、`diff-gate`、`graph-diff` | `test/tools/` | 只给开发与测试用；根目录不再有 `tools/` |
| `editors/vscode/` | 删除 | 插件有专门的仓库（负责人 2026-11-22："删了吧"）。同批改掉依赖它的：`test/plans/feat/tools/grammar.plan.toml` 删除；`scripts/context-keywords.mjs`、`scripts/install.js`、`scripts/source-package.mjs`、`test/tools/tree-guard.mjs` 去掉对它的引用；`docs/editor-support.*` 改为指向插件仓库，`docs/debugging.*` 里的插件安装步骤同样改指 |
| `lib/<模块>/` 里 Camel 写的部分：`.cml` 声明与定义、规则集、签名表 | `src/stdlib/`，排成安装后 `stdlib/` 的样子 | 负责人 2026-11-22：`stdlib` 是随 Camel 发布附带的标准库目录，Camel 运行与测试时看到的就该是它，不能让它到 `lib` 里去翻。打包改为原样拷贝 `src/stdlib/` 加上编好的 `.cmo`；`scripts/collect-out.js` 里按模块挑文件、铺平、特例（规则集、签名表）的那段拼装删掉 |
| `lib/<模块>/` 里的 C++ | `src/lib/<模块>/` | 目录名与 `src/stdlib/` 里的模块一一对应 |
| `hello.cml` | `examples/hello.cml` | |
| `logo.png` | 删除 | 与 `assets/logo.png` 逐字节相同；引用改指 `assets/` |
| `assets/` | 留在根目录 | README、打包、宣传都用，不只是文档（待负责人确认） |
| `export_matrix.csv`、`export_matrix.md` | 不入库 | 生成物：生成它的脚本输出到 `out/`，引用它的文档改为"运行某命令生成" |
| `changes.log` | `CHANGELOG.md` | 按 Keep a Changelog；`agents/branching.md` 与发布脚本跟着改 |
| `ROADMAP.md` | `internal/ROADMAP.md` | 内容是"当前这一轮开发的目标与大纲"，属内部协作 |
| `REPRODUCE.md` | `internal/REPRODUCE.md` | 论文复现，属内部 |
| `promo/` | `internal/promo/` | 宣传片源码 |
| `exemplars/` | `internal/exemplars/` | 第 6 节 I1：论文评估材料 |
| `agents/` | 留在根目录 | 给维护仓库的 AI 看的工作规则，与 `AGENTS.md` 一起（负责人 2026-11-22） |
| `showcase/` | 这次不动 | 示例要按官方示例计划重新整理，单独做（负责人 2026-11-22） |
| `pypi/` | `packaging/pypi/` | 发布配置不是源码，不进 `src/`；以后其他发布渠道的配置也放 `packaging/`（负责人 2026-11-22） |
| `benchmarks/` 里的论文评估脚本（e*/d*） | `internal/benchmarks/` | 第 6 节 I1；对外有意义的基准留下 |
| `test/` | 不改名 | 原稿写 `tests/`；改名牵动全部脚本、计划与文档，收益只是一个字母，不做 |

## 依赖原有结构、要同批改的

- **打包**：`packaging/pypi/` 的 wheel 内容与 `_native.py` 找 `stdlib/`、`libs/` 的逻辑，按新的安装布局核对；`pyproject.toml` 里写的目录跟着改。
- **运行与测试**：开发构建与测试运行时，标准库搜索路径直接指向安装出的 `stdlib/`（来自 `src/stdlib/` 原样拷贝加 `.cmo`）；`CAMEL_STD_LIB` 的用法（如 `test/tools/check-vjp-ruleset.mjs`）照旧可用。
- **其余读路径的**：所有权表、违例清单、`test/plan-selection.json`、各检查脚本、文档引用、`.github` 工作流。

## 做法

- 用搬家脚本同一套办法：一张对照表、一笔只做 `git mv` 的提交、一笔跟改所有引用（脚本、计划、文档链接、`README`、`.github` 工作流、`.gitignore`）。
- 验收：`node scripts/check-doc-links.mjs` 无断链；快层全绿；CI 五腿全绿；`git ls-files` 里根目录条目与上面的清单逐项一致。
- **要开一次搬家窗口**：源码收进 `src/` 会动每个代码文件的路径。做法同阶段 1：开着的分支先合或暂停，合并位跑脚本（前缀改写，比上次简单），比对前后读数逐条一致，CI 五腿全绿再关窗。时机：算子记录返工合入之后、阶段 3 开工之前——之后车道更多，代价更大。
- `docs/design/public-repo/README.cn.md` 第 2 节的目录树同步改成上面这棵，第 3 节注明"源码布局以核心抽象第 9 节为准"；核心抽象第 9 节的路径加 `src/` 前缀。
