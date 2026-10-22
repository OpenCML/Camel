# 50 `pyplot` 的 CPython 初始化：从导入期移到首次使用

**结论**（2026-10-22 语言作者裁定，H221 实现）：`import` 不执行代码（设计原则 3.1），所以
`PyplotModule::load()` 在导入期初始化 CPython 是**违规**，**可以优化掉**——在第一次真正使用时初始化，
不在导入时。已按此实现：`pyplot` 与 `python` 的 `load()` 只注册执行器与算子的纯度，解释器由第一次
`plot` 或 `py_*` 调用创建。本页记录这笔成本的测量、裁定、实现、改动后的数字和把这条规则钉住的测试；
H203 那一版"能否推迟"的论证到此为止（原文在 git 历史里）。

## 1. 改动前：钱花在哪

### 1.1 量法

Release 构建，主机 `88zjwei`。墙钟用 `process.hrtime.bigint()` 包住整个子进程；阶段用
`CAMEL_TIME_PHASES=-`（`docs/cli.md` 里 `--time-phases` 的计数器），`cmo_load` 是自身时间（已减去嵌套阶段）。改动前后的两个
安装**在同一窗口内交错**跑（每个 rep 两边各一次，并交替谁先），所以负载对两边一样；窗口的
`/proc/loadavg` 记在每张表下。每组 3 次预热 + 21 次计时（`startup-cost` 那组 41 次）。

### 1.2 数字：9.x ms 落在 `cmo_load` 的 `pyplot` 一项（H203 主窗口，load 44.18 → 41.92）

| 命令 | 墙钟中位 (p10–p90) | `cmo_load` | `pyplot` | `python` |
|---|---:|---:|---:|---:|
| `inspect --ops registry_all` | 40.12 ms (38.55–42.97) | 18.47 ms | **9.26 ms** | 0.51 |
| `imports_nothing` | 8.46 ms | 0.00 | 0 | 0 |
| `import pyplot` | 18.67 ms (17.33–21.58) | **9.33 ms** | 9.33 | 0 |
| `import python` | 19.08 ms | 9.56 ms | 0 | 9.56 |

那 9.3 ms 具体落在哪一步：`Context::loadModuleFrom`（`src/core/context/context.cpp:824 # "Context::loadModuleFrom("`）为每个模块开一个
`cmo_load` 阶段（detail = 模块名），兄弟声明 `.cml` 存在时调 `mod->load()`（同文件 758 行）；
`PyplotModule::load()` 里的 `set_python_home_from_venv()` → `py::initialize_interpreter()` →
`ensure_site_packages_in_path()` / `ensure_extra_python_paths_in_path()` 全部在 `load()` 里，所以整段记在
`cmo_load`（detail `pyplot`）下。它**不**在 `import <module>`（声明装载）、`resolve`、`parse`、`gct`、
`declare`、`gir` 任何一项里：`import pyplot` 相对 `imports_nothing` 的边际约 +10.2 ms，其中 `cmo_load`
9.33、`startup` 0.95，其余各项 0.1–0.3 ms。改动后同一项只剩 1.17 ms（见 §4.2），可见这 9.33 ms 里约
1.1 ms 是 `.cmo` 与 bridge 的 dlopen、约 8.2 ms 是初始化本身。

## 2. 裁定

语言作者（2026-10-22，回答上一版本页提出的 P1/P2）：`import` 不执行代码（3.1），`pyplot` 在导入期
初始化 CPython 违反这一点，**可以优化掉**；在第一次真正使用时初始化，不在导入时。这条裁定已写入
`docs/design-principles-log.cn.md` 的同日条目。上一版把"导入期错误码"和"第一个初始化者决定设置"当作
反对推迟的理由；它们现在不是理由，而是改动的一部分：诊断跟着初始化移到调用处（§3），创建序列统一后
"谁先"不再决定任何设置（§3、§5）。

## 3. 改了什么

- **共享初始化入口** `modules/python/embed.h`（`camel::pyembed::ensureInitialized()`）：env guard →
  `promote_libpython_to_global()` → `PYTHONHOME`（venv）→ `py::initialize_interpreter()` → Python 输出的
  行缓冲与退出 flush → site-packages → extra 路径（`CAMEL_PYTHONPATH`）→ `PyEval_SaveThread()`。它先看
  `Py_IsInitialized()`：已经初始化就什么都不做。于是"谁先创建"不再决定解释器的设置——H203 §3.2 里
  `pyplot` 先创建、`_ssl` 因缺 promote 而 exit 1 的那个程序，现在是 exit 0。这是有意的结果：`pyplot`
  单独使用也获得了 promote、输出顺序与 venv site-packages。
- **进程级一次**：模块是各自独立的 `.cmo`（RTLD_LOCAL），每个都有自己的静态变量，所以守卫放在 libcamel
  里（`camel::utils::runOnce`，`include/camel/utils/once.h` + `src/utils/once.cpp`，基于
  `std::call_once`），key 是 `camel.embedded-python`。动作抛异常时 `call_once` 不置标志，下一次使用可以
  重试。第一个使用者可能在工作线程上（并行调度）：它在 GIL 下完成创建，最后 `PyEval_SaveThread()` 交还。
- **两个 `load()` 只剩注册**：executor factory 与 `OperatorTable::declare(...)`（纯度，3.3）。声明仍然
  eager（Q13 不变：坏的声明仍在导入期报）。`stdlib/pyplot.cml`、`stdlib/python.cml`、
  `docs/stdlib-reference.md` 与 `declarations=345` 都没有动。
- **每个算子先 ensure**：`modules/python/operators.cpp` 的 12 个 kernel 与 `modules/pyplot/operators.cpp`
  的 `plot`，在 `py::gil_scoped_acquire` 之前调用 `ensurePythonInterpreter(ctx)` /
  `ensurePyplotInterpreter()`。模块的 executor 没有 dispatch 钩子，所以这一步放在每个 kernel 的第一行；
  之后是一次 `std::call_once` 的原子读，可以忽略。
- **失败跟着初始化走**：`py::initialize_interpreter()` 抛出的异常在首次使用时变成
  `RuntimeDiag::RuntimeError`，消息是 `Failed to initialise pyplot's/python's embedded Python interpreter
  at its first use: ...`，位置指向调用，退出码仍是 1（与导入期相同，种类也不变）。`python` 的 venv
  版本检查同样移到首次使用，fault kind 不变，消息写明 `checked at the first use of python, not by
  import`。
- **仍留在导入期的一步**：激活的 venv 版本没有对应 bridge 时，`.cmo` 本身加载不了
  （`SemanticDiag::ModuleNotFound`，说明缺少哪个 `libpy_bridge<tag>`，
  `src/core/module/dynamic.cpp:280 # "bool tryLoadPythonBridgeForActiveEnv("` 的 `tryLoadPythonBridgeForActiveEnv`）。那一步决定加载哪个二进制，
  不是 CPython 初始化，没有动。

## 4. 改动后的数字

### 4.1 五条测量（交错 A/B，load 22.59 → 21.42，各 21 次）

| 命令 | 改动前 | 改动后 | 差 | 说明 |
|---|---:|---:|---:|---|
| `imports_nothing`（量具零点） | 9.94 | 8.66 | −1.28 | 两边跑同一段启动代码；这个差就是本窗口的噪声（p10–p90 8.4–11.7 对 7.2–12.1） |
| `import pyplot` | 18.33 | **10.07** | **−8.26** | 不再创建解释器 |
| `import pyplot` + 一次 `plot` | 18.41 | 19.34 | +0.92 | 初始化没有消失，只是换到调用处；同一程序的总时间不变 |
| `import python` | 17.94 | **10.40** | **−7.54** | 同上 |
| `import python` + 一次 `py_exec` | 17.69 | 18.02 | +0.33 | 同上 |
| 全注册表（`registry_all` + `std::nvm`） | 39.29 | **30.54** | **−8.75** | 两个模块都导入、只付一次初始化的那条路径 |
| `inspect --ops registry_all` | 42.67 | **34.34** | **−8.33** | H203 的主测量 |

把 `imports_nothing` 减掉（它是量具的零点，不是改动的一部分），`import pyplot` 的边际成本 8.4 → 1.4 ms，
`import python` 8.0 → 1.7 ms；一次 `plot` 的边际 8.5 → 10.7 ms——那 8 毫秒没有蒸发，它在调用处。

### 4.2 阶段分解（同一窗口，load 15.88；各一次，用来看钱落在哪个阶段）

| 命令 | 前：墙钟 / `cmo_load` | 后：墙钟 / `cmo_load` | `pyplot` 前→后 | `python` 前→后 |
|---|---:|---:|---:|---:|
| `import pyplot` | 20.37 / 9.25 | 10.86 / 1.17 | 9.25 → 1.17 | 0 |
| 一次 `plot` | 17.59 / 8.64 | 17.02 / 0.91 | 8.64 → 0.91 | 0 |
| `import python` | 18.95 / 9.43 | 7.64 / 1.03 | 0 | 9.43 → 1.03 |
| 一次 `py_exec` | 17.19 / 8.98 | 16.08 / 0.96 | 0 | 8.98 → 0.96 |
| `registry_all` | 36.13 / 17.36 | 26.81 / 9.11 | 8.61 → 0.91 | 0.44 → 0.34 |
| `inspect --ops` | 39.60 / 17.48 | 28.00 / 8.52 | 8.85 → 0.93 | 0.47 → 0.33 |

`registry_all` 的 `cmo_load` 里其余 27 个模块合计 8.31 → 7.86 ms（没动），差别全部落在两个 Python 模块上。
改动后 `pyplot` 的 1.17 ms 只剩 `.cmo` 与 bridge 的 dlopen 和注册——比其它模块的中位 0.3 ms 贵，是因为
它仍然 `DT_NEEDED` libpython。只 `plot` 一次的墙钟两边相同（17.6 对 17.0），正是"钱换了个时间点"的直接读数。

### 4.3 `perf/startup` 的 ceiling 要不要重定基线

`test/tools/startup-cost.mjs --reps 41`，同一窗口（load 10.68 → 10.68）：

| 项 | 改动前 | 改动后 | ceiling |
|---|---:|---:|---:|
| `version` | 5.1 | 4.8 | 40 ms |
| `imports_nothing` | 6.6 | 6.8 | 45 ms |
| `inspect_ops` | 42.9 | 31.8 | 100 ms |
| `imports_all` | 40.7 | 31.5 | 90 ms |
| `declaration.cost` | 22.0 us/条（345 条） | 22.3 us/条（345 条） | 60 us |
| 声明占编译工作的比例 | 19.1% | 24.2% | 30% |

**不收紧**。两条整库命令的中位数现在比 ceiling 低三倍上下；ceiling 是"标准库在程序跑之前就加载完了"的
烟雾报警，不是预算，压到当前中位数只会把主机噪声变成红灯。唯一明显移动的是比例：分母少了那 9.5 ms，
声明占的份额从 19.1% 升到 24.2%，仍在 30% 以内——这恰好是**不收紧**的第二个理由：收到 25% 会把一次
正常的导入期变化重新变成红灯。两组数字写进 `test/tools/startup-cost.mjs` 的注释与
`test/plans/perf/startup.plan.toml`。

## 5. 把规则钉住

`test/cases/modules/stdlib/interpreter_first_use.mjs`（计划
`test/plans/feat/modules/interpreter_first_use.plan.toml`，19 条断言）：

- `CAMEL_PYTHON_INHERIT_HOST_ENV=1` + `PYTHONHOME=<空目录>`：CPython 在这个环境下根本起不来（它自己打印
  `Python path configuration`，pybind11 抛异常）。此时**只导入** `pyplot` / `python` 的程序必须 exit 0
  且什么都不打印；把初始化放回 `load()` 会让它们 exit 1——正是 H203 量的第一个可观察量。
- 同一环境 `inspect --ops` 必须 exit 0 并列出 `pyplot:plot`（H203：exit 1、不列算子）。
- 同一环境一次 `plot` / 一次 `py_exec` 必须 exit 1，诊断是 `RuntimeDiag::RuntimeError`、位置指向调用、
  消息写明 first use，并带上 CPython 自己的原因：失败是搬了家，没有消失。
- 健康环境下两种顺序（`python`→`pyplot`、`pyplot`→`python`）都能跑；`plot` 两次夹一次 Python 调用也是
  一个解释器；`pyplot` 先的程序随后 `py_import("_ssl")` 成功（libpython 由第一个初始化者 promote，
  改动前这条是 exit 1）。`_ssl` 两条在 python-first 也导不进来的环境里跳过并打印原因。
- **不用墙钟做守卫**：一个"导入后立即调用"的程序，初始化在导入还是首次调用，墙钟差不到 1 ms 的量级，
  任何 ceiling 都分不出来。上面这个环境判断是行为判断，不是计时；§4 的数字是记录，不是闸门。

## 6. 没有改什么

`python` 的 12 个算子体与 `pyplot` 的算子体（除第一行 ensure）、`stdlib/*.cml`、声明面、加载顺序、
`docs/stdlib-reference.md`、`perf/startup` 的 ceiling、`tools/lang-server/server.cpp`，以及其它任何模块的
惰性化。`import python` 仍然在导入期做它一直做的事：解析声明、检查签名、注册纯度与 executor。