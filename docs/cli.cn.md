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
- `--passes <list>`：逗号分隔的遍列表（例如 `--passes std::gir,other`）。省略时，第一个位置参数是文件，其余是遍。  
- `--timeit <pass>/<N>`：从全新的图出发把一个遍测量 `N` 次；文本模式打印各次样本和一张简表，`--output-format json` 则输出结构化的基准数据  
- `--dump-after-each-pass <dir>`：把第一个遍之前的图（`00_input`）和每个返回图的遍之后的图（`NN_<pass>`，`::` 写作 `.`）写入 `<dir>`：一份可读的 DOT（`.rgir.dot`，即 `std::rgir` 格式，每个节点带 `origin`/`span`/`srcfile`）和 `std::stats` 的计数（`.stats.json`）。调度遍和翻译遍消耗图，不留下快照。比较两份快照用 `python tools/graph-diff/graph_diff.py BEFORE AFTER`  
- `--arg <name>=<value>`：`main` 的一个参数的值（可重复；见“`main` 带参数或有返回值的程序”）  
- `-o`, `--output <file>`：`main` 的返回值写到哪里（默认：标准输出；见同一节）  
- `-P`, `--profile`：记录各阶段耗时，把跟踪文件写到 `profile_reports/`。只在 debug 构建中生效；release 构建接受这个选项，但不产生报告  
- `-S`, `--scheduler <type>`：指定调度器类型  
- `-t`, `--threads <num>`：最大线程数  
- `--max-recursion-depth <n>`：可同时处于打开状态且不是尾调用的调用数上限（1 到 1000000；见“资源上限”）。也可用 `CAMEL_MAX_RECURSION_DEPTH=<n>`；选项优先  
- `--max-macro-depth <n>`：`std::macro` 提前求值一次调用时嵌套调用的上限（默认 64；取值范围相同）。也可用 `CAMEL_MAX_MACRO_DEPTH=<n>`  
- `-n`, `--no-cache`：不使用缓存模块  
- `-I`, `--include <dir>`：添加模块搜索路径（可多次使用）  
- `-L`, `--stdlib <path>`：指定标准库路径，加入模块搜索路径。省略时用环境变量 `CAMEL_STD_LIB`；两者都没有时，标准库在可执行文件旁和安装根目录（`CAMEL_HOME`）下的 `stdlib` 目录里查找  
- `-E`, `--error-format`, `--output-format <text|short|json>`：诊断的格式（默认：`text`），见下文“诊断”；`--timeit` 的输出也沿用这个选项
- `--color`、`--max-errors`、`-N`/`--max-warning`、`-W...`：见下文“诊断”

### 诊断

编译错误和运行期故障打印到标准输出，警告打印到标准错误；到达 `main` 出口而没有被处理的错误值（`UnhandledError`，见“`main` 带参数或有返回值的程序”）也打印到标准错误。全部诊断见 [diagnostics.md](diagnostics.md)，单条诊断用 `camel --explain <name or code>` 查看。

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
  - `int`、`float`、`bool`、`string`：字面量（`3`、`0.5`、`true` / `false`、任意文本）。
  - `Tensor`：一个 `.npy` 文件（扩展名可以省略）。文件按 `tensor.load_npy` 的方式读入（float64 数据以 float32 存储，即该模块的浮点存储格式），然后按声明的元素类型和形状检查张量；不匹配（给浮点参数一个整数文件、形状不同）是错误，不做转换。
  - 结构体或元组（模型的权重）：一个目录，每个叶子一个文件，文件名是用点连接的叶子路径（`fc1.w.npy`、`0.npy`），也就是 `nn.save_params` 写出的布局。
  - `with` 参数的给法与普通参数相同。`var` 参数不能给值。
- **绑定就是部分应用。** 得到值的参数在第一个遍运行之前成为入口图中的常量，因此各个遍把它当常量看待（据此折叠和特化）。其余参数仍是入口图的参数；`std::rgir` 把它们显示为与参数同名的 `PORT` 节点。
- **运行需要每个参数都有值。** 调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`，或默认运行）拒绝仍带参数的入口图，诊断为 `RuntimeDiag::EntryArgumentMissing`（`0x04000010`，阶段 `run`），定位在 `main` 的定义处，并列出没有值的参数。什么都不会运行；绝不假定默认值、零值或随机值。重写遍和翻译遍接受未绑定的参数。
- **错误的实参**（名字不是参数、同一名字给了两次、文本不是该类型的字面量、文件不存在、张量形状不同、对没有参数的 `main` 使用 `--arg`）是 `RuntimeDiag::EntryArgumentInvalid`（`0x04000011`），定位在 `main`，在任何遍运行之前报告。
- **`main` 的返回值写到输出**，即 `-o` / `--output <file>`，默认是标准输出，前提是有调度遍运行了程序。确切规则如下：
  - **只有写明了返回类型的 `main`，它的返回值才算程序的结果**（设计原则 2.10）。没有写返回类型的 `main`（`func main() sync { println('a') }`）：函数体的值（这里是 `println` 返回的 `'a'`）不是结果，不打印、不写出，退出码是 0；此时使用 `-o` 是 `EntryResultNotWritten`。`println(x)` 仍然返回 `x`：`func main(): string sync { println('a') }` 的结果是 `'a'`，由 `println` 打印一次，再作为结果输出一次。
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
- **所有遍的名字都在运行之前检查。** 程序编译之后（模块贡献的遍此时才存在）、第一个遍运行之前、第一份 `--dump-after-each-pass` 快照写出之前，流水线和后备遍的所有名字都被解析（先解析别名）。第一个未知的名字报告为 `RuntimeDiag::UnrecognizedGraphPass`，阶段为 `pass:<name>`，可用的遍和别名列在 stderr 上（`Pass <std::no_such_pass> not found, available passes are:`，然后是 `Available aliases are:`），退出码为 1。此时什么都没有执行：`camel f.cml std::nvm std::no_such_pass` 不会运行程序。
- **写在消耗图的遍之后的遍永远不会运行。** 调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`）执行程序，翻译遍（`std::gir`、`std::rgir`、`std::cpp`、`onnx::export`、字节码和即时编译的转储）翻译程序；二者都不把图传下去，流水线到此结束。`camel f.cml std::nvm std::inline` 在节点虚拟机上运行程序，永远不会运行 `std::inline`。这种写法被接受，但在任何东西运行之前会在 stderr 上给出警告 `RuntimeDiag::PassesNeverRun`（`0x0410000F`，阶段 `pass:<消耗图的遍>`），列出不会运行的遍（见“警告”）。
- **语言不限制遍的顺序，而顺序可能有影响。** 一个遍并不承诺得到与另一种顺序相同的图：它修改图，并且可能有副作用。“任意遍序列给出相同行为”不是 Camel 的性质。
- **标准优化遍**（`std::devirtualize`、`std::specialize`、`std::inline`、`std::opt::fold`、`std::opt::cse`、`std::opt::dce`、`std::opt::simplify`、`tensor::fuse`）力求保持程序的结果。这是对这些遍的质量要求，由测试检查，不是语言保证。
- **`std::macro` 是可选的。** 没有它，`macro` 函数就是一个随程序运行的普通函数。
- **建立在遍之上的库可以有前置条件。** `autodiff` 库要求先运行 `std::macro`，`grad` / `value_and_grad` / `vjp` 才能展开：没有运行过宏的图缺少求导所需的信息。这是该库的约束，不是语言核心的约束。

### 常用遍

- `std::inline`：只做内联的重写遍；默认目标策略是 `std::inline::hybrid`
- `std::inline::small` / `std::inline::arm` / `std::inline::hybrid`：显式的内联目标策略
- `std::devirtualize`：运行时原生的去虚化遍
- `std::specialize`：运行时原生的特化遍
- `std::opt`：聚合的运行时原生优化流水线（`devirtualize` + `specialize` + `inline`）
- `std::fvm`：高性能字节码虚拟机调度器
- `std::jit`：启用即时编译的字节码虚拟机调度器
- `std::gir`：转储当前的图中间表示并停止
- `tensor::fuse`、`onnx::export`：由模块贡献的遍。在命令行上写 `<module>::<pass>` 时，如果程序没有导入模块 `<module>`，就加载它，所以程序不必仅仅为了让某个模块的遍来翻译它而导入该模块。找不到模块，或模块里没有这个遍，名字就仍是未知的（`RuntimeDiag::UnrecognizedGraphPass`）

### 遍的顺序：`std::macro` 在内联遍之前

作用于函数的宏算子（`autodiff` 的 `grad`、`value_and_grad`、`vjp`）变换的是函数编译出来的图。请先运行 `std::macro`（`camel m.cml std::macro std::opt::simplify std::fvm`）：它提前展开这些算子，后面的遍再优化展开的结果。不运行 `std::macro`，或者把它放在 `std::inline` / `std::opt` / `std::opt::simplify` 之后，算子就随程序运行；为了让这种情况仍然正确，内联遍不向未展开的宏算子将要变换的函数里内联任何东西（当算子的函数实参不是常量，例如是一个参数时，则是所有函数）。这样的流水线给出相同的结果，但这些函数没有得到优化。见 `docs/technical/26_autodiff_as_graph_rewrite.md` §2。

### `std::macro` 报告什么

当每个实参都是常量时，`std::macro` 把对 `macro` 函数或宏算子的调用替换为它的结果。有两种情况会让调用留在程序里：

- **某个实参不是常量。** 这是正常情况，不是失败；什么都不打印。带 `-v` 时 stderr 上有一行日志指出这次调用和那个实参：`[WARN] <Macro> 'count' at m.cml:11:17 stays a run-time call: argument 1 is not a constant.`
- **求值失败**：某个算子抛出错误（除以零、`autodiff: no derivative rule for operator ...`）、有异常逃逸，或者求值所需的嵌套调用数超过求值器的上限（64，除非 `--max-macro-depth` 或 `CAMEL_MAX_MACRO_DEPTH` 另有指定）。这是编译期错误（见设计原则 2.7）：`RuntimeDiag::MacroEvaluationFailed`（`0x0400000E`，阶段 `pass:std::macro`），定位在这次调用，并带有原因（`short` 格式，后面还有建议和诊断名）：`(m)m.cml:19:17: [Error]: 'boom' could not be evaluated by std::macro: DivisionByZero: Attempted division by zero. ...` 流水线在此停止，退出码为 1；后面的遍不运行，程序不执行。

这条规则的后果：

- 一次失败的宏调用会使程序被拒绝，**即使这次调用在运行期永远不会到达**（调用位于没有任何地方调用的函数里，或位于没有被选中的分支里）：`std::macro` 对每一个实参都是常量的宏调用求值。
- 深度超过求值器上限（默认 64 帧）的宏递归是一次失败的求值，所以不带 `std::macro` 能运行的程序，带上它可能被拒绝（对 `macro func count(n) = if n == 0 then 0 else 1 + count(n - 1)` 调用 `count(40)`）。消息给出上限以及提高它的办法：`'count' could not be evaluated by std::macro: the evaluation needs more than 64 nested calls (at 'count'). The limit is set with --max-macro-depth <n> or CAMEL_MAX_MACRO_DEPTH=<n>.`
- 失败的求值在失败之前已经产生的效应，发生在遍运行的时候（遍的每一轮一次）。它们不会重复，因为程序不运行。
- 流水线里没有 `std::macro` 时一切照旧：这次调用是普通调用，只有在被执行到时才可能失败。
- `onnx.export_model` 在程序运行期间用同一个求值器展开它所导出函数中的宏；那里求值失败是同一条诊断，阶段为 `run`。

### 以遍的形式导出 ONNX（`onnx::export`）

    CAMEL_ONNX_OUT=model.onnx camel m.cml std::macro std::opt::simplify onnx::export

`onnx::export` 是 `onnx` 模块贡献的翻译遍。作为流水线的最后一个遍，它写出一个 ONNX 模型并消耗图：程序不执行。

- **导出什么** 在程序里声明，用的就是运行期导出的那次调用：`onnx.export_model(fn, example, path[, dynamic_axes[, target]])`。这个遍在前面各遍留下的入口图里查找这次调用，并在那里读取它的实参，所以到那时它们必须是图中的常量：函数（闭包可以捕获常量）、示例输入（它们的 dtype 和形状成为模型输入）、`dynamic_axes` 和 `target`。`dynamic_axes` 作用于每一个张量实参，不能单独指定其中某一个：只有一个张量实参时，它的第 0 轴是符号尺寸 `batch`；有多个时（权重与数据一起传入），每个实参得到自己的符号尺寸（`<input>_dim0`），因此不对它们是否相等作任何断言。以结构体传入的参数永远不是动态的。只在运行期存在的值（随机初始权重、加载的文件）对遍不可用；这样的程序用 `camel m.cml` 导出，由它运行 `export_model`。
- **写到哪里**：如果设置了环境变量 `CAMEL_ONNX_OUT` 就写到那里（此时要求恰好有一次 `export_model` 调用），否则写到这次调用的 `path` 实参，它必须是字符串字面量（遍不对 `get_env(...)` 求值）。
- **这个遍自己做什么**：它把示例的类型（dtype 和形状）绑定到函数的参数上，重新推断节点类型，然后用只有导出才知道的两件事完成对函数私有副本的化简：示例的形状（形状运算被折叠，以形状为界的递归被展开），以及把依赖数据的分支的梯度拆成每个分支臂一次调用，这只对从不执行该图的使用者成立。这与 `export_model` 所做的化简相同，所以两个入口接受相同的函数并写出等价的模型。这个遍不展开宏（那是它之前的 `std::macro` 的事），并且仍然需要在它之前运行 `std::opt::simplify`：只有 `main` 被内联进入口图之后，导出请求才可见。
- **拒绝**：如果图还需要上述某一步（入口图仍然调用 `main`、`grad` 之类的宏算子没有展开、函数里还留有不是尾递归的调用：深度取决于模型输入的尾递归导出为 ONNX `Loop`），或者含有 ONNX 无法表达的东西（非纯算子、没有下降规则的算子），这个遍不写任何文件，以非零状态退出，诊断为 `RuntimeDiag::TranslationRefused`（`0x0400000B`，阶段 `pass:onnx::export`）；消息指出应当运行的遍。每次拒绝都有源码位置：有肇事节点时是该节点；`export_model` 调用写在条件在编译期未知的分支的某个分支臂里时是该分支（`if get_env('MODE') == ... then export_model(...)`：这个遍不运行程序，无法判断请求是否发出；消息给出这次调用的位置）；针对整个请求的拒绝定位在 `export_model` 调用（动态轴越界、未知的 target、示例的个数、结果不是张量、模型文件无法写出；设置了 `CAMEL_ONNX_OUT` 而有多次调用时是第二次调用）；看不到调用时（入口图仍然调用 `main`，请求在它里面），或者拒绝针对的是作为模型的 `main` 时（参数没有形状、没有设置 `CAMEL_ONNX_OUT`），定位在入口函数 `main`。内联器已经运行却仍然看不到调用时，消息说明内联器为什么留下了请求所在的那次调用：它的增长预算用尽（消息给出预算、它是默认值还是由 `CAMEL_OPT_GROWTH_BUDGET` 设置、已经花掉多少、这次调用需要多少，并指出要调高的变量），或者发出请求的函数会到达一个递归并且不小，或者它是递归的，或者它太大而无法复制到各调用点。只有在没有任何做内联的遍运行过时，消息才要求运行 `std::opt::simplify`。非纯算子只要能从函数的结果到达就会被拒绝，不论经由数据边还是控制边，也不论是否在它调用的函数内部：`sync` 函数里的一条 `println` 语句在该 `println` 处被拒绝，而不是从模型里漏掉。`onnx.export_model` 做同样的检查。

**没有 `export_model` 调用的程序：`main` 就是模型。** 只做计算的程序用同样的遍组合导出，程序里不必为导出写任何东西（连 `import onnx` 也不必：写出遍的名字就会加载模块）：

    CAMEL_ONNX_OUT=model.onnx camel model.cml std::macro std::opt::simplify onnx::export
    CAMEL_ONNX_OUT=model.onnx camel model.cml --arg p=weights std::macro std::opt::simplify onnx::export

当程序里任何地方都没有 `export_model` 调用时，这个遍翻译入口图本身，也就是 `main` 的计算：

- **输入**：入口图仍带有的每个参数一个，`with` 参数在前，名字与参数相同。张量参数必须声明元素类型和形状（`Tensor<f32, const [-1, 3]>`）；负的维度是动态轴（符号尺寸 `<name>_dim<axis>`）。只声明为 `Tensor` 而不带这些信息的参数会被拒绝，并指出该参数。数是 0 阶输入。结构体或元组每个叶子一个输入，以其路径命名（`p_fc1_w`、`t_0`）。没有参数的 `main` 得到没有输入的模型（常量模型）。
- **权重**：导出时用 `--arg` 给了值的参数是图中的常量，成为初始化器，以参数命名；对结构体，则以它的字段命名（`w`、`b`）。所以通常的划分是：数据作为普通参数（输入），权重作为 `with` 参数；导出时提供了权重的值，它们就是初始化器，没有提供就是输入。写成 `main` 的一个训练步（权重作为 `with` 参数传入，更新后的权重在结果里）不带值导出：权重既是输入也是输出。
- **输出**：`main` 的返回值；元组或结构体展开为每个叶子一个输出（`output`，或 `output_0`、`output_1_w`……）。
- **写到哪里**：`CAMEL_ONNX_OUT`，此处它是必需的。
- **拒绝**：这个遍翻译它能翻译的部分，在不能翻译的地方拒绝，以 `TranslationRefused` 定位在相应的构造上：效应（`println`、读文件）、未展开的宏（请运行 `std::macro`）、任何化简都去不掉的调用、没有下降规则的算子。这里没有单独检查程序是否“纯”；不只是做计算的程序改用 `onnx.export_model` 导出它的某个函数，拒绝消息也是这么说的。这个遍自己内联对 `main` 的调用，所以前面不带 `std::opt::simplify` 也能工作；使用 `grad` 或 `value_and_grad` 的程序仍然需要 `std::macro`。
- 含有 `export_model` 调用的程序，不论调用在哪里，都不会按这种方式翻译：如果调用在入口图里不可见（什么都没有内联），拒绝消息同上文一样指出 `std::opt::simplify`。

要把导出的模型与程序本身对照，用相同的值运行程序再比较：`camel model.cml --arg x=x.npy --arg p=weights -o out` 把 `main` 的返回值写成 NumPy 脚本能读的文件。

不带遍运行程序（`camel m.cml`）仍然可用：此时 `export_model` 自己展开宏并化简函数。它接受而遍拒绝的，是导出请求或捕获的值只在运行期存在的程序（随机初始权重、来自 `get_env` 的路径）。

### 警告

警告（`[Warn]` 诊断）不会让程序停下。下表是编译器关于效应的警告和流水线的警告；其余的警告（`InvalidEscapeSequence`、`ExitInUnusedBranch`、`MatchWithoutDefault`）见 [diagnostics.md](diagnostics.md)。

| 名字 | 代码 | 位置 | 含义 |
|---|---|---|---|
| `SemanticDiag::EffectNotExecuted` | `0x03100004` | 调用、赋值或分支 | 它有副作用，但没有东西依赖它，它也不在同步块里，所以永远不会运行。这是合法的（求值由依赖驱动）；报告它是为了让人看到被遗忘的 `sync`。每条被丢弃的链一个警告（`println(log(1))` 报告的是 `println`）。在同步块内从不报告 |
| `SemanticDiag::IgnoredSideEffect` | `0x03100002` | 调用或分支 | 它写一个变量（`var` 实参，或被捕获的变量），既没有被等待，也不在同步块里：它不排在该变量先前的使用之后 |
| `SemanticDiag::VarParamInAsyncFunction` | `0x03100001` | 参数 | 没有 `sync` 修饰符的函数带有 `var` 参数 |
| `SemanticDiag::CapturedWriteInAsyncFunction` | `0x03100003` | 函数 | 函数写一个被捕获的变量，却没有 `sync` 修饰符 |
| `RuntimeDiag::PassesNeverRun` | `0x0410000F` | 没有源码位置（属于命令行的问题） | 有遍写在消耗图的遍之后；它们不会运行。与程序的警告一起打印（JSON 模式下是 stderr 数组的最后一个元素） |
| `RuntimeDiag::ConstantExpressionFaults` | `0x04100015` | 表达式 | `std::opt::fold`（也包括 `std::opt::simplify` 内部的）遇到一个操作数全是常量而求值会出故障的表达式（`1 / 0`）。它不被折叠：有没有这个遍，程序的行为相同；运行到达该表达式时，故障连同位置一起报告。在这个遍运行之后打印到 stderr，每个表达式一次，在下一个遍之前 |

对 `EffectNotExecuted` 来说，什么算效应：在算子特性注册表里既不是纯的也不是仅状态的算子（与效应追踪记录的是同一个集合）、对声明为 `sync func` 或带 `var` 参数的函数的调用、对自身结果依赖某个效应的函数的调用，以及有这样的分支臂的分支。通过函数值进行的调用只能凭它的类型识别（`sync` 修饰符、`var` 参数）。

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

- `range` 从 0 开始计数；`data.modulePath` 是文件。没有源码位置的诊断（未知的遍名、永远不会运行的遍）的 range 全为零，`modulePath` 为空。
- `data.name` 是稳定的诊断名；`code` 是它的数字形式。
- 遍提交的每一张图（以及前端编码的每一张图）都做结构校验，release 构建中也是如此：节点、槽和边的引用在范围内，输入和输出互为镜像，`BRCH`/`JOIN` 的分支臂数目，`GATE` 有一个值输入和一个控制输入，直接调用传入的实参个数与被调用者接受的一致。违反时是 `RuntimeDiag::PassInternalError`，指出提交该图的遍，在任何东西运行之前报告。
- 从某个遍逃逸出来而又不是诊断的异常（提交重写时的图校验、调度器构建执行顺序时遇到环）报告为 `RuntimeDiag::PassInternalError`（`0x0400000C`），阶段为 `pass:<name>`，消息里是异常的文本，没有源码位置，退出码为 1。它标志的是编译器的缺陷，不是对程序的拒绝。即使那是调度遍，阶段给出的也是遍的名字，所以它不能说明程序是否已经运行了一部分。任何遍之外的异常报告为 `InternalDiag::UnknownInternalError`，阶段为 `internal`。
- `data.stage` 说明诊断在流水线的哪里产生：`parse`、`typecheck`、`compile`（见“资源上限”）、重写遍或翻译遍的 `pass:<name>`（包括未知的遍名）、调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::tf`）的 `run`，或 `internal`。

### 效应追踪（`CAMEL_EFFECT_TRACE`）

设置环境变量 `CAMEL_EFFECT_TRACE=<file>` 后，进程为每个被执行的有效应算子追加一行 JSON，按调用顺序排列（有效应算子指任何没有在算子特性注册表里登记为纯的或仅状态的算子：`println`、文件 I/O、`py_*`、赋值 `:op/assn*`、原地数组算子……）。创建和读取 `var` 变量（`:var/new`、`:var/get`）是仅状态的，不产生事件；向变量存入值（`:op/assn*`）则产生事件：

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
| `std::inline`、`std::specialize`、`std::opt`、`std::opt::simplify` 的增长（特化和内联复制的节点；`std::opt::simplify` 的所有轮次共用一个预算） | 4096 个节点 | `CAMEL_OPT_GROWTH_BUDGET=<nodes>` | 不再特化或内联；剩下的调用仍是调用。带 `-v` 时有日志 |
| `std::inline` 向其中内联的图的大小 | 4096 个节点 | 否 | 这次调用不内联 |
| `std::opt::simplify` 的轮数 | 16 | 否 | 程序按现状传下去。带 `-v` 时有日志 |
| 同一个递归函数的仅类型特化（张量实参的形状在递归的每一层都变化） | 8 | 否 | 这次调用保留它的被调用者 |
| 非尾递归，在每个调度器上相同：`std::nvm`、`std::fvm`、`std::jit`（解释执行的和编译为原生代码的调用一样计数）和 `std::tf` / `std::prl`（计数随元素转到运行它的工作线程上） | 1000 个打开的调用 | `--max-recursion-depth <n>`、`CAMEL_MAX_RECURSION_DEPTH=<n>`（1 到 1000000） | 程序停止，诊断为 `RuntimeDiag::MaxRecursionDepthExceeded`（`0x04000004`，阶段 `run`，退出码 1），定位在将成为第 1001 个打开调用的调用表达式；在它之前运行的已经运行，其输出已经写出。消息给出上限（`Maximum recursion depth (1000) exceeded when evaluating graph '...'.`），建议给出选项和变量 |
| `std::jit` 编译为原生代码的代码中的递归（分支臂已被内联的自递归函数，例如在 `std::inline` 之后）占用的原生栈 | 无单独的上限：递归深度按上一行计数 | 上一行的选项和变量 | 默认上限下与解释执行给出同一条诊断（`camel f.cml std::inline std::jit`：`depth(999)` 运行，`depth(1000)` 是 `MaxRecursionDepthExceeded`）。原生栈将满的检查不覆盖已编译的代码：上限调得很高而递归确实很深时，进程以段错误结束，没有诊断（上限为 1000000 时，20000 层的递归能运行，100000 层的递归崩溃） |
| `std::tf` 上线程的原生栈 | 与下一行相同：计数之外，线程的栈用掉十六分之十五时拒绝再嵌套一次调用 | `ulimit -s` 等（工作线程的栈大小取进程的栈上限） | 在该调用处给出 `MaxRecursionDepthExceeded`；此时消息给出打开的调用数和栈的大小。默认上限（1000）在 Linux 默认的 8 MiB 栈下远够不到这里（见下文的实测） |
| 线程的原生栈，所有执行后端 | 线程的栈（Linux 上主线程默认 8 MiB） | `ulimit -s` 之类 | 在原生栈将满（剩余不足十六分之一，且至少保留 128 KiB）时还要嵌套的调用，在到达上面的上限之前就被拒绝，诊断为 `MaxRecursionDepthExceeded`；消息说明原生栈已满。正是这一点使调高的上限不至于以段错误告终。栈的边界在 Linux（glibc）、Windows 和 macOS 上读取（后两者已实现，但没有编译或运行过）；在其他平台上，上限不能调高到默认值以上 |
| `std::macro` 对 `macro` 函数的提前求值 | 64 个求值器帧（32 层的自递归：`count(31)` 被求值，`count(32)` 不被求值） | `--max-macro-depth <n>`、`CAMEL_MAX_MACRO_DEPTH=<n>`（1 到 1000000） | 求值失败：在该调用处给出错误 `RuntimeDiag::MacroEvaluationFailed`，流水线停止（见“`std::macro` 报告什么”）。求值器在原生栈上嵌套（每帧约 630 字节），栈将满时以同样的方式停下 |
| 一张图的节点、节点块和边表项（一个节点占若干个 8 字节的块，即它的头部和它携带的内容；每条边占两个表项，两端各一个；所以一张图远在有 65535 个节点之前就满了） | 各 65535（16 位） | 否 | 构建该图的遍在任何东西运行之前失败，诊断为 `RuntimeDiag::GraphTooLarge`（`0x04000013`，阶段 `pass:<name>`），定位在编译出该图的函数（对遍创建的图，定位在它的某个节点），指出图、数量、它需要的大小和上限。在不受大小上限约束进行内联的 `onnx::export` 和 `onnx.export_model` 里，它是在 `export_model` 调用处的一次拒绝，带有同样的四项信息 |
| 图空间（程序的运行时图；一个函数约占 1 到 2 KB） | 总共 256 MB，随程序的需要按段保留 | `CAMEL_GRAPH_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`（`0x04000014`，退出码 1），定位在正在编码其图的函数，指出空间、用量、上限和变量。程序本身放不下时阶段为 `compile`，是某个遍使它增长时阶段为 `pass:<name>` |
| 永久空间（类型和布局；一个函数约占 0.6 到 2 KB，遍创建的内容永不释放） | 总共 256 MB，按段 | `CAMEL_PERM_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`，同上，除非知道正在编译的函数，否则没有位置 |
| 元空间（正在编译的图的常量和函数值；每个拥有它的函数、lambda 或分支臂约 1.2 KB） | 总共 256 MB，按段 | `CAMEL_META_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`，阶段 `compile`，定位在正在编译的函数 |
| 自动空间（运行期的值，受垃圾回收管理） | 宿主机的内存 | 否 | `RuntimeDiag::MemoryExhausted`（阶段 `pass:<scheduler>`），说明宿主机拒绝了这次分配 |

**递归上限计的是什么** 在每个执行后端上都相同：每一次处于打开状态且不是尾调用的函数调用，不论它是写成一次调用、通过函数值进行，还是由高阶算子（`map`、`filter`、`reduce`、`foreach`）为某个元素进行。处于尾位置的调用替换它的调用者，不计数（见 `docs/technical/19_tail_call_optimization.md`），所以任意深度的尾递归在任何上限下都能运行。`if` 的分支臂是它们所在函数的尾调用。唯一不由程序自己发出的调用，即运行时对 `main` 的调用，不计数。因此上限为 L 时，对于从 `main` 调用的 `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)`，`depth(L - 1)` 是能运行的最深调用，在 `std::nvm`、`std::fvm`、`std::jit`（解释执行的和编译为原生代码的）以及 `std::tf` 上都是如此：默认值下是 `depth(999)`。改变存在哪些调用的遍（`std::inline`）也改变被计数的内容，因为它们改变了图。

**设置它。** 命令行上的 `--max-recursion-depth <n>`，否则是环境变量 `CAMEL_MAX_RECURSION_DEPTH=<n>`；`n` 是 1 到 1000000 的整数。其他任何值（0、文本、更大的数）在程序编译之前就在标准错误上被拒绝（`Invalid --max-recursion-depth value: '0'. Expected an integer from 1 to 1000000.`），退出码为 1。没有对应的 `.opencmlrc` 键。既没有选项也没有变量时，每个调度器都在 1000 处停下，并行调度器同样计数（设计原则 2.10）。

**调高上限的代价**（在 Linux x86-64、release 构建上测得，函数有三个槽）：

| 执行后端 | 一次调用存放在哪里 | 每个打开的调用 | 8 MiB 栈上最深的递归 |
|---|---|---|---|
| `std::nvm` | 帧池（堆）里的帧，加上原生栈上解释器循环的一次激活 | 约 545 字节原生栈，约 100 字节帧 | 14441 次调用；然后是诊断（不做栈检查时进程在 15399 处崩溃） |
| `std::fvm`，直接调用 | 帧池里的帧和解释器自己的调用栈里的一项，都在堆上；不占原生栈 | 约 100 字节 | 上限本身（上限为 1000000 时运行了 999000） |
| `std::fvm`，通过函数值的调用或高阶算子的元素调用 | 同上，再加上原生栈上解释器循环的一次嵌套激活 | 嵌套激活约占 1.7 KiB 原生栈 | 4587 次嵌套激活；然后是诊断（不做检查时在 4890 处崩溃） |
| `std::tf` | 每张图一个的池（堆）里的帧，加上原生栈上的求值器 | 约 520 字节原生栈 | 14875 次调用；然后是诊断（不做检查时在 15858 处崩溃） |

**默认上限的余量**（Linux x86-64、发布构建实测）：把栈上限逐步调小，默认上限允许的最深递归（直接递归 `depth(999)`、经函数值 `step(499)`、`map` 元素内 `depth(998)`）仍能跑完所需的最小 `ulimit -s`：`std::nvm` 与 `std::tf` 三种情形都是 768 KiB；`std::fvm` 与 `std::jit`（含 `std::inline std::jit`）直接递归和 `map` 内是 192 KiB，经函数值是 1024 KiB。默认的 8 MiB 栈因此有 8 倍以上的余量；栈更小时得到的是上面那条“原生栈已满”的诊断，不是崩溃。主线程栈为 1 MiB 的平台（Windows 的默认值）和工作线程栈为 512 KiB 的平台（macOS）上，默认上限下最深的递归可能先遇到这条诊断，尚未在这些平台上验证。

`std::nvm` 和 `std::fvm` 的帧池按上限确定大小：每个可能打开的调用 4 KiB（默认值下约 4 MiB），最多 256 MiB；它一次性保留，只有用到的部分才被触及。帧非常大的函数的递归可能在计数到达之前就把它填满；那是同一条带位置的诊断，说明帧池已满。运行时间不受上限影响。`CAMEL_NATIVE_STACK_GUARD=0` 关闭原生栈检查；它用于测量栈实际在哪里结束（上面的崩溃点），不供正常使用。

三个空间上限限制的是编译后程序的大小，不是单张图的大小：它们是总量，只用来防范不停分配的编译器（展开或内联永不结束的遍，会因图空间或永久空间被填满而停下；这所需的时间增长得比上限更快，这就是默认值没有定得更大的原因）。`CAMEL_GC_STATS=1` 在退出时打印三个空间的占用情况（`[camel] spaces: ...`：使用中的字节数、最高水位、保留的字节数、段数、分配次数、上限）。

其他标为“否”的上限是编译期常量（`generic.h`、内联器的配置）；它们没有命令行选项，也没有 `.opencmlrc` 键（`.opencmlrc` 只配置遍列表）。

`std::opt::simplify` 对实参是常量的递归每轮展开一层（`fib(15)` 会变成它的整棵调用树）；让它停下的是增长预算。`onnx.export_model` 化简它所导出的函数时不受增长预算和内联大小上限约束，因为导出必须得到直线代码；通过 `std::opt::simplify onnx::export` 导出的流水线则受它们约束，对大模型可能需要调高 `CAMEL_OPT_GROWTH_BUDGET`（此时导出遍以 `TranslationRefused` 拒绝，而不是写出不完整的模型，拒绝消息给出预算的名字、它的值和变量）。

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

- **camel-cpp**：一步把 `.cml` 转译为 C++ 并编译。产物输出到 `tmp/`。
  - 用法：`camel-cpp <file.cml>`
  - 等价于：`camel <file.cml> std::cpp`，输出写入 `tmp/<stem>.cpp`，然后用 clang++ 编译，生成 `tmp/<stem>.exe` 和 `tmp/libcamel.dll`。
  - 构建位置：`build/tools/camel-cpp/Release/camel-cpp.exe`（Windows；为便于移动，libcamel.dll 会一并复制到旁边）。

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

# 转译为 C++ 并编译（一步完成，输出到 tmp/）
camel-cpp test/cases/linear/nvm/fib.cml

# 格式化代码（用 camel-format，不是 camel format）
camel-format -i src/xxx.cml

# 只做语法检查并以 JSON 输出
camel check -s -O json src/

# 打印中间表示
camel inspect --gir main.cml

# 查看 Camel 之禅
camel --zen
```
