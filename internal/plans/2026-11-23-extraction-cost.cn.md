# 提取的代价归目标库，内核不判贵贱，不做隐式提取

依据：设计原则「提取的代价」一条（负责人 2026-11-23）与「饱和」一条（提取时被多处读的值只算一次）。

## 现状（要改的）

- **内核按名字登记代价模型**：`src/kernel/runtime/writings_extract.cpp` 的 `costModels()` 表，内置 `"nodes"`（每个节点算 1）与 `"weighted"`（`WeightedModel`：除法比乘法贵、乘法比加法贵——内核在替各个目标判断贵贱）；`findCostModel(name)`、`registerCostModel`、`writings.h` 的 `kWritingsCostModel = "weighted"`。
- **隐式提取**：`src/kernel/execute/pass.cpp` 约 2530 行，程序还带着多种写法时，下一个遍若是后端或导出器，内核用 `"weighted"` 先提取一遍，只记一条日志。
- **两条路**：C++ 的 `std::cxx::extract` 用字符串参数 `cost: string = 'weighted'`（`src/stdlib/std.cml` 约 375 行，`std/cxx/opt/generic/extract.cpp`）挑内核模型；Camel 的 `rewrite::extract<cost>` 收一个函数值（`rewrite/module.cpp` 的 `CamelCostModel`）。

## 要达到的状态

1. **内核只留机制**：`extractWritings(ctx, program, const CostModel &)` 与 `CostModel` 这个接口保留；内核里的代价表、两个内置模型、`findCostModel`、`registerCostModel`、`kWritingsCostModel` 全部删除。
2. **代价是函数值，由目标库提供**：标准库给一个默认代价（今天 `weighted` 的意思，但它判"贵"读的是算子记录上声明的性质——在登记时写进记录，例如相对代价——不按算子名字）；CPU、tiles、GPU 等目标库各自导出自己的代价函数。代价函数能读节点的算子、操作数的形状事实、算子记录上的性质。
3. **一条路，一个入口**：删掉 `std::cxx::extract`，提取只有 `rewrite::extract<cost>`。把 Camel 函数变成代价模型的那一层（`CamelCostModel`）在 rewrite 模块，它链 graph、认得 `Node`；`libcamel` 不能反向链模块，所以 C++ 的入口要么另造一座桥、要么不存在——取后者。命令行把 Camel 函数当遍用（`docs/cli.md`「A pass written in Camel」），函数型遍参数让它写成 `rewrite::extract cost=std::cost::default`。
4. **不做隐式提取**：pass.cpp 那段删掉；程序带着多种写法交给后端或导出器时，报 `PassNeedsExtraction`，诊断写明"在它前面加一个写明代价的提取"。若默认流水线里有饱和，它也写明提取与所用代价。
5. **按共享计**：同一个值被多处读，只计一次代价（按有共享的图算，不展开成树）；若现在的实现按树计，改过来。

## 验收

- `git grep -nE 'costModels\(\)|findCostModel|kWritingsCostModel|WeightedModel|NodeCountModel|std::cxx::extract' -- src test` 为 0；内核没有任何从函数值到代价模型的登记处（槽、表、回调都没有）。
- 用例：① 饱和后不提取直接 `std::nvm`，报 `PassNeedsExtraction` 且诊断点名要加提取；② 同一个饱和结果用两个不同的代价函数提取，选出不同的写法；③ 代价函数读形状事实，同一程序在不同形状上选出不同写法；④ 一个被三处读的子表达式只计一次代价（构造两种写法，按树计与按图计会选反的那种）。
- 默认代价判"贵"不按名字：`git grep` 默认代价的实现里没有算子名字或 URI 的字符串比较。
