# 标准库的重新规划（设计稿）

状态：第 2–4 节已定（2026-10-20；第 4 节：`json`、`re`、`csv` 保持独立的顶层模块，一次改完、按模块分批提交）；第 5 节已定（含表格的列结构）。现状盘点见 [盘点](inventory.cn.md)。

## 1. 现状的问题（摘自盘点）

- **几乎全是 C++**：30 个模块中只有 3 个用 Camel 写（约 270 行），标准库约 98% 在 C++。与设计原则 10.5"原语用 C++、组合用 Camel"相反，也使"自己的库尽量用 Camel 写、暴露语言不足"（第 1 节）无从谈起。
- **重复与不一致**：`sleep` 在全局与 `process` 各一份；`algo` 有四个排序名（11.3 只留 `sort`）；`Set` 同时有 `has` 与 `contains`；`min`/`max`/`sum`/`mean`/`exp` 在 `math`、`array`、`tensor`、`statistics` 各有一份；`from_bytes` 有三种含义，`str` 里的"字节"是 `int[]` 而不是 `Bytes`。
- **约定不一**：参数的顺序与尖括号的用法违背 4.4（`format<args>` 把数据放进尖括号、`re::test(pattern, text)` 把数据放在后面）；错误的约定违背 3.9（`fs` 几乎全是故障，`get_env` 缺变量是故障，`strptime` 失败是故障而 `parse_int` 返回 `int!`）；故障种类笼统（大量 `RuntimeError`）。
- **资源名不一致**：C++ 侧已声明资源，但旧名字（`io:stdout`、`rand:generator`、`process:environment` 等）与 3.3 约定的 `stdout`、`fs`、`clock`、`env` 不一致；没有输出到 `stderr` 的函数。（H134 已把可拼写的改名统一到 3.3 的名字；终端模式的资源已按作者裁定并入 `stdin`，`eprintln` 已进前奏。）
- **同一事实写三处**：算子的签名、纯度、资源在 C++ 注册、算子表、`stdlib/signatures/*.cml` 各写一份；签名文件只覆盖 18 个模块，且不参与编译。
- **残留**：`io` 是空壳；`this`（彩蛋）、`sim`（基准内核）、`para::rendezvous`（测试设施）混在标准库里；没有用命名空间；没有版本号。
- **缺口**：网络与 HTTP、并发与取消、流、键式随机数、数据加载、优化器、CSV/Parquet/Arrow、压缩、日期时间、完整的正则、进程、日志、目录操作。

## 2. 原则

1. **原语用 C++，组合用 Camel**（10.5）：只有必须接触系统或对性能敏感的原语用 C++；其余用 Camel 写。目标：按函数数，Camel 写的占多数。
2. **一个事实只写一处**：每个函数的签名、纯度、资源、故障只在 `.cml` 的声明里写一次——Camel 写的函数有函数体；原生函数是**没有函数体的声明**（10.1），由 C++ 按名字绑定实现。C++ 一侧不再重复声明签名与事实；签名文件参与编译，不再是参考材料。
3. **统一的约定**（写成一页[标准库约定](../../stdlib-conventions.cn.md)，新函数按它写）：
   - 命名：小写加下划线；动词开头的是有副作用或计算，名词是取值；转换写 `to_x`；判断写 `is_x`、`has_x`。
   - 参数：数据在前（便于 `x->f(a)`），配置用尖括号（4.4）。
   - 错误：环境造成的失败（文件不存在、网络、解析用户输入）返回 `T!`；"可能没有"返回 `T?`；程序本身的错误（越界、违反前提）是故障，种类具体（不再笼统用 `RuntimeError`）。
   - 资源：用 3.3 的标准名字（`stdout`、`stderr`、`stdin`、`fs`、`clock`、`env`、`net`、`rng`、`process`）。
   - 纯度：纯函数都标明（让 `pure` 承诺可以检查）。
4. **一件事只在一个地方**：同一功能不在多个模块各放一份；需要对多种类型适用的，用重载或泛型放在一处。
5. **用命名空间组织**，默认可见的前奏（prelude）尽量小。
6. **类型对齐**：文本是 UTF-8 的 `string`、字节是 `Bytes`、数组是原生多维数组、流是 `Stream<T>`、映射与集合、`Json`。
7. **标准库与语言同一版本**，随语言发布。

## 3. 建议的布局

**前奏（默认可见，尽量小）**：`print`、`println`、`eprintln`、`format`、类型转换（`int`、`float`、`string`……）、`len`、`range`、`enumerate`、`min`、`max`、`abs`、`assert`、`error`、`exit`。

**基础**

| 模块 | 内容 | 实现 |
|---|---|---|
| `str` | 字符串（按码点）：切分、修剪、大小写、替换、查找、前后缀、重复 | 原语 C++，其余 Camel |
| `bytes` | `Bytes`：编解码、拼接、切片 | C++ |
| `array` | 一维与多维数组的通用操作：`map`/`filter`/`reduce`、`sort`（唯一的排序）、`argsort`、`reverse`、`take`、`drop`、`find`、`any`/`all`/`count`（提前结束）、`unique`、`flatten`、`zip` | 大部分 Camel；`sort` 的内核 C++ |
| `map`、`set` | 映射与集合 | 原语 C++，组合 Camel |
| `math` | 标量数学（逐元素作用于数组时由重载给出）、常量、位运算、复数函数 | C++ 原语 |
| `random` | **键式随机数**（`key`、`split`、`fold`、`normal`、`uniform`、`randint`，Philox，纯函数）与带全局状态的随机数（资源 `rng`）；`choice`/`sample`/`shuffle` 用 Camel 写在其上 | 原语 C++，其余 Camel |
| `stats` | 描述统计（原 `statistics`），泛型写一份 | Camel |
| `time` | 时钟（资源 `clock`）、日期时间、时长、格式化与解析（解析失败返回 `T!`） | C++ 原语 |
| `re`、`csv`、`json` | 完整正则（匹配、查找、替换、分组）；CSV；JSON。三个独立的顶层模块 | 原语 C++ |
| `hash`、`compress` | 哈希（sha256 等）、压缩（gzip、zstd） | C++ |

**系统**

| 模块 | 内容 |
|---|---|
| `fs` | 文件与目录（读写、列目录、遍历、删除、移动）；失败返回 `T!`；文本与二进制分开；大文件以流读取 |
| `io` | 标准输入输出与错误（`stdin` 作为流、`stdout`、`stderr`） |
| `env` | 环境变量（缺失返回 `T?`）、命令行参数、当前目录 |
| `process` | 子进程（启动、等待、管道，返回流）、`sleep`、`cpu_count` |
| `net`、`http` | 见 [网络、并发与流](../io-concurrency-streams/README.cn.md) |
| `log` | 分级日志（资源 `stderr` 或文件） |
| `stream` | `Stream<T>` 与窗口、按键聚合、合并、分批 |

**数值与 AI**

| 模块 | 内容 |
|---|---|
| `tensor` | 数值数组的运算：逐元素、线性代数、归约、卷积、形状操作；定义图（原 `tensor_definitions`）并入 |
| `nn` | 层与损失（函数形式，参数是结构体） |
| `optim` | 优化器（Optax 式：`init`、`update` 两个纯函数）：`sgd`、`adam`、`adamw`、学习率调度；用 Camel 写在 `tree` 上 |
| `data` | 数据集、加载、打乱、分批、预取（基于流） |
| `tree` | 参数树（泛型第一期后改为 Camel 写）与检查点 |
| `autodiff` | `grad`、`with_grad`、`vjp`、`stop_gradient` |
| `onnx`、`tiles` | 导出、GPU |
| `arrow` | Arrow C 数据接口、Parquet、Arrow IPC |
| `python`、`pyplot` | 互操作 |

**工具（不属于标准库的使用层面）**：`test`（测试与随机检验）、`bench`（原 `perf`、`sim`）、`profiler`；`this`、`para::rendezvous` 移入测试设施；空壳 `io` 被新的 `io` 取代。

## 4. 需要语言作者决定的问题

1. 第 2 节的七条原则。
2. 第 3 节的布局，尤其是模块的拆分与改名（`statistics` → `stats`；系统层已拆成 `env`、`process`、`io`；`re`、`csv`、`json` 归入 `text` 命名空间还是保持独立的顶层模块）。
3. 前奏的内容（第 3 节开头）。
4. 原生函数以 `.cml` 中无函数体的声明为唯一的事实来源，C++ 只按名字绑定实现。
5. 错误约定：环境造成的失败返回 `T!`，程序的错误是故障（据此改写 `fs`、`env`、`time` 等）。
6. 迁移：一次改完（内测阶段不考虑兼容），还是分批改。

## 5. 官方扩展库（2026-10-20 定）

与标准库分开：标准库精简、随语言发布；扩展库由官方提供、单独维护，遵守同一套约定（第 2 节）。

| 档 | 库 | 要点 |
|---|---|---|
| 一（示例马上要用） | `datasets` | 常用数据集的下载、缓存、读取；Hugging Face 数据集经 Arrow/Parquet 直接读；大数据集以流读取 |
| | `data`、`optim` | 见第 3 节 |
| | `safetensors` | 读写预训练模型的权重 |
| | `cli`、`config` | 命令行参数解析；YAML、TOML |
| | `progress` | 进度条（写 `stderr`） |
| | `monitor` | 记录标量、直方图、图像；写 TensorBoard 兼容的格式，现成的 TensorBoard 可以直接查看 |
| 二 | `plot` | 声明式绘图（生成类似 Vega-Lite 的描述，输出 SVG、PNG、交互式 HTML）；`pyplot` 继续提供 matplotlib |
| | `image` | 封装 OpenCV：读写、缩放、颜色、几何变换、数据增强；图像是 `u8[h, w, 3]`，与 `cv::Mat` 零拷贝互转 |
| | `tokenizers` | 封装 Hugging Face tokenizers（C 接口） |
| | `frame` | 类似 Polars 的表格处理：惰性的查询计划就是图，投影下推、谓词下推等优化写成 `rule`；列以 Arrow 存储。列结构的确定方式见下 |
| 三 | `ml` | 经典机器学习（线性模型、K 均值、PCA 等），用 Camel 写 |
| | `audio` | 读写、重采样、频谱（用复数与 FFT） |
| | `sql` | 经 Arrow 与 DuckDB 零拷贝交换 |

### 5.1 表格的列结构（2026-10-20 定）

1. **表格的类型是 `Frame<行类型>`**：行类型是普通结构体；按列存储（每个字段一列数组），兼容 Arrow；可空列写 `T?`，对应 Arrow 的有效位图。
2. **列结构的三种来源**：
   - 手写：`type Sale = { year: int, region: string, amount: f64 }`，`read_csv<Sale>('sales.csv')` 返回 `Frame<Sale>!`（文件不存在或内容不符返回错误值）；
   - 编译期推出：`type Sale = schema_of<'sales.csv'>`，编译时读表头与前若干行推出行类型（类型函数，设计原则 5.6；先例 F# 类型提供器）；结果按文件内容缓存；编译时找不到文件是错误，提示用 `camel schema sales.csv` 打印推出的类型、贴进源码固定；
   - 运行时：`read_csv('upload.csv')` 返回 `Table!`（列结构在运行时，按列名访问，出错返回错误值）；`t as Frame<Sale>` 显式转换、转换处检查——与形状未知的数组用 `as` 对称（设计原则 5.4）。
3. **不需要 `collect()`**：`filter`、`group_by`、`agg`、`select`、`join` 是普通的纯函数；惰性与优化来自按需求值与 `frame` 库声明的 `rule`（投影下推、谓词下推、合并相邻操作）；运行时把流水线融合、按批流式执行，数据可以大于内存。
4. **结果的行类型由类型函数推出**：`agg` 的结果由返回的结构体推出；`select`、`join` 由类型函数计算；`join` 时列名冲突在编译期报错。
5. 官方示例：用 `frame` 与 Polars 对照代码与性能（见示例计划）。
