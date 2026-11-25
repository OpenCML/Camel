# 转译遍把原图原样交下去

依据：设计原则第 9 节"遍的输入与输出都是图"（负责人 2026-11-24 修订，取代 10-22 那条中打印与导出的部分）。

遍分三类（概念上的，实现里不设字段）：重写遍返回改写后的图；转译遍做完打印或导出，把原图原样交下去；调度遍执行效应，返回部分求值剩下的子图或空图，`std::nop` 是不产生效应的调度遍。流水线只有一条规则：写下的遍依次运行，跑完图还没变空，就交给 `std::default`。只想看、不想执行，就在末尾写 `std::nop`。

## 代码

- **转译遍返回输入的程序**，不再返回 `nullptr`：`std::gir`、`std::rgir`、`std::jgir`、`std::agir`、`std::argir`、`std::gir_regions`、`std::rgir_regions`、`std::cml`、`std::cpp` 及其 `module_file`/`inspect`、`std::tfdump`、`std::topo_node_seq`、`onnx::export`，以及各模块注册的同类遍（逐个查 `registerModulePass`）。字节码与即时编译的转储遍，如果它们只是打印，也照此处理；如果它们本身就执行程序，就是调度遍。
- **`std::nop` 不变**：它是不产生效应的调度遍，返回空图。`src/stdlib/std.cml` 里它的文档注释改成"扔掉这张图，不执行：返回空图，不产生任何效应"（D-1124-84 挂起的那一句）。
- **`PassesNeverRun` 警告**：只在调度遍（含 `std::nop`）返回空图之后还写着遍时给出。
- **`--dump-after-each-pass`**：转译遍之后照常留快照（图没变，快照与前一个相同；文档说明这一点即可）。

## 文档

- `docs/cli.cn.md` 与 `docs/cli.md`：第 44 行（"调度遍和翻译遍消耗图"）、第 150 行、第 155–159 行（"写在消耗图的遍之后的遍永远不会运行"那段改成调度遍）、第 230 行（`onnx::export` 不再消耗图）。
- `AGENTS.md` 的遍表：`std::gir`、`std::rgir`、`std::jgir` 那几行的"returns an empty graph"改掉，示例 `camel fib.cml std::gir` 改成 `camel fib.cml std::gir std::nop`。
- `docs/design/graph-store/plan.cn.md` 第 7 行（`std::cml` 返回 `nullptr`）。
- 文档里所有只为看图或导出的示例命令，在末尾补 `std::nop`；本意是看完接着运行的保留原样。

## 测试与脚本：在末尾补 `std::nop`

- **测试计划**：参数以转译遍结尾的流水线，在末尾补 `std::nop`。粗数是以翻译遍结尾的 342 处、以 `onnx::export` 结尾的 60 处，要用工具找全，包括参数里带空格的写法和别的写法。
- **判断标准**：这条用例本来就不期望执行程序（期望里没有程序的输出），就补；期望里有程序输出的，说明它原来就靠别的遍执行了，不补。拿不准的逐条看期望。
- **脚本**：基准、showcase、性能基线脚本里只为编译、打印或导出的命令，同样补。
- **新增几条用例钉住新规则**：
  - `camel f.cml std::gir` 打印图之后接着执行，输出里有程序的结果；
  - `camel f.cml std::gir std::nop` 只打印；
  - `camel f.cml std::gir std::opt std::gir` 打印两次；
  - `onnx::export` 之后接 `std::nvm`，导出并执行。

## 验收

- 测试闸门无新增红，迁移那一笔的合并说明列出补了 `std::nop` 的计划数。
- 上面四条新用例全过。
- `git grep` 找不到还以转译遍结尾、又没有 `std::nop` 的计划参数，例外逐条写明原因。
