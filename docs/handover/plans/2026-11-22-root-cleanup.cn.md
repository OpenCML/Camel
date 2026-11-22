# 根目录整理

依据：公开仓库的组织（`docs/design/public-repo/README.cn.md`，已定 2026-10-22）第 2 节与第 6 节 I1、I2——当时定了却一直没做；源码布局以核心抽象第 9 节为准（`kernel/ parse/ compile/ lib/ tools/`，取代该稿第 2、3 节里的 `src/ include/ stdlib/`）。负责人 2026-11-22："根目录确实太混杂了……需要进一步精简"。

## 整理后的根目录

```
README.md  LICENSE  CONTRIBUTING.md  CODE_OF_CONDUCT.md  SECURITY.md  CHANGELOG.md
ABOUT  ZEN  AGENTS.md  llms.txt
CMakeLists.txt  conanfile.txt  package.json  package-lock.json      （及 .github .husky .vscode 与点文件）

kernel/  parse/  compile/  lib/  tools/  third_party/  cmake/     源码与构建
bindings/  editors/                                               宿主语言与编辑器
examples/  benchmarks/  test/  docs/  scripts/                    示例、基准、测试、文档、脚本
internal/                                                         内部协作，公开时整体删除
```

## 逐项

| 现在 | 去处 | 说明 |
|---|---|---|
| `hello.cml` | `examples/hello.cml` | |
| `logo.png` | 删除 | 与 `assets/logo.png` 逐字节相同；引用改指 `docs/assets/` |
| `assets/` | `docs/assets/` | |
| `export_matrix.csv`、`export_matrix.md` | 不入库 | 生成物：生成它的脚本输出到 `out/`，引用它的文档改为"运行某命令生成" |
| `changes.log` | `CHANGELOG.md` | 按 Keep a Changelog；`agents/branching.md` 与发布脚本跟着改 |
| `ROADMAP.md` | `internal/ROADMAP.md` | 内容是"当前这一轮开发的目标与大纲"，属内部协作 |
| `REPRODUCE.md` | `internal/REPRODUCE.md` | 论文复现，属内部 |
| `promo/` | `internal/promo/` | 宣传片源码 |
| `exemplars/` | `internal/exemplars/` | 第 6 节 I1：论文评估材料 |
| `agents/` | `internal/agents/` | 第 6 节 I1；根目录的 `AGENTS.md` 留下并改指新位置 |
| `showcase/` | `examples/models/` | 旗舰模型 |
| `pypi/` | `bindings/python/` | 与 Python 宿主合在一处 |
| `benchmarks/` 里的论文评估脚本（e*/d*） | `internal/benchmarks/` | 第 6 节 I1；对外有意义的基准留下 |
| `test/` | 不改名 | 原稿写 `tests/`；改名牵动全部脚本、计划与文档，收益只是一个字母，不做 |

## 做法

- 用搬家脚本同一套办法：一张对照表、一笔只做 `git mv` 的提交、一笔跟改所有引用（脚本、计划、文档链接、`README`、`.github` 工作流、`.gitignore`）。
- 验收：`node scripts/check-doc-links.mjs` 无断链；快层全绿；CI 五腿全绿；`git ls-files` 里根目录条目与上面的清单逐项一致。
- 由合并位做，不开窗口：这些目录不在任何车道的所有权表里，与阶段 2、3 的代码车道不冲突。
- `docs/design/public-repo/README.cn.md` 第 2 节的目录树同步改成上面这棵，第 3 节注明"源码布局以核心抽象第 9 节为准"。
