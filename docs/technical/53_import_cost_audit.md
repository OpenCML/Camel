# 53. 导入时做了什么：标准库 27 个模块的审计（H222）

设计原则 3.1 在 2026-10-22 由语言作者裁定：**`import` 不执行任何代码**。裁定来自 pyplot 的测量（导入即初始化
CPython，41 ms 的运行里 9.9 ms，见 [50](50_pyplot_cpython_init.md)）；3.1 的表格行已写明同理的一半——
`模块顶层的 let` 是公共 root 图上的一个节点，被依赖才求值，导入不执行任何代码。

本页是那条裁定要求的账：`test/cases/modules/stdlib/registry_all.cml` 导入的 27 个模块，每个模块导入时除了声明名字
还做了什么、判成哪一类、代价多少，以及怎么在五分钟内复核。执行工具是 `test/tools/import-audit.mjs`，计划是
`test/plans/feat/modules/import_cost.plan.toml`。

## 53.1 判定口径

三分照任务的定义：

- **(a)** 违反 3.1，且把工作挪到首次使用是便宜的；
- **(b)** 违反，但挪动会改变可观测行为，需要作者裁定；
- **(c)** 不是违反：声明名字、root 图上的 `let`、无观测效果的注册表。判据是裁定里的那一句：
  **注册一个 pass、注册一张算子表是声明；读 `env` 是执行**。

每个模块有两份证据，缺一不可：

1. **探针**：一个只 `import <m>`、`main` 返回 0 的程序必须退出 0、stdout/stderr 全空（默认日志级别下）、不在
   工作目录建任何文件。这抓的是**可观测**的导入期工作（输出、错误码、文件）。
2. **源码扫描**：读模块自己的 `load()` 与构造函数，找五种形状——读环境变量、读文件/列目录、初始化 CPython、
   起线程、注册退出钩子。这抓的是**源码里**的导入期工作。

两份证据分工的原因：pyplot 的 CPython 初始化对探针是隐形的（不打印、不建文件、退出 0），只有扫描能看见；反过来，
一个模块在导入期打印一行，扫描看不见它，探针会红。实测里 (a) 为 0，两个 (b) 行由扫描点名。

## 53.2 27 行

24 个 `.cmo` 行（再加无 `.cmo` 的 `array`）都做同样的三件事，表里不再重复：把算子表（含纯性声明）合入 `OperatorTable`、注册一个执行器
工厂（`ExecutorManager::registerExecutorFactory`，第一次查找才构造执行器）、声明资源（`declareResources`）。
`load()` 由 `Context::importModule` 在 `import` 阶段调用一次；构造函数由 `camel_module_create` 在 `cmo_load`
阶段跑，也只做同一类注册。表里只列**额外的**注册。

| # | 模块 | 类 | 除算子表/工厂/资源外，导入时还声明什么 | 探针 |
|---|------|----|----------------------------------------|------|
| 1 | `bytes` | c | — | clean |
| 2 | `hash` | c | — | clean |
| 3 | `env` | c | 读环境/工作目录的算子的资源声明 | clean |
| 4 | `fs` | c | 文件读写的资源声明 | clean |
| 5 | `graph` | c | 8 个 handle 类型的导出 | clean |
| 6 | `io` | c | 终端 raw mode 的句柄与资源 | clean |
| 7 | `json` | c | `Json` 类型、2 个 computed resolver | clean |
| 8 | `math` | c | 4 个常量、semantics、一条导数规则 | clean |
| 9 | `process` | c | 进程查询算子的资源 | clean |
| 10 | `para` | c | 并行原语的资源 | clean |
| 11 | `random` | c | `Key` 类型、生成器资源 | clean |
| 12 | `re` | c | — | clean |
| 13 | `sim` | c | 纯性（同一算子的纯/不纯两种写法） | clean |
| 14 | `statistics` | c | — | clean |
| 15 | `str` | c | — | clean |
| 16 | `this` | c | — | clean |
| 17 | `time` | c | 时钟算子的资源 | clean |
| 18 | `ckpt` | c | 张量算子目录、4 个 tree 内建、checkpoint 资源（源码在 `modules/ckpt`） | clean |
| 19 | `tensor` | c | 张量属性键、算子目录、fuse pass、`Tensor` 类型、`from_bytes`、入口参数 loader/saver、类型精化器、节点角色 | clean |
| 20 | `array` | c | Camel 模块：15 个 Camel 函数与 8 个原生算子的声明，没有 `.cmo`；原生内核编在 libcamel，导入时整份编译 | clean |
| 21 | `nn` | c | 张量算子目录、nn 目录、optimizer 组 | clean |
| 22 | `autodiff` | c | tag、属性键、算子元数据 | clean |
| 23 | `tiles` | c | region pass、device 探针钩子（只赋值一个 lambda，不调用）、profile 钩子、`Device` 类型、10 组算子 | clean |
| 24 | `rewrite` | c | `Rule` 类型、3 个保留组 | clean |
| 25 | `onnx` | c | lowering registry、export pass、节点角色 | clean |
| 26 | `python` | **b** | **初始化 CPython**、读 `pyvenv.cfg` 查 venv 版本、写 `sys.path`、行缓冲、`atexit` | clean |
| 27 | `pyplot` | **b** | **初始化 CPython**、加 site-packages、`CAMEL_PYTHONPATH` | clean |

计数：**a=0，b=2，c=25**。27 = 26 个 `.cmo` 模块 + Camel 的 `array`；`test_attrs` 与 `profiler` 是测试/NDEBUG
内置模块，不是这 27 行之一。

两个 (b) 行的探针是 clean 的——它们不打印、不建文件、退出 0——这正是 53.1 说要两份证据的原因。

## 53.3 (b) 的价格

数字是 9 次样本的中位数，2026-10-22 于 Linux 主机（`88zjwei`），与 50 的机器相同。共享主机的墙钟噪声约 ±5 ms，
所以看 `cmo_load` 相位而不是整程序墙钟。

### 53.3.1 `python` 与 `pyplot`：初始化 CPython

| 模块 | `cmo_load` | 整程序边际 |
|------|-----------|-----------|
| `python` | **10.29 ms** | +15.4 ms |
| `pyplot` | **9.90 ms** | +11.9 ms |
| 其余 24 个 `.cmo` 行 | 0.19–3.3 ms（中位 ~0.43） | 噪声内 |

50 已经算过同一件事（`import pyplot` 18.67 ms 对 `imports_nothing` 8.46 ms，边际 +10.2 ms），本页的数字是同一
现象的另一次测量。为什么是 (b) 而不是 (a)：把初始化推迟到首次使用会改变错误码（`inspect --ops` 在导入期 exit 1，
推迟后 exit 0），并且“第一个初始化者决定解释器设置”（`_ssl` 的 `PyExc_ValueError`、libpython 提升为全局、
行缓冲）。pyplot 归 50 / 另一任务，python 同理，本审计不改。

### 53.3.2 搜索根与目录扫描（`CAMEL_PACKAGES` / `-I` / stdlib 目录）

读什么、什么时候读：

- **进程启动时一次**：`buildModuleSearchPaths`（`include/camel/utils/install_layout.h`）读 `CAMEL_HOME`、
  `CAMEL_STD_LIB`、`CAMEL_PACKAGES`（`;` 或 `:` 分隔）与命令行 `-I`，只做字符串展开与绝对化，**不 stat、不列目录**。
- **每次 `import`**：`Context::importModule` → `getModulePathAndKind` → `findModuleUnder` 按搜索根顺序列举
  `<root>/<name>` 与 `<root>/<name>/` 的目录项，命中即停；列举结果按目录路径缓存在 `Context::directoryListings_`，
  同一 context 内后续 `import` 不再列举（编译流水线的成本账 §8 第 5 条（该页已随测量报告清理删除，结论见本节））。这就是“标准库扫描”：
  一次 `$CAMEL_HOME/stdlib` 的 `directory_iterator`，加上入口目录一次，之后全是缓存命中。
- **价格**：每次 import 的 `resolve` 相位 ≈0.2 ms；10 个 `CAMEL_PACKAGES` 根把首次 resolve 从 **0.233 ms 抬到
  0.410 ms**（+0.18 ms），整程序 +1.2 ms；3 个 `-I` 根 +1.8 ms（单次测量）。

判定：这不是模块代码的执行，是 import 找到文件所必需的；把列举挪到“首次使用”只会把 `Module not found` 的诊断
从 import 挪到首次使用，与 53.3.1 的 (b) 理由同型。所以它在 (b) 的价格表里，不在 (a) 的修复清单里。

### 53.3.3 声明模块整份编译

每个被导入的 `stdlib/<m>.cml` 在 `import` 时**整份**编译（parse / GCT / declare / GIR，函数体也在内），与导入者是否
用其中一条声明无关。这是 (c) 的成本——声明名字本来就要读完声明——但它是 import 最大的固定成本，列在这里：

| 模块 | 行数 | 导出 | 声明编译 |
|------|------|------|---------|
| `array`（Camel 模块，无 `.cmo`） | 174 | 28 | **5.12 ms** |
| `graph`（`.cmo` + `.cml`） | 225 | 57 | **3.33 ms** |

`registry_all` 的全集是数百条声明、约 20 µs/条、合计约 7 ms（模块缓存的成本账（该页已随测量报告清理删除） 的启动账本）；与它相比，
`.cmo` 装载的 0.2–2 ms 是小头。

## 53.4 探针与守门

`test/tools/import-audit.mjs`：

- 对 27 个模块各生成一个只导入它的程序，跑 `camel <file> std::nvm`，断言退出 0、stdout/stderr 空、无新文件；
- 扫描每个模块的 `load()` 与构造函数（`ckpt` 走 `modules/ckpt`），五种形状见 53.1；lambda 体内的工作不算——
  那是注册一个回调，回调在之后才跑（`tensor` 的入口参数 loader 读 `.npy` 就是这种形状）；
- 一个形状出现在判为 (c) 的行上就失败，并点名模块、行列与原因；class (b) 的形状必须出现在具名表里。
  这就是“工作回到导入期就变红”的守门：它不依赖墙钟，也不依赖日志级别。
- `--scan-only` 不需要 `camel`，可在任何树上跑；计划里只跑全量模式（约 2–3 s）。

模块层的 `let` 由两个独立程序证明（`test/cases/modules/import_audit/`）：

- `unused_let`：导入一个顶层 `let` 会 `write_text` 一个 flag 的模块，从不使用它——flag 必须不存在；
- `use_let`：同型模块，程序依赖那个值——flag 必须存在。

这里有一个审计时必须说清的边界：**不能**在“依赖它”的程序里断言“刚 import 完、flag 还不存在”。root 图上的节点
与入口函数的第一个效果之间没有顺序（3.3 只管同一函数体内效果之间的顺序），调度器可以在 `main` 的第一行之前就
求值它。可观测的契约是“不依赖就永不发生”和“依赖了就发生”，两个程序各证一半。第一版探针正是在这里误报过一次
（`after import, flag exists = true`），记在此处以免下次重犯。

## 53.5 结论与没改的东西

- **(a) = 0**：没有可修的违反。(b) = 2（`python`、`pyplot`），价格见 53.3.1。
- 本任务不改运行时行为。新增的只有审计工具、一个计划、两个 `let` 探针用例、本页与索引行。
- 读代码时发现两处 `.cmo` 装载期的构造，都不在 `load()` 里，也都没有观测效果，因此既不是 (a)（没有探针能证明
  “它不存在”）也不是 (b)（没有需要裁定的东西），记录在此：`modules/random/operators.cpp` 的
  `static rnd::Engine g_rng;`（默认种子，不读系统熵）与 `modules/io/operators.cpp` 在 Windows 上的
  `static HANDLE hStdin = GetStdHandle(...)`。
- 复核：`node test/tools/import-audit.mjs`（全量）或 `--scan-only`（不建编译），然后跑
  `feat/modules/import_cost`；表里每一行的类与理由都在工具顶部的 `IMPORT_TIME_WORK` 里，改一行就等于改账。
