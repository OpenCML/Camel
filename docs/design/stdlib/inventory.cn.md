# Camel 标准库盘点（fsh-dev，2026-10-20，只读核对）

依据：docs/design-principles.cn.md（3、5、10、11、13 节）、docs/stdlib-reference.md、stdlib/、modules/、src/builtin/modules/、docs/design/{training,arrays-memory,showcase,io-concurrency-streams}、docs/language/05_modules.md、docs/reviews/2026-10-19/{frontend,modules}.cn.md。
标注"未验证"的是只读下无法确认的判断（没有运行构建产物）。个数口径：stdlib/signatures 里按函数名去重，重载不重复计。

## 1. 模块表

"默认可见"：全局内建不需导入；其余都要 `import`。模块查找：入口文件同目录 .cml、`-I` 目录、`CAMEL_PACKAGES`、随安装的 `out/latest/stdlib/`（.cml 与 .cmo 都放在这里）。
"使用者"栏的数字是 test/ 与 examples/ 等目录下 import 该模块的 .cml 文件数（测试/示例）。

### 1.1 内建（src/builtin/modules，C++ 编进核心）

| 模块 | 用途 | 实现 | 导出 | 主要导出 | 可见性 | 使用者 |
|---|---|---|---|---|---|---|
| globals（名字 ""） | 转换、输出、字符串/数组基础、高阶、Map/Set | C++（globals.cpp 1750 行 + src/builtin/operators） | 47 个名字（79 个重载） | int/i64/i32/float/f64/f32/string/parse_int/parse_float/error；print/println/input/sleep/exit；format/join/len；range/head/tail/slice/concat/contains/zip/append/extend；map/filter/reduce/foreach/apply/attempt；Map：get/get_or/put/remove/has/keys/values/items/merge；Set：add/union/intersect/difference/is_subset | 默认可见 | 全部 |
| profiler | 性能追踪 begin/end/instant/enable | C++ | 4 | begin、end、instant、enable | 仅 Debug 构建（`#ifndef NDEBUG`），需导入 | 0 个测试/示例 |
| test_attrs | 测试用装饰器 mark/flags/level/plain | C++ | 4 | 同左 | 测试用，需导入 | 3 个测试 |

### 1.2 用 Camel 写的库（stdlib/*.cml）

| 模块 | 用途 | 实现 | 导出 | 主要导出 | 可见性 | 使用者 |
|---|---|---|---|---|---|---|
| array | 数组重排、查找、聚合、变换 | Camel（171 行，靠 algo 三个原语） | 17 个名字 | reverse/take/drop/enumerate/flatten/unique；index_of/find/any/all/count；sum（int/float/i32/f32 四份重载）/min/max；flat_map/sort_by/zip_with | 需导入 | 10 测试 / 12 示例 |
| perf（文件 perf.cml，文件内写 `module util`） | 计时 | Camel（35 行） | 2 | timeit、perfms | 需导入，名字按文件名 `perf` | 1 / 1 |
| tensor_definitions | tensor 算子的规范定义图（softmax/log_softmax/mean/relu/sigmoid，仅 f32） | Camel（63 行） | 5 | 同左 | 不供用户导入；核心在需要定义图时自己编译（C++ 侧 `declareDefinition`） | 1 测试 |

### 1.3 C++ 模块（modules/*，每个编成 .cmo）

| 模块 | 用途 | 导出 | 主要导出 | 使用者（测试/示例） | 备注 |
|---|---|---|---|---|---|
| str | 字符串（按字节切分/修剪/大小写/替换/查找；码点编解码） | 16 | split/trim/trim_start/trim_end/to_upper/to_lower/replace/find/substr/starts_with/ends_with/repeat/bytes/from_bytes/codepoints/from_codepoints | 21 / 6 | |
| bytes | 不可变二进制 `Bytes` 与 UTF-8 互转 | 6 | encode/decode/from_array/to_array/size/concat | 12 / 0 | |
| tree | 参数树与检查点 | 7 | tree_map/tree_reduce/tree_unzip/tree_leaves/tree_paths（类型导向的编译期内联）；save_tree/load_tree（真算子） | 10 / 0 | 不是空壳；见 2.6 |
| tensor | 张量类型与算子；.npy 读写；二进制转换 | 约 60 个名字（含 `Tensor::` 限定副本）；signatures/tensor.cml 只登记 to_bytes/from_bytes | new/zeros/ones/full/eye/range/random/randn/seed/reshape/transpose/permute/concat/slice/where/cast/sum/mean/max/min/matmul/linear/softmax/log_softmax/layer_norm/relu/gelu/sigmoid/tanh/exp/log/sqrt/abs/pow/maximum/minimum、load_npy/save_npy/show/set_num_threads/gemm_backend | 299 / 39 | 大头在 C++（kernels/ops/passes）；运算符 `__add__` 等也由它导出 |
| nn | 网络层、损失、优化器 | 约 19 + 类型 OptimizerState | conv2d/max_pool2d/avg_pool2d/batch_norm/batch_norm_train/embedding/softmax_cross_entropy 及各 `_grad`；sgd/adam_state/adam/save_params | 37 / 30 | 链接 tensor |
| autodiff | 自动微分 | 4 | grad/with_grad/vjp/stop_gradient | 95 / 34 | |
| onnx | 导出 ONNX | 2 | export_model/supported_operators | 75 / 31 | 链接 tensor；export_matrix.md 由脚本生成 |
| tiles | GPU/CUDA 区域编译（实验） | 约 12 + 类型 Device | run/run_device/upload/download/available/device/gpu/regions/inspect 等；`tiles::compile` 遍 | 72 / 0 | README 标明 experimental；协作线另一台机器 |
| math | 常量与数值函数、位运算、进制 | 41 个函数 + 4 个常量（pi/e/inf/nan） | sqrt/exp/log*/三角双曲/floor/ceil/round/trunc/abs/sign/min/max/clamp/pow/hypot/gcd/lcm/is_nan/is_inf/bit_*/shl/shr/hex/bin/oct | 30 / 9 | 算子没有数值声明（评审第 15 条） |
| statistics | 描述统计 | 5（每个 4 份类型重载） | mean/stdev/variance/median/quantile | 6 / 3 | |
| algo | 排序与数组原语 | 8 | sort/insert_sort/quick_sort/merge_sort/merge_sorted_arrays；flatten/unique/argsort（array 的原语） | 11 / 4 | |
| random | 带全局状态的随机数 | 7 | seed/rand/randn/randint/choice/sample/shuffle | 13 / 2 | 无键式随机数 |
| time | 时钟与格式化 | 3 | now（float 秒）/strftime/strptime | 25 / 14 | |
| fs | 文件系统 | 13 | read_text/try_read_text/read_bytes/read_chunk/write_text/write_bytes/replace/exists/is_file/is_dir/file_size/mkdir/mkdirs | 13 / 4 | 全部 @effect |
| os | 进程与终端 | 8 | sleep/whoami/cpu_count/set_terminal_raw_mode/has_input/get_char/get_chars/clear_input_buffer | 7 / 1 | 另有 exit 在全局 |
| env | 环境 | 2 | get_env/get_cwd | 105 / 33（几乎都是入口参数用 get_env） | |
| json | `Json` 类型与编解码 | 12 | encode/decode/decode_as；kind/is_null/get/as_bool/as_int/as_float/as_string/as_array/as_object | 17 / 2 | |
| re | 正则 | 1 | test | 4 / 1 | |
| crypto | 哈希 | 2 | sha256（str/Bytes 两重载）/sha256_rounds | 6 / 0 | |
| para | 无序并行与同步 | 3 | unordered_foreach/unordered_reduce/rendezvous | 11 / 0 | rendezvous 看起来是测试设施 |
| this | `zen()` 彩蛋 | 1 | zen | 4 / 0 | |
| io | 空壳：module.cpp 注释写 print/println/input 已移入内建，`getOperatorGroups()` 返回空 | 0 | 无 | 1 测试（registry_all） | 仍编译、仍可导入 |
| sim | 两个蒙特卡洛基准内核 | 2 | pi_hits、asian_call_price | 6 / 3 | 领域专用，更像基准而不是库 |
| python | 嵌入 Python（pybind11） | 11 + 类型 PyObject | py_call/py_exec/py_eval/py_run/py_import/py_attr/py_wrap/py_unwrap/py_print/py_println/py_dispose | 6 / 0 | 构建时可选；README 在 modules/python |
| pyplot | matplotlib 绘图 | 1 | plot | 2 / 0 | README 示例写 `pyplot.plot(...)`，而不是 `pyplot::plot`，文档过时（推断） |

没有签名文件的模块（stdlib/signatures 只有 18 个文件）：io、nn、autodiff、onnx、sim、python、pyplot、tiles；tensor 只登记 2 个函数。docs/stdlib-reference.md 明说这些不在参考里。

## 2. 重叠与不一致

### 2.1 同一件事多处实现或多个名字

- `sleep`：全局 `sleep` 与 `os::sleep` 是同一个算子（signature 里的 URI 都是 `os:sleep`）。
- `exit` 在全局，其余进程功能在 `os`；环境变量/工作目录在 `env`，用户名/CPU 数在 `os`。`os` 与 `sys` 的划分没有依据。
- 排序：`algo` 导出 sort/insert_sort/quick_sort/merge_sort 四个名字（只接受数字元素），`array::sort_by` 是另一条路；原则 11.3 说"只有 `sort`，返回新数组"，docs/language 5.2.1 也写"每种只有一个名字"，实际不符。没有对任意可比较元素的通用 `sort`。
- `min`/`max`：`math` 是两数（数值，4 重载），`array` 是数组→`T?`，`tensor` 是张量（`max(x, axis, keepdim)`）；`sum`、`mean` 在 array/statistics/tensor 各有一份，`softmax`、`exp`、`log`、`sqrt`、`abs`、`pow` 在 math 与 tensor 各一份。导入多个模块时靠重载/改名区分。
- `range`、`slice`、`concat`、`len` 在全局与 `tensor`（range/slice/concat）里同名；`seed`/`randn` 在 `random` 与 `tensor`（带自己的 seed/random/randn）里同名。后者是否共享生成器：未验证。
- 字节相关：`str::bytes(s)`/`str::from_bytes(xs)` 的"bytes"是 `int[]`；`bytes` 模块的 `Bytes` 另用 `to_array/from_array`、`encode/decode`；`tensor::from_bytes` 又是 `Bytes→Tensor`。`from_bytes` 有三种含义（str、tensor，加 bytes 的 from_array）。原则 11.1 的"字节序列用 `Bytes`"在 str 里没有贯彻。`bytes::concat`/`bytes::size` 与全局 `concat`/`len` 同义。
- 序列化散落：`tensor::load_npy/save_npy`、`tree::save_tree/load_tree`、`nn::save_params`、`fs::write_*`、`json::encode/decode`，命名没有统一模式（save/load/write/replace/encode）。`fs::replace`（原子替换文件）与 `str::replace` 同名不同义。
- Set 同时有 `has` 与 `contains`（global.cml 两者都有）；Map 有 `get<key>`、`has`，原则 11.3 示例用下标 `m['a'] ?? 0`；Json 又有自己的 `get`。原则 11.4 要求"只保留一种写法"，现状没收敛。
- 全局 `head`/`tail` 是 `slice` 的特例；`extend` 与 `concat` 功能重复；`apply<f>`（T→T）与 `map` 的区别未在文档说明（未验证它的语义）。
- `i64`/`f64` 与 `int`/`float` 是别名（各 5 个重载）。

### 2.2 命名

- 全部是 snake_case，类型 PascalCase；没有 camelCase。未统一的是动词：
  - 取值：`get_env`/`get_cwd`/`get_char(s)`（get_ 前缀）对 `now`/`whoami`/`cpu_count`（无前缀）。
  - 转换：`to_upper`/`to_lower`/`to_array`/`to_bytes`（to_x）与 `from_bytes`/`from_array`/`from_codepoints`（from_x）成对，但 `str::bytes`、`str::codepoints`、`json::as_int`、`json::encode`、`bytes::encode`、`string(x)`、`parse_int` 走另外的模式。`encode/decode` 在 `json` 与 `bytes` 里意义不同。
  - 错误变体：`fs::try_read_text` 用 `try_` 前缀，`parse_int` 直接返回 `int!`，`fs::read_text` 没有错误形式的"对应物"之外的规律；全库只此一例 `try_`。
  - `unordered_foreach`/`unordered_reduce`（para）对 `foreach`/`reduce`（全局）。
  - `size`（bytes）、`file_size`（fs）、`len`（全局）、`numel`（tensor）都是"个数/大小"。
  - 参数名：`substr<start, length>` 对 `slice<start, end>`，起止语义不同。
- 文件名与模块名：`stdlib/perf.cml` 里是 `module util`，导入时用 `perf`；`modules/this` 的目录名/模块名是 `this`（与关键字风格相近）。

### 2.3 参数顺序与调用风格（原则 4.4、11.4）

遵守"主输入为普通参数、配置为 with"的：str 全部、array 全部、statistics::quantile、json 访问器、tree_*（树普通，回调 with）、autodiff（grad(f) 函数作主输入）、Map/Set 操作。

违反或有分歧的：
- `format<args...>`：被格式化的数据放在尖括号里，格式串是主输入（`'{} is {}'->format<a, b>`），与 4.4 相反；`print<fmt>(...)` 方向正确。
- `re::test(pattern, text)`：被处理的文本在第二位，不能写成 `text->test(...)`；其余库都是"数据第一"。
- `fs` 全部用普通参数且路径在前：`read_chunk(path, offset, size)`、`write_text(path, content)`；`time::strftime(ts, format)`、`strptime(text, format)`；`random::sample(xs, n)`；`crypto::sha256_rounds(s, rounds)`；`json::encode(value, indent)`；`math::clamp(x, lo, hi)`；这些"配置"用的是普通参数。同一库里 `str::repeat<count>`、`array::take<n>` 却用 with。
- tensor 的配置全是普通参数：`sum(x, axis, keepdim)`、`reshape(x, shape)`、`max(x, axis, true)`（见 tensor_definitions.cml）。`tiles::run<f>(args)`、`attempt(f, args)` 把函数放在不同位置。
- 原则 4.4 末句"标准库各函数的归属在示例写出来后逐个确定"：这项审查尚未做。

### 2.4 错误约定（原则 3.9：可能失败的操作返回 `T!`，故障是程序错误）

- `fs` 几乎全是故障：`read_text`、`read_bytes`、`read_chunk`、`write_*`、`file_size`、`mkdir(s)` 打不开文件都是 fault；只有 `try_read_text` 返回 `string!`。文件缺失是典型的"可能失败的操作"，应是 `T!`；`mkdir` 同时返回 bool 又可能故障。
- `env::get_env` 找不到变量是 fault（RuntimeError），应当是 `string?`；`os::set_terminal_raw_mode` 终端拒绝是 fault。
- `time::strptime` 文本与格式不符是 fault（TimeFormatFailed），而 `parse_int` 对同类问题返回 `int!`。
- `re::test` 的坏正则是 fault（可以接受，但与 `json::decode` 的坏文本返回 `Json!` 的处理不同）。
- 空集合：`head([])`、`random::choice([])` 是 fault，`array::min/max/find`、`statistics::median/quantile` 返回 `T?`。
- `T!` 的只有：parse_int、parse_float、from_codepoints、bytes::decode、fs::try_read_text、json::decode/decode_as、tree::load_tree、attempt。`T?` 的：str::find、array 查找/极值、statistics::median/quantile、json 访问器。
- 故障种类名没有统一枚举（InvalidArgument、RuntimeError、FileOpenFailed、FileSystemError、TimeFormatFailed、RegexInvalid 混用；RuntimeError 过于笼统）。

### 2.5 效应、资源、纯度声明

- 纯度：仓库内模块有测试强制逐个声明（`warnUndeclaredOperatorTraits` 对第三方给警告；test/plans/feat/modules/stdlib_registry.plan.toml）。但 `tree` 为过测试把永不成节点的名字声明为有副作用（评审 frontend #10）；`math` 无数值声明（评审 modules #15）。
- 资源：C++ 侧已有 `declareResources`（fs、os、env、time、random、para、algo、nn::save_params、tree、tiles 都有），内建 print/println/input 在 builtin_semantics.cpp 声明。资源名是 `io:stdout`、`io:stdin`、`io:terminal`、`time:clock`、`process:environment`、`process`、`rand:generator`、`fs`、`profiler`，与原则 3.3 约定的默认资源名（`stdout/stderr/stdin/fs/clock/env`）不一致；`stderr` 没有对应的输出函数。
- 签名文件里的 `@effect` 是自由文本（"reads a file"、"advances the generator"、"what f does"），不是资源名；`.cml` 签名头注明"for reference only: not compiled"。算子的类型规则、纯度、资源、故障在 C++ 注册、OperatorTable 声明、signatures/*.cml 三处各写一遍（评审 frontend #10）。
- 语言层面 `uses`/`pure`/`resource` 的语法：docs/language 里没有出现（只有 design-principles 与 blueprint 描述），推断尚未实现；所以库作者现在无法用 Camel 声明资源。
- `random` 全部 @effect（全局生成器）；原则 13.5 的键式纯随机数尚未实现。

### 2.6 空壳、半成品、测试设施外露

- `io`：空模块，仍随构建发出，一个测试 import 它。可删。
- `tree`：不是空壳。tree_map 等五个是类型导向的内联，核心建图器按 URI 硬编码展开（评审 modules #1），`stdlib` 层面只有签名登记。已出现在 stdlib-reference。
- `sim`：两个基准内核，领域专用；`para::rendezvous`、`this::zen`：测试/彩蛋性质。
- `tiles`：实验模块；导出里有 `test_call`、`test_compiled_call` 等，测试用，在流水线里写出名字即可用。
- `test_attrs` 受环境变量控制；`profiler` 仅 Debug。
- `tensor_definitions` 不是给用户用的库，却放在 stdlib 目录下与用户库同级。
- `perf` 的 `timeit` 用递归、`println` 和 `format` 打印，不适合当库接口；`perfms` 返回元组（均值, 误差）。
- `pyplot` 只有一个 `plot`；`re` 只有一个 `test`。两者是"占位"程度。
- `algo` 的三个具体排序（insert/quick/merge）是演示性的。

## 3. 缺口

按对 Camel 目标（AI 栈、官方示例、智能体）的重要程度排序。

| 缺口 | 现状 | 为什么需要 |
|---|---|---|
| 网络与 HTTP（`net`、`http`） | 无；设计稿 io-concurrency-streams 2.5 已定，未实现 | D9 智能体示例要调用模型与工具；roadmap 列为近期 |
| 长耗时副作用的并发：无序资源、超时、竞速、取消 | 无；`para` 只有 unordered_foreach/reduce | 智能体同时调用多个工具；设计稿 2.1-2.4 |
| 流类型与窗口算子 | 无 | 流式接收模型输出、流处理（设计稿第 3 节） |
| 键式（无状态）随机数 `key/fold/split/normal/uniform/randint` | 无；`random` 只有全局状态，tensor 的 random/randn 同；代码里找不到 Philox | 训练原则 5、整步上设备、检查点可复现（原则 13.5；训练稿 4.1） |
| 数据加载与分批：`gather`、分片读取器、预取、数据集清单 | tensor 无 `gather`（未在 ops 里找到）；无读取器；示例靠 read_chunk + Python 脚本 | C1-C6、I1-I3 都要；原则 13.6 说由标准库提供；训练稿 T4 |
| 优化器与学习率调度库 | nn 里只有 sgd、adam/adam_state（C++）；无 AdamW、动量以外变体、调度器、梯度裁剪、EMA、梯度累积 | 训练示例；训练稿 3.5 要求用 tree_map 在 Camel 里写 |
| 表格与 CSV/Parquet/Arrow | 无；examples/text/csv_parse.cml 手写 | 数据管线；设计稿第 4 节（Arrow C 接口） |
| 压缩（gzip/zstd/tar/zip） | 无 | 数据集与检查点分发 |
| 日期时间 | 只有 `now` 浮点秒、strftime/strptime；无日期类型、时区、时长、单调时钟 | 日志、实验记录、计时（perf 靠 now 相减） |
| 正则完整接口 | 只有 `test` | 日志过滤、文本抽取（examples/text/log_filter） |
| 字符串缺口 | 无 lines/按码点 len 与切片/pad/rfind/strip_prefix/分行/大小写以外的 Unicode 处理；`len` 按字节（globals.cml 文档），原则 11.1 要求按码点 | 文本处理、分词、示例 word_frequency |
| 通用 `sort`、`group_by`、`zip` 以外的集合 | array::sort_by 只有键排序；无 group_by（需 Map，array.cml 头注释已说明）；无 chunk/window/partition/dedup_by | 数据处理示例 |
| 进程与命令行 | 无 spawn/exec；无命令行解析（只有 env::get_env 与 `--arg`） | 工具型程序、调用外部命令、智能体的代码执行工具 |
| 日志 | 无 | 长时间训练与智能体 |
| 哈希与摘要扩展 | 只有 sha256；无 md5/sha1/blake3/hmac、随机字节、编码（base64/hex） | 检查点清单哈希、数据版本（训练稿 8.2） |
| 序列化格式 | 只有 json、.npy、tree 检查点；无 safetensors/msgpack/yaml/toml | 模型权重交换（PyTorch/HF 生态） |
| stderr 输出与错误打印 | 无 `eprintln`；资源名 stderr 已在原则中，库里没有 | 原则 3.9 要求未处理错误打印到标准错误 |
| 路径与目录操作 | fs 无 list_dir/remove/rename/copy/path join/临时文件 | 数据集分片、检查点轮转（训练稿 8.2/8.4） |
| 数值函数 | math 无 cbrt/exp2/asinh 等反双曲、isfinite、整数除余/floor_div、gamma/erf（tensor 里有 erf） | 科学计算、示例 ode/hmc |
| 线性代数以外的数值库 | 无 FFT、稀疏、量化（arrays-memory 稿说"标准库先给样板"）、复数函数 | D8 图神经网络、I3 推荐、复数类型已进语言 |
| 集合通信 | 无 | 多卡训练（训练稿第 9 节，NCCL 在 Tiles 侧） |
| 测试/断言库 | 仓库测试靠 .cml 加期望输出，库里无 assert、近似相等 | 官方示例"结果一致"按数值契约比较 |
| 张量以外的数组数值运算 | 数组上无向量化运算（dot、cumsum、argmax）；要转成 Tensor | 小规模数值代码 |

## 4. 边界核对（原则 10.5：原语用 C++，组合用 Camel）

### 4.1 C++ 实现但可以是 Camel

位置在 modules/*/ 与 src/builtin/modules/globals.cpp。按"能否用现有原语组合"判断；是否值得改要看性能与图可见性，下面只列事实。

- **nn 优化器** `nn:sgd`、`nn:adam_state`、`nn:adam`（modules/nn/optim.cpp，`OptimizerResolver`）：训练稿 3.5 已给出用 `tree_map` 在 Camel 里写 `adam` 的样子；C++ 版是图上不透明节点，`pure` 手工设置。优先级最高。
- **nn 损失/层的组合部分**：`softmax_cross_entropy`、`softmax`、`log_softmax`、`layer_norm`、`linear` 有规范定义图（tensor_definitions.cml 已部分是 Camel），内核可保持 C++。
- **globals 里的 Map/Set 组合操作**：`get_or`、`merge`、`union`、`intersect`、`difference`、`is_subset`、`items(Set)`、`values`（`:coll/*` 算子）可由原语 `get/put/has/keys/add` 组成；原语本身需在 C++（内建类型）。
- **globals 数组辅助**：`head`/`tail`（=`slice`）、`extend`（=`concat`）、`contains_arr`（=`array::any`）、`apply`。已经是 C++ 内建，保留是为了性能与早退，注意 array.cml 的 `find/any/all/count` 不早退。
- **str**：`trim_start/trim_end`、`starts_with`、`ends_with`、`repeat`、`contains_str` 可由 `substr`/`find`/`codepoints` 组成；`to_upper/to_lower`、`split`、`replace`、`find` 涉及 UTF-8 与性能，留 C++ 合理。
- **algo**：`insert_sort`、`quick_sort`、`merge_sort`、`merge_sorted_arrays` 是组合逻辑（可用 Camel 写成，也可删），`sort`/`argsort`/`flatten`/`unique` 是原语。
- **statistics**：`mean/stdev/variance/median/quantile` 由 `reduce`+`sort` 可写成泛型 Camel（现在是 4 个元素类型各一份 C++ 重载）；与 `array::sum` 同样受"没有数值类型变量"限制。
- **random**：`choice`/`sample`/`shuffle` 可由 `randint` 组合；`seed/rand/randn/randint` 是原语。
- **math**：`clamp`、`sign`、`min`/`max`（两数）、`gcd`/`lcm`、`hypot`、`hex/bin/oct`、`expm1`/`log1p`（后两个需要精度，留 C++）。
- **fs**：`read_text`（=`decode(read_bytes)`，但有流式与错误形式问题）、`write_text`（=`write_bytes(encode)`）、`mkdirs`（=循环 `mkdir`）、`is_file/is_dir/exists` 可共用一个 stat 原语；`try_read_text` 与 `read_text` 的错误形式差异可在 Camel 层一次解决。
- **json**：`as_*`/`get`/`kind`/`is_null` 是对 `Json` 值的访问，类型本身在 C++；访问器的 `Json?`/`Json!` 链式重载可由一个 C++ 原语加 Camel 包装产生。
- **crypto::sha256_rounds**：`sha256` 的循环，Camel 即可。
- **this::zen**：常量字符串。

### 4.2 Camel 实现但应是（或可能是）C++

- `array::find/index_of/any/all/count`（`first_true` 用 `reduce` 扫描全部元素、不早退，并且先 `map` 出整个布尔数组）：文档接受这是语义（并行 map），但 O(n) 内存与不早退对大数组不友好；是否上 C++ 取决于是否要早退语义。
- `array::sum` 四份重载（int/float/i32/f32）：不是边界问题，是缺"数值类型变量"。
- `array::reverse`（`range`+`map`）：可换成原语以避免闭包开销（未测）。
- `perf::timeit`：递归 + 打印；应是返回时间的纯函数或 `time` 里的小工具，不应在库里 `println`。
- 没有发现"本该是 C++ 的性能关键 Camel 库"之外的情况；Camel 写的库总共只有 array、perf、tensor_definitions 三个文件，共约 270 行。

### 4.3 其他

- `tree_map` 等五个类型导向算子写在 C++ 里并被核心硬编码展开（评审 modules #1）；评审建议长期用 Camel 写（依赖类型函数里的结构匹配，原则第 15 节未决）。
- tensor 的 `show`、`load_npy/save_npy`、`num_threads` 等工具混在张量算子表里。

## 5. 规划用事实（模块系统能力）

- **命名空间**：支持 `namespace N { ... }`、嵌套、`using namespace`（仅在所在块有效）、导入改名 `import { a as b } from m`、模块别名 `import m as x`、`module` 自己的名字可限定自己、`import { x } from m::N`。同层同名函数合成重载集，跨层不合并（运算符例外）；两个导入同名同签名是 ImportNameConflict。stdlib 目前没有任何 C++ 或 Camel 模块使用命名空间；tensor 导出 `Tensor::name` 限定副本是唯一类似做法。
- **默认可见（prelude）**：只有 globals（模块名 ""）一个，名字写死在 src/builtin/modules/globals.cpp，用户不可扩展。优先级里"内建"最低，用户模块可覆盖。没有"再导出（re-export）"之外的 prelude 配置机制（`export { a, b }` 只导出本模块已声明的名字；是否支持转导出：未验证）。
- **资源名可见性**：原则 3.3 说基础资源默认可见，语言层 `resource`/`uses` 语法目前没有（推断）。
- **版本**：模块无版本号。C++ 模块有 ABI 版本 `CAMEL_MODULE_ABI_VERSION = 3`（include/camel/core/module/dynamic.h），.cmo 导出 `camel_module_abi_version()` 与 `camel_module_create(Context*)`，加载前检查。原则 10.5 要求"公开 C++ 接口按版本承诺兼容"，尚未建立；.cml 模块没有版本、没有包清单。
- **模块缓存**：`.cmo` 缓存（`__cmo__/`）默认关闭。C++ 模块与缓存共用扩展名，靠文件头区分。
- **C++ 模块声明签名的方式**：每个模块在 `module.cpp` 里继承 `BuiltinModule`，构造函数用 `exportEntity(name, OperatorGroup)`/`exportType` 导出；类型规则用 `StaticFuncTypeResolver`（固定签名）或 `DynamicFuncTypeResolver`（按实参算，用户重载会悄悄胜出，评审 frontend #11）；`load()` 里 `registerExecutorFactory`、`OperatorTable::declare` 声明纯度、`declareResources` 声明资源、`declareSemantics` 声明数值语义（只有 tensor 做）。tensor 另用 `OpDef`（name/exports/params/infer/kernel/traits，modules/tensor/ops）一个条目生成多项。
- **stdlib/signatures 的角色**：纯参考元数据，不编译；`node scripts/stdlib-reference.mjs --write` 生成 docs/stdlib-reference.md，`--verify-ops` 对照 `camel inspect --ops` 检查算子名、参数个数、纯度是否一致。只覆盖 18 个模块，是三处来源之一。
- **用 Camel 写的库怎么被找到**：`stdlib/*.cml` 随 collect-out 拷到 `out/latest/stdlib/`，按文件名导入；它们是泛型函数，在调用模块按元素类型实例化，全程序一张 `__root__`。库可以导出值（C++ 的 `math::pi` 也是值）；不能导出 `var`；既有标注函数又有其他重载的名字目前不能导出（ConstructNotSupported）。
- **构建**：`modules/camel_module.cmake` 的 `add_camel_module(NAME ... [USE_PYTHON])`，共 25 个子目录，linking tensor 的模块（nn、onnx、tree、tiles）要用 `$ORIGIN` rpath 与同目录的 tensor.cmo。
- **文档同源**：写在 `///` 注释里的 `@pure/@effect/@fails/@example`，Camel 写的库同样被读取。
- **相关设计稿**：训练稿（tree/Bytes/键式随机数/分片读取器/检查点）、io-concurrency-streams（net/http/流/Arrow，已定稿未实现）、arrays-memory（原生数组，tensor 模块不改名，Tensor 保留为别名）、showcase（C1-C6、D1-D9、I1-I4 的依赖清单）。roadmap 第 83 行："标准库的重新规划与设计：待讨论（盘点进行中）"。
