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

## 1.5 把 `capture_ports` 拆成两类（外加报出一个判不出来的）

作者裁定（`7b04394b3`，原文第 4 条）：**生成器跨 `yield` 保存自己的局部状态，是生成器的实现，不是
`var` 的语义**；在实现换成「生成器状态作为值在拉取之间传递」之前，内部仍可借用单元，**但这些单元
不经捕获口暴露给闭包，也不计入「按引用捕获」的剩余数**，合并时把两类分开报。因此脚本把
`capture_ports` 拆成三个桶，判据**只看捕获口所在的图**：

| 名字 | 判据 |
|------|------|
| `capture_ports_generator` | 口所在的图是生成器机器自己造的：图名以 `__gen_` 开头（机器自己的 `__gen_step__`，`src/compile/gct/builder.cpp#Builder::visitGeneratorBody` 里的 `stepName`），**或**该图的体调用了一个 `__gen_*` 图 |
| `capture_ports_closure` | 其余图里的口：程序写的闭包按引用捕获。**这才是「目标 0」要归零的那个数** |
| `capture_ports_unknown` | 判不出来的：文档没打印图名，或一个 `__gen_*` 名字的口坐在两个标记都不认的图里（两个信号互相矛盾）。单独报，不塞进任何一类 |
| `capture_ports` | 合计，口径不变，便于与历史数对账 |

**为什么不按口名判。** 生成器体自己的局部跨 `yield` 会保留源码拼写（`i`、`e`、`x`），按口名判会把这些
格子留在闭包那类，只把 `__gen_pc__`/`__gen_halt__` 之类搬走——那是按拼写拆，不是按口是什么拆。图是编译器
造的东西：`__gen_step__` 只是生成器自己作用域里的绑定，程序写不出解析到它的名字，所以「该图的体调用
`__gen_*`」只可能是 `visitGeneratorBody` 的 `streamValue()` 造的那个闭包（`return __gen_step__()`，即一次
拉取的流值），也就是「某个生成器体的直接产物」；反过来，程序写的闭包不可能是这种图。

**三个桶必须相加等于 `capture_ports`**：两个总和各自独立数（合计走原来那趟遍历，拆分走第二趟），不闭合
就退出 2 并说明差在哪，不打印一个不闭合的数。拆分的两趟是刻意分开的——同一趟里累加的拆分永远不会不闭合。

**它不做的事**：它不判「某个口该不该是按引用的」。生成器体里**程序自己写的**闭包（
`test/cases/compile/stream/generators.cml` 里 `reading()` 的 `let read = () => x`）按图被判进闭包那类，
哪怕它捕的格子是生成器状态：那正是作者说「不该存在」的形状，得先被规则消掉，不该在数里被藏起来。

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

拆数之后（2026-11-16，`1c7f70705`，同一台主机、同一个 `out/latest`、`--jobs 6`）。这一列是「拆」的
读数，`capture_ports` 与上面那份口径相同，只是树前进到了 `1c7f70705`：

| scope | files | capture_ports | generator | closure | unknown | list_sha |
|-------|-------|---------------|-----------|---------|---------|----------|
| `test/cases/**`（默认） | 3848 | 379 | 300 | 79 | 0 | `e0c6dff38c04…` |
| 全仓（`--all`） | 4131 | 391 | 312 | 79 | 0 | `2faf598fb4ec…` |
| `--path test/cases/compile/stream` | 83 | 292 | 291 | 1 | 0 | `0b0d8ff53c61…` |
| `--path test/cases/compile/function_values` | 13 | 5 | 0 | 5 | 0 | `94188a1c8963…` |
| `--path test/cases/linear/byref` | 44 | 11 | 0 | 11 | 0 | `53ed7d0195e5…` |

五个 scope 全部闭合（`generator + closure + unknown == capture_ports`）。默认 scope 同一棵树跑两次
整份输出逐字节相同（`cmp` 通过，`sha256(6936f9bf…)`）；`capture_ports`、`files=3848` 与 `list_sha`
在没有这次拆分的旧脚本上读出来是同一串（`e0c6dff38c04…`），也就是**这次只把一个数拆成三个，分母与口径
都没动**。

拆开的读数是这份报告里最要紧的一句：**379 个按引用捕获口里，300 个是生成器机器自己造的闭包的**——它们
坐在 `streamValue()` 造的那个流值闭包里，捕的是 `__gen_pc__`/`__gen_halt__` 与生成器体自己的局部
（`i`、`x`、`n`、`__gen_for_seq_0`、`__gen_match_subject_2` …）。按引用的**程序闭包口只有 79 个**。
也就是说：作者说「这些单元不经捕获口暴露给闭包」——对**程序**闭包，基本成立（79 个里只有 1 个例外，
见 §1.5 末尾那条 `generators.cml#reading`）；但**它们确实进过 `capturePorts` 表**，作为机器自己那个
闭包的口。这不是口径错误，是实现的现状：生成器状态今天就是靠一个编译器造的闭包在两次拉取之间传递的。
要让它从 `capturePorts` 里消失，得等实现换成「生成器状态作为值在拉取之间传递」，不能靠脚本改口径。

### 3.1 合并读数（2026-11-17，`10b73723d`，同一台主机，`--jobs 8`）

合并 `lane-e9crash` 之后的一次读数，命令就是闸门的 `-Check capture-census`：

| scope | files | counted | refused | var_new | var_new_empty | capture_ports | value | total | cell_typed_nodes | list_sha |
|-------|-------|---------|---------|---------|---------------|---------------|-------|-------|------------------|----------|
| `test/cases/**`（默认） | 3874 | 2709 | 1163 | 216 | 41 | 170 | 2256 | 2426 | 1177 | `6a5b52d04d89…` |

拆数闭合：`104 + 66 + 0 = 170`（`generator` 104、`closure` 66、`unknown` 0），`capture_ports_untyped`
是 0。`outcomes: ok=2709 refused=1163 crashed=2 timeout=0 unreadable=0`：那 2 个没给出图的文件是
`test/cases/opt/e9/` 里按缺陷留着的复现程序（`rc14_seed176` 与 `rc20_seed262`，两者都停在图构造器
里），脚本因此以退出码 2 说「a census over a set it lost files from is not a census」——它不拿一份
少了文件的清单当普查。

**与作者的预测对不上，原样记下**：作者说 `var_new` 在 S1 应 293、在 S2 应 275，这一次实测是 216。
预测与实测不同，差异不解释掉、也不去挑一棵对得上预测的树：`293`/`275` 是预测，`216` 是在
`list_sha=6a5b52d04d89…` 这份语料上的实测。路上测过的数是 228（`3ae5faa53`）、以及协调者记录里的
263（撤回的那条路上读到的）、244、200，和这一次的 216；每棵树不同，而 `list_sha` 只在语料相同时才
让两个数可比。

### 3.2 又一段读数（2026-11-17，`19b14ba0f`，同一台主机，`--jobs 8`）

`lane-s3b` 把两个 case 加进了 `test/cases/`（`consistency/new_values/two_names_for_a_struct.cml`、
`modules/nn/sgd_leaves_its_argument.cml`），scope 从 3874 涨到 3876，分母换了，`list_sha` 也换了。
**两份读数的语料不同，不许写成「216 → 219」**，各报各的：

| scope | files | counted | refused | var_new | var_new_empty | capture_ports | value | total | cell_typed_nodes | list_sha |
|-------|-------|---------|---------|---------|---------------|---------------|-------|-------|------------------|----------|
| `test/cases/**`（默认） | 3876 | 2711 | 1163 | 219 | 41 | 170 | 2256 | 2426 | 1184 | `c43589ff653d…` |

拆数仍闭合（`104 + 66 + 0 = 170`，`capture_ports_untyped=0`），`outcomes: ok=2711 refused=1163
crashed=2 timeout=0 unreadable=0`。`capture_ports` 与 §3.1 那份相同（170），但这一栏不能说成「没动」：
两个新 case 本来就不带按引用捕获口，而分母不同，两份数各说各的。

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
把它自己的摘要行归约成 `capture_ports` 的读数（`findings`）。拆数之后摘要行在这一句后面带着两个类
（`379 reference capture port(s) (generator 300, closure 79, unknown 0), …`），**`findings` 仍然只归约
到第一个数（`capture_ports`），所以这一行与历史读数对得上；两个类在它前面的 `capture_ports=` 那行与
`capture_ports_split` 那行里各自成字段。** **现在不判红**：作者的目标是 0/0，而树里还没有基线，判红等于
凭空造一个阈值。脚本里的 `--enforce` 就是读数到 0/0 之后这一项该变成的形态；它问的已经是
`capture_ports_closure`（程序闭包那类），不是生成器自己的状态。

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
- 它不判语义，也不判断某个捕获口「该不该」是按引用的——那是作者的裁定，这个脚本只负责把数数准；
- 拆出的两类是**按图**分的，不是按「这个格子是不是生成器的」分的：一个口坐落在程序写的图里，就进闭包那
  类，哪怕它捕的格子是生成器状态（`generators.cml#reading` 的 `x`，79 个里的 1 个）。要按「这个格子属于
  谁」来分，需要一条跨图的对应关系（机器图的港口名 ↔ 程序图的港口名），那是一份新的读数，不是这次拆数。