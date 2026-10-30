# Camel CLI 使用说明

[English](cli.md) | 中文简体

> 本文与英文版 [cli.md](cli.md) 对应。文中的示例输出、诊断文本和默认值按当前实现写出。

    camel [options] [<target file>]
    camel <command> [command-options] <target file>

默认的 run 命令中，输入文件可以写为位置参数，也可以通过 `--input <file>` 指定，二者至少提供一个。

## 通用选项（日志）

日志由 **全局级别阈值** 与可选的 **scope 过滤** 控制。日志开启时（阈值不是 `off`）写到 **stderr**。未作任何设置时，默认阈值为 **`fatal`**（只输出 fatal 级别的行）。

### 级别阈值

- `-l`, `--log-level <level>`：`fatal` | `warn` | `info` | `debug` | `trace` | `off`  
  达到或高于所选级别的消息才可能输出（例如 `warn` 允许 `warn` 与 `fatal`）。
- 快捷方式（等价于设置 `--log-level`）：
  - `-v`, `--verbose` → `warn`
  - `-vv` → `info`
  - `-vvv` → `debug`
  - `-vvvv` → `trace`

若同时出现 `--log-level` 与 `-v*`，以命令行中 **后出现者** 为准。

### Scope 过滤（在阈值之后应用）

- `--log-preset <none|wall|extra>`：把日志限制在文档列出的 scope 前缀集合内（`none` 表示不使用预设过滤）。
- `--log-include <scopes>`：逗号分隔的 scope 前缀；一旦指定，过滤时 **取代** 预设列表。一条日志的 scope 必须与某个前缀相等，或以 `prefix.` 开头。

**注意：** 在 `libcamel` 的 **release** 构建中，`debug` 与 `trace` 的 **宏** 在编译期被删除，因此 `-vvv` / `-vvvv` / `--log-level debug|trace` 虽被 CLI 接受，却不会显示这些行。要看到它们，请使用 **debug** 构建。

---

## 主执行入口（默认行为）

以指定的 `.cml` 文件或目录作为入口执行。**输入文件** 必须通过位置参数或 `--input <file>` 给出（至少一个）。

- `--input <file>`：输入文件路径（位置参数的替代写法；与 `--passes` 一起使用可避免歧义）  
- `--passes <list>`：逗号分隔的遍列表（例如 `--passes std::gir,other`）；每个遍后面跟它自己的 `key=value` 参数词，逗号只分隔遍。省略时，第一个位置参数是文件，其余是遍。遍可以带参数：`std::opt safe_fp=true`（见“遍的参数”）  
- `--timeit <pass>/<N>`：从全新的图出发把一个遍测量 `N` 次；文本模式打印各次样本和一张简表，`--output-format json` 则输出结构化的基准数据。失败的那一轮会点出它是第几轮（从 1 起）：文本模式打印 `[timeit] i/N ... failed` 一行，JSON 模式输出一个 timeit 对象，带 `"iteration"`、诊断（没有诊断时是 `"failure"` 一句话）以及失败前已完成的样本  
- `--dump-after-each-pass <dir>`：把第一个遍之前的图（`00_input`）和每个返回图的遍之后的图（`NN_<pass>`，`::` 写作 `.`）写入 `<dir>`：一份可读的 DOT（`.rgir.dot`，即 `std::rgir` 格式，每个节点带 `origin`/`span`/`srcfile`）和 `std::stats` 的计数（`.stats.json`）。调度遍和翻译遍消耗图，不留下快照。比较两份快照用 `python tools/graph-diff/graph_diff.py BEFORE AFTER`  
- `--arg <name>=<value>`：`main` 的一个参数的值（可重复；见“`main` 带参数或有返回值的程序”）  
- `-o`, `--output <file>`：`main` 的返回值写到哪里（默认：标准输出；见同一节）  
- `-P`, `--profile`：记录各阶段耗时，把跟踪文件写到 `profile_reports/`。只在 debug 构建中生效；release 构建接受这个选项，但不产生报告  
- `-S`, `--scheduler <type>`：指定调度器类型  
- `-t`, `--threads <num>`：一次运行可用的线程数，作用于它的每个池：并行调度器与各模块自己的工作者池（张量内核的池也在内）。每个池都从这一个数取大小；不给选项时各用机器的核数。它没有环境变量。`tensor::num_threads()` 报告池的大小；程序自己可以用 `tensor::set_num_threads` 在运行时改动  
- `--max-recursion-depth <n>`：可同时处于打开状态且不是尾调用的调用数上限（1 到 1000000；见“资源上限”）。也可用 `CAMEL_MAX_RECURSION_DEPTH=<n>`；选项优先  
- `--max-macro-depth <n>`：`std::macro` 提前求值一次调用时嵌套调用的上限（默认 64；取值范围相同）。也可用 `CAMEL_MAX_MACRO_DEPTH=<n>`  
- `--max-macro-rounds <n>`：`std::macro` 提前求值时一个循环至多运行的轮数（默认 1000000，取值 1 到 2^40）；超过则这次求值失败。也可用 `CAMEL_MAX_MACRO_ROUNDS=<n>`，选项优先  
- `--gc <collector>`：运行时值的回收器：`generational`（默认；分代，年轻对象会被复制）或 `mark-sweep`（对象地址不变）。详见英文版 `docs/cli.md` 的 "The collector" 一节  
- `--block-limit <megabytes>`：数据块空间的上限（默认 2048 MB，取值 1 到 2^20 MB）；它只防范失控的分配，最后一个持有者消失时块的内存立即归还  
- `--graph-gc-mb <megabytes>`：图空间比上次回收后多出这么多时要求回收（默认 64）  
- `--native-stack-guard <on|off>`：嵌套调用前是否检查原生栈（默认 `on`）；`off` 只用于测量栈真正到哪里结束，不供正常使用  
- `--devices <cpu|ordinal[,ordinal...]>`：一条流水线可以使用的设备（设计原则 13.4）：`cpu` 表示只用主机，或者按流水线可用的次序写 CUDA 序号（例如 `0`、`0,1`）。这里点名的设备必须存在：只要已加载的模块拥有设备，启动时就会在**任何图运行之前**检查一次（`--devices cpu` 永远通过；不给这个选项时，缺失只意味着程序在主机上运行）。缺失的序号是运行时诊断 `DeviceUnavailable`（`0x04000070`），报告中点出这个设备、机器上实际有多少 CUDA 设备（或为什么一台都问不到），以及其它同样缺失的序号；退出码为 1。程序即使一次都没有执行到设备操作，也照样报出（见 `modules/tiles/README.md` 的 "Tiles"）。今天一条流水线在第一个序号上执行，多卡执行是 G7。`--devices cpu` 下 `tiles:available()` 为假，需要设备的操作报错，而不是悄悄移到 GPU 上。  
- `--profile-mode <normal|full>`：`--profile` 启动的剖析器模式；`full` 在写出报告时同时打开 Perfetto 界面（需要 `--profile`）  
- `-n`, `--no-cache`：不使用缓存模块  
- `-I`, `--include <dir>`：添加模块搜索根（可多次使用）。模块的 `import` 按这样的顺序查找：导入它的模块自己的目录，然后是按给出顺序排列的每个 `-I` 目录，然后是 `CAMEL_PACKAGES` 列出的目录，最后是标准库（设计原则 10.2）。入口程序与库模块同一条规则：它自己的目录就是入口文件所在目录，而这个目录不是任何别的模块的搜索根，所以一个库无论被谁导入，它的 import 都解析到同一处。`CAMEL_PACKAGES` 添加的是与 `-I` 同类的根：排在各个 `-I` 之后、标准库之前。相对路径按入口文件所在目录解析。不存在的 `-I` 根不会被静默跳过：无法解析的 import 是 `ModuleNotFound` 诊断，它按这个顺序列出试过的每个目录，并说明其中哪些不存在（缺的那个根就在其中）。只有 `.cml` 和 `.cmo` 是模块文件：程序旁边同名的 Markdown 文档、文本文件不是模块 `foo`；同一目录里名字 `xx` 指的是 `xx.cmo`，它旁边的 `xx.cml` 是它的声明模块  
- `-L`, `--stdlib <path>`：指定标准库路径，加入模块搜索路径。省略时用环境变量 `CAMEL_STD_LIB`；两者都没有时，标准库在可执行文件旁和安装根目录（`CAMEL_HOME`）下的 `stdlib` 目录里查找。`CAMEL_HOME` 会被校验而不是直接采信：它指的不是已存在的目录，或目录里没有 `bin`、`stdlib`、`libs`、`include` 中的任何一个，运行就在加载任何东西之前报错，并指出这个变量和这个路径  
- `-E`, `--error-format`, `--output-format <text|short|json>`：诊断的格式（默认：`text`），见下文“诊断”；`--timeit` 的输出也沿用这个选项
- `--color`、`--max-errors`、`-N`/`--max-warning`、`-W...`：见下文“诊断”

### 诊断

编译错误和运行期故障打印到标准输出，警告打印到标准错误；到达 `main` 出口而没有被处理的错误值（`UnhandledError`，见“`main` 带参数或有返回值的程序”）也打印到标准错误。全部诊断见 [diagnostics.md](diagnostics.md)，单条诊断用 `camel --explain <name or code>` 查看。

两个流在所有平台上写出的字节相同：行以换行符（`\n`）结束，Windows 上也是如此（否则 C 运行库会写成 `\r\n`）。程序用 `fs::write_text` 写的文件同理：文件内容就是字符串的字节。

- `-E`, `--error-format`, `--output-format <text|short|json>`：诊断的书写方式（默认：`text`；`--timeit` 的输出也沿用这个选项）。
  - `text`：消息、源码行及其下划线标出的范围、次要位置（例如实例化泛型函数的那次调用，各带自己的代码片段），然后是 `note:` 与 `help:` 行。

        error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'undefined_name'.
          --> prog.cml:3:13
           |
         3 |     let x = undefined_name + 1
           |             ^^^^^^^^^^^^^^
        help: Make sure the name is declared and in scope.

        camel: 1 error

    编译错误之后有一行总数（`camel: 1 error`、`camel: 3 errors`）；运行期故障之后没有这一行。

    编译器知道怎么改的诊断把改法也印出来，做法照 rustc 印 suggestion：一行 `help:` 给出改法的标题，它下面是被改法改写后的那一行源码，改法写下的文字用 `+` 标出。只删不写的改法没有“改写后的一行”可看，于是印它原来的样子，被删掉的文字用 `-` 标出。工具从 JSON 的 `data.fixes` 应用的改法就是同一处改动，`camel file.cml` 不加任何选项就能同时看到两者：

        error[SyntaxDiag::MissingComma]: Expected ',' or ')' before 'b'.
          --> prog.cml:3:17
           |
         3 | func add(a: int b: int): int { return a + b }
           |                 ^
        help: The items of a list (parameters, arguments, elements) are separated by ','.
        help: insert ','
           |
         3 | func add(a: int, b: int): int { return a + b }
           |                +

        camel: 1 error

    不带改法的诊断只有文字形式的 `help:` 行，也就是在此之前每条诊断的样子。

  - `short`：每条诊断一行，`(module)path:line:col: [Error]: message suggestion (name=..., code=0x...)`。这是脚本匹配的形式，测试运行器用的就是它。
  - `json`：对象数组，含 `range`、`severity`、`code`、`message`、`relatedInformation`（次要位置：`location.path`、`location.range`、`message`）和 `data`（`stage`、`name`、`moduleName`、`modulePath`、`suggestion`、`notes`）。
  - 未给出该选项时，由环境变量 `CAMEL_ERROR_FORMAT` 决定格式。
  - 行号和列号在 `text` 与 `short` 中从 1 开始，在 `json` 中从 0 开始；列按字符计，不按字节计。
- `--color <auto|always|never>`：`text` 与 `short` 中的颜色（默认 `auto`：只在输出流是终端时启用，设置了 `NO_COLOR` 时一律不启用）。
- `--max-errors <n>`：编译器放弃之前报告的编译错误数（默认 20；0：不限）。到达上限时，`text` 格式的总数行写明这一点：`camel: 2 errors; stopped at the limit of 2 errors (--max-errors <n> raises it, 0 removes it)`。一次运行会报告它找到的每一个相互独立的错误；由已报告的错误引出的错误不再报告（见 [technical/30_diagnostics_reporting.md](technical/30_diagnostics_reporting.md)）。运行期故障总是在第一个故障处结束运行。
- `-N`, `--max-warning <n>`：显示的警告数（默认 1000）；其余只计数，数目在最后打印（`camel: 1 more warning(s) not shown (--max-warning 0)`）。
- 警告的名字与 [diagnostics.md](diagnostics.md) 中一致，但不带类型前缀；大小写、`-` 和 `_` 都不影响（`ignored-side-effect` 就是 `IgnoredSideEffect`）。不是警告的名字会被拒绝：stderr 上是 `Error: -Wno-bogus: no warning is named 'bogus'. The warnings are: ...`，后面列出全部警告名，退出码为 1，程序不编译。
  - `-Wno-<name>`：不报告警告 `<name>`。
  - `-Wnone`：不报告任何警告。
  - `-Werror`：把每个警告都按错误报告（程序不运行，退出码为 1）。`text` 格式下这样的错误带一行 `note: this warning is reported as an error because of -Werror`。
  - `-Werror=<name>`：把警告 `<name>` 按错误报告。
  - `-Wno-error=<name>`：在 `-Werror` 下让 `<name>` 仍是警告。
- `--explain <name or code>`：打印一条诊断在目录中的条目（`camel --explain IndexOutOfRange`、`camel --explain 0x04000002`）。名字可以不带类型前缀，大小写、`-` 和 `_` 同样不影响。未知的名字或代码在 stderr 上被拒绝，退出码为 1。

        RuntimeDiag::IndexOutOfRange  (0x04000002, Error)

          message: Index {0} is out of range for {1} of length {2}.
          help:    Ensure the index is within the valid range of the collection.

          {0}, {1}, ... stand for the values a concrete report fills in. A triggering example is in docs/diagnostics.md.

### `main` 带参数或有返回值的程序（`--arg`、`-o`）

`main` 与其他函数一样，可以带参数，包括普通参数和 `with` 参数。它们就是程序的输入：运行时它们的值来自命令行，翻译时它们成为产物的输入（见“以遍的形式导出 ONNX”）。

    camel model.cml --arg x=x.npy --arg p=weights --arg scale=0.5 -o out

- **`--arg name=value`** 给参数 `name` 赋值；每个参数重复一次该选项。`value` 如何读取取决于参数声明的类型：
  - `int`、`i32`、`float`、`f32`、`bool`、`string`：字面量（`3`、`0.5`、`true` / `false`、任意文本），按参数声明的宽度读取。`i32` 全程是 int32，`f32` 全程是 float，声明类型装不下的值被拒绝，而不是按更宽的类型读取再收窄：`i32` 参数给 `--arg n=3000000000` 时消息说 `i32` 及其范围。数是一个完整的数字记号，由 `camel::utils::parseWholeInteger` / `parseWholeFloat` 读取：没有十六进制浮点、没有 `Infinity`、没有空白（前导 `+` 是符号，允许）。
- `c64`、`c128`：语言里的复数文字（语言参考 2.5.3）：实部、虚部的符号与虚部，`1+2i`、`0.5-0.25i`，或只有带符号的虚部 `-2i`。每个部分按声明类型的宽度、用同一条完整数字记号规则当作浮点数读取（`c64` 的两个部分都是 float，所以超出 f32 范围的部分会指明是哪个部分与 `c64`）。实数就是实部、虚部为 0 的复数：`1.5` 读作 `1.5+0i`，与 `let z: c128 = 1.5` 相同（字面量按上下文取类型，设计原则 5.1）。
  - `f32[..]`、`int[..]` 这类多维数值数组：一个 `.npy` 文件（扩展名可以省略）。文件按 `tensor::load_npy` 的方式读入（float64 数据以 float32 存储，即该模块的浮点存储格式），然后按声明的元素类型与各维检查这个数组；不匹配（给浮点参数一个整数文件、形状不同）是错误，不做转换。
  - 结构体或元组（模型的权重）：一个目录，每个叶子一个文件，文件名是用点连接的叶子路径（`fc1.w.npy`、`0.npy`），也就是 `nn::save_params` 写出的布局。
  - `with` 参数的给法与普通参数相同。`var` 参数不能给值。
- **绑定就是部分应用。** 得到值的参数在第一个遍运行之前成为入口图中的常量，因此各个遍把它当常量看待（据此折叠和特化）。其余参数仍是入口图的参数；`std::rgir` 把它们显示为与参数同名的 `PORT` 节点。
- **运行需要每个参数都有值。** 调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`，或默认运行）拒绝仍带参数的入口图，诊断为 `RuntimeDiag::EntryArgumentMissing`（`0x04000010`，阶段 `run`），定位在 `main` 的定义处，并列出没有值的参数。什么都不会运行；绝不假定默认值、零值或随机值。重写遍和翻译遍接受未绑定的参数。
- **错误的实参**（名字不是参数、同一名字给了两次、文本不是该类型的字面量、文件不存在、张量形状不同、对没有参数的 `main` 使用 `--arg`）是 `RuntimeDiag::EntryArgumentInvalid`（`0x04000011`），定位在 `main`，在任何遍运行之前报告。
- **`main` 的返回值写到输出**，即 `-o` / `--output <file>`，默认是标准输出，前提是有调度遍运行了程序。确切规则如下：
  - **只有写明了返回类型的 `main`，它的返回值才算程序的结果**（设计原则 9.8）。没有写返回类型的 `main`（`func main() sync { println('a') }`）：函数体的值（这里是 `println` 返回的 `'a'`）不是结果，不打印、不写出，退出码是 0；此时使用 `-o` 是 `EntryResultNotWritten`。`println(x)` 仍然返回 `x`：`func main(): string sync { println('a') }` 的结果是 `'a'`，由 `println` 打印一次，再作为结果输出一次。
  - `main` 的返回类型是 `T!`：返回成功值时按 `T` 处理（`int!` 的成功值是退出码）。返回的是**没有被处理的错误值**时，它作为诊断 `RuntimeDiag::UnhandledError`（`0x04000051`，阶段 `run`）打印到**标准错误**，退出码为 1，标准输出上只有程序自己的输出。诊断按 `--error-format` 的格式呈现（`json` 时是标准错误上的一个数组），位置是错误值里记录的产生处：`error(...)` 调用的位置，或被 `attempt` 隔离的故障的位置；由算子产生的错误（如 `parse_int`）不记录位置，这时定位在 `main`，并用一条 `note:` 指出是哪个算子。
  - `main` 不返回值（声明为 `: void`）：什么都不写。此时使用 `-o` 是 `RuntimeDiag::EntryResultNotWritten`（`0x04000012`），定位在 `main`，在程序运行之后报告，退出码为 1。
  - `main` 返回退出码（`int`、`bool`、`byte`：进程退出码由这些类型导出）：该值就是退出码，与任何 `main` 一样，并且 **不** 打印到标准输出，所以 `main` 返回自身状态的程序行为不变。只有 `-o` 指定了文件时才把它（作为文本）写出。
  - `main` 返回其他值（`float`、`string`、张量、元组、结构体……）：该值是程序的结果。不带 `-o` 时，它按 `println` 的方式打印到标准输出，在程序自身的输出之后单独占一行（元组或结构体用其文本形式，例如 `(tensor([...]), tensor([...]))`）。带 `-o path` 时，它按 `--arg` 读取的布局写出：标量写成存放字面量的文本文件，张量写成 `.npy` 文件（`path` 是别的扩展名或没有扩展名时追加 `.npy`），结构体或元组写成目录 `path`，每个叶子一个文件（张量叶子是 `0.npy`、`1.w.npy`，标量叶子是不带扩展名的文本文件 `0`、`1`）。
  - 无法写入文件的值（路径不可写、类型没有文件格式，如数组或函数）是 `EntryResultNotWritten`，定位在 `main`；退出状态为 1。
  - 这条规则与 `main` 是否带参数无关。翻译遍和各种转储不受影响：它们写出的内容不变。
- `--timeit` 像普通运行一样绑定 `--arg`；在那里 `main` 的结果既不打印也不写出。

### 遍流水线

命令行上给出的遍按书写顺序运行。每个遍或者变换图，或者执行图（调度遍），或者翻译图（翻译遍）。

- **默认流水线。** 不给出任何遍时运行 `std::default`；它是 `std::nodevm`（节点虚拟机）的别名，本文其余地方用的 `std::nvm` 是同一个遍的另一个别名（同样，`std::fvm` 是 `std::fastvm`，`std::jit` 是 `std::fastvm::jit`，`std::tf` 与 `std::prl` 是 `std::taskflow`）。给出的遍都没有消耗图时，在它们之后也运行这个默认遍。`.opencmlrc`（`passes.fallback`）可以改变它。
- **所有遍的名字都在运行之前检查。** 程序编译之后（模块贡献的遍此时才存在）、第一个遍运行之前、第一份 `--dump-after-each-pass` 快照写出之前，流水线和后备遍的所有名字都被解析（先解析别名）。第一个未知的名字报告为 `RuntimeDiag::UnrecognizedGraphPass`，阶段为 `pass:<name>`，可用的遍和别名列在 stderr 上（`Pass <std::no_such_pass> not found, available passes are:`，然后是 `Available aliases are:`），退出码为 1。此时什么都没有执行：`camel f.cml std::nvm std::no_such_pass` 不会运行程序。`module::pass` 这样的名字按它所指的模块区分：模块在、但没有这个遍，报告为 `RuntimeDiag::PassNotProvidedByModule`（模块声明了该函数却没有 `export` 时是 `RuntimeDiag::PassNotExportedByModule`），只写出模块和它确实提供的遍，不再列出全部遍；模块在、但加载或编译失败，报告为 `RuntimeDiag::InvalidPassParameter`，带上加载器或编译器的原因（编译错误还带它的文件与位置）。
- **写在消耗图的遍之后的遍永远不会运行。** 调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`）执行程序，翻译遍（`std::gir`、`std::rgir`、`std::cpp`、`onnx::export`、字节码和即时编译的转储）翻译程序；二者都不把图传下去，流水线到此结束。`camel f.cml std::nvm std::opt::inline` 在节点虚拟机上运行程序，永远不会运行 `std::opt::inline`。这种写法被接受，但在任何东西运行之前会在 stderr 上给出警告 `RuntimeDiag::PassesNeverRun`（`0x0410000F`，阶段 `pass:<消耗图的遍>`），列出不会运行的遍（见“警告”）。
- **语言不限制遍的顺序，而顺序可能有影响。** 一个遍并不承诺得到与另一种顺序相同的图：它修改图，并且可能有副作用。“任意遍序列给出相同行为”不是 Camel 的性质。
- **标准优化遍**（`std::opt::devirtualize`、`std::opt::specialize`、`std::opt::inline`、`std::opt::fold`、`std::opt::cse`、`std::opt::dce`、`std::opt`、`tensor::fuse`）力求保持程序的结果。这是对这些遍的质量要求，由测试检查，不是语言保证。
- **`std::macro` 是可选的。** 没有它，`#macro` 函数就是一个随程序运行的普通函数。
- **建立在遍之上的库可以有前置条件。** `autodiff` 库要求先运行 `std::macro`，`grad` / `with_grad` / `vjp` 才能展开：没有运行过宏的图缺少求导所需的信息。这是该库的约束，不是语言核心的约束。

### 遍的参数

遍是标准库里的函数（设计原则 9.3）：它的名字、说明和参数是模块 `.cml` 文件里的一条声明——核心的遍在 `stdlib/std.cml`，模块的遍在 `stdlib/<module>.cml`——流水线从那里读取。参数写作遍名之后的 `key=value` **词，不带括号**：`camel m.cml std::opt safe_fp=true growth_budget=8192 std::fvm`。值是一个词，按声明给这个参数的类型读（设计原则 9.3）：`bool`、`int`、`float` 参数取该类型的 Camel 常量表达式——布尔、整数、浮点字面量，以及作用在它们上的运算符（`n=2*8`、`growth_budget=2*8+1`）——值必须是一个词，带空格的表达式省掉空格写；`int[]` 参数取数组常量，元素全是整数（`axes=[0,1]`，空数组写 `[]`）；`string` 参数取那个词的原文，不需要引号（`onnx::export out=model.onnx` 写出的文件就叫 `model.onnx`），值里有空格时用 shell 的引号，与任何命令行工具一样；引号必须作为那个词的一部分到达命令行，所以整条遍写法要一起引起来（`'onnx::export out="a b.onnx"'`）；声明写成 `T?` 的参数另外接受 `null`，默认值是 `null` 时可以整个省略，省略即 `null`（不是"没有值"）。声明就是函数自己的 `with` 列表，所以遵守函数的规则（设计原则 4.4）：名字、`:`、类型（`bool`、`int`、`float`、`string`、`int[]`，以及可空的 `T?`）、默认值写 `= 值`；没有默认值的参数是必需参数，必需参数不能跟在有默认值的参数后面。用 Camel 写的遍，其声明就是函数自己的 `with` 列表、写不出默认值，因此只取 `bool`、`int`、`float`、`string` 四种标量；`int[]` 与 `T?` 需要一条带默认值的声明。**默认值是在声明处求值一次的常量表达式**，不在每次调用时重算：帮助列表显示值本身（`top: int = 24`，不是 `top=8*3`）。`when` 子句是遍的适用条件：流水线认的是 `is_function::<F>()`，别的条件会被拒绝而不是假定成立。遍只收它所作用的那个函数，一个普通形参；带同样条件、普通形参更多的无函数体声明是要一个函数的算子（`onnx::export_model(f, example)`），不是遍。括号形式（`std::opt(safe_fp=true)`）、命令行上的冒号写法（`safe_fp: true`）和位置参数都已删除，均报告 `RuntimeDiag::InvalidPassParameter`（`0x04000060`），消息说明应该怎么写。声明在流水线解析时按模块搜索路径读取；找不到模块的 `.cml` 文件，或文件里没有声明这个遍，流水线拒绝这个遍，而不是拿声明从未给过的默认值去跑内核：消息写明模块和读到的文件（或找过的那些路径），所以缺少标准库的安装会直接说出来，而不是把遍报成不存在。

遍的设置是参数：

| 遍 | 参数（默认值） |
| --- | --- |
| `std::opt`、`std::opt::algebra` | `safe_fp: bool = false`（严格等价的浮点算式；设计原则 7.4） |
| `std::opt::dce` | `values_only: bool = false`（只删除无人读取的常量与纯运算，不动 GATE 与 SYNC；用作同类遍（例如用 Camel 写的遍）的对照基准） |
| `std::opt::algebra` | 规则库的遍（`rules::simplify`，`safe_fp` 下为 `rules::simplify strict=true`）；`neutral_only: bool = false` 只做整数的单位元化简 `x + 0`、`0 + x`、`x - 0`、`x * 1`、`1 * x`，不做同类项与常数因子合并（这是没有库对应物的 C++ 对照模式，供同类遍（例如用 Camel 写的遍）对照） |
| `std::opt`、`std::opt::inline`（及 `::small` / `::arm` / `::hybrid`）、`std::opt::specialize`、`std::opt::devirtualize` | `growth_budget: int = 4096`（特化和内联可复制的节点数；用完后剩下的调用仍是调用；0 表示不再复制；`std::opt` 的所有轮次共用一个预算） |
| `std::opt`、`std::opt::fold` | `constant_budget: int = 8388608`（折叠可物化成常量的字节数：数组按元素数×元素大小加形状开销计，超过上限的值保留生成它的计算——程序含义不变，只是少折了一些；0 表示一个都不物化） |
| `std::fvm`、`std::jit` | `stats: string = ""`（把本次运行每张图的字节码数与执行计数追加写入该文件） |
| `std::jit` | `hot_threshold: int = 1`（同一调用点调用多少次后编译一张图；0 表示第一次调用就编译）。**含循环区域的图在 0 与 1（默认）这两个"尽早编译"的阈值下第一次调用就编译**：使图变贵的是轮次，调用次数看不见轮次；显式把阈值设到 1 以上仍按调用次数走（`hot_threshold=1000000000` 因此仍表示"什么都不编译"） |
| `std::tf`、`std::prl` | `linear: bool = false`（逐节点求值），`fanout_all: bool = false`（把每个无序的高阶调用都展开成并行任务），`diag: bool = false`（打印对每张图的决定），`bytecode: bool = true`（把串行的图交给字节码解释器），`gc_stats: int = 0`（1：运行结束时打印统计；2：另外每次回收一行） |
| `onnx::export` | `out: string = ""`（模型文件；见下） |

仍留在环境变量里的是进程的环境，不是某个遍的设置：`CAMEL_HOME`、`CAMEL_STD_LIB`、`CAMEL_PACKAGES`，日志与诊断格式变量，资源上限（`CAMEL_*_SPACE_*`，以及 `--max-recursion-depth`、`--max-macro-depth`、`--max-macro-rounds` 的环境变量写法），回收器的诊断与节奏变量（`CAMEL_GC_HEAP_LIMIT_MB`、`CAMEL_GC_GROWTH_PERCENT`、`CAMEL_GC_PROMOTION_AGE`、`CAMEL_GC_POISON`、`CAMEL_GC_VERIFY`、`CAMEL_GC_STRESS_*`），以及 `CAMEL_STARTUP_PROFILE`。重写引擎的增长预算是上面那些遍的 `growth_budget` 参数，不从环境变量读取。开关是选项和遍参数：`--gc`、`--block-limit`、`--graph-gc-mb`、`--native-stack-guard`、`--profile-mode`，以及 `std::profile` 的 `gc_stats`、`type_stats`、`effect_trace` 参数；测试专用的遍在流水线里写出名字即可。

### 模块的遍

`std::` 前缀只给核心自带的遍。模块的遍用模块自己的名字：`tiles::compile`、`tensor::fuse`、`onnx::export_model`（短别名 `onnx::export`）。每个遍，无论核心的还是模块的，都是模块 `.cml` 文件里的一条无函数体声明——`export func fuse::<F>(f: F): F! when is_function::<F>()`——`registerModulePass` 只绑定它的内核；编译器不为这样的声明建图或算子组。模块可以为自己的遍登记别名（`registerPassAlias`），核心不认识任何模块的名字。

模块的遍只在流水线写出它们的位置运行：流水线是显式组合的，导入模块不会加入任何遍。需要某个遍的库（如自动微分需要它之前的 `std::macro`）在前提不满足时给出诊断并说明要加哪个遍；标签没有读到它的遍时给出警告。模块遍的参数就是它声明的 `with` 列表，和核心的遍用同一套检查。

### 常用遍

- `std::opt::inline`：只做内联的重写遍；默认目标策略是 `std::opt::hybrid`
- `std::opt::small` / `std::opt::arm` / `std::opt::hybrid`：显式的内联目标策略
- `std::opt::specialize`：运行时原生的特化遍
- `std::opt`：集成优化总入口；循环执行去虚化、特化、内联、fold、cse、algebra 和 dce，直到稳定或达到共享的增长及轮数预算
- `std::opt::algebra`：就是规则库的化简遍 `rules::simplify`，定律即 `stdlib/rules.cml` 里的规则，一条定律只写一次；`safe_fp=true` 时运行 `rules::simplify strict=true`（设计原则 7.4：严格等价的浮点算式）。默认 `safe_fp=false`，不自动允许降精度、近似函数或任意特殊值假设。它不要求前面先有 `std::opt::fold`；`std::opt` 会先跑 fold，以得到语料对照所用的系数形式。`neutral_only=true` 保留 C++ 对照模式（只做整数单位元）。PowerShell 中带参数的遍须加引号（这是普通的遍参数）；所有参数在运行第一个遍之前校验。
- `std::opt::fold` / `std::opt::cse` / `std::opt::dce`：分别运行常量折叠、公共子表达式消除、死代码消除
- `std::opt::order`：顺序边的传递约简——A→B→C 已在时删去 A→C（设计稿 `docs/design/effects/edges.cn.md` 第 5 节第 4 条）；证明路径读顺序边与同一个 `sync` 块的链，同步边本身是一条链、没有冗余且带“用到”，所以从不删掉。CTRL 边还没有从图里删掉，所以在它消失之前这一遍同时继续做控制边的传递约简：删掉被数据路径、`sync` 链或另一条控制路径覆盖的 CTRL 边，保留分支及循环边界、臂锚点，不把未选中的分支当作必需依赖。`std::opt` 的其他阶段结束后默认执行一次
- `std::inline`（含 `small` / `arm` / `hybrid`）：`std::opt::inline` 及其策略的别名
- `std::defun_value`：让每个函数值都有设计稿（`docs/design/defunctionalization`）要的形式，从而程序里不再留下闭包、也不再留下通过函数值的调用。程序赋予值的那个函数类型变成一个 `enum`：程序当成值用的每个函数是其中一个变体，该函数捕获的值就是这个变体的数据，于是标签就在值的类型里。通过值的调用变成对被指函数的直接调用，捕获值作为末尾的实参；只有在运行期才知道的标签变成对它的一个 `match`——`:enum/tag`、每个变体一条臂（从值里取出该变体的捕获值）、一个 `JOIN`；在构造处就知道的标签直接折掉，只有一个变体的家族也直接折掉。它不是优化遍：它改变哪些调用是直接调用，而不改变代码有多少，所以它是想要通过函数值的调用消失的流水线里的固定一步（`camel m.cml std::defun_value std::opt std::fvm`）。它看不全的家族保留原来的形式，并报出那个类型和原因（`RuntimeDiag::DefunctionalizationIncomplete`，`0x04100017`）：值被不是调用的节点读（`map` 这类高阶算子会在算子内部调用它）、函数既被直接调用又被当成值、调用的被调方还在等东西、类型是入口函数的实参。它不猜：猜一个调用到达哪个函数会改变哪些效应执行。
- `std::fvm`：高性能字节码虚拟机调度器
- `std::jit`：启用即时编译的字节码虚拟机调度器。即时编译器只为 x86-64 生成代码；在其他处理器的构建中（arm64 macOS；`ENABLE_FASTVM_JIT` 关闭）`std::jit` 仍被接受，程序在字节码解释器上运行，与 `std::fvm` 相同，结果一致；该构建在日志里说明一次（`-v`：`std::jit: this build has no native code generator ...`）。这是一条日志而不是警告，所以同一条命令在所有平台上都能运行，`-Werror` 下也一样
- `std::gir`：转储当前的图中间表示并停止
- `tensor::fuse`、`onnx::export`：由模块贡献的遍。在命令行上写 `<module>::<pass>` 时，如果程序没有导入模块 `<module>`，就加载它，所以程序不必仅仅为了让某个模块的遍来翻译它而导入该模块。找不到模块，或模块里没有这个遍，名字就仍是未知的（`RuntimeDiag::UnrecognizedGraphPass`）

### 遍的顺序：`std::macro` 在内联遍之前

作用于函数的宏算子（`autodiff` 的 `grad`、`with_grad`、`vjp`）变换的是函数编译出来的图。请先运行 `std::macro`（`camel m.cml std::macro std::opt std::fvm`）：它提前展开这些算子，后面的遍再优化展开的结果。不运行 `std::macro`，或者把它放在 `std::opt::inline` / `std::opt` 之后，算子就随程序运行；为了让这种情况仍然正确，内联遍不向未展开的宏算子将要变换的函数里内联任何东西（当算子的函数实参不是常量，例如是一个参数时，则是所有函数）。这样的流水线给出相同的结果，但这些函数没有得到优化。见 `docs/technical/26_autodiff_as_graph_rewrite.md` §2。

### `std::macro` 报告什么

当每个实参都是常量时，`std::macro` 把对 `#macro` 函数或宏算子的调用替换为它的结果。有两种情况会让调用留在程序里：

- **某个实参不是常量。** 这是正常情况，不是失败；什么都不打印。带 `-v` 时 stderr 上有一行日志指出这次调用和那个实参：`[WARN] <Macro> 'count' at m.cml:11:17 stays a run-time call: argument 1 is not a constant.`
- **求值失败**：某个算子抛出错误（除以零、`autodiff: no derivative rule for operator ...`）、有异常逃逸，或者求值所需的嵌套调用数超过求值器的上限（64，除非 `--max-macro-depth` 或 `CAMEL_MAX_MACRO_DEPTH` 另有指定）。这是运行时故障，是遍运行中的故障（见设计原则 8）：`RuntimeDiag::MacroEvaluationFailed`（`0x0400000E`，阶段 `pass:std::macro`），定位在这次调用，并带有原因（`short` 格式，后面还有建议和诊断名）：`(m)m.cml:19:17: [Error]: 'boom' could not be evaluated by std::macro: DivisionByZero: Attempted division by zero. ...` 流水线在此停止，退出码为 1；后面的遍不运行，程序不执行。

这条规则的后果：

- 一次失败的宏调用会使遍停止，**即使这次调用在运行期永远不会到达**（调用位于没有任何地方调用的函数里，或位于没有被选中的分支里）：`std::macro` 对每一个实参都是常量的宏调用求值。
- 深度超过求值器上限（默认 64 帧）的宏递归是一次失败的求值，所以不带 `std::macro` 能运行的程序，带上它可能失败（对 `#macro func count(n) = if n == 0 then 0 else 1 + count(n - 1)` 调用 `count(40)`）。消息给出上限以及提高它的办法：`'count' could not be evaluated by std::macro: the evaluation needs more than 64 nested calls (at 'count'). The limit is set with --max-macro-depth <n> or CAMEL_MAX_MACRO_DEPTH=<n>.`
- 失败的求值在失败之前已经产生的效应，发生在遍运行的时候（遍的每一轮一次）。它们不会重复，因为程序不运行。
- 流水线里没有 `std::macro` 时一切照旧：这次调用是普通调用，只有在被执行到时才可能失败。
- `onnx::export_model` 在程序运行期间用同一个求值器展开它所导出函数中的宏；那里求值失败是同一条诊断，阶段为 `run`。

### 以遍的形式导出 ONNX（`onnx::export`）

    camel m.cml std::macro std::opt onnx::export out=model.onnx

`onnx::export` 是 `onnx` 模块贡献的翻译遍。作为流水线的最后一个遍，它写出一个 ONNX 模型并消耗图：程序不执行。

- **导出什么** 在程序里声明，用的就是运行期导出的那次调用：`onnx::export_model`，实参依次是导出的函数、`example`、目标路径，随后可选 `dynamic_axes` 与 `target`。这个遍在前面各遍留下的入口图里查找这次调用，并在那里读取它的实参，所以到那时它们必须是图中的常量：函数（闭包可以捕获常量）、示例输入（它们的 dtype 和形状成为模型输入）、`dynamic_axes` 和 `target`。`dynamic_axes` 作用于每一个张量实参，不能单独指定其中某一个：只有一个张量实参时，它的第 0 轴是符号尺寸 `batch`；有多个时（权重与数据一起传入），每个实参得到自己的符号尺寸（`<input>_dim0`），因此不对它们是否相等作任何断言。以结构体传入的参数永远不是动态的。只在运行期存在的值（随机初始权重、加载的文件）对遍不可用；这样的程序用 `camel m.cml` 导出，由它运行 `export_model`。
- **写到哪里**：如果给了参数 `out` 就写到那里（此时要求恰好有一次 `export_model` 调用），否则写到这次调用的 `path` 实参，它必须是字符串字面量（遍不对 `get_env(...)` 求值）。
- **这个遍自己做什么**：它把示例的类型（dtype 和形状）绑定到函数的参数上，重新推断节点类型，然后用只有导出才知道的两件事完成对函数私有副本的化简：示例的形状（形状运算被折叠，以形状为界的递归被展开），以及把依赖数据的分支的梯度拆成每个分支臂一次调用，这只对从不执行该图的使用者成立。这与 `export_model` 所做的化简相同，所以两个入口接受相同的函数并写出等价的模型。这个遍不展开宏（那是它之前的 `std::macro` 的事），并且仍然需要在它之前运行 `std::opt`：只有 `main` 被内联进入口图之后，导出请求才可见。
- **拒绝**：如果图还需要上述某一步（入口图仍然调用 `main`、`grad` 之类的宏算子没有展开、函数里还留有不是尾递归的调用：深度取决于模型输入的尾递归导出为 ONNX `Loop`），或者含有 ONNX 无法表达的东西（非纯算子、没有下降规则的算子），这个遍不写任何文件，以非零状态退出，诊断为 `RuntimeDiag::TranslationRefused`（`0x0400000B`，阶段 `pass:onnx::export`）；消息指出应当运行的遍。每次拒绝都有源码位置：有肇事节点时是该节点；`export_model` 调用写在条件在编译期未知的分支的某个分支臂里时是该分支（`if (get_env('MODE') ?? '') == ... then export_model<out: ...>(fn, example)`：这个遍不运行程序，无法判断请求是否发出；消息给出这次调用的位置）；针对整个请求的拒绝定位在 `export_model` 调用（动态轴越界、未知的 target、示例的个数、结果不是张量、模型文件无法写出；给了参数 `out` 而有多次调用时是第二次调用）；看不到调用时（入口图仍然调用 `main`，请求在它里面），或者拒绝针对的是作为模型的 `main` 时（参数没有形状、没有给参数 `out`），定位在入口函数 `main`。内联器已经运行却仍然看不到调用时，消息说明内联器为什么留下了请求所在的那次调用：它的增长预算用尽（消息给出预算、它是默认值还是由 `growth_budget` 参数设置、已经花掉多少、这次调用需要多少，并指出要调高哪个遍的哪个参数），或者发出请求的函数会到达一个递归并且不小，或者它是递归的，或者它太大而无法复制到各调用点。只有在没有任何做内联的遍运行过时，消息才要求运行 `std::opt`。非纯算子只要能从函数的结果到达就会被拒绝，不论经由数据边还是控制边，也不论是否在它调用的函数内部：`sync` 函数里的一条 `println` 语句在该 `println` 处被拒绝，而不是从模型里漏掉。`onnx::export_model` 做同样的检查。

**没有 `export_model` 调用的程序：`main` 就是模型。** 只做计算的程序用同样的遍组合导出，程序里不必为导出写任何东西（连 `import onnx` 也不必：写出遍的名字就会加载模块）：

    camel model.cml std::macro std::opt onnx::export out=model.onnx
    camel model.cml --arg p=weights std::macro std::opt onnx::export out=model.onnx

当程序里任何地方都没有 `export_model` 调用时，这个遍翻译入口图本身，也就是 `main` 的计算：

- **输入**：入口图仍带有的每个参数一个，`with` 参数在前，名字与参数相同。数组参数必须在类型里写出元素类型与每一维（`f32[?, 3]`）；`?` 是动态轴（符号尺寸 `<name>_dim<axis>`）。元素类型与形状写不出来的参数会被拒绝，并指出该参数。数是 0 阶输入。结构体或元组每个叶子一个输入，以其路径命名（`p_fc1_w`、`t_0`）。没有参数的 `main` 得到没有输入的模型（常量模型）。
- **权重**：导出时用 `--arg` 给了值的参数是图中的常量，成为初始化器，以参数命名；对结构体，则以它的字段命名（`w`、`b`）。所以通常的划分是：数据作为普通参数（输入），权重作为 `with` 参数；导出时提供了权重的值，它们就是初始化器，没有提供就是输入。写成 `main` 的一个训练步（权重作为 `with` 参数传入，更新后的权重在结果里）不带值导出：权重既是输入也是输出。
- **输出**：`main` 的返回值；元组或结构体展开为每个叶子一个输出（`output`，或 `output_0`、`output_1_w`……）。
- **写到哪里**：参数 `out`，此处它是必需的。
- **拒绝**：这个遍翻译它能翻译的部分，在不能翻译的地方拒绝，以 `TranslationRefused` 定位在相应的构造上：效应（`println`、读文件）、未展开的宏（请运行 `std::macro`）、任何化简都去不掉的调用、没有下降规则的算子。这里没有单独检查程序是否“纯”；不只是做计算的程序改用 `onnx::export_model` 导出它的某个函数，拒绝消息也是这么说的。这个遍自己内联对 `main` 的调用，所以前面不带 `std::opt` 也能工作；使用 `grad` 或 `with_grad` 的程序仍然需要 `std::macro`。
- 含有 `export_model` 调用的程序，不论调用在哪里，都不会按这种方式翻译：如果调用在入口图里不可见（什么都没有内联），拒绝消息同上文一样指出 `std::opt`。

要把导出的模型与程序本身对照，用相同的值运行程序再比较：`camel model.cml --arg x=x.npy --arg p=weights -o out` 把 `main` 的返回值写成 NumPy 脚本能读的文件。

不带遍运行程序（`camel m.cml`）仍然可用：此时 `export_model` 自己展开宏并化简函数。它接受而遍拒绝的，是导出请求或捕获的值只在运行期存在的程序（随机初始权重、来自 `get_env` 的路径）。

### 警告

警告（`[Warn]` 诊断）不会让程序停下。下表是编译器关于效应的警告和流水线的警告；其余的警告（`InvalidEscapeSequence`、`ExitInUnusedBranch`、`MatchWithoutDefault`）见 [diagnostics.md](diagnostics.md)。

| 名字 | 代码 | 位置 | 含义 |
|---|---|---|---|
| `SemanticDiag::EffectNotExecuted` | `0x03100004` | 调用、赋值或分支 | 它有副作用，但没有东西依赖它，它也不在同步块里，所以永远不会运行。这是合法的（求值由依赖驱动）；报告它是为了让人看到被遗忘的 `sync`。每条被丢弃的链一个警告（`println(log(1))` 报告的是 `println`）。在同步块内从不报告 |
| `SemanticDiag::IgnoredSideEffect` | `0x03100002` | 调用或分支 | 它写一个变量（`var` 实参，或被捕获的变量），而且不在同步块里：写排在它之前的读之后，但没有东西让它运行，所以只有之后有人读这个变量时它才运行 |
| `RuntimeDiag::PassesNeverRun` | `0x0410000F` | 没有源码位置（属于命令行的问题） | 有遍写在消耗图的遍之后；它们不会运行。与程序的警告一起打印（JSON 模式下是 stderr 数组的最后一个元素） |
| `RuntimeDiag::ConstantExpressionFaults` | `0x04100015` | 表达式 | `std::opt::fold`（也包括 `std::opt` 内部的）遇到一个操作数全是常量而求值会出故障的表达式（`1 / 0`）。它不被折叠：有没有这个遍，程序的行为相同；运行到达该表达式时，故障连同位置一起报告。在这个遍运行之后打印到 stderr，每个表达式一次，在下一个遍之前 |

对 `EffectNotExecuted` 来说，什么算效应：在算子特性注册表里既不是纯的也不是仅状态的算子（与效应追踪记录的是同一个集合）、对带 `var` 参数的函数的调用、对自身结果依赖某个效应的函数的调用，以及有这样的分支臂的分支。通过函数值进行的调用凭它类型上的资源识别，不凭类型上的某个标记：签名里写了资源就是碰该资源，没写子句就是什么都没承诺（按最宽读），只有 `pure` 才说明这次调用不是效应。

- **编译没有错误的程序** 把它的警告打印到 **stderr**，只打印一次，在编译之后、第一个遍运行之前，使用默认日志级别即可（不需要 `-v`）。默认的 `text` 格式下，警告的写法与错误相同，以 `warning[SemanticDiag::IgnoredSideEffect]: ...` 开头，带源码行和 `help:`；`short` 格式下每个警告一行：`(module)file:line:col: [Warn]: text suggestion (name=SemanticDiag::IgnoredSideEffect, code=0x03100002)`（没有源码位置的警告写作 `():?:?: [Warn]: ...`）。使用 `--output-format json` 时，stderr 上是一个 JSON 数组，其中的对象与下文的错误数组相同（`"severity":"Warn"`）。没有警告时什么都不写（没有空数组）。
- **两种模式下 stdout 都与没有警告时相同**：程序的输出，以及只在有错误时才出现的诊断数组。解析 stdout 的工具不受影响。
- **有错误的模块** 把它的警告与错误一起显示在 stdout 上，警告在前（JSON 模式下在同一个数组里）；它们不在 stderr 上重复。`EffectNotExecuted` 在模块有错误时不报告。
- `camel check` 以同样的方式打印它们。`--timeit`、`camel inspect`、`camel-cpp` 和调试器不打印警告。

### JSON 诊断（`--output-format json`）

错误打印在 stdout 上，位于程序的输出之后，是一个由对象组成的 JSON 数组（`[` 和 `]` 各占一行，每个对象一行）：

```json
[
{"range":{"start":{"line":6,"character":13},"end":{"line":6,"character":14}},"severity":"Error","code":"0x04000001","source":"Camel","message":"Attempted division by zero.","data":{"stage":"run","name":"RuntimeDiag::DivisionByZero","moduleName":"m","modulePath":"m.cml","suggestion":"..."}}
]
```

- `range` 从 0 开始计数；`data.modulePath` 是诊断所在的文件，`data.moduleName` 是诊断为它的模块携带的名字（模块名就是文件名，语言参考 05 的 5.1；仅由位置定位的诊断是文件的词干）。没有源码位置的诊断（未知的遍名、模块不提供的遍、永远不会运行的遍）的 range 全为零，二者均为空：它说的是什么在 `data.stage`（`pass:<模块>::<遍>`）和 `message` 里。
- `data.name` 是稳定的诊断名；`code` 是它的数字形式。
- 遍提交的每一张图（以及前端编码的每一张图）都做结构校验，release 构建中也是如此：节点、槽和边的引用在范围内，输入和输出互为镜像，`BRCH`/`JOIN` 的分支臂数目，`GATE` 有一个值输入和一个控制输入，直接调用传入的实参个数与被调用者接受的一致。违反时是 `RuntimeDiag::PassInternalError`，指出提交该图的遍，在任何东西运行之前报告。
- 从某个遍逃逸出来而又不是诊断的异常（提交重写时的图校验、调度器构建执行顺序时遇到环）报告为 `RuntimeDiag::PassInternalError`（`0x0400000C`），阶段为 `pass:<name>`，消息里是异常的文本，没有源码位置，退出码为 1。它标志的是编译器的缺陷，不是对程序的拒绝。即使那是调度遍，阶段给出的也是遍的名字，所以它不能说明程序是否已经运行了一部分。任何遍之外的异常报告为 `InternalDiag::UnknownInternalError`，阶段为 `internal`。
- `data.stage` 说明诊断在流水线的哪里产生：`parse`、`typecheck`、`compile`（见“资源上限”）、重写遍或翻译遍的 `pass:<name>`（包括未知的遍名）、调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`）的 `run`，或 `internal`。
- `data.fixes` 出现时，就是这条诊断要求的改法，形状取自编辑器的 code action：一个由若干个 fix 组成的数组，每个 fix 可以单独应用；每个 fix 里是一个由若干 edit 组成的数组，这些 edit 必须一起应用。一个 edit 把它的 `range` 覆盖的文本换成 `replacement`，计数方式与本文件其他地方一致（行和列从 0 开始，`end` 不含）。空 range 是插入，空 `replacement` 是删除；一个 fix 里的各个 edit 指向诊断所在文件（`data.modulePath`）中互不重叠的区间，因此它们应用在原文上，而不是应用在前一个 edit 产生的文本上。

  ```json
  [
  {"range":{"start":{"line":2,"character":16},"end":{"line":2,"character":17}},"severity":"Error","code":"0x02000007","source":"Camel","message":"Expected ',' or ')' before 'b'.","data":{"stage":"parse","name":"SyntaxDiag::MissingComma","moduleName":"m","modulePath":"m.cml","suggestion":"The items of a list (parameters, arguments, elements) are separated by ','.","fixes":[{"title":"insert ','","edits":[{"range":{"start":{"line":2,"character":15},"end":{"line":2,"character":15}},"replacement":","}]}]}}
  ]
  ```

  （源文件第 2 行写 `func add(a: int b: int)`：改法插在第 15 列，也就是 `int` 与 `b` 之间本来就有的那个空格处，于是得到 `a: int, b: int`，而不是 `a: int , b: int`。）

  只有在编译器知道要写什么文本时才给出改法：写不出确定的文本时没有 `fixes`，此时 `suggestion` 就是用文字说的建议。拿到 `fixes` 的一方可以不再询问就应用；拿不到的一方不得自己编一个。某个码是否给改法由 `diagnostics/registry.tsv` 的一列声明——`edit`，或者 `none: <cause>` 说明为什么不给——`docs/diagnostics.md` 会为每个码印出它，`node scripts/diagnostics-catalog.mjs --check` 会拒绝两者都没写的声明。`cause` 取 `runtime`（运行时才报，源码里没有可改的文本）、`choice`（改法是作者的取舍）、`context`（改法是已知的，但不是一段文本：要改第二处、另一个文件，或者要重构）、`internal`。改法不只是给工具的：`text` 格式也把它印出来（见上文“诊断”），所以看不到 JSON 的读者拿到的也是同一处改动；`short` 格式仍然是一行，没有变化。新增诊断必须在这一列里写其中之一，于是“每个码要么带改法、要么写明为什么没有”是被检查的，而不是靠记性。

### 效应追踪（`std::profile effect_trace="..."`）

在流水线里给出 `std::profile effect_trace="<file>"` 后，进程为每个被执行的有效应算子追加一行 JSON，按调用顺序排列（有效应算子指任何没有在算子特性注册表里登记为纯的或仅状态的算子：`println`、文件 I/O、`python` 的算子、赋值 `:op/assn*`，包括写成语句的 `acc = acc->append<v>` 所用的 `:op/assn_append`……）。`report=false` 使 `std::profile` 的文本报告不进入程序的输出：

    camel m.cml 'std::profile effect_trace="trace.jsonl" report=false' std::nvm

创建和读取 `var` 变量（`:var/new`、`:var/get`）是仅状态的，不产生事件；向变量存入值（`:op/assn*`）则产生事件：

```json
{"seq":0,"uri":":io/println","stage":"run","origin":{"file":"a.cml","line":12,"col":5},"payload_sha1":"...","payload":"string=hello|||"}
{"seq":1,"uri":":op/assn_l","stage":"run","origin":{"file":"a.cml","line":13,"col":7},"payload_sha1":"...","payload":"var i64=(1)|i64=5|||"}
```

- `std::macro` 提前求值一次调用期间 `stage` 是 `macro`，其他时候是 `run`。
- `origin` 是算子节点从 1 开始计数的源码位置；节点没有位置时（由遍合成的节点）为 `null`。
- `payload_sha1` 是对打印出来的实参值的哈希，实参值在算子运行之前取得。
- `payload` 是被哈希的文本：每个实参写作 `type=value` 后跟 `|`，先是普通实参，然后是 `||`，然后是 `with` 实参（在被哈希的字节里，这两个分隔符是 `0x1f` 和 `0x1e`）。它被截断到 2048 字节；被截断的行还带有 `"payload_truncated":true`，而 `payload_sha1` 仍然覆盖完整的文本。

追踪由安装在算子 URI 解析处的包装器写出，所以覆盖所有执行后端（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`）和宏求值器。它不记录算子是否在 `sync` 作用域内运行。文件在每个进程的第一个效应处被截断。见 `include/camel/core/effect_trace.h`。

### 资源上限

编译器和各执行后端的上限。一个遍到达自己的某个上限时停止变换，把程序传下去（程序的含义相同，只是优化得少一些），除非表中说明它会失败。

| 上限 | 值 | 可调 | 到达时 |
|---|---|---|---|
| `std::opt::inline`、`std::opt::specialize`、`std::opt::devirtualize`、`std::opt` 的增长（特化和内联复制的节点；`std::opt` 的所有轮次共用一个预算） | 4096 个节点 | 遍的参数 `growth_budget=<nodes>`，如 `std::opt growth_budget=8192`、`std::opt::inline growth_budget=0` | 不再特化或内联；剩下的调用仍是调用。带 `-v` 时有日志 |
| `std::opt::inline` 向其中内联的图的大小 | 4096 个节点 | 否 | 这次调用不内联 |
| `std::opt` 的轮数 | 16 | 否 | 程序按现状传下去。带 `-v` 时有日志 |
| 同一个递归函数的仅类型特化（张量实参的形状在递归的每一层都变化） | 8 | 否 | 这次调用保留它的被调用者 |
| 非尾递归，在每个调度器上相同：`std::nvm`、`std::fvm`、`std::jit`（解释执行的和编译为原生代码的调用一样计数）和 `std::tf` / `std::prl`（计数随元素转到运行它的工作线程上） | 1000 个打开的调用 | `--max-recursion-depth <n>`、`CAMEL_MAX_RECURSION_DEPTH=<n>`（1 到 1000000） | 程序停止，诊断为 `RuntimeDiag::MaxRecursionDepthExceeded`（`0x04000004`，阶段 `run`，退出码 1），定位在将成为第 1001 个打开调用的调用表达式；在它之前运行的已经运行，其输出已经写出。消息给出上限（`Maximum recursion depth (1000) exceeded when evaluating graph '...'.`），建议给出选项和变量 |
| `std::jit` 编译为原生代码的代码中的递归（分支臂已被内联的自递归函数，例如在 `std::opt::inline` 之后）占用的原生栈 | 无单独的上限：递归深度按上一行计数 | 上一行的选项和变量 | 默认上限下与解释执行给出同一条诊断（`camel f.cml std::opt::inline std::jit`：`depth(999)` 运行，`depth(1000)` 是 `MaxRecursionDepthExceeded`）。原生栈将满的检查不覆盖已编译的代码：上限调得很高而递归确实很深时，进程以段错误结束，没有诊断（上限为 1000000 时，20000 层的递归能运行，100000 层的递归崩溃） |
| `std::tf` 上线程的原生栈 | 与下一行相同：计数之外，线程的栈用掉十六分之十五时拒绝再嵌套一次调用 | `ulimit -s` 等（工作线程的栈至少 8 MiB：Unix 上进程的栈上限、Windows 上可执行文件的栈大小更大时取后者） | 在该调用处给出 `MaxRecursionDepthExceeded`；此时消息给出打开的调用数和栈的大小。默认上限（1000）在 Linux 默认的 8 MiB 栈下远够不到这里（见下文的实测） |
| 线程的原生栈，所有执行后端 | 线程的栈（Linux 上主线程默认 8 MiB） | `ulimit -s` 之类 | 在原生栈将满（Unix 上剩余不足十六分之一，且至少保留 128 KiB；Windows 为 SEH 故障展开保留十六分之十五）时还要嵌套的调用，在到达上面的上限之前就被拒绝，诊断为 `MaxRecursionDepthExceeded`；消息说明原生栈已满。正是这一点使调高的上限不至于以段错误告终。栈的边界在 Linux（glibc）、Windows 和 macOS 上读取（Windows 也已验证；macOS 已实现，但没有编译或运行过）；在其他平台上，上限不能调高到默认值以上 |
| `std::macro` 对 `#macro` 函数的提前求值 | 64 个求值器帧（32 层的自递归：`count(31)` 被求值，`count(32)` 不被求值） | `--max-macro-depth <n>`、`CAMEL_MAX_MACRO_DEPTH=<n>`（1 到 1000000） | 求值失败：在该调用处给出运行时故障 `RuntimeDiag::MacroEvaluationFailed`，流水线停止（见“`std::macro` 报告什么”）。求值器在原生栈上嵌套（每帧约 630 字节），栈将满时以同样的方式停下 |
| 图空间（程序的运行时图；一个函数约占 1 到 2 KB） | 总共 256 MB，随程序的需要按段保留 | `CAMEL_GRAPH_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`（`0x04000014`，退出码 1），定位在正在编码其图的函数，指出空间、用量、上限和变量。程序本身放不下时阶段为 `compile`，是某个遍使它增长时阶段为 `pass:<name>` |
| 永久空间（类型和布局；一个函数约占 0.6 到 2 KB，遍创建的内容永不释放） | 总共 256 MB，按段 | `CAMEL_PERM_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`，同上，除非知道正在编译的函数，否则没有位置 |
| 元空间（正在编译的图的常量和函数值；每个拥有它的函数、lambda 或分支臂约 1.2 KB） | 总共 256 MB，按段 | `CAMEL_META_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`，阶段 `compile`，定位在正在编译的函数 |
| 自动空间（运行期的值，受垃圾回收管理） | 宿主机的内存 | 否 | `RuntimeDiag::MemoryExhausted`（阶段 `pass:<scheduler>`），说明宿主机拒绝了这次分配 |

**递归上限计的是什么** 在每个执行后端上都相同：每一次处于打开状态且不是尾调用的函数调用，不论它是写成一次调用、通过函数值进行，还是由高阶算子（`map`、`filter`、`reduce`、`foreach`）为某个元素进行。处于尾位置的调用替换它的调用者，不计数（见 `docs/technical/19_tail_call_optimization.md`），所以任意深度的尾递归在任何上限下都能运行。`if` 的分支臂是它们所在函数的尾调用。唯一不由程序自己发出的调用，即运行时对 `main` 的调用，不计数。因此上限为 L 时，对于从 `main` 调用的 `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)`，`depth(L - 1)` 是能运行的最深调用，在 `std::nvm`、`std::fvm`、`std::jit`（解释执行的和编译为原生代码的）以及 `std::tf` 上都是如此：默认值下是 `depth(999)`。改变存在哪些调用的遍（`std::opt::inline`）也改变被计数的内容，因为它们改变了图。

**设置它。** 命令行上的 `--max-recursion-depth <n>`，否则是环境变量 `CAMEL_MAX_RECURSION_DEPTH=<n>`；`n` 是 1 到 1000000 的整数。其他任何值（0、文本、更大的数）在程序编译之前就在标准错误上被拒绝（`Invalid --max-recursion-depth value: '0'. Expected an integer from 1 to 1000000.`），退出码为 1。没有对应的 `.opencmlrc` 键。既没有选项也没有变量时，每个调度器都在 1000 处停下，并行调度器同样计数（设计原则 3.9）。

**调高上限的代价**（在 Linux x86-64、release 构建上测得，函数有三个槽）：

| 执行后端 | 一次调用存放在哪里 | 每个打开的调用 | 8 MiB 栈上最深的递归 |
|---|---|---|---|
| `std::nvm` | 帧池（堆）里的帧，加上原生栈上解释器循环的一次激活 | 约 545 字节原生栈，约 100 字节帧 | 14441 次调用；然后是诊断（不做栈检查时进程在 15399 处崩溃） |
| `std::fvm`，直接调用 | 帧池里的帧和解释器自己的调用栈里的一项，都在堆上；不占原生栈 | 约 100 字节 | 上限本身（上限为 1000000 时运行了 999000） |
| `std::fvm`，通过函数值的调用或高阶算子的元素调用 | 同上，再加上原生栈上解释器循环的一次嵌套激活 | 嵌套激活约占 1.7 KiB 原生栈 | 4587 次嵌套激活；然后是诊断（不做检查时在 4890 处崩溃） |
| `std::tf` | 每张图一个的池（堆）里的帧，加上原生栈上的求值器 | 约 520 字节原生栈 | 14875 次调用；然后是诊断（不做检查时在 15858 处崩溃） |

**默认上限的余量**（Linux x86-64、发布构建实测）：把栈上限逐步调小，默认上限允许的最深递归（直接递归 `depth(999)`、经函数值 `step(499)`、`map` 元素内 `depth(998)`）仍能跑完所需的最小 `ulimit -s`：`std::nvm` 与 `std::tf` 三种情形都是 768 KiB；`std::fvm` 与 `std::jit`（含 `std::opt::inline std::jit`）直接递归和 `map` 内是 192 KiB，经函数值是 1024 KiB。默认的 8 MiB 栈因此有 8 倍以上的余量；栈更小时得到的是上面那条“原生栈已满”的诊断，不是崩溃。Windows 上线程的默认栈是 1 MiB，测试集在那里第一次运行时，默认上限下 `std::nvm`（以及经函数值的 `std::fvm`）遇到的就是这条诊断，`std::tf` 则是栈溢出；因此各工具以 32 MiB 的栈链接（`/STACK`，见 `tools/libcamel_tool.cmake`），在 Windows 上这也是进程创建的每个线程的栈。`std::tf` 的工作线程在所有平台上都以指定的栈大小创建，至少 8 MiB：不指定大小创建的线程在 macOS 上只有 512 KiB，在 Windows 上由宿主加载 `libcamel` 时（`python.exe`）只有宿主的 1 MiB（`include/camel/utils/big_stack_thread.h`、`cmake/TaskflowWorkerStack.cmake`）。macOS 这一侧尚未运行过。自行加载 `libcamel` 的宿主（Python 包）用的是宿主自己的栈。

`std::nvm` 和 `std::fvm` 的帧池按上限确定大小：每个可能打开的调用 4 KiB（默认值下约 4 MiB），最多 256 MiB；它一次性保留，只有用到的部分才被触及。帧非常大的函数的递归可能在计数到达之前就把它填满；那是同一条带位置的诊断，说明帧池已满。运行时间不受上限影响。`--native-stack-guard off` 关闭原生栈检查；它用于测量栈实际在哪里结束（上面的崩溃点），不供正常使用。

三个空间上限限制的是编译后程序的大小，不是单张图的大小：它们是总量，只用来防范不停分配的编译器（展开或内联永不结束的遍，会因图空间或永久空间被填满而停下；这所需的时间增长得比上限更快，这就是默认值没有定得更大的原因）。`std::profile gc_stats=1` 在退出时打印三个空间的占用情况，连同分配器自己的一行（`[camel] spaces: ...`：使用中的字节数、最高水位、保留的字节数、段数、分配次数、上限）。

其他标为“否”的上限是编译期常量（`generic.h`、内联器的配置）；它们没有命令行选项，也没有 `.opencmlrc` 键（`.opencmlrc` 只配置遍列表）。

`std::opt` 对实参是常量的递归每轮展开一层（`fib(15)` 会变成它的整棵调用树）；让它停下的是增长预算。`onnx::export_model` 化简它所导出的函数时不受增长预算和内联大小上限约束，因为导出必须得到直线代码；通过 `std::opt onnx::export` 导出的流水线则受它们约束，对大模型可能需要在做内联的那个遍上调高 `growth_budget`，例如 `std::opt growth_budget=8192`（此时导出遍以 `TranslationRefused` 拒绝，而不是写出不完整的模型，拒绝消息给出预算、它的值、设置它的遍和参数）。

---

## 信息展示

- `-V`, `--version`：显示版本信息  
- `-h`, `--help`：显示帮助信息  
- `-d`, `--docs`：显示包或模块的文档  
- `-a`, `--about`：显示版权和项目信息  
- `-z`, `--zen`：显示 Camel 之禅
- `--explain <name or code>`：说明一条诊断（见“诊断”）

---

## check：代码健康检查

`-i` 与 `-s` 直接使用前端，不初始化运行时堆、执行上下文或内置模块；诊断格式、位置和错误数量上限与其他检查保持一致。普通 `check` 仍执行语义检查。

    camel check [options] <cml file or directory>

- `-i`, `--lexical-only`：只做词法分析：只报告词法错误（非法字符、没有结束的字符串），不做语法分析  
- `-s`, `--syntax-only`：只做词法与语法分析：文件读成语法树即止，不检查名字、类型与导入  
- `-O`, `--output-format <text|short|json>`：诊断的格式（默认：`text`）  
- `-N`, `--max-warning <num>`：显示的警告数；`--color`、`--max-errors` 和 `-W...` 选项的用法与运行时相同（见“诊断”）  
- `-c`, `--config <file>`：规则定义文件的路径  
- `-e`, `--ignore`：忽略定义文件  
- `-o`, `--output <file>`：输出文件（默认：控制台）  
- 可用的通用选项：`-v` / `-vv` / `-vvv` / `-vvvv`、`--verbose`、`--log-level`、`--log-preset`、`--log-include`

错误打印到标准输出，警告打印到标准错误，格式与运行时相同。`camel check` 不运行程序。发现错误时退出码是 1，没有错误（只有警告也算）时是 0。

---

## inspect：中间状态查看

    camel inspect [options] <cml file>

- `-t`, `-T`, `--tok`, `--token-stream`：打印词法记号  
- `-s`, `-S`, `--cst`, `--concrete-syntax-tree`：打印具体语法树（CST）  
  记号一行一个：序号、`行:列`、它前面有什么（`n` 换行、`s` 空白或注释、`-` 紧贴）、种类、文本；注释也是记号。语法树按产生式命名节点，只做转交的层级不产生节点（`a + b` 是一个 `AdditiveExpr`，下面直接是两个 `RefData`）。
- `-a`, `-A`, `--ast`, `--abstract-syntax-tree`：打印抽象语法树（AST）  
- `-c`, `-C`, `--gct`, `--graph-construct-tree`：打印图构造树（GCT）  
- `-g`, `-G`, `--gir`, `--graph-ir`：打印图中间表示（GIR）  
- `--tns`, `--topo-node-seq`：打印拓扑排序后的节点序列  
- `--ops`, `--operators`：列出内建模块与该程序导入的每个模块的算子，每个重载一行 `OPERATOR {json}`：模块、名字、URI、参数个数、声明的纯度（`pure`、`effectful`、`state`、`undeclared`）与签名，例如 `OPERATOR {"module":"<builtin>","name":"__add__","uri":":op/add_l","with":0,"norm":2,"purity":"pure","signature":"(i64, i64) => i64"}`
- `--gen`, `--gene-code`：从 AST 生成代码  
- `-p`, `-P`, `--pass-until <n>`：执行到指定的图优化阶段为止  
- 可用的通用选项：`-v` / `-vv` / `-vvv` / `-vvvv`、`--verbose`、`--log-level`、`--log-preset`、`--log-include`

---

## 相关工具

Camel CLI（`camel`）负责运行、检查和查看。格式化由单独的工具提供：

- **camel-format**：格式化 `.cml` 源文件。用法：`camel-format [options] <file>`  
  - `-i`, `--inplace`：写回文件  
  - `-t`, `--tab-size <N>`：缩进宽度（默认 4）  
  - `-u`, `--use-tabs`：使用制表符  
  - `-q`, `--quote-prefer <single|double>`  
  - `-m`, `--max-width <N>`（默认 80）

- **camel-cpp**：一步把 `.cml` 转译为 C++ 并编译成可执行文件。
  - 用法：`camel-cpp <file.cml> [-o <可执行文件>] [std::cpp 之前的遍…]`
  - 等价于：`camel <file.cml> std::cpp > <名字>.cpp`，再用 `$CXX`（默认 `c++`，Windows 上 `clang++`）以 `-std=c++20 -O2 -ffp-contract=off -I$CAMEL_HOME/include` 编译。生成的程序不依赖 Camel 运行时；用到没有独立译法的模块算子时自动链接 `libcamel`。详见 [cpp_export_build.md](cpp_export_build.md)。

其他工具：`camel-codegen`、`camel-pr`（性能分析器）、`camel-db`（调试器）、`camel-ls`（语言服务器）。全部构建在 `build/tools/` 下，与 `camel` 一起安装到 `out/latest/bin/`。

---

## 示例

```bash
# 运行主文件（位置参数）
camel main.cml

# 用选项给出输入和遍（不需要位置参数）
camel --input main.cml --passes std::gir,other

# 运行并记录性能
camel -P main.cml

# 只测量 std::jit，从全新的图执行 10 次
camel --input test/cases/perf/fib_recursive.cml --timeit std::jit/10

# 以 JSON 输出 timeit 的汇总
camel --input test/cases/perf/fib_recursive.cml --timeit std::jit/10 --output-format json

# 转译为 C++ 并编成可执行文件（一步完成）
camel-cpp test/cases/linear/nvm/fib.cml -o fib

# 格式化代码（用 camel-format，不是 camel format）
camel-format -i src/xxx.cml

# 只做语法检查并以 JSON 输出
camel check -s -O json src/

# 打印中间表示
camel inspect --gir main.cml

# 查看 Camel 之禅
camel --zen
```

