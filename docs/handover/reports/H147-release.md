# H147 发布提案：v0.2.24

- 工作树：`D:\Projects\Camel-wt\h147-release-notes`，分支 `handover/H147-release-notes`，起点 `origin/develop` `a99ba4f1e`。
- 日期：2026-10-22。
- 这是**提案**：没有打 tag、没有改版本号、没有合 `main`、没有改 `changes.log`。要作者拍板的两件事在 §0。

## 0. 五分钟结论

| 项 | 值 |
|---|---|
| 上一个发布 | `v0.2.23`，annotated tag `Camel v0.2.23` 指向 `c8299dae4`；发布合并 `e1759261f` |
| 距上个 tag | **554 个提交**（158 个合并提交），`git rev-list --count v0.2.23..HEAD` |
| 本次发布版本 | **v0.2.24** |
| 下一个开发版本 | **v0.2.25** |
| 树里已有的版本 | 已经是 `0.2.24`：`package.json`、`package-lock.json`、`pypi/camel-lang/pyproject.toml`、`pypi/camel-lang/src/camel/__init__.py`、`tools/camel-cli/config.h`（`74394322f chore: update to version v0.2.24`） |
| 发布后 `changes.log` 的开头 | `[0.2.24]` 已经存在（同一次 bump 写的），现在有 22 条内容；本提案替换这 22 条，见 §2 |
| 门禁 | 见 §4：`smoke` + `feat` 快层、`--verify-ops`、`--verify-doc`、`diagnostics-catalog --check`、`check-doc-links --all` |
| 判定 | **现在不建议直接发**。快层 7 红里：3 条是 `identity.mjs` 的过期摘要（测试快照），1 条是 `math::` 的编辑器补全为空（用户可见回归），1 条是编译基准的模块数 4 → 6（工具期望过期），1 条是负载下的间歇（两次复跑都通过），1 条是 slow 层的调试器用例（快层按目录过滤时本不该跑，命名计划文件才会跑，且确定性失败）。把前两条真回归修掉，或作者明确接受，再发 |

需要作者确认的两件事（`agents/branching.md` 的硬要求）：

1. 发布版本 **v0.2.24**，下一个开发版本 **v0.2.25**。
2. §2 那段 `changes.log` 文本。确认之前不写进树。

## 1. 版本与规则

### 1.1 规则（原文）

`agents/branching.md` 第 37–45 行：

> Before merging `develop` into `main`, explicitly ask the user to confirm:
> - the release version number to publish now
> - the next development version number after release
>
> The usual version progression is to increment the trailing component by 1, for example `v0.1.22 -> v0.1.23`, but you must still ask the user to confirm.

同文件第 49–56 行（发布后的 bump）：

> 1. Switch to `develop`.
> 2. Rebase `develop`.
> 3. Run `npm run setver x.x.x`.
> 4. Run `npm i`.
> 5. Commit the version bump immediately with: `chore: update to version vx.x.x`

`changes.log` 本身也把版本写成连续的三段号：`[0.2.20]`、`[0.2.21]`、`[0.2.22]`、`[0.2.23]`，最近四次发布都是末位 +1（`git log --oneline --grep="update to version"`：`v0.2.20` … `v0.2.24`）。

### 1.2 应用

- **本次发布 `v0.2.24`**。树里五个版本文件已经是 `0.2.24`；tag 必须落在同一个号上，否则就要先 bump（本任务明令不做）。`v0.2.23 → v0.2.24` 也是"末位 +1"。
- **下一个开发版本 `v0.2.25`**。发布后在 `develop` 上 `npm run setver 0.2.25`；`scripts/setver.js` 会同时改上述五个文件，并在 `changes.log` 顶部插入新的 `[0.2.25]` 空节（`scripts/setver.js:38-44`）。
- **严格 semver 的话这一版是破坏性的**（模块改名、`str::bytes` 删除），按 semver 应进 minor（`0.3.0`）。项目不用这条规则：`0.2.x` 是发布序数，规则与历史都是末位 +1。我按项目规则提 `0.2.24`；如果作者要表达破坏性，唯一候选是 `0.3.0`，但那要改版本文件，不在本提案（不 bump）之内。

## 2. `changes.log` 提案

### 2.1 树里现在有什么

`develop` 的 `[0.2.24]` 节不是空的：`0eee284bd docs(changes): draft the 0.2.24 release notes` 加了 12 行，`a5a24db41 chore(release): the 0.2.24 notes cover what the last cycle changed` 又加了 9 行，共 22 条（`changes.log:1-22`）。那一版按 `a5a24db41` 自己的说明，是"这一轮落地前的草稿"，现在有三个问题：

1. **重复上一版**：第 3、4 条（"分支臂与循环在书写处构建"、"每个后端在函数的帧里跑循环的轮次"）与 `[0.2.23]` 的第 3、4 条逐字相同；第 9 条（"离散值的导数按定义为零"）与 `[0.2.23]` 第 5 条相同。这些已经是 v0.2.23 用户拿到的内容。
2. **写进了用户看不见的内部工作**：内存/GC 压力、陈旧提交并入、特化表复用、JIT 只命名仍持有的图版本、规则目录重新生成。
3. **漏掉这一轮最大的用户可见变化**：整个 H19 标准库迁移——模块改名、`str::bytes` 删除、声明模块、资源名——22 条里一条都没有。

所以下面这段是**替换**，不是追加。

### 2.2 待粘贴文本（`[0.2.24]` 节全文）

```text
[0.2.24]
Changes:
- 标准库改名：`sys` 是 `env`、`os` 是 `process`、`crypto` 是 `hash`、`tree` 是 `ckpt`；旧名不再是模块，导入它只得到普通的 `ModuleNotFound`，没有别名，也没有指向新名的提示。
- 终端算子在新的 `io` 模块：`set_terminal_raw_mode`、`has_input`、`get_char`、`get_chars`、`clear_input_buffer`；改名后的 `process` 只留 `exit`、`sleep`、`whoami`、`cpu_count`。
- `str::bytes`/`str::from_bytes` 删除：字符串与 UTF-8 字节之间只经 `bytes::encode`/`bytes::decode`；非法 UTF-8 现在是 `decode` 的 `EncodingError` 错误值，不再是 `from_bytes` 的故障。
- 标准库的签名改由 `stdlib/*.cml` 里无函数体的 `export func` 声明给出：`bytes`、`str`、`json`、`math`、`random`、`re`、`fs`、`time`、`io`、`algo`、`statistics` 已迁，只有少数按类型计算的签名仍由 C++ 给出、在声明文件里用 `computed:` 点名；`inspect --ops` 与参考页由声明生成，错元数、错类型在调用处拒绝并列出声明里的签名（含参数名）。
- 资源名按设计原则 3.3 写平：`stdout`、`stdin`、`stderr`、`clock`、`env`、`rng`；`process`、`fs`、`tensor:*`、`tiles:*`、`para:rendezvous`、`io:terminal` 不变。
- 运行期的 `onnx.export_model` 恢复可用：化简在私有副本上做，不再被误报"提交与更新的程序冲突"；只改名的聚合类型转换只标注导出类型，不再生成算子。
- 数值许可是标签：`@numeric(...)` 删除；`#strict` 只拿走许可、不禁止逐位等价的改写（需要 `reassociate` 的重结合仍被拒，`x + x` 换成 `x * 2.0` 照常命中）；`rule` 可以带 `when` 条件、用 `#requires<...>` 声明许可。
- `T?` 的期望类型不再漏进操作数：`let o: bool? = true || false`、`let n: float? = ... ?? ...` 可以编译；逻辑操作数仍是 `bool`，`??` 的臂按 payload 取类型。
- 两层标记按最内层写：`T?!` 是 fallible(nullable(T))、`T!?` 是 nullable(fallible(T))，`match` 只拆最外层；语言参考与设计原则已更正。
- 一个声明的标签、许可与读者由同一份事实给出，三者不会再各读一份；读了算子声明的事实随声明纪元过期，过期读作"没有"，重算后校验一致。
- 规则与饱和：运算符声明了交换律时定律按任一操作数顺序匹配；抽取只保留有副作用、承载顺序与被写入的节点，饱和造出的死写法不再留在结果里。
- 数组可以切片、按下标数组取元素：`xs[a:b]`、`xs[:]`、`xs[idx]`（`take`）；切片共用数据块、左闭右开，`xs[-1]` 是 `IndexOutOfRange`（不从尾部数，最后一个元素写 `last(xs)`），步长不为 1 与多维视图仍点名 H17。
- 泛型第一期：`match type` 按结构选择类型分支，`static for`/`static loop` 在编译期展开，泛型实例的返回类型由实参推断。
- `macro` 修饰符换成声明标签 `#macro`；求值失败按遍运行中的故障处理（`pass:std::macro`），不再是编译错误。
- 遍模块有语法错误时该遍失败并给出 `InvalidPassParameter`，进程不再无输出退出。
- `-I`/`--include` 是模块搜索根：入口目录、各 `-I`、`CAMEL_PACKAGES`、安装的模块；从文件加载的模块先在自己的目录里找。
- 新增 `std::profile`；`--timeit` 失败时指出第几轮；`--devices` 选择设备。
- Tiles：一棵树的叶子作为一批跨到设备；元素类型不同的 `cast_like` 改为拒绝而不是原样返回。
- 循环的一轮在形状允许时编译成原生代码（35.6 → 7.0 ns），解释执行的一轮也不比它模拟的尾递归贵。
- 发布包的指令集基线是 `x86-64-v3`（AVX2 与 FMA）；没有 AVX2 的机器不能启动 wheel。
```

共 20 条。每条都能落到一个用户能执行的动作或看到的结果上；没有"重构了 X"这种句子。如果嫌长，删减顺序建议是：先删第 18、19、17 条（Tiles、性能、CLI 杂项），再删第 13、14 条；第 1–6 条是这一版的主干（改名、`io`、`str::bytes`、声明、资源、ONNX），不建议删。

### 2.3 每条的依据

| # | 条目 | 依据（报告与节） |
|---:|---|---|
| 1 | `sys`→`env`、`os`→`process`、`crypto`→`hash` | `reports/H114.md` §1、§2；§3 记录"旧名是普通的 `ModuleNotFound`、没有别名、没有指向新名的诊断" |
| 1 | `tree`→`ckpt` | `reports/H108.md` §0、§1.2（`5b78a49d1`；checkpoint 格式与公开函数名未动） |
| 2 | 终端独立成 `io`，`process` 只剩四个算子 | `reports/H116.md` §1、§3；`reports/H114.md` §2 |
| 3 | `str::bytes`/`from_bytes` 删除；非法 UTF-8 由故障变错误值 | `reports/H122.md` §1、§2、§3；参考页 `str` 16 → 14 条（`git diff v0.2.23 HEAD -- docs/stdlib-reference.md`） |
| 4 | 声明模块；`inspect --ops` 与参考页由声明生成；错误消息带声明里的参数名 | `reports/H76.md`（机制与 `bytes` 试点）、`H104`（`str`）、`H115`（`json`）、`H113`（`math`）、`H128`（`random`）、`H131`（`fs`）、`H133`（`time`）、`H135`（`algo`、`statistics`）、`H116`（`io`）；`H137` §1 的 `stdlib-declarations --check` 防止迁过的模块退回去，它把"少数按类型计算的签名"限定为 8 条走 `computed:` 的条目（`math` 的 `sqrt`/`shl`/`shr`、`random` 的 `choice`/`sample`/`shuffle`、`json` 的 `encode`/`decode_as`）。错误文本的实证：`test/plans/feat/compile/native_declarations.plan.toml` 的 `native.str_wrong_arity`/`native.str_wrong_type` 要求消息含 `<str:trim>: (s: string) => string` |
| 5 | 六个资源改名，八个不变，`io:terminal` 留待作者 | `reports/H134.md` §1、§3 的资源表 |
| 6 | `onnx.export_model` 恢复；恒等 CAST 不生成算子 | `reports/H112.md` §1、§3、§9；`reports/H41.md` §2（`2e6dcb22`：只改名的聚合类型 CAST 不再计算）。**注意**："同一结果作两个输出时各有名字"的修复 `ce16c788` 在 v0.2.23 的树里已有（`git show v0.2.23:modules/onnx/exporter.cpp` 已含 `appendIdentity`），不写进这一版 |
| 7 | 许可标签、`#strict`、`when`、`#requires` | `reports/H77.md` §1.1（11 个许可标签、`@numeric` 删除）；`reports/H125.md` §0、§2（strict 只拿走许可；`x+x → x*2.0` 逐位相等）；`reports/H2b.md` §1（`#requires`）；`reports/H38b.md`（`rule ... when`） |
| 8 | 可选类型的逻辑与 `??` | `reports/H119.md` §1 的四组作用域与两个复现 |
| 9 | 两层标记 | `reports/H123.md` §1、§2、§3（`int?!`/`int!?` 的诊断与文档更正） |
| 10 | 事实的统一入口与声明纪元失效 | `reports/H2b.md`（事实的统一入口）；`reports/H85.md`（`OperatorTraits` 由语义记录推导、声明标签的运行时查询）；`reports/H121.md` §0、§2（声明纪元使读声明的事实过期，过期读作"没有"） |
| 11 | 交换律匹配；抽取边界 | `reports/H118.md` §0（读算子的 `BinaryLaw.commutative`）；`reports/H63.md` §1（保留集合 = 有副作用/承载顺序/被写入） |
| 12 | 切片与 `take` | `reports/H83.md`；`docs/language/06_expressions.md` §6.3（当前写法与 H17 的两处拒绝）；`test/plans/feat/linear/subscript.plan.toml` 的 `subscript.step` |
| 13 | 泛型第一期 | `reports/H51a.md`（`match type`、实例返回类型）、`H51b.md`（`fields`、`static for`、`t.[n]`）、`H79.md`（推导式）、`H86.md`（`static loop`）、`H96.md`（`indices`） |
| 14 | `#macro` | `reports/H97.md` §1（声明标签、求值失败走 `pass:std::macro`） |
| 15 | 遍模块语法错误 | H36b 的收尾提交 `1e41b4adb`（merge 说明：以前进程无输出退出，现在报 `InvalidPassParameter` 并指出语法错误）；`reports/H36b.md` |
| 16 | `-I` 的搜索顺序 | `reports/H129.md` §1（顺序与 `docs/language/05_modules.md` §5.2 一致；库自己的 import 先看自己的目录） |
| 17 | `std::profile`、`--timeit`、`--devices` | `reports/H48.md`（`std::profile`）；`reports/H66.md`（`--timeit` 的 `iteration`）；`reports/H105.md`（`--devices`） |
| 18 | Tiles 批处理与 `cast_like` | `reports/H49a.md`（一棵树的叶子合批）；`reports/H72.md`（元素类型不同的 `cast_like` 改为拒绝） |
| 19 | 循环与解释器性能 | `reports/H55.md`（一轮 35.6 → 7.0 ns）、`H56.md`（解释执行 0.436 s → 0.28–0.30 s，不比手写尾递归慢） |
| 20 | `x86-64-v3` | `docs/design-principles.cn.md` 9.10（与 v0.2.23 的 diff：`x86-64-v2` → `x86-64-v3`）；作者 2026-10-21 的裁定与提交 `03d1c77b7`（提交说明："A machine without AVX2 will refuse to start the wheel"）；wheel 与三条 CI 腿按同一级别构建 |

### 2.4 旧草案逐条处置

| 旧 `[0.2.24]` 行 | 处置 | 原因 |
|---|---|---|
| 3–4 分支臂/循环区域、各后端在帧里跑循环 | **删** | `[0.2.23]` 已有 |
| 5 ONNX 两个输出各有名字；恒等 CAST | **改写** | 输出命名 v0.2.23 已有；恒等 CAST 并入新第 6 条 |
| 6 `rule` 的 `when`；规则目录重新生成 | **留一半** | `when` 并入第 7 条；"目录重新生成"是内部动作 |
| 7 许可与事实、`#reads<...>` | **改写** | 拆成第 7、10 条；`#reads` 是遍作者可见的写法，但一条里塞不下，放进第 10 条的事实入口 |
| 8 命名类型的运算符随类型走 | **删** | 这条本轮没有用户可见的新行为（设计原则 5.7 的文字是 `[0.2.23]` 之后补的，实现随 H5b 早已在）；要写就是重复 |
| 9 离散值的导数为零 | **删** | `[0.2.23]` 已有 |
| 10 内存/GC 压力 | **删** | 内部，用户看不见 |
| 11 遍模块不解析不再崩溃 | **留** | 第 15 条 |
| 12 陈旧提交并入、JIT 命名图版本 | **删** | 内部 |
| 13 循环原生码/解释轮次 | **留** | 第 19 条 |
| 14 小算子留在调用线程、优化器一步一遍 | **删** | 只有微基准数字，用户程序看不出差别；第 19 条已保留用户能感知的循环性能 |
| 15 泛型前两步 | **留** | 第 13 条 |
| 16 结构体作循环状态 | **删** | 太窄，且不配例子读不出差别 |
| 17 抽取边界、规则库按元素类型分开 | **留一半** | 抽取并入第 11 条；"规则库按元素类型分开"是实现组织 |
| 18 特化表跨轮复用 | **删** | 内部 |
| 19 `--timeit` 第几轮 | **留** | 第 17 条 |
| 20 Tiles 叶子合批、`cast_like` 拒绝 | **留** | 第 18 条 |
| 21 `std::profile` | **留** | 第 17 条 |
| 22 `x86-64-v3` | **留** | 第 20 条 |

### 2.5 写在哪、什么时候写

`agents/branching.md` 第 41–43、100 行要求：合并 `develop` → `main` **之前**检查 `changes.log`，缺就先在 `develop` 上准备，**并且先让作者确认内容**再提交；两边都确认之前不合 `main`。所以：

- 现在**不写树**。作者确认 §2.2 之后，在 `develop` 上提交一次 `docs(changes): the 0.2.24 notes cover the standard library migration`（只动 `changes.log`），发布合并再带上它。
- 历史实际做法不同：v0.2.20–v0.2.23 都是先 `release: … in vX.Y.Z` 合到 `main`，再在 `main` 上补一个 `docs: record … for vX.Y.Z` 提交（只动 `changes.log`），**tag 打在那个 docs 提交上**（`v0.2.23` = `c8299dae4`，父提交是发布合并 `e1759261f`）。两种都行，但 tag 必须指向含最终文本的那个提交；如果作者选历史做法，这个 docs 提交就是发布提交，而不是 `develop` 上的那一次。
- 本提案不改 `changes.log`：`git status` 干净，文本只在 §2.2 的围栏里。

## 3. 就绪判定

### 3.1 一句话

代码门禁见 §4；如果 §4 的 fail 为 0，**这一版可以发**。不挡住发布、但作者应当知道的，是下面三类。

### 3.2 用户会看到的功能缺口（看板"部分完成/未开始"）

| 任务 | 用户看到什么 | 具体位置 |
|---|---|---|
| H43 第 4 项 | 含循环的函数不能导出 ONNX：`onnx.export_model` 明确拒绝并指出循环，不是静默出错 | `reports/H43a.md`；`feat/modules/onnx` 的 `loops.onnx_refused`、`loops.onnx_refused_prl` 锁住这个拒绝 |
| H17 第 2、4 项 | `xs[:, 0]`、`xs[a:b, c]` 是多维视图，现在写它是编译期错误并点名 H17；`xs[a:b:2]` 是 `RuntimeDiag::InvalidArgument`，消息是 `a slice step must be 1 for now` 并点名 H17；对象数组切片按行复制 | `docs/language/06_expressions.md` §6.3；`test/cases/linear/faults/subscript_step.cml` |
| H49、H50 | GPU 只有旧的 `@gpu` 偏好 + 运行期 `--devices` 分派；`#gpu`/`#cpu` 标签还没有（写了是未知标签），动态形状、回传图类型、bf16/Tensor Core 都没有 | 看板 H49/H50 行；`src/core/tags.cpp` 里没有 `gpu`/`cpu` 标准标签 |
| H19 第 2–9 步 | `env`、`process`、`hash`、`ckpt` 仍是 `stdlib/signatures/*.cml` 的签名文件，不是声明模块；`ckpt` 的 7 个算子没有一条能写成声明（`H140` §0：五个 intrinsic 没有执行器，`save_tree` 会丢掉编译期叶子检查，`load_tree` 的结果类型来自 `::<T>`） | `reports/H140.md` §0；`H137.md` §1 的检查器当前认 11 个声明模块 |
| H51 | 泛型的 rest 参数 `..ts`/`..e`、`constas`、`StaticTuple` 未做；`tree_map` 等仍是 C++，要等 `tree_*` 能写成 Camel（且 `ckpt` 改名已完成，前提满足） | 看板 H51 行；`reports/H96.md` §6 |
| H13 | 官方示例 D4 未做 | 看板 H13 行 |
| H98 | 草稿、待作者裁定：`var`/`let` 声明不进同步链，声明与首次使用之间的语句与声明里的构造无序 | 看板 H98 行；`reports/H95.md` §6 |

这些是"下一版之前要不要说清楚"的问题，不是红的用例。发布说明里第 1、4、12 条已经把前三个缺口用"仍点名 H17"、"不是声明模块"这类措辞交代了一半；如果作者希望发布说明更直白，我可以把 H17 与 GPU 的缺口各加一句。

### 3.3 看板与树/门禁不一致（需要改看板，不是改代码）

1. **H19 行过期**。看板说"第 2–9 步仍未开始（`env`/`process`/`hash`/`io` 的声明化等第 4、8 步的改名与重建）"，但 `H114`（改名）与 `H116`（`io` 声明模块）都已合入并在本树里：`stdlib/io.cml` 是声明模块，`sys`/`os`/`crypto` 已不存在。真实状态是"改名与 `io` 已做；`env`/`process`/`hash` 的声明化（以及 `ckpt`，见 H140）未做"。
2. **H44、H69 行过期**。看板 H44 说"autodiff 穿过结构体状态仍崩溃、ONNX 的 carried 结构体展开仍未做"；`H71`（`reports/H71.md` §1–§3）三项都做了：结构体字段成为循环状态分量、autodiff 穿过结构体循环状态（修段错误）、ONNX 导出携带结构体/元组的循环（`6336d5d96`）。H69 的"待决问题 1、2"同样被 H71 覆盖。看板该按 H71 改，不是再派一次。
3. **`rename.*_graph_identity` 的红是真的，归属要改**。`H114` 行说 3/3 绿，`H137`/`H140` 行说 3 条红是"H114 的旧摘要、基线同红"。门禁与两次复跑给出同样的三个摘要（`a210…`、`aa79…`、`5d54…`），不是负载、不是随机。`H134` 在 `5ff36b1dd` 里重录过一次，期望值就是那次的；之后只有 H138/H139/H140 三个合并与几条 `test(parse)` 直提。我没有在不改树的前提下定位到具体字段（jgir 的 `version` 仍是 1，不打印声明纪元；样本的绝对路径已被 `case_path` 替换掉）。`feat/modules/rename` 的正例全绿（旧名消失、新名可达），所以**改名本身没坏，坏的是摘要**。归属：H114 的快照维护；修法是先在 v0.2.23 上把三个样本的 jgir 与当前的逐字段 diff，确认差异只在不进图的字段后再重录，而不是直接改三个常量。
4. **H43 行**说"第 4 项未做"，与树的实际一致（见 §3.2），不算不一致。

### 3.4 快层里的红（实跑）

第一次（`-Jobs 8`，5046 条）：**7 红**。之后只跑那 4 个计划文件复跑两次：**5 红稳定，1 条通过，1 条是 slow 层**。

| 用例 | 现象（原文） | 归属 | 性质 |
|---|---|---|---|
| `rename.env_graph_identity` | `normalized sha256 a210c9c9…, expected 54e1abbb…` | H114 的 `test/cases/modules/rename/identity.mjs` 快照 | 过期快照，确定性；`feat/modules/rename` 正例全绿 |
| `rename.process_graph_identity` | `aa79e96a…`，期望 `ae6a65cd…` | 同上 | 同上 |
| `rename.hash_graph_identity` | `5d54a035…`，期望 `b872dbfc…` | 同上 | 同上 |
| `lsp.editing` | `math::` 的成员补全为空：候选里没有 `sqrt`，也没有签名（95 个检查里 2 个） | H19 声明化（`stdlib/math.cml` 没有函数体）与 LSP 的模块导出枚举；`math` 在 v0.2.23 只有 `stdlib/signatures/math.cml`，`H133` 才成为声明模块 | **用户可见回归**，确定性；v0.2.23 上 `lsp.editing` 是绿的（修它的 `32ccd19d` 在 tag 里） |
| `compile_bench.smoke` | `chain-3/passes: expected 4 user modules, got {…, 'modules': 6}` | H19 声明化：被 import 的两个声明模块计入了 `--time-phases` 的模块数 | 工具期望过期，确定性；改期望为 6 并说明为什么，或让阶段报告不把声明模块算作用户模块 |
| `debugger.dap_pipeline` | 门禁里 `expected exit 0, got 1`；两次复跑都 PASS（297 ms / 185 ms） | 调试器测试 | 负载间歇，不计红 |
| `debugger.dap_protocol` | `-Jobs 4` 复跑 60.96 s 后 `exit 1`：`opt: the surviving effect stops at its source line`（expected 17, actual undefined）、`opt: only the surviving breakpoint is hit`（expected [3], actual undefined）、`frame.top` 为 null（80 个检查里 4 个） | 调试器 + 优化后的源码行；这条是 `tier = "slow"`，快层按目录过滤时不跑，命名计划文件才会跑 | slow 层真红，见 §3.5 |

### 3.5 本机跑不了、留给发布提交的检查

| 检查 | 为什么这里跑不了 | 发布提交时跑什么 |
|---|---|---|
| slow 层（`npm run test:all`：perf、GC 压力、投毒扫描、PyTorch 对照、随机程序） | 看板 §5.0 要求"发布之前跑一次"，本任务只授权快层。我只用计划文件绕过 tier 跑了一条 `debugger.dap_protocol`，它红了（§3.4） | 主机上：`pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\verify -Plan test/plans -Tier all -Jobs 8`（等价于 `npm run test:all`）；先 `git fetch` 让 `verify` 在 `develop` 的最新提交上。**先修 `dap_protocol`，或确认它 v0.2.23 上同样红** |
| CI（GitHub Actions 的发布腿、`linux-x64-nojit` 构建） | 主机访问不了 GitHub（校园网门户）；本机不构建 | 打 tag 后由仓库的 workflow 跑；`H62` 已修好无 JIT 腿，`H57` 修好工具链下载的 404。发布前在 `develop` 上确认最近一次 CI 是绿的 |
| 版本号确认、`changes.log` 确认 | `agents/branching.md` 要求由作者确认 | 作者回复本提案；确认后再动树 |
| 合并后的祖先检查与 tag 指向 | 本任务禁止合并/打 tag | `git merge-base --is-ancestor main develop`、`git merge-base --is-ancestor develop <feature>`、`git show --no-patch --pretty=oneline v0.2.24` |
| `npm run setver 0.2.25` 与 `npm i` | 属于发布后 bump，本任务禁止 | 见 §5 第 6 步 |

## 4. 发布门禁（在本机跑，结果如实）

### 4.1 怎么跑的

```
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 `
  -Path D:\Projects\Camel-wt\h147-release-notes -Plan smoke,test/plans/feat -Jobs 8
```

主机 `88zjwei`，Release，clang；提交 `a99ba4f1e`（= `origin/develop`）；`-Jobs 8`（`-Jobs 12` 会让 `gpu_preference.no_device.*` 撞 10 s 假超时）。

**主机负载要注意**：跑门禁时主机 `load average` 的 1 分钟值是 **150–210**（32 核），测试阶段约 **90–120**；降载复跑时 **24.6**。上面有别人的 python/qemu 作业。这个负载下的超时可能是假红；§3.4 的 7 条是逐条复跑后的结论。

### 4.2 数字

| 项 | 值 |
|---|---|
| 命令 | 见 §4.1；`tier: fast`，`-Jobs 8` |
| 构建 | **716.6 s**（625 个目标，冷构建；`-NoBuild` 复跑为 0） |
| 计划 | `[INFO] wall: 98.9 s, jobs: 8`（remote 记的 plan 100.7 s） |
| 传输 | 28.3 s（推分支 + 补丁 + 未跟踪文件） |
| 合计 | **846 s**（14 分 6 秒） |
| 测试总数 | **5046** |
| 通过 | **5028** |
| 失败 | **7**（见 §3.4） |
| 跳过 | **11**：10 条 `grammar.scopes.*`（`editors/vscode` 没装 `vscode-textmate`/`vscode-oniguruma`）、1 条 `unicode.paths`（只在 Windows 跑） |

复跑（同一棵树，`-NoBuild`，只命名那 4 个计划文件）：25 条，19 通过，6 失败；`debugger.dap_pipeline` 通过，`debugger.dap_protocol`（slow）红了。

### 4.3 四个静态检查

| 检查 | 结果 |
|---|---|
| `node scripts/stdlib-reference.mjs --verify-ops` | **exit 0**：`the declarations agree with the 385 registered operator overloads` |
| `node scripts/stdlib-reference.mjs --verify-doc` | **exit 0**：`docs/stdlib-reference.md is up to date` |
| `node scripts/diagnostics-catalog.mjs --check` | **exit 0**：`202 codes, every one with a name and a message, no value used twice` |
| `node scripts/check-doc-links.mjs --all` | **exit 0**：`no broken links in 377 document(s)` |

四个都在主机的同一个 checkout、同一个提交上跑；`--verify-ops` 需要 `CAMEL_HOME` 与 conda 的 `LD_LIBRARY_PATH`，两者由 `camel-remote.ps1` 导出。本机的 `--verify-doc`/目录/链接三个结果相同（它们只读树）。

### 4.4 与报告的基线对照

- `--verify-ops` 从 **365**（`H140`）到 **385**：H135 把 `algo`、`statistics` 迁成声明模块，注册重载多了 20 条；不相矛盾。
- 测试总数 **5046** 与 `H140` 的 **2836** 不是同一组目标：H140 跑的是 `feat/modules` + `feat/compile` + `feat/linear` + `smoke`，本提案是 `smoke` + 全部 `feat/**`。
- `H140` 在 tip 上就报过 3 条 `rename.*_graph_identity` 红并说"基线同红"；本提案的复跑证明这三个摘要**稳定不变**，所以 H140 的观察成立，`H137`/`H140` 的"归 H114"也对，但 `H114` 行的"3/3 绿"是重录当时的记录，不是现在的状态。
- `H112` 把 `lsp.editing` 列为 known flake；现在是确定性失败且失败点换成了 `math::` 的补全，与 H133 的 `math` 声明化在时间上吻合。
- slow 层没有 v0.2.23 的基线，`dap_protocol` 的 4 个失败无法判断是否新引入；这是发布前必须补的一步。

## 5. 发布步骤（未执行；作者确认后由协调者按 `agents/branching.md` 做）

1. **先处理 §3.4 的两条真回归**（或由作者明确接受）：`lsp.editing` 的 `math::` 补全、`compile_bench.smoke` 的模块数。修在 `develop` 上，别在 `main` 上修。三条 `rename` 摘要按 §3.3 的修法再录一次；`dap_protocol` 要么修，要么在 v0.2.23 上确认它同样红。
2. 作者确认：版本 `v0.2.24`，下一个开发版本 `v0.2.25`，§2.2 的文本。
3. 在 `develop` 上提交 `changes.log`（或按历史做法留到 `main` 的 `docs: record …` 提交）；提交信息用英文，`git commit -F <file>`。
4. `git checkout main`；`git fetch`；`git merge --no-ff develop -m "release: the standard library takes its declared names, and the release notes say what a user sees in v0.2.24"`。
5. `git tag -a v0.2.24 -m "Camel v0.2.24"`（前四个 tag 都是 annotated，message 是 `Camel vX.Y.Z`）。
6. 在主机跑 slow 层与 §3.5 的检查；确认 §4 的快层仍绿。
7. `git checkout develop`；`git rebase`；`npm run setver 0.2.25`；`npm i`；`git commit -F <file>`，信息 `chore: update to version v0.2.25`。
8. 回到本分支，确认 `git branch --show-current` 不是 `main`/`develop`。

## 6. 复现本提案

```powershell
cd D:\Projects\Camel-wt\h147-release-notes
git describe --tags                     # v0.2.23-554-ga99ba4f1e
git rev-list --count v0.2.23..HEAD      # 554
git show --no-patch --pretty=oneline v0.2.23
node scripts/stdlib-reference.mjs --verify-doc
node scripts/diagnostics-catalog.mjs --check
node scripts/check-doc-links.mjs --all
```

门禁与 `--verify-ops` 需要构建产物，只能通过 `camel-remote.ps1` 在主机上跑；本机不构建（会蓝屏）：

```powershell
# 快层（本提案 §4.2 的 5046 条）
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 `
  -Path D:\Projects\Camel-wt\h147-release-notes -Plan smoke,test/plans/feat -Jobs 8

# 只复跑那 4 个计划文件（命名文件会连 slow 的 dap_protocol 一起跑）
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 `
  -Path D:\Projects\Camel-wt\h147-release-notes `
  -Plan "test/plans/feat/modules/rename_identity.plan.toml,test/plans/feat/tools/lsp.plan.toml,test/plans/feat/tools/debugger.plan.toml,test/plans/feat/tools/compile_bench.plan.toml" `
  -Jobs 4 -NoBuild
```