# 命令行的形状

## 初心

命令行是"遍即函数"的另一张脸：一行命令说的是**一个程序、它的输入、一条流水线**，外加只关乎这个进程的设置。每一项设置只有一个归宿，同一件事只有一种写法。今天的命令行是逐步长出来的：有的设置只属于某个遍却做成了全局选项，有的事有两三种写法，有的单字母在不同子命令里意思不同。这里按三条规则把它收拢，不计兼容（设计原则第 1 节）：

1. **只属于一个遍的设置是那个遍的参数**（设计原则 9.3），写成遍名之后的 `key=value`；全局选项只留进程级的事：日志、诊断的格式与上限、回收器与内存、设备与线程、模块搜索路径。
2. **同一件事只有一种写法。** 流水线就是遍名一个接一个，不再有第二种拼法；从图开始的转储都是遍；选执行器就是写调度遍的名字。
3. **一个名字在所有子命令里同一个意思。** 长名是正式写法；单字母只给最常用的几项，并在每个子命令里意思相同。

## 一行命令的组成

```
camel [进程选项] <程序文件> [--arg 名字=值]... [遍 [键=值]...]...
camel check   [进程选项] [--stage lex|syntax|semantic] <文件或目录>
camel inspect <tokens|cst|ast|gct|ops> <文件>
camel bench   [进程选项] <程序文件> <遍> [--runs N] [--arg ...]
camel explain <诊断名或编号>
camel docs    <模块>
camel about | camel zen
camel --version | camel --help
```

第一个不以 `-` 开头的词是程序文件，之后的词是流水线：遍名开始一项，紧跟的 `键=值` 是它的参数。遍名与源码里的名字走同一套解析（遍即函数计划第 1 步），所以这样写没有歧义，不需要 `--input`、`--passes`。

## 逐项去留

### 程序与输入

| 今天 | 之后 | 理由 |
|---|---|---|
| 位置参数 / `--input <file>` | 只留位置参数 | 同一件事一种写法 |
| `--arg name=value` | 保留 | 绑定就是对 `main` 的部分应用，已与遍参数同一写法 |
| `-o`, `--output <file>` | `--result <file>` | `main` 的值由驱动程序写出（设计原则 9.8），这个选项只选去处；`-o` 让给 `camel-cpp` 的可执行文件 |

### 流水线

| 今天 | 之后 | 理由 |
|---|---|---|
| `--passes <list>`（逗号分隔） | 删 | 位置参数就是流水线 |
| `-S`, `--scheduler <type>` | 删 | 写调度遍的名字（`std::fvm`）就是选择 |
| `--max-macro-depth`、`--max-macro-rounds` | `std::macro max_depth=… max_rounds=…` | 只属于 `std::macro` |
| `-P`, `--profile`、`--profile-mode` | 删 | 已有 `std::profile` 遍 |
| `--timeit <pass>/<N>` | 子命令 `camel bench <文件> <遍> --runs N` | 测量是另一种用法，不是一次运行的选项；也不再借用诊断格式选项 |
| `--dump-after-each-pass <dir>`、`--time-phases[=file]` | 保留（长名） | 观察整条流水线，属于宿主一侧的驱动程序 |
| 调度遍的长短两套名字（`std::nodevm`/`std::nvm`、`std::fastvm`/`std::fvm` 等） | 每个只留一个名字（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`、`std::tf`） | 一个遍一个名字；`std::default` 是一个转发声明，指向的后端以设计原则 9.2 为准（今天文档写的是节点虚拟机、原则写的是 `std::fvm`，以原则为准） |

### 进程级设置

| 今天 | 之后 | 理由 |
|---|---|---|
| `-l`/`--log-level`、`-v`…`-vvvv`、`--log-preset`、`--log-include` | 保留 | 进程级 |
| `-E`/`--error-format`/`--output-format`（运行）、`-O`（`check`） | `--diag-format <text\|short\|json>`，各子命令同名 | 三个名字说一件事 |
| `--color`、`--max-errors`、`-W…` | 保留 | 进程级 |
| `-N`, `--max-warning` | `--max-warnings` | 复数、只用长名，`-N` 不再占用 |
| `--gc <collector>`、`--graph-gc-mb`、`--gc-nursery-mb`、`--gc-promotion-age`、`--gc-large-threshold-kb`、`--gc-large-budget-mb`、`--gc-growth-percent`、`--gc-heap-limit-mb` | `--gc kind=generational,nursery_mb=4,promotion_age=4,…,graph_mb=64` | 一个选项一组键值，与 `--gc-stress` 同一风格 |
| `--block-limit`、`--space-segment-kb`、`--meta-space-limit-mb`、`--graph-space-limit-mb`、`--perm-space-limit-mb` | `--spaces block_mb=2048,segment_kb=…,meta_mb=…,graph_mb=…,perm_mb=…` | 同上 |
| `--gc-stress`、`--gc-check`、`--coll`、`--native-stack-guard` | 保留 | 进程级、已是键值写法 |
| `--max-recursion-depth`、`--devices`、`--tensor-isa`、`-t`/`--threads` | 保留 | 进程级 |
| `-I`/`--include`、`-L`/`--stdlib` | 保留 | 进程级 |
| `-n`, `--no-cache` | `--no-cache` | 不常用，不占单字母 |

### 信息与子命令

| 今天 | 之后 | 理由 |
|---|---|---|
| `-V`/`--version`、`-h`/`--help` | 保留 | 惯例 |
| `-d`/`--docs`、`-a`/`--about`、`-z`/`--zen`、`--explain` | 子命令 `docs`、`about`、`zen`、`explain` | 腾出单字母；它们不是一次运行的选项 |
| `check -i`/`-s` | `check --stage lex\|syntax`（默认 `semantic`） | 一个选项说停在哪一阶段 |
| `check -o`/`--output` | `check --report <file>` | `-o` 不再一词两义 |
| `check -c`/`--config`、`-e`/`--ignore` | `--rules <file>`、`--no-rules` | 长名说清是规则定义 |
| `inspect --tok/--cst/--ast/--gct`（及大小写两套单字母） | `inspect tokens\|cst\|ast\|gct` | 图之前的阶段只有这里看得到 |
| `inspect --ops` | `inspect ops` | 同上 |
| `inspect --gir`、`--tns`、`--pass-until`、`--instances`、`--facts`、`--tags`、`--copies` | 删；对应的都是遍（`std::gir`、`std::gir_regions` 等），`--pass-until` 就是写出流水线的前一段再接 `std::gir std::nop` | 从图开始的转储都是遍 |
| `inspect --gen` | 删 | 与 `camel-format` 重复 |

## 单字母一览

只留：`-h`、`-V`、`-v`（可重复）、`-l`、`-I`、`-L`、`-t`。别的都用长名。`camel-cpp` 自己的 `-o` 指可执行文件。
