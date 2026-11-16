# 捕获普查：`:var/new` 节点与按引用捕获口

作者用两个数验收闭包捕获改造完成，目标是**两个都到 0**：`:var/new` 节点数、按引用捕获口（capture
ports）数。这两个数不在 `std::stats` 里，所以此前每条车道都自己写一遍临时扫描、自己定一套文件集合，
结果就是「410 → 408 / 621 → 604」这类读数今天谁也复核不了，连它带的文件数（5687）也和仓库对不上。
本页记录 `scripts/check-capture-census.mjs`：口径写在树里、读数带着分母的指纹。

## 1. 口径

每个文件用 `std::jgir` 翻译一次，读它的版本化 JSON GIR（`format: camel.gir`、`version: 1`）：

| 名字 | 定义 | 读的是 |
|------|------|--------|
| `var_new` | `label` 恰为 `:var/new` 的节点 | 每个图里每个节点的 `label` |
| `var_new_empty` | `label` 恰为 `:var/new/empty` 的节点 | 同上（另一个算子，不并进 `var_new`） |
| `capture_ports` | 捕获口里 `outputType` 是 cell（`var T`）的那些 | 每个图的 `capturePorts` 表 + 该口节点的 `outputType` |
| `capture_ports_value` | 捕获口里带值（不是 cell）的那些 | 同上 |
| `capture_ports_untyped` | 文档没打印 `outputType` 的口，单独计数而不是塞进上面两栏 | 同上 |
| `cell_typed_nodes` | 所有图里 `outputType` 是 cell 的节点 | 每个节点 |

「按引用」的判据是**捕获口自己带的类型**：`var int` 是 cell，口上带的是 cell 的身份，闭包里的读看到
后来的写；`int` 是值，闭包拿到的是造闭包那一刻的值。三个聚合数分别对应三种写法，`capture_ports` 就是
作者说的那个数。

`std::jgir` 而不是别的：`std::stats` 的 `GRAPH_STATS` 只有合计（graphs/nodes/edges/opers/calls/
closures/branches），没有按算子的拆分，也没有捕获表；`std::gir` 是 DOT，算子在节点的属性串里、口的
类型是样式，读它等于解析打印器的排版。JSON GIR 有 `label`/`kind`/`outputType`/`parameter` 与
`capturePorts` 表，脚本先断言 `format`，不是就退出 2，而不是在没读懂的文档上数出 0。

被编译器拒的文件（`test/cases` 里大量 `err_*.cml` 是**必须被拒**的用例）不进分子，只报数量：
`outcomes: ok=… refused=…`。这是口径的一部分：分子是「树建出来的图」，被拒的程序没有图。

## 2. 分母：scope 与 `list_sha`

- 默认 `scope`：`test/cases/**/*.cml`，按路径排序（计划按其目录选用例；没有哪个计划条目写 `.cml`
  路径，所以 `test/plans/**` 不会带来这套清单之外的用例）。
- `--all`：仓库里每个 `.cml`，走的正是 `test/tools/parsecheck/corpus.py --scope repository` 的排除
  规则（同样 `SKIP_DIRS`、同样排除 `docs/handover`）。
- `list_sha`：把每个文件算成一行 `<内容 sha256>  <相对路径>`，按路径排好，再对整个清单做一次
  SHA-256。增删改名改内容都会变，同一棵树跑两次不变。**没有它的数就是没有分母的数。**

## 3. 读数（2026-11-16，`48e12d4b8`，主机 `out/latest`，`--jobs 8`）

| scope | files | counted | refused | var_new | var_new_empty | capture_ports | value | total | cell_typed_nodes | list_sha |
|-------|-------|---------|---------|---------|---------------|---------------|-------|-------|------------------|----------|
| `test/cases/**`（默认） | 3846 | 2684 | 1162 | 343 | 44 | 379 | 2126 | 2505 | 1907 | `0fa49c90f684…` |
| 全仓（`--all`） | 4129 | 2886 | 1243 | 356 | 48 | 391 | 2289 | 2680 | 1976 | `5e2e7403174a…` |

两次同树全量跑的整份输出逐字节相同（`cmp` 通过，`sha256(f4f58fd2…)`）。`capture_ports_untyped`
两次都是 0。

## 4. 全量 5689 与子集的差

同一棵树（`48e12d4b8`）上 `corpus.py --scope repository` 的读数是 **repository 4129 / documents 1560
/ total 5689**，本机 Windows（Python 3.12.1）与主机（python3 3.10）一致；脚本自己走同一套规则重数一遍
也是 4129 + 1560。所以：

- `outside_scope=283`：全仓 4129 个 `.cml` 里，283 个不在 `test/cases/**` 内（`benchmarks/`、
  `examples/`、`exemplars/`、`stdlib/`、`tutorial/` 等），前几个是
  `benchmarks/common/camel_bench.cml`、`benchmarks/e10_first.cml`、`benchmarks/e10_second.cml`；
- 1560 个 Markdown 代码块**不是**文件，不参与编译，只在分母里报出数量；
- **5912 复现不出来**：4352 + 1560 里的 4352 比今天的 4129 多 223 个 `.cml`，任何一棵本机工作树都
  不是这个数；而前一条车道报的 5687 正好比今天的 5689 少 2 条（此后新增的用例），说明那一份读数
  其实是同一套规则、更早的树。
- **410 / 621 也复现不出来**：本脚本在全仓是把 `:var/new` 记成 356、把按引用捕获口记成 391。两个数
  不是「口径不同」能对上的，是那两次读数来自别的树、没有留下文件和内容清单。**换一棵树这两个数就
  该变；`list_sha` 就是让「变了多少」可被追问的东西。**

## 5. 闸门里的位置

`internal/tools/camel-remote.ps1` 的 `$checkers` 里有一项 `capture-census`
（`reportOnly = $true`、`compiler = $true`）：`-Check capture-census` 在本轮刚构建的 install 上跑它，
把它自己的摘要行归约成 `capture_ports` 的读数（`findings`）。**现在不判红**：作者的目标是 0/0，而树里
还没有基线，判红等于凭空造一个阈值。脚本里的 `--enforce` 就是读数到 0/0 之后这一项该变成的形态。

它跑在 `-Check all` 里，代价是一次 `test/cases` 全量翻译：主机 32 核、`--jobs 8`、负载 15 的机器上
约 20 秒，两次同树逐字节相同。

## 6. 这份读数看不到什么

- 它数的是**图**，不是源码文本：加一行没人读也没人写的 `var x = 1` 不改 `var_new`（无写的 `var`
  被折成值，不建节点）；在 `main` 里加一个被写的 `var`（`var p = read_c(1)` 后跟 `p = p + 1`）则
  `var_new` 343 → 344、`cell_typed_nodes` 1907 → 1909，`capture_ports` 不动；删掉一个贡献了 1 个
  `:var/new`、2 个按引用捕获口的用例，则正好各减 1 和 2。三种改动都改 `list_sha`，数字只在图真的
  变了时才动；
- 它只读当前这棵树：库里没有历史读数，`410 → 408` 这类「降了 2」需要两次各自带 `list_sha` 的读数才
  能成立；
- 它不判语义，也不判断某个捕获口「该不该」是按引用的——那是作者的裁定，这个脚本只负责把数数准。