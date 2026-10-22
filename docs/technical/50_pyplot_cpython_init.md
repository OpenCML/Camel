# 50 `pyplot` 的 CPython 初始化：能否推迟到首次使用

**问题**（H203）：H191 量到，`inspect --ops registry_all.cml` 的冷启动 **39.00 ms** 中
`cmo_load` 占 **18.87 ms（48.4%）**，其中 `pyplot` 一个模块 **9.27 ms（9.27 / 18.87 = 49.1%，
占冷启动 23.8%）**，原因是 `PyplotModule::load()` 调用 `py::initialize_interpreter()`；
其余 26 个模块合计 9.45 ms、中位数 0.29 ms。只导入 `pyplot`、从不绘图的程序也付这 9.3 ms。
能不能把这笔初始化推迟到第一次 `plot`，而对导入期和使用期的行为都不可见？

## 0. 回答

**不能，在没有作者裁定的情况下不能。** 有两个互相独立的可观察量，任何一个都足以否决
“推迟后行为不变”：

1. **初始化失败是一个导入期错误码。** 今天 `camel import_pyplot.cml std::nvm` 在 CPython
   初始化失败时退出 1（`Failed to load pyplot module` / 解释器路径配置打印）；把
   `initialize_interpreter()` 移出 `load()` 后，同一程序退出 0，错误推迟到第一次 `plot`。
   `inspect --ops` 也从“退出 1、不列算子”变成“退出 0、列出 `pyplot:plot`”。
2. **“第一个初始化 CPython 的模块”决定运行时的设置。** `modules/pyplot` 与 `modules/python`
   共享同一个进程内解释器，但两者 `load()` 的初始化序列并不相同：`python` 在
   `!Py_IsInitialized()` 分支里 `promote_libpython_to_global()`、把 Python 输出设为行缓冲并
   注册退出时 flush、加 site-packages 与 extra 路径；`pyplot` 只加后两者，不 promote、不设
   输出。此外 `python` 每次 `load()` 都检查 venv 版本，`pyplot` 完全不检查。今天先导入
   `pyplot` 的程序由 `pyplot` 初始化，`python` 的 `load()` 只看到“已初始化”并跳过第一组
   设置。下面第 3.2 节的 `_ssl` 用例在今天的树上退出 1，推迟后由 `python` 初始化则会
   退出 0——这不是时序，是同一程序的结果改变。

因此这不是一个纯实现优化，而是**模块导入是否获取 CPython 这一进程级资源**的策略问题：
P1“导入即获取、导入期报错”还是 P2“首次算子才获取、首次调用报错”。按任务约定，这里停，
不改 `load()`、不改声明、不改加载顺序。

## 1. 量法

与 H191 同形：Release 构建，主机 `88zjwei`，`CAMEL_TIME_PHASES=-` 读 doc 38 的
`cmo_load` 阶段计数器；每窗口 3 次预热 + 41 次计时，窗口内随机交替各命令；记录
`/proc/loadavg`。另用 `test/tools/startup-cost.mjs --reps 41` 量整进程墙钟。
`cmo_load` 是自身时间（已减去嵌套阶段）；`import_pyplot`、`imports_nothing` 的差别给出
单个导入的边际成本。基线提交 `880ee74b1`（`origin/develop`）。

## 2. 数字

### 2.1 主窗口（load 44.18 → 41.92）

| 命令 | 墙钟中位 (p10–p90) | CPU 中位 | `cmo_load` 中位 (p10–p90) | `pyplot` |
|---|---:|---:|---:|---:|
| `inspect --ops registry_all` | **40.12 ms** (38.55–42.97) | 38.85 ms | **18.47 ms** (17.56–19.80) | **9.26 ms** (8.91–9.78) |
| `imports_nothing` | 8.46 ms (7.32–10.62) | 7.17 ms | 0 | 0 |
| `import pyplot` only | **18.67 ms** (17.33–21.58) | 17.40 ms | **9.33 ms** | 9.33 ms |
| `import python` only | 19.08 ms | 17.87 ms | 9.56 ms | 0 |
| `import pyplot; import python` | 19.53 ms | 18.24 ms | 9.78 ms | 9.30 ms |

- `inspect --ops` 的 `cmo_load` 里，`pyplot` 9.26 ms 占 **50.1%**；其余 26 个模块合计
  9.08 ms（中位数差），代表运行里逐模块求和 8.91 ms、逐模块中位数 0.27 ms。H191 的
  18.87 / 9.27 / 其余 9.45、中位 0.29 在噪声内一致；本窗口 `python` 只花 0.51 ms，因为
  `pyplot` 已经初始化了解释器。
- `import pyplot` 相对 `imports_nothing` 的边际墙钟 **+10.22 ms**（CPU +10.23 ms），其中
  `cmo_load` 9.33、`resolve` 0.21、`parse` 0.31、`gct` 0.19、`declare` 0.12、`gir` 0.12、
  `startup` 0.95 ms。`import python` 的边际几乎相同（+10.2 ms），所以两个模块谁先加载谁付。
- 如果只让 `pyplot` 推迟，`registry_all` 仍会由随后加载的 `python` 付 9.3 ms，`inspect_ops`
  与 `imports_all` **不会变快**；要让它们变快必须两个模块一起推迟，这也把第 3 节的两个
  可观察量同时放大。
- 窗口 A（load 39.22 → 39.29）：`inspect --ops` 41.19 ms、`cmo_load` 19.16、`pyplot` 9.32。
  窗口 B（load 64.76 → 71.75）：墙钟被拉长到 85.5 ms、`pyplot` 17.04 ms，只用于确认比例，
  绝对值不采用。

### 2.2 整进程墙钟（`startup-cost.mjs --reps 41`）

| 窗口 | version | imports_nothing | inspect_ops | imports_all |
|---|---:|---:|---:|---:|
| load 36.94 → 34.94 | 4.81 | 7.35 | **42.54** (39.69–49.32) | 41.97 (37.69–49.96) |
| load 31.27 → 28.15 | 5.24 | 7.66 | 44.55 (40.08–48.24) | 43.52 (36.77–48.54) |

H191 的 39.00 ms 在 load 19.5 测得；本机 load 28–37 时 42.5–44.6 ms，差在负载，`cmo_load`
与 `pyplot` 两项与 H191 基本重合。若两个模块都推迟、且 CPython 初始化是唯一变化，
`inspect_ops` / `imports_all` 的上界约为 **−9.3 ms（−22%）**，只导入 `pyplot` 的程序
**−10.2 ms（−55%）**；这是没有扣除“首次 `plot` 仍要付 9.3 ms”的数字。

## 3. 为什么推迟不是“不可见”

### 3.1 错误码与诊断

```
CAMEL_PYTHON_INHERIT_HOST_ENV=1 PYTHONHOME=/nonexistent \
  camel /tmp/h203/import_pyplot.cml std::nvm
# 今天：exit 1，Failed to load pyplot module / Python path configuration ...
# 推迟后：load() 里不再有会抛错的初始化，import 只能成功；错误到第一次 plot 才出现。
```

（`import_pyplot.cml` 只有 `import pyplot` 与返回 0 的 `main`，是 H191 边际测量的同一形状；
上面两条命令的完整输出在提交前的基线运行里核对过。）

同一环境下 `camel inspect --ops /tmp/h203/import_pyplot.cml` 今天 exit 1、只打印 2 行头；
推迟后它会 exit 0 并列出 `pyplot:plot`。`python` 模块还有 venv 版本不匹配的导入期诊断
（`python module version mismatch`），`pyplot` 今天没有；推迟会把“哪个模块、在什么时候
报 Python 环境错误”全部改写。这是任务所说的“错误码 / 时序相关诊断”，不是计时噪声。

### 3.2 第一个初始化者决定解释器设置

```
import { plot } from pyplot
import { py_import } from python
...
plot([1.0, 2.0, 3.0], "/tmp/h203/combined.svg")
py_import("_ssl")
```

今天：`pyplot` 在 `load()` 里初始化 CPython，但没有 `promote_libpython_to_global()`；
随后 `python` 的 `load()` 看到 `Py_IsInitialized()` 为真，跳过 promote。结果
`_ssl` 导入失败，程序 exit 1：

```
ImportError: .../_ssl.cpython-310-x86_64-linux-gnu.so: undefined symbol: PyExc_ValueError
```

只导入 `python` 的同一段 `_ssl` 程序 exit 0。把 `pyplot` 的初始化推迟后，`python` 成为
第一个初始化者并 promote，上面这个程序会从 exit 1 变成 exit 0；反过来，如果让推迟后的
`pyplot` 初始化也 promote，`pyplot` 单独使用时的行为又和今天不同。两种选择都改变结果，
不是“同一程序同一输出”。同理，`sys.path` 的 `CAMEL_PYTHONPATH` / venv site-packages
快照、Python 输出的行缓冲与退出 flush，今天也是由 `pyplot` 还是 `python` 先加载决定的。

### 3.3 声明面

`stdlib/pyplot.cml` 的 `plot` 只声明 `@effect writes an image file`；C++ 侧
`OperatorTable::declare("pyplot", {{"plot", false}})` 只声明“有副作用”，没有声明
CPython 资源。也就是说，模块今天**没有**把“加载时获取 CPython”写进声明；推迟不会改
声明文本，但会改“导入是否获取进程级资源”这一事实。按任务第 3 条，这本身也需要作者先
定策略，不能由实现顺手改掉。

## 4. 如果作者选 P2（首次使用才获取）

需要一次独立的设计改动，至少包括：

- 一个共享初始化入口（`modules/python` 或公共 bridge），把版本检查、libpython promote、
  输出顺序/flush、site-packages 与 entry 路径、GIL 释放合成一条序列；两个 `load()` 只
  注册执行器与算子，不初始化。
- 明确失败时机：导入成功、第一次 `plot`/`py_*` 失败，诊断文本与错误码写进声明/文档；
  并发调度下由一个线程完成 `Py_Initialize`，其余线程只取 GIL。
- `pyplot`/`python` 的 `load()` 不再有导入期错误，`inspect --ops` 不再能证明解释器可用；
  需要新的测试表达这个承诺，并检查 `stdlib/pyplot.cml`、`python.cml` 的 `@effect` 是否
  要加资源。
- 重新量 `inspect --ops`、`imports_all`、`import pyplot` 和 `use pyplot` 四条路径；
  只改 `pyplot` 不减少 `registry_all` 的 9.3 ms。

## 5. 没有改什么

没有改 `modules/pyplot`、`modules/python`、`stdlib/pyplot.cml`、`stdlib/python.cml`、
加载顺序、`OperatorTable` 的纯度/资源声明、`perf/startup` 的 ceiling，也没有新增开关。
本页只记录测量与“需要作者裁定”的结论，避免下一次重新量同一件事。