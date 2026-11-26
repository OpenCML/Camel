# 命令行重构：施工计划

设计见 `docs/design/cli/README.cn.md`（负责人 2026-11-26 同意由决策者设计，不计兼容）。旧写法直接删除：删掉之后就是"未知选项"，退出码 1，不留指向新写法的提示。

## 依赖

- 遍名与源码同一解析（`internal/plans/2026-11-26-pass-as-function.cn.md` 第 1 步）落地之后，才删 `--input`、`--passes`。
- `--result` 与 `main` 的新规则（`internal/plans/2026-11-26-main-effects.cn.md`）同一批落地：那份计划落运行时与 `exit` 时，`-o` 直接改名为 `--result`。

## 顺序（每步一笔可合）

1. **进程级的改名与合并**：`--diag-format`、`--max-warnings`、`--gc k=v,…`、`--spaces k=v,…`、`--no-cache`；信息类改子命令（`docs`、`about`、`zen`、`explain`）。改 `src/tools/camel-cli` 的选项表与 `--help`，同一笔改全仓测试计划里的 `args`（脚本一次性改完，脚本不进树）与 `test/tools/*`、`internal/tools/*` 里拼 camel 命令行的地方。
2. **遍参数归位**：`--max-macro-depth/rounds` 成为 `std::macro` 的 `with` 参数（在 `src/stdlib/std.cml` 声明）；删 `-S`、`-P`、`--profile-mode`；调度遍只留短名，`std::default` 成为转发声明并按设计原则 9.2 指向 `std::fvm`（若这一改动让快层读数变化，先报决策者）。
3. **`bench` 子命令**：`--timeit` 改成 `camel bench`，输出格式与今天 `--timeit` 相同（文本与 `--diag-format json` 之外单独的 `--json`），`test/tools` 里调用它的地方随之改。
4. **`check` 与 `inspect`**：`check --stage`、`--report`、`--rules`、`--no-rules`；`inspect` 改成阶段名作参数，删去与遍重复的那些转储（先把测试计划里用到它们的条目改成对应的遍）。
5. **删 `--input`、`--passes`**（依赖遍名解析落地）。
6. **文档**：`docs/cli.md` 与 `docs/cli.cn.md` 按设计重写（只写现行的，不写变更史），`--help` 文本与之逐项一致。

## 验收

- `camel --help` 列出的选项与设计稿逐项一致；每个删掉的旧选项各一条负例：退出码 1、诊断是"未知选项"。
- 迁移前后的快层与合并闸门：判词逐名相同（只改了写法，不改行为）；逐名比新增红 0。
- `git grep -n -E -- '--passes|--input |--max-macro-depth|--scheduler|--timeit|--output-format|--error-format|-E |--gc-nursery-mb|--block-limit' -- test internal/tools scripts docs` 只剩设计稿本身。
