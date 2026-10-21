# H19 计划：标准库重新规划的落地

> **状态**：计划提交，未开始实现。分支 `handover/H67-stdlib-plan`，基于 `origin/develop` 的 `043ac0eb`；写本文时 `origin/develop` 已到 `9e25f2c7`（H60 与文档改动，不碰标准库）。实现前先 rebase 到当时的 `origin/develop`。
> **依据**：[标准库的重新规划](../../design/stdlib/README.cn.md) 第 2、3 节，[盘点](../../design/stdlib/inventory.cn.md)，[设计原则](../../design-principles.cn.md)。
> **交付**：本文件是计划；实现完成后另交 `docs/handover/reports/H19.md`（做了什么、和计划的差异、留下的项）。
> **位置说明**：任务指定 `reports/H19-plan.md`；H51 的计划在 `plans/`。`docs/handover/README.cn.md` 的“先交计划”一节仍写 `reports/`，本文从任务指定的位置。

---

## 0. 摘要

### 0.1 步骤

| # | 步骤 | 依赖 | 能否单独落地 | 主要验收 |
|---|---|---|---|---|
| 0 | 写《标准库约定》 | 无 | 是（纯文档） | 链接检查；约定页与本文的模块账本一致 |
| 1 | 原生声明机制，试点 `bytes` | 无 | 是（新语法 + 装载器 + 一个模块） | `bytes` 全部测试不变；`inspect --ops` 前后一致；`--verify-ops` 通过 |
| 1b | 泛型原生声明 + 混合模块夹具 | 1 | 是 | 泛型夹具按类型实例化；`std::gir` 上是 OPER，不是 CALL |
| 2 | 删掉没有去处的公开面（`this`、`para::rendezvous`、空 `io`） | 无 | 是 | `feat/modules/std` 绿；`registry_all` 更新；无旧导入 |
| 3 | 工具收拢：`perf` + `sim` → `bench` | 1 | 是（条件于问题 1/6） | `bench` 测试与示例数值不变（仅标题）；`bench` 不进参考 |
| 4 | 系统层改名与拆分：`sys`→`env`、`os`→`process`、终端进 `io`、`exit`/`sleep` | 无 | 是（4a 改名，4b `get_env` 可空） | 三个模块的测试与 `inspect` 绿；无 `os`/`sys` 活引用 |
| 5 | `algo` 并入 `array`，删演示排序 | 1b | 是 | `array` 测试、示例与排序原地形式不变；`algo` 无活引用 |
| 6 | `statistics`→`stats`，再改写为 Camel | 5（6b 用 `array::sort`） | 是（6a 改名、6b 改写分开） | 6b 前后逐位相同的输出；`stats` 测试与示例绿 |
| 7 | 错误约定：`fs`/`env`/`time` 改 `T!`/`T?`，加 `list_dir`/`remove_all`，错误种类目录 | 无（7c 在 7a/7b 之后） | 是（7a fs、7b time、7c 目录分开） | `fs`/`time` 测试与示例；`error-kinds --check` |
| 8 | 去重与调用约定：`Set::has`、`append`/`extend`、`re::test`、`str::bytes` | 无 | 是 | `--verify-ops`、`stdlib_values`、示例 |
| 9 | 资源名与 `eprintln` | 4 | 是 | `resources.plan`；stderr/stdout 分离 |
| 10 | 其余固定签名模块声明化 | 1b、4–8 | 每个模块一次提交 | 每个模块 `--verify-ops`；`inspect --ops` 不变 |
| 11 | 文档、示例、参考生成与收尾 | 全部 | 是 | 下面第 6 节的完整门禁 |

每个编号提交都能构建，相关模块计划与 `examples.plan.toml` 通过；不允许“先改名、下一次提交再修用户”的中间态（见 3.1）。

### 0.2 最危险的三步

1. **第 1 步（原生声明机制）**。它换掉的是每个 C++ 模块“名字和类型从哪里来”的事实来源：今天是 C++ 的 `OperatorGroup`/`DynamicFuncTypeResolver` 加 `OperatorTable` 加只读签名文件三处，之后固定签名的模块只剩 `.cml` 声明一处。装载器还要在 `xxx.cmo` 旁边认出 `xxx.cml` 并把它当声明合并进来。做错时不会崩，而是**重载悄悄选错**（字面量进到另一个重载）或**声明根本没有被加载**（`.cml` 被 `.cmo` 挡住，模块仍是旧实现，Camel 声明成了死文件）。这是 H51 §1.6 已经指出的冲突，只是它把优先级说反了（见 1.2）。
2. **第 5 步（`algo` 并入 `array`）**。`sort` 的原地形式（`setInPlaceForm`）是按 URI 登记的；一旦 `array::sort` 变成 Camel 包装函数，`xs = xs->sort` 就不再走 `_in_place`，性能与图形状都变。必须让 `array` 里的 `sort` 是**原生声明**（OPER），而不是包装 CALL。`argsort`/`flatten`/`unique` 还有泛型声明与跨模块 URI（`array_kernel` 只注册执行器，不导出名字）。这一步同时是第 1b 步泛型声明的第一个真实用户。
3. **第 7 步（`fs`/`env` 的错误迁移）**。`fs` 从“故障”改成 `T!` 会改用户可见行为与示例输出；`get_env` 从“缺失返回空串”改成 `string?` 要动约 160 处调用（115 个文件）。前者漏一处是红构建，后者写错一个 `?? ''` 会把“缺失”悄悄当成空串。两者都不难，但都容易在大量机械修改里丢掉一处语义。

`stats` 的 Camel 改写（第 6b 步）另有一个数值逐位一致的风险，但它的判据就是现有测试与 golden，改不对会红，不会静默。

### 0.3 需要作者裁定

见第 7 节。七个问题的答案会改变步骤的范围或顺序；其余（`mkdir` 幂等、`list_dir` 排序、`Set::has` 的取舍等）本文直接定了。

---

## 1. 今天的代码事实

### 1.1 已经做完、不要重做的

- `str` 的 `len`/下标/`substr` 按码点，`Bytes` 类型与 `bytes` 模块已存在（盘点第 2.1 节；`test/cases/modules/stdlib/utf8_string.cml`）。
- 缓冲区对象、数据块、`std::mem::buffers()` 已落地（H17 第 1 项、H28）；H19 的 Camel 代码不需要为“数组会不会被复制”另做假设。
- 键式随机数 `random::key`/`split`/`permutation` 已落地（H20-3），是纯函数，`random.plan.toml` 钉了逐位结果。
- `fs::remove`/`fs::remove_dir` 已经返回 `T!`；`fs::read_text` 仍返回 `string` 并在失败时故障，`try_read_text` 是它的 `T!` 版本。
- JSON 访问器已接受 `Json!`/`Json?!`，`JsonTooDeep` 已有（H16 报告）。
- `match type`、返回类型推断已落地（H51a），但 H19 不依赖它们。
- `stdlib/array.cml` 已经是 Camel；`stdlib/rules.cml` 由 H38 新建、H65 继续扩展。
- H51 只落了第 1–2 步（`70db0548`、`e94e6860`、`cb76ff24`）；`tree_*` 的 Camel 改写、`tree`→`ckpt`、删 `src/compile/gir/tree.cpp` 都还没做。

### 1.2 与设计稿/旧文档不一致，本文怎么裁

| 说法 | 代码事实 | 本文采用 | 理由 |
|---|---|---|---|
| H51 计划 §1.6：新增 `stdlib/tree.cml` 会遮蔽 C++ 的 `tree` | 装载器在同一目录里**先找 `xx.cmo`，再找 `xx.*`**（`src/core/context/context.cpp:547`）；`out/latest/stdlib/tree.cmo` 存在时，`tree.cml` 根本不会被加载 | **代码优先**。H51 的补救（C++ 模块改名 `ckpt`）仍然必要，但真正的失败模式是“Camel 库静默成为死文件”，不是“遮蔽” | 装载顺序是已实现的行为；H51 的结论对、理由要改。H19 的装载器改动不改变这个顺序，只增加“`.cmo` 旁边的 `.cml` 是声明”的规则 |
| 设计 §2.2：原生函数以 `.cml` 无函数体声明为唯一来源 | 语言没有 `native`/无体函数；`stdlib/signatures/*.cml` 是只读参考（`docs/technical/09_operator_registration.md:272`），不参与编译 | **设计优先**，但 H19 要先把机制做出来（第 1 步）；机制不通则退回“签名只做校验”，并把第 1 项留给后续任务（问题 1） | 这是 H19 的核心，不能假装已经存在 |
| 设计 §3.3：声明里写 `uses`/`resource`/`pure` | 语言没有这些语法；H7（资源）未开始；纯度和资源今天在 C++ 的 `OperatorTable::declare`/`declareResources` | **先按代码**：第 9 步只改资源名；声明的 `@pure`/`@effect` 注释与 C++ 表对账；语法等 H7 | 语法不是迁移能顺带做的；H7 是它的归属 |
| 设计 §2“一个事实只写一处”适用于所有算子 | `tensor`/`nn`/`onnx`/`graph`/`rewrite`/`para` 的类型由 `OpDef`/`DynamicFuncTypeResolver` 按实参算（形状、dtype、闭包），固定 `.cml` 声明表达不了 | **代码的现实优先**：固定签名用声明；计算签名保留 `OpDef`/resolver 作为唯一来源，参考文档由 `inspect --ops` 生成 | 强行搬会要求在编译器里重写形状推断；问题 1/2 |
| 盘点 §2.3：`get_env` 缺变量是故障 | `modules/sys/operators.cpp:35` 缺变量返回 `""`，不故障；签名文件也写“未设置时为空串” | **代码优先**；H19 按设计改成 `string?`，这是有意的行为变化，不是修 bug | 盘点这条已过时；迁移时用 `?? ''` 保留旧行为的地方要逐处判断 |
| 盘点 §2.5：`math` 没有数值声明 | 盘点之后 `3326e431` 已补上 `modules/math/semantics.cpp`（503 行，集中声明）；盘点这条已过时 | **代码优先**；`math` 的语义与值留在 C++，只把函数签名搬进声明 | 数值契约不能丢；盘点不是规范 |
| H19 正文第 3 项：`random` 的组合部分、`Map`/`Set` 的组合操作用 Camel 写 | `random` 的 `choice`/`sample`/`shuffle` 读全局生成器，输出序列被 `random.plan.toml` 与 `std::cpp` 一致性测试逐位钉住；`map`/`foreach` 的调用顺序在并行调度下没有保证。`Map`/`Set` 的名字在前奏里，`map` 还是前奏函数，`import map` 会遮蔽它 | **代码/名字规则优先**：`random` 不改写；`Map`/`Set` 不搬出前奏。H19 只做真正的重复项（见 1.3） | 这是本文与任务正文最大的偏差，理由见本行与 1.3；`Map`/`Set` 见问题 4 |
| 设计 §3：`tensor_definitions` 并入 `tensor` | 核心按名字 `"tensor_definitions"` 单独编译定义库（`modules/tensor/ops/catalog.cpp:67`）；`tensor` 是动态签名模块，不能把 `.cml` 放在 `tensor.cmo` 旁边当可执行库 | **先不并**；文件保留，继续不供用户导入；并入要等 H17 第 2 项与动态声明，另立任务 | 合并会破坏定义库的构建路径 |
| 设计 §3：`io` 是新模块（stdin 作为流、stdout、stderr） | 今天 `io` 是空壳；`Stream` 类型不存在 | 第 4 步把空壳换成**终端/控制台**模块（raw mode、`get_char` 等），流与 `stdin` 值留给流任务；`eprintln` 进前奏 | 不发明 `Stream`；问题 5 |
| 设计 §3：`bench` 是 `perf` + `sim` | `stdlib/perf.cml` 的模块名竟写成 `util`；`sim` 是独立的 C++ 工具模块 | 第 3 步合并成 `bench`（Camel 计时 + 原生 `pi_hits`/`asian_call_price`），不进公开参考 | 修 `module util` 的错名，顺带收拢；问题 6 |

### 1.3 真正的重复项（分散在第 4、5、7a、8 步）

| 重复 | 今天 | 之后 |
|---|---|---|
| `sleep` | 前奏的 `sleep` 与 `os::sleep` 两份 | 只留 `process::sleep` |
| 排序 | `algo::sort`、`insert_sort`、`quick_sort`、`merge_sort` 四个名字做同一件事 | `array::sort` 一个；三个演示排序删除；`merge_sorted_arrays` 作为合并原语留在 `array` |
| `Set` 的成员判断 | `has(Set)` 与 `contains(Set)` 两种写法 | 只留 `Set::contains`；`Map::has` 不变 |
| 字符串转字节 | `str::bytes`/`from_bytes` 与 `bytes::encode`/`decode` | 只留 `bytes` |
| `fs::replace` 与 `str::replace` 同名不同义 | 两个模块各一个 | `fs::replace_file` |
| `min`/`max`/`sum`/`mean` | 见下 | **不合并**：`math::min/max` 是二元 IEEE 极值，`array::min/max` 是数组归约（`T?`），`tensor` 的 `sum`/`mean`/`min`/`max` 是沿轴归约，`stats::mean` 是统计量，`array::sum` 是数组和。它们是不同的运算，不是同一事实的两份 |

`min`/`max`/`sum` 在 H19 正文的去重清单里，但审计的结论是它们不是重复项。是否要把二元 `min/max`（以及 51 个文件在用的 `abs`）搬进前奏，是语言表面问题，见问题 3。

### 1.4 依赖：H17 第 2 项（`T[维度...]`）

**H19 可以先落，不需要等 H17 第 2 项。** H17 第 1 项（缓冲区/数据块）已落地；第 3 项（排序、`append`、下标、循环状态）部分落地，H19 用到的 `sort`/`argsort`/`reduce` 已经够用；第 2 项（`T[维度...]`、维度绑定、`Tensor` 别名、第一维语义）未开始。

H19 在 rank 1 上写和测，`T[]` 就是今天的 `T[]`。H17 第 2 项落地后需要复查的地方，本文先列清楚，交给 H17 的 P4/P5，不在 H19 里假装完成：

- 核心的 `len`/下标/`slice`/`append`/`extend`/`concat`/`sort`/`argsort`/`map`/`filter`/`reduce`/`foreach` 改成按第一维解释（`docs/design/arrays-memory/plan.cn.md` §6.1）。`array::reverse` 用 `range` + 下标、`take`/`drop` 用 `slice`；H17 的 P5 **不改** `slice`/下标（它们归 H21 的行视图），所以这三个函数在 rank ≥ 2 上要等 H21，不会自动跟随。`flatten` 是**嵌套数组**的一层摊平（`T[][]`），不是 rank 2 的第一维摊平，文档要写清。
- `array::sort_by` 不泛化到 rank ≥ 2（H17 计划 §6.1 明说“不泛化（H21）”）；H19 的文档只承诺 rank 1，H21 再做行排序。
- `array::sort`/`argsort` 的原生声明类型要随第一维语义从 `T[]` 改成带维度的形式；声明是一行，比改 C++ resolver 便宜。这是第 1 步的附带收益。
- `stats` 只承诺 rank 1；rank ≥ 2 的 `mean`/`median` 是沿第一维还是全体元素，是 H17 第 2 项之后的产品决定，H19 不猜。
- `Tensor<T, [..]>` 的拼写与 `Tensor` 别名由 H17 第 2 项统一；H19 的文档与示例遇到时按今天的写法，H17 落地后一起改。

### 1.5 与其他任务的边界

| 范围 | 归属 | H19 的动作 |
|---|---|---|
| `stdlib/tree.cml`、`tree_*`、`ckpt` 拆分、`modules/tree`、`tree.plan.toml`、树用例 | H51（步骤 13） | **不碰**。只在 `stdlib-reference.mjs` 的 `MODULES` 与文档里与 H51 协调；谁后落谁 rebase。注意第 1 步的装载规则：`tree.cml` 与 `tree.cmo` 不能并存（前者会被当成声明模块），H51 必须在同一提交把 C++ `tree` 改名 `ckpt`；H51 计划本来就这么做，但顺序不能反 |
| `stdlib/rules.cml`、`modules/rewrite`、`rules.plan.toml`、规则用例 | H38 / H65（typed laws） | **不碰**。`rules` 与 `rewrite` 不进 H19 的声明化清单；参考文档是否收录由 H38 决定 |
| `json` 的 H16 第 6、7 项 | H66 | H19 只在第 10 步把 `json` 声明化；若 H66 同时在改 `json` 的测试，先合 H66 再 rebase |
| `nn` 优化器 | H64、H51 步骤 14 | 不碰；H19 的模块账本里 `nn` 不变 |
| `tensor`/`onnx`/`tiles`/`autodiff`/`graph`/`para` 的签名 | H17/H51/各自任务 | 保持 `OpDef`/resolver，不做声明化 |

H19 拥有：`bytes`、`str`、`array`、`stats`、`env`、`process`、`io`、`fs`、`time`、`json`、`re`、`random`、`crypto`/`hash`、`algo`、`bench`、`test_support`、`scripts/stdlib-reference.mjs`、`test/plans/feat/modules/**`（除 `tree`/`rules`/tensor 栈）、错误种类目录。

---

## 2. 模块账本

| 今天 | 之后 | 动作 | 步 |
|---|---|---|---|
| `io`（空壳） | `io`（终端/控制台） | 删空壳；用终端算子重建；`Stream` 以后 | 4 |
| `this`（`zen` 彩蛋） | — | 删；`zen` 进 `test_support` | 2 |
| `para` | `para` | 留 `unordered_foreach`/`reduce`；`rendezvous` 进 `test_support` | 2 |
| `perf`（`module util`） | `bench`（工具） | 与 `sim` 合并；不进公开参考 | 3 |
| `sim` | `bench`（工具） | 合并；`pi_hits`/`asian_call_price` 原生声明 | 3 |
| `profiler` | `profiler`（工具） | 只在 Debug；不进参考 | 2 |
| `statistics` | `stats` | 改名；再改写为 Camel | 6 |
| `sys` | `env` | 改名；`get_env`→`string?`、`get_cwd`→`string!` | 4 |
| `os` | `process` + `io` | `sleep`/`whoami`/`cpu_count` 留 `process`；终端进 `io`；`exit` 的 URI 与声明归内建 | 4 |
| `crypto` | `hash` | 只改名；不扩 md5/hmac | 4 |
| `algo` | —（`array`） | `sort`/`argsort`/`flatten`/`unique`/`merge_sorted_arrays` 进 `array`；删三个演示排序；C++ 内核改名 `array_kernel`，协议 `array`，不导出名字 | 5 |
| `array` | `array` | 加五个原生声明；其余不变 | 5 |
| `bytes` | `bytes` | 声明化试点 | 1 |
| `str` | `str` | 删 `bytes`/`from_bytes`；声明化 | 8、10 |
| `math` | `math` | 声明化；数值语义与值留 C++ | 10 |
| `fs` | `fs` | `T!`；`list_dir`/`remove_all`；`replace`→`replace_file`；声明化 | 7、10 |
| `time` | `time` | `strptime`→`float!`；声明化 | 7、10 |
| `json` | `json` | 表面不变；声明化 | 10 |
| `re` | `re` | 只改 `test` 的参数顺序；声明化 | 8、10 |
| `random` | `random` | 不改写；声明化（含泛型 `choice`/`sample`/`shuffle`） | 10 |
| `graph`、`rewrite` | 不变 | 计算签名，保持 `OpDef`/resolver | — |
| `tensor`、`nn`、`onnx`、`tiles`、`autodiff`、`pyplot`、`python` | 不变 | 动态/可选，H19 不动 | — |
| `tensor_definitions` | 不变 | 合并进 `tensor` 延后 | — |
| `tree` | `tree` + `ckpt` | H51 | H51 |
| `rules` | `rules` | H38/H65 | H38/H65 |
| `test_support` | 新 | `zen`/`rendezvous`；不进参考 | 2 |
| `bench` | 新 | `perf`+`sim`；不进参考 | 3 |

新模块：`hash` 只是 `crypto` 改名；`csv`、`compress`、`log`、`stream`、`net`、`http`、`data`、`optim`、`arrow` 都不在 H19。它们是新功能，不是迁移；扩展库也不在本项。

---

## 3. 步骤

### 3.1 每一步都保持绿的规则

1. **改名/删除与所有用户在同一提交**：`import`、`test/cases`、`test/plans` 条目、`registry_all`、`stdlib-reference.mjs` 的 `MODULES`、`check-std-includes`（如果涉及）、`examples/`、`benchmarks/` 一起改。没有“旧名先留着、下次再删”。
2. **不留别名/兼容层**：内测阶段，旧名直接消失。唯一的“过渡”是声明化时签名文件搬到模块的 `.cml`，同一提交完成，不并存。
3. **按模块分批，不按动作分批**：一步可以只包含一个模块的改名 + 声明 + 测试，不把“所有模块改名”放在一个提交里；但一个模块的改名不能跨提交。系统层的 `sys`/`os`/`io` 用户集重叠，允许在同一个系统批次里一起改。
4. **围栏**：`tree`、`rules`/`rewrite` 的文件与计划 H19 不编辑；它们的计划在每个提交上都应保持绿。
5. **行为变化单独标出**：`get_env` 可空、`fs` 的 `T!`、`fs` 的错误种类、`bench` 的输出文本、`stats` 的模块名是用户可见变化；每个都在 `H19.md` 里列一行。

### 第 0 步：约定页

**做什么。** 新增 `docs/stdlib-conventions.cn.md`，一页，作为后面每一步的裁判：

- 分层：前奏（内置，只有 `print`/`println`/`eprintln`/`format`/转换/`len`/`range`/`enumerate`/`assert`/`error`/`exit` 等），基础（`str`/`bytes`/`array`/`math`/`random`/`stats`/`time`/`re`/`json`/`hash`；`Map`/`Set` 暂在前奏，见问题 4），系统（`fs`/`io`/`env`/`process`），数值（`tensor` 栈），工具（`test_support`/`bench`/`profiler`）。
- 命名：小写下划线；函数是动词；判断 `is_x`/`has_x`；转换 `to_x`；已有 `get_env` 保留。
- 参数：数据在前，配置/函数在 `<...>`；链式 `x->f(a)->g<b>`；例外只有 `format<args>`（格式串是接收者，写在约定页里），`tensor` 的轴参数仍是普通参数。
- 错误：环境失败 `T!`，可能不存在 `T?`，程序错误是故障；同一函数不混用；调用点用 `!!`/`??` 显式处理；错误种类见目录。
- 资源：`stdout`、`stderr`、`stdin`、`fs`、`clock`、`env`、`net`、`rng`、`process`；环境资源读/写模式；`@effect` 注释。
- 纯度：`@pure` 当且仅当不读环境、不写环境、同输入同输出；键式随机是纯的，全局生成器不是。
- **一个事实一处**：固定签名 → 无体 `native func` 声明；计算签名 → `OpDef`/`DynamicFuncTypeResolver`；纯度/资源/数值语义/原地形式 → C++ 的 `OperatorTable`/`declareSemantics`/`setInPlaceForm`；文档由 `stdlib-reference` 生成。没有第四处。
- 运算分类：值（二元）/数组（第一维）/轴（张量）是不同运算；同名只在不同模块出现，调用方用导入决定是哪一个。
- 兼容：pre-1.0，不保留别名。

**文件。** 新页；在 `docs/design/stdlib/README.cn.md` 第 2 节加一行链接；`scripts/stdlib-reference.mjs` 生成的头加一行“约定见 …”（这样 `--write` 会把它带进 `docs/stdlib-reference.md`，不手改生成物）。

**验收。** `node scripts/check-doc-links.mjs --all`；页里的旧名→新名表与第 2 节账本逐行一致；无代码改动。

### 第 1 步：原生声明机制（最危险）

#### 1a 语法与前端

**语法（提案）。**

```camel
module bytes

/// The bytes of a string, UTF-8 encoded.
/// @pure
export native func encode(text: string): Bytes = 'bytes:encode'

/// The text of the bytes; an error value when they are not UTF-8.
/// @pure
/// @fails error `EncodingError` when the bytes are not UTF-8
export native func decode(bytes: Bytes): string! = 'bytes:decode'
```

- `native` 是函数修饰符；没有函数体；`export`/`inner` 与普通函数相同；URI 可省略，默认 `<module>:<name>`；同名重载可以各自写 URI（`math:min_i` 等），也可以重复同一个 URI（今天的 `statistics:mean` 四个重载共用一个执行器）。
- 声明可以带 `(typeas T)` 泛型参数（1b）。
- 一个**没有** `.cmo` 的 `stdlib/<m>.cml` 是普通 Camel 模块，可以同时有 `native` 声明和普通 `export func`（`array`/`bench` 用这种混合形态）。
- 一个**有**同名 `.cmo` 的 `stdlib/<m>.cml` 是声明模块：顶层函数必须全是 `native`，普通函数报“把实现放到没有 `.cmo` 的模块里”；值仍由 C++ 导出。这样不会出现同名模块一半 C++ 一半 Camel。

**改什么。** 词法/语法（`native` 关键字、无体函数、格式化器打印）；CST/AST/GCT 的函数符号带 URI、无函数体；`export` 进模块导出表；`std::gir`/`std::jgir` 上调用是 OPER，类型来自声明，不是 CALL。

**不改什么。** `stdlib/signatures` 里尚未搬的模块；动态签名模块；`std::cpp` 的算子表（只按 URI 翻译，类型来自图）。

#### 1b 装载器、执行器绑定与泛型

**装载器。** `Context::getModulePathAndKind` 的查找顺序不变（`xx.cmo` 先于 `xx.*`，`context.cpp:547`）。`tryLoadModule` 找到 `.cmo` 后，若同目录有同名 `.cml`：

1. 解析为声明模块；
2. 对每个声明检查执行器注册表里有该 URI、`OperatorTable` 里有纯度声明；缺一个就是**装载错误**（带 URI/模块名），不是运行时故障。**参数个数与类型不再与 C++ 对账**：声明就是唯一来源，执行器是类型擦除的，读错参数是库的缺陷，由测试发现。删掉的只是 C++ 重复的类型/参数事实，不是执行器；
3. 用声明构造模块的导出组（名字、每个重载的声明类型、URI），C++ 模块不再导出 `OperatorGroup`。

**C++ 侧。** `BuiltinModule` 增加最小的执行器登记接口（URI → 函数指针，URI → traits）；`modules/<m>/module.cpp` 删除 `OperatorGroup::create`，保留 `registerExecutorFactory`、`OperatorTable::declare`、`declareResources`、`declareSemantics`、`setInPlaceForm`。执行器仍是类型擦除的 `operator_t`，一个 URI 一个函数。

**四个调度器。** 不变：它们按 OPER 的 `OperId`/URI 取执行器，运行时不需要类型；类型的唯一来源变成声明。`std::cpp` 也不变：它按 URI 翻译，图上的类型已经由声明确定；重命名时它的 URI 表跟着改（第 4/5/8 步各自带一个 `feat/trans/cpp` 测试）。`std::opt` 的常量折叠/内联仍查 `OperatorTable` 的纯度/资源/traits，不查声明。

**`inspect --ops` / `--verify-ops`。** `inspect` 输出每条声明（类型来自 `.cml`，纯度/资源/语义来自 `OperatorTable`）。`--verify-ops` 对已声明化的模块检查“声明集合 = 执行器 URI 集合”，`@pure`/`@effect` 与 traits 一致；参数个数与类型以声明为准，没有第二份可对；未声明化的模块仍按旧路检查。

**泛型。** 声明可用 `(typeas T)`；实例化按调用点的具体类型，URI 不变，执行器仍是一份（`Array*` 类型擦除）。`load_npy::<i64>` 这类需要 `T[..]` 的写法仍等 H17 第 2 项，不在 1b 里硬做。

**试点 `bytes`。** `stdlib/signatures/bytes.cml` 搬到 `stdlib/bytes.cml`，加 `native`；`modules/bytes` 删除导出组，保留执行器与 traits；`bytes.plan` 与 `registry` 绿；`--write` 生成参考。

**夹具。** `test/cases/compile/native/`：
- `generic.cml`：一个混合模块，`export native func at(xs: (typeas T)[], i: int): T = ':op/idx'` + 一个普通 `export func`；用 `int[]`/`string[]` 调用，`std::gir` 上是带具体类型的 OPER，运行时值正确。这是第 5 步 `array` 的预演。
- `missing.cml`：声明一个没有执行器的 URI，装载必须失败并指名 URI。
- `body.cml`：在一个有同名 `.cmo` 的声明文件里放普通 `export func`，装载必须失败并说明“把实现放到没有 `.cmo` 的模块里”。

**验收。**
- `npm run build` 与 `npm run debug`（Debug 的断言覆盖装载器）。
- `node scripts/test.js test/plans/feat/compile`（含新夹具）、`test/plans/feat/modules/bytes.plan.toml`、`test/plans/feat/modules/stdlib_registry.plan.toml`、`test/plans/smoke`。
- 改动前用 `camel inspect --ops test/cases/modules/stdlib/registry_all.cml` 存一份输出；改动后逐行 diff，`bytes` 的名字/参数个数/纯度/URI 必须不变。
- `node scripts/stdlib-reference.mjs --verify-doc --verify-ops`。

**回退。** 1a/1b 各一次提交；试点只动 `bytes`。失败就 revert，H19 的第 1 项改为问题 1 的退路，其余步骤照常。

### 第 2 步：删掉没有去处的公开面

**做什么。**

- `this`：删 `modules/this`、`stdlib/signatures/this.cml`、`test/cases/modules/std/this/basic.cml`、`std.plan.toml` 的 `this.basic`、`registry_all.cml` 的 `import this`、`registry` 的模块清单与纯度清单。
- `para::rendezvous`：新建 `modules/test_support`（C++，永远构建，不进参考），导出 `zen`、`rendezvous`；`para` 只留 `unordered_foreach`/`reduce`。四个 `test/cases/para/core/*` 用例改导入。
- 空 `io`：删 `io.basic`（它测的是前奏的 `print`/`println`，根本没导入 `io`）与 `registry_all` 的 `import io`；`modules/io` 的代码在第 4 步整体替换，这里不删目录（避免与第 4 步的 CMake 改动打架）。
- `profiler`：确认它仍只在 Debug、不在 `MODULES`；文档里标为工具。

**验收。** `node scripts/test.js test/plans/feat/modules/std.plan.toml`、`test/plans/feat/para`、`test/plans/feat/modules/stdlib_registry.plan.toml`；`registry_all` 的 `inspect --ops` 不再有 `this`/`para:rendezvous`，`para` 用例的 `inspect --ops` 有 `test_support:zen`/`rendezvous`；`git grep -n 'import this\|this::' -- test examples stdlib` 为空（历史文档除外）。

### 第 3 步：`perf` + `sim` → `bench`（条件于问题 1）

**做什么。**

- `sim` 的 C++ 模块改名 `bench_kernel`（目录可留 `modules/sim`，CMake target/`BuiltinModule` 名改），协议 `bench`，不再导出名字；执行器与 traits 按 `bench:pi_hits`、`bench:asian_call_price` 登记。
- 删 `stdlib/perf.cml`；新建 `stdlib/bench.cml`（Camel）：`timeit`/`perfms` 的代码搬过来，`pi_hits`/`asian_call_price` 用 `export native func` 绑定 `bench:` 的两个 URI（第 1 步的混合模块形态）。
- `examples/numeric/monte_carlo_pi.cml`、`examples/perf/fibs.cml`、`std.plan` 的 `sim.basic` 改 `bench`；`bench` 不进 `stdlib-reference.mjs` 的 `MODULES`，不进 `registry_all`。
- `perf.cml` 里 `module util` 的错名随文件删除消失。

**验收。** `bench.basic` 与原 `sim.basic` 的数值逐位相同（`pi_hits`/`asian_call_price` 固定种子），只有模块标题从 Sim 改成 Bench；`examples/perf/fibs` 与 `monte_carlo_pi` 的 golden 只改导入与标题；`inspect --ops` 不再有 `sim:` URI。若问题 1 的答案是“不做声明机制”，本步退化为：只把 `perf.cml` 的 `module util` 改成 `module perf`，`bench` 合并延后。

### 第 4 步：系统层改名与拆分

#### 4a 改名（一次提交）

- `modules/sys` → `modules/env`；`sys:get_env`/`sys:get_cwd` → `env:`；`stdlib/signatures/sys.cml` → `signatures/env.cml`（仍留在 `signatures/`，第 10 步再搬进 `stdlib/`）；用例 `test/cases/modules/std/sys/*` → `std/env/*`；`std.plan` 的 `sys.basic`/`sys.string_equality` → `env.*`。
- `modules/os` → `modules/process`：`sleep`/`whoami`/`cpu_count` 留 `process`；`os:sleep` 等 → `process:`。
- 终端算子（`set_terminal_raw_mode`/`has_input`/`get_char`/`get_chars`/`clear_input_buffer`）搬进 `modules/io`（替换空壳），URI `io:`；`os.basic` 只留 process 的三项，`io` 不再有运行用例（终端交互不进 CI），靠 `inspect --ops` 与 `resources` 覆盖。
- `exit`：它注册在 `globals.cpp`，URI 却是 `os:exit`，traits 由 `modules/os` 声明。改成 `:io/exit`，由内建执行器处理（`:` 前缀），traits/资源声明搬到内建的声明处；`io` 模块只注册 `io:` 协议，两者不冲突。`registry` 的 URI 清单更新。`sleep` 的全局版本删除（前奏不再有 `sleep`）；用例 `test/cases/consistency/faults/sleep_global.cml` 删除，`snake`/`siblings` 等改 `import { sleep } from process`。
- `crypto` → `hash`：目录/模块名/URI 前缀/签名文件/用例 `crypto.basic` → `hash.basic`；`crypto` 的 `sha256_rounds` 一并改。不扩 md5/hmac。

**验收。** `env.basic`、`process.basic`、`hash.basic`、`std.plan`、`registry` 绿；`inspect --ops` 里没有 `os:`/`sys:`/`crypto:`；示例（`files/*`、`ml/export_onnx`、`text/log_filter`、`numeric/random_walk` 等）只改导入；`feat/trans/cpp` 对改名后的 URI 有对应条目；`git grep` 无活引用。

#### 4b `get_env` 可空（单独提交）

- `env::get_env(name): string?`（缺变量 `null`，不再 `""`）；`get_cwd(): string!`（目录被删/不可读时是错误值）。
- 约 160 处调用点（115 个文件，含注释与签名）用脚本列出后逐类改：`int(get_env('N'))` → `int(get_env('N') ?? '')`（保留旧行为，测试里 N 总是设置）；`get_env('X') == ''` → `(get_env('X') ?? '') == ''`；确实把缺失当错误的用 `!!`。
- 新增 `env` 用例断言 `get_env('UNSET') == null`，并保留 `sys.string_equality` 的 `?? ''` 版本（它测的是字符串按内容比较，不是缺失语义）。

**验收。** `env.basic`/`env.string_equality`；`feat/consistency`、`limits`、`attempt` 等用 `get_env` 的计划；`examples` 四个文件；无“`string?` 直接参与算术/比较”的编译错误。

#### 4c `time::strptime`

并入第 7b 步（错误约定），不在 4a 里混。

### 第 5 步：`algo` 并入 `array`

**前提。** 第 1b 步的泛型声明可用。

**做什么。**

- `modules/algo` 只留 `sort`、`argsort`、`flatten`、`unique`、`merge_sorted_arrays` 的执行器；删 `insert_sort`、`quick_sort`、`merge_sort` 的注册与实现（演示用），模块改名 `array_kernel`，协议 `array`，不再导出名字。`setInPlaceForm("array:sort", "array:sort_in_place")` 保留。
- `stdlib/array.cml` 增加：
  ```camel
  export native func sort(xs: (typeas T)[]): T[] = 'array:sort'
  export native func argsort(keys: (typeas T)[]): int[] = 'array:argsort'
  export native func flatten(xss: (typeas T)[][]): T[] = 'array:flatten'
  export native func unique(xs: (typeas T)[]): T[] = 'array:unique'
  export native func merge_sorted_arrays(left: (typeas T)[], right: (typeas T)[]): T[] = 'array:merge_sorted_arrays'
  ```
  文件头导入 `array_kernel`（为了加载执行器工厂；模块没有导出，导入只为副作用）。已有的 `flatten`/`unique` Camel 包装删掉，改由上面的声明提供；`sort_by` 继续调用 `argsort`。若编译器拒绝空导出的导入，就让 `array_kernel` 导出一个内部 `kernel_version` 值，`array.cml` 用 `let _ = kernel_version` 触发加载；不改变公开面。
- 用户改用 `import { sort, ... } from array`：`examples/algorithms/{binary_search,merge_sort}`、`test/cases/modules/std/algo/basic.cml`（改名 `array/sort.cml`）、`test/cases/para/core/{concurrent_alloc,merge_sort_small}.cml`、`test/cases/consistency/new_values/sort_rebinding.cml`、`benchmarks/parallel/*`。
- `sort_rebinding` 里 `sort`/`merge_sort`/`insert_sort` 三个函数值改成 `array::sort` 与 `array::sort_by` 两个，保留“把函数值绑到变量再原地调用”的覆盖；`concurrent_alloc`/并行归并的叶子用 `array::sort`，合并用 `array::merge_sorted_arrays`，测试目的（并发分配/任务图）不变。
- `stdlib/signatures/algo.cml` 删除；`algo.reference` 改成 `array` 的排序参考：保留 `sort`/`merge_sorted_arrays`、原地重绑与 20 万元素压力，删掉 `quick_sort`/`insert_sort`/`merge_sort` 三个演示排序的循环与对照。

**验收。** `test/plans/feat/modules/std.plan.toml` 的排序项、`array_basic`/`array_large`、`feat/para`、`feat/consistency/new_values`、`examples.plan.toml` 的 `algorithms/*`；`std::gir` 上 `xs->sort` 是 `array:sort`，`xs = xs->sort` 是 `array:sort_in_place`；`inspect --ops` 无 `algo:`；排序耗时与改动前同量级（`feat/perf` 或 `bench` 手工记录，不作为红线）。

**风险。** 原地形式与“包装 CALL”退化；`array` 是第一个公开的混合模块。两个都有夹具/图断言。

### 第 6 步：`statistics` → `stats`，改写为 Camel

#### 6a 改名（C++，单独提交）

- 目录/模块/URI/签名/用例/示例：`statistics`→`stats`，`statistics:mean` 等→`stats:`；`statistics.basic`→`stats.basic`；`stats_quantiles.cml` 改导入；`examples/numeric/{monte_carlo_pi,random_walk}` 改导入。
- 只改名字，不改实现与输出。

**验收。** `stats.basic`/`stats_quantiles` 的 golden 除模块名/标题外不变；`inspect --ops` 无 `statistics:`。

#### 6b 改写为 Camel

- `stdlib/stats.cml`：`mean`/`stdev`/`variance`/`median`/`quantile` 用 `T[]` 泛型写；用 `reduce` 直接在 float 上累加（不先 `map` 出一个临时数组），`median` 复制成 `float[]` 后 `array::sort`，`quantile` 用 type-7 公式 `a*(1-f)+b*f`（避免 `b-a` 溢出）。保留：空数组 `mean/stdev/variance = 0.0`、`median/quantile = null`、NaN 传播、`quantile` 的 q 越界是 `InvalidArgument` 故障。
- 删除 `modules/statistics`（改名后的 `modules/stats`）与 `stats.cmo`；6b 新建 `stdlib/stats.cml`（此前它在 `signatures/`，6b 不经过声明化），没有 `.cmo` 后它自然就是普通 Camel 模块。
- 若 `float(x)` 在 `T` 是类型变量时不被接受，退路是四个具体类型重载共用私有 `*_float` 实现（文件仍是一个，文档仍是一份）；这是语言限制，不改变契约。

**验收。** `stats.basic`/`stats_quantiles` 的 golden **逐位不变**（6a 之后、6b 之前存一份）；`examples` 的两个数值示例；`inspect --ops` 里 `stats` 不再是 C++ 模块；`registry` 的模块种类更新。任何数值差异按 C++ 行为修 Camel，不改测试。

### 第 7 步：错误约定

#### 7a `fs`（单独提交）

- `read_text: string!`（`NotFound`/`PermissionDenied`/`IOError`/`EncodingError`；路径是目录仍用 `IOError`）；删 `try_read_text`，调用点改 `read_text`。
- `read_bytes`/`read_chunk: Bytes!`；`write_bytes`/`write_text`/`replace_file: void!`；`file_size: int!`；`exists`/`is_file`/`is_dir` 仍是 `bool`（读不到就是 false）。
- `mkdir`/`mkdirs` 保持 `bool`（已存在返回 false，失败故障）：H19 只把“读文件”改成错误值；把创建目录也改成 `bool!` 会让每个示例/测试多一次 `let _ = ...`，收益不抵改动。这是本步有意留下的不一致，写进 `H19.md`。
- 新增 `list_dir(path): string[]!`（按字典序列出名字，便于测试）；`remove_all(path): void!`（不跟随符号链接：链接本身删掉，不删目标）。
- `replace` → `replace_file`（与 `str::replace` 分开）；唯一用户 `test/cases/modules/bytes_values.cml` 改。
- 错误位置保留 `<fs::...>` 的操作数名字；不用 Camel 组合去包 `fs` 的读，避免位置变成内部原语。

**验收。** `fs` 的计划（`std.plan` 的 fs 项、`stdlib_faults` 的 fs 项、`unicode_paths`）、`bytes_values`、四个示例；`stdlib_faults` 里指向 `stdlib/*.cml` 行号的断言改成“诊断名 + 操作数名”，不再钉库源码行号。

#### 7b `time`

- `strptime: float!`（`TimeFormatFailed`）；`now`/`strftime` 不变。

#### 7c 错误种类目录（单独提交，纯文档/工具）

- 新 `scripts/error-kinds.mjs`：`--check`（代码里用到的种类与文档表一一对应，`@fails` 里写的也在表里）、`--write`（生成 `docs/error-kinds.md`）、`--verify-doc`。
- 源表 `docs/error-kinds.toml`：kind、含义、产生者（模块）、是否稳定（用户可匹配）、测试计划。扫描 `makeError("…")`、`throw error('…')`、`@fails error \`…\``；`RuntimeDiag` 的故障不在此表。
- 初版只收录现有种类，不重映射 errno；7a 的新 `T!` 函数用 `NotFound`/`PermissionDenied`/`IOError`/`EncodingError`，旧故障路径里不再被任何函数使用的 `FileOpenFailed`/`FileSystemError` 从目录里删掉，其余旧种类保留。彻底的 errno 分类留到后续任务（问题 7）。
- 在 `test/plans/feat/modules/stdlib_registry.plan.toml` 加一个 test（`case = scripts/error-kinds.mjs`，`args = ["--check"]`）和一个 `--verify-doc` test。

**验收。** `node scripts/error-kinds.mjs --check --verify-doc`；目录里每个 kind 都有含义与产生者；`docs/error-kinds.md` 是生成的。

### 第 8 步：去重与调用约定

- `Set`：删 `has(Set)`，只留 `contains(Set)`；`Map::has` 不变；`globals.cml` 与 C++ resolver/`--verify-ops` 的参数个数更新。
- `append`/`extend`：`value`/`other` 从 `<...>` 改成普通参数（`xs->append(v)`、`xs->extend(ys)`）；147 + 4 处调用点机械改；`concat` 已是普通参数。
- `re::test`：`test(pattern, text)` → `test(text)<pattern>`（数据在前，模式是配置）；7 处调用点（4 个文件）与签名更新。
- `str::bytes`/`from_bytes`：删除；`test/cases/modules/stdlib/str_bytes.cml` 删掉字节部分，留码点/长度/切片的断言（可改名 `str_unicode.cml`）；`bytes` 的用例已有 encode/decode 覆盖。
- `fs::replace` 已在 7a 改。
- `format<args>`：保留，约定页写明这是“格式串是接收者”的例外，不改。

**验收。** `--verify-ops`（参数个数变了）、`stdlib_values`、`stdlib_faults` 的 `from_bytes` 项删除或移到 `bytes`、示例（`word_frequency`/`csv_parse`/`table_report` 等用 `append`）；`git grep '->append<'`/`->extend<'` 为空。

### 第 9 步：资源名与 `eprintln`

- C++ `declareResources` 改成原则 3.3 的名字：`io:stdout`→`stdout`，`io:stdin`→`stdin`，`io:terminal`→`stdin`（raw mode 是 stdin 的写模式），`time:clock`→`clock`，`process:environment`→`env`，`process`→`process`（`exit`），`rand:generator`→`rng`，`fs` 不变；新增 `stderr`（原来没有）。
- 新前奏 `eprintln`（URI `:io/eprintln`）：写 `stderr`，效果与 `println` 同类；`print`/`println` 写 `stdout`，`input` 读 `stdin`。`std::cpp` 增加 `:io/eprintln` 的翻译（`fprintf(stderr, ...)`），`feat/trans/cpp` 覆盖。
- `resources.plan` 的期望表逐条改；`registry` 的纯度清单加 `:io/eprintln`；`docs/technical/18_sync_control_semantics.md` 等提到旧资源名的地方改。

**验收。** `test/plans/feat/modules/resources.plan.toml`；一个把 stdout/stderr 分开捕获的用例（`eprintln` 只出现在 stderr）；`feat/trans/cpp`；`inspect --ops` 显示新名字。

### 第 10 步：其余固定签名模块声明化

按这个顺序，一个模块一次提交：`str`、`math`、`fs`、`time`、`env`、`process`、`json`、`re`、`random`、`hash`、`io`。

每个模块：

1. `stdlib/signatures/<m>.cml` 搬到 `stdlib/<m>.cml`，每个函数加 `native`，URI 与重载逐个对齐（`math` 的 `min_i`/`min_f` 等逐个写；`stats` 那种四个重载共用一个 URI 的保持共用）。
2. `modules/<m>/module.cpp` 删除 `OperatorGroup::create`，保留执行器、traits、`declareResources`、`declareSemantics`、`setInPlaceForm`；`math` 的 `pi` 等值仍由 C++ 导出，声明文件不重复。
3. `stdlib-reference.mjs` 的 `MODULES` 把该模块的 `kind` 改成 `declaration`（或 `camel`）；`--write`。
4. 验收：该模块的计划 + `--verify-ops` + `inspect --ops` 前后 diff（名字/参数个数/纯度/URI 不变）+ `feat/trans/cpp`。

`globals`、`para`、`graph`、`rewrite`、`tensor`、`nn`、`onnx`、`tiles`、`autodiff`、`python`、`pyplot` 保持 resolver/`OpDef`；`stdlib/signatures/` 最后只剩其中原本就有签名文件的（`globals`、`para`、`graph`、`tensor`）与 H51 的 `tree`（如果作者接受，可把它们集中到 `signatures/computed/`，只是整理，不是必须）。`tree` 归 H51。

**验收（整步）。** `signatures/` 里不再有已声明化模块；`--verify-ops` 覆盖所有固定签名模块；`docs/stdlib-reference.md` 无遗漏；`registry_all` 的 `inspect --ops` 总数与基线只差第 2 节账本列出的删除项。

### 第 11 步：文档、示例与收尾

- `docs/stdlib-reference.md` 用 `--write` 重新生成；`scripts/stdlib-reference.mjs` 的说明与 `docs/technical/09_operator_registration.md:272`“签名文件不参与编译”改成新机制（声明参与编译；只读签名只剩计算签名模块）。
- `examples/README.md` 的旧名（`algo.sorted`、`statistics` 等）改；`examples` 的 24 个程序逐个跑；`test/plans/feat/examples.plan.toml` 的 golden 只按 3.1 的“行为变化”清单更新，逐行 review，不整体 `--update-golden`。
- `docs/tutorial`、`docs/technical/18`、`docs/technical/41` 等 grep 旧名并改；`docs/design/stdlib/README.cn.md` 的模块表如果与账本有出入，改设计稿或记差异，不在实现里悄悄偏离。
- `docs/handover/README.cn.md` 的 H19 行在本提交里已改成“计划已提交（reports/H19-plan.md，2026-10-22；未开始实现）”；实现完成后再改成“已完成”并写 `docs/handover/reports/H19.md`。
- 最终门禁：第 6 节全部命令。

---

## 4. 模块计划：哪些期望移动、哪些删除

| 计划/用例 | 动作 | 说明 |
|---|---|---|
| `std.plan.toml` 的 `io.basic` | **删除** | 它测的是前奏的 `print`/`println`，没导入 `io`；空壳没有可测面 |
| `std.plan.toml` 的 `this.basic` | **删除** | 彩蛋；`zen` 移到 `test_support`，只保留一个测试设施用例 |
| `std.plan.toml` 的 `sim.basic` | 移动/改名 `bench.basic` | 工具收拢；数值不变，只有标题改 |
| `std.plan.toml` 的 `statistics.basic` | 改名 `stats.basic` | 6a 改名；6b 后 golden 不变 |
| `std.plan.toml` 的 `sys.basic`/`sys.string_equality` | 改名 `env.*` | `string_equality` 的 `get_env` 结果加 `?? ''`，语义仍是“字符串按内容比较” |
| `std.plan.toml` 的 `os.basic` | 改名 `process.basic` | 只留 `sleep`/`whoami`/`cpu_count`；终端项删 |
| `std.plan.toml` 的 `algo.basic` | 移动/改名 `array/sort.cml` | 只测 `sort` 与 `merge_sorted_arrays`；两者都保留，改名即可 |
| `std.plan.toml` 的 `crypto.basic` | 改名 `hash.basic` | 只改名 |
| `std.plan.toml` 的 `algo.reference` | **改** | 删掉 `quick_sort`/`insert_sort`/`merge_sort` 三个演示排序的循环，保留 `sort`/`merge_sorted_arrays`、原地重绑与 20 万元素压力；计数与说明跟着改 |
| `stdlib_registry.plan.toml` | **改** | 模块清单（去 algo/os/sim/statistics/sys/this/io，加 stats/env/process/bench/test_support 按是否公开）、纯度清单、URI 清单、`--verify-ops` 的来源 |
| `resources.plan.toml` | **改** | 资源名与 `eprintln`；期望文本逐条改 |
| `stdlib_faults.plan.toml` | **改/删** | 指向 `stdlib/array.cml` 行号的 `expect_diagnostic_line` 全部删掉，改断言诊断名 + 操作数名；`from_bytes` 项删除（函数没了）；`fs` 的故障种类按 7a 改 |
| `stdlib_values.plan.toml` | **改** | `stats_quantiles`/`str_bytes`/`array_basic` 等的导入；golden 不变（`stats` 6b 逐位不变） |
| `bytes.plan.toml` | 改/加 | 承接 `str::bytes` 删掉后的 encode/decode 覆盖；声明化试点 |
| `tree.plan.toml` | **不动** | H51 拥有；H19 不改 `tree.cml`/`tree.cmo`/诊断文本 |
| `rules.plan.toml` | **不动** | H38/H65 拥有 |
| `random.plan.toml` | **不动** | 逐位序列是契约；H19 不改 `choice/sample/shuffle` |
| `definitions`/`operator_semantics` | 预期不动 | `math` 的语义、`tensor` 的定义图都不搬；若 `min/max` 按问题 3 搬，才改 |
| `feat/trans/cpp.plan.toml` | 改 | 改名后的 URI、`:io/eprintln` |
| `feat/compile/*` | 加 | 原生声明/泛型/混合模块/负例 |
| `feat/docs_links.plan.toml` | 加 | 约定页、错误种类页 |
| `error_kinds`（新 test） | 加 | `scripts/error-kinds.mjs --check`/`--verify-doc` |
| `examples.plan.toml` + golden | 改 | 所有改名/`T!` 示例的导入与输出；逐行 review |

**删除与更新的分界**：测试的是**旧排列**（空壳模块、彩蛋、演示排序、库源码行号、`str::bytes` 与 `bytes` 重复）就删；测试的是**行为**（字符串按内容比较、随机序列、排序结果、错误种类、示例输出）就更新到新名字/新返回值，断言不变。

---

## 5. 风险与缓解

| 风险 | 步 | 表现 | 缓解 |
|---|---|---|---|
| 声明与执行器不一致 | 1、10 | 调用走错执行器/装载即失败 | 装载时“每条声明一个执行器、每个执行器一条声明”的硬检查；试点 + 每模块 `--verify-ops` |
| `.cml` 声明被 `.cmo` 挡住 | 1 | 模块仍是旧实现，声明成死文件 | 装载器显式识别同目录同名 `.cml`；负例测试（声明一个不存在的 URI 必须失败，而不是静默） |
| 重载顺序/字面量进入变化 | 1、10 | 同一个调用选到另一个重载 | 保留 H24/H25 的 entering/struct/partial-application 用例；`inspect --ops` 基线 diff |
| `array::sort` 丢原地形式 | 5 | `xs = xs->sort` 变成复制/新数组 | 原生声明 + `array:sort_in_place`；`std::gir` 断言；排序基准 |
| `stats` 数值不逐位一致 | 6b | golden 差一位 | C++ 行为是规范；按同一顺序/同一公式写；6a 后先存 golden |
| `get_env` 迁移丢语义 | 4b | 缺失被当成空串/算术出错 | 按调用点分类改（`?? ''` 保留旧行为，`!!` 才是新语义）；`string_equality` 保留 |
| `fs` 错误种类/输出变化 | 7a | 示例 golden 变 | 行为变化清单；逐个 golden review；不改测试来迁就实现 |
| 与 H51/H38/H65 冲突 | 全程 | 同文件/同计划两方改 | 围栏 + 小提交 + rebase；`MODULES` 只有一行冲突 |
| 计划过大 | 全程 | 半迁移 | 3.1 的规则；每步独立绿；第 1 步失败即有退路 |

---

## 6. “做完”的度量

实现完成必须同时满足：

1. `npm run build` 成功；第 1 步另跑 `npm run debug`。
2. `node scripts/test.js test/plans/feat/modules` 全绿（含 `stdlib_registry`、`resources`、`stdlib_faults`、`stdlib_values`、`bytes`、`std`）。
3. `node scripts/test.js test/plans/feat/examples.plan.toml` 全绿：24 个示例 × `std::nvm`/`std::fvm`/`std::jit`/`std::prl`，golden 一致；golden 的每一处变化能在 `H19.md` 里找到原因。
4. `node scripts/test.js test/plans/smoke` 与 `test/plans/feat/consistency` 全绿（值、故障、`get_env`、`sleep` 等）。
5. `node scripts/test.js test/plans/feat/compile` 全绿（原生声明、泛型、混合模块、负例）。
6. `node scripts/stdlib-reference.mjs --verify-doc --verify-ops` 全绿；`docs/stdlib-reference.md` 由 `--write` 生成，没有手改。
7. `node scripts/check-std-includes.mjs` 全绿。
8. `node scripts/check-doc-links.mjs --all` 全绿。
9. `node scripts/diagnostics-catalog.mjs --check --verify-doc` 全绿（没被 H19 碰坏）。
10. `node scripts/error-kinds.mjs --check --verify-doc` 全绿。
11. **旧名门禁**：在 `src/`、`include/`、`modules/`、`stdlib/`、`test/cases/`、`test/plans/`、`examples/`、`benchmarks/` 里 `git grep` 不到 `import .*\b(algo|statistics|sys|os|this|sim|crypto)\b`、`algo::`、`statistics::`、`sys::`、`os::`、`crypto::`、`this::`、`sim::`、`str::bytes`、`str::from_bytes`、`append<`、`extend<` 的活引用（`docs/handover`/`docs/reviews`/`docs/design` 的历史文本除外）；`Set::has` 不再被调用由 `--verify-ops`/测试覆盖。结果记进 `H19.md`。
12. **模块清单**：第 2 节账本里每一行的“之后”状态在 `inspect --ops`/`docs/stdlib-reference.md` 里可见；`tensor_definitions` 仍不供用户导入；`tree`/`rules` 的状态与 H51/H38 一致。
13. 没有 `T[维度...]` 的改动；H17 第 2 项之后要复查的清单（1.4）写进 `H19.md`，不假装已经处理。
14. 最后写 `docs/handover/reports/H19.md`：做了什么、和本文的差异（逐条）、行为变化、留下的项（`H17-2` 的数组复查、`Map`/`Set` 模块化、`min/max` 前奏、`bench` 是否进参考、错误种类的 errno 统一、`tensor_definitions` 合并）。

---

## 7. 需要作者裁定

只列会改变计划的。每个都有推荐答案与影响。

### 问题 1：原生声明与绑定机制

**背景。** 设计要求 `.cml` 无体声明是唯一来源；今天语言没有这个构造，签名文件不编译，`.cmo` 还优先于同名 `.cml`。

**选项。**
(a) 加 `export native func ... = 'uri'`，并规定“同目录同名 `.cmo` 旁的 `.cml` 是声明模块”；固定签名模块逐个搬过去（推荐）。
(b) 保持签名文件只读，构建时由签名生成 C++ 注册/校验（不改编译器；“一个事实”变成构建时对账，声明仍不是类型来源）。
(c) 两处都留，加一致性检查（没有改进）。

**推荐 (a)**，范围限固定签名；计算签名仍用 `OpDef`/resolver（问题 2）。试点 `bytes`；1b 泛型；通过后再做 `array`/`bench`。

**影响。** (a) 决定第 1、3、5、10 步；(b) 会把 H19 缩成改名/错误/文档，第 1 项留给你们另立语言任务。**这是唯一一个“不做就无法满足 H19 第 1 项”的问题。**

### 问题 2：计算签名模块的“一处”

**背景。** `tensor`/`nn`/`onnx`/`graph`/`rewrite`/`para` 的类型由实参算（形状、dtype、闭包），固定声明表达不了；搬过去要在编译器里重写推断。

**选项。**
(a) 固定签名用声明；计算签名保留 `OpDef`/resolver 作为唯一来源，`inspect --ops`/参考由它生成（推荐）。
(b) 全部改成声明，删 resolver（大语言工程，丢形状推断）。
(c) 两处都写，只做检查。

**推荐 (a)**；约定页写“一种事实一个来源”，不是“所有算子一个文件”。

**影响。** 第 10 步的清单与 `--verify-ops` 的边界；(b) 会让 H19 超出迁移。

### 问题 3：`min`/`max`（以及 `abs`）是否搬进前奏

**背景。** 设计 §3 的前奏列了 `min`/`max`/`abs`；今天 `math::min/max` 是二元 IEEE 极值（有数值契约），`array::min/max` 是数组归约（`T?`），`abs` 只在 `math`，51 个文件在用。H19 正文把 `min/max/sum` 列进“去重”。

**选项。**
(a) 保持现状；约定页写清“二元/数组/轴”三种运算，去重只做真正的重复项（推荐）。
(b) 把二元 `min/max` 搬进前奏（删 `math::min/max`），`array::min/max` 也并进前奏重载；`abs` 暂不动（若也搬，51 个文件）。
(c) 只把 `array::min/max` 删掉，让用户用 `reduce`（会让空数组/NaN 语义变难写）。

**推荐 (a)**；如果要实现前奏清单，另立“前奏表面”任务，因为它会改数值契约的归属与大量调用点。

**影响。** 第 8 步与示例；(b) 会加一步并改 `operator_semantics`。

### 问题 4：`Map`/`Set` 是否搬出前奏

**背景。** 设计列了 `map`/`set` 模块，但 `map` 是前奏函数；按原则 10.2，导入的模块名优先，`import map` 会遮蔽 `map(f, xs)`。搬走 `get/put/has/...` 还要前奏支持 Camel（今天没有），并改约 100 个文件。

**选项。**
(a) H19 不搬；只删 `Set::has`（留 `contains`），其余保持前奏 C++（推荐）。
(b) 模块改名 `collections`，把查询函数搬进去（大 break）。
(c) 先做“Camel 前奏”机制，再搬（语言任务）。

**推荐 (a)**；`map`/`set` 的模块化与前奏缩小是一个独立的表面设计。

**影响。** 第 8 步的范围；(b)/(c) 会显著加长 H19。

### 问题 5：`io` 在没有 `Stream` 时做什么

**背景。** 空壳要删；终端算子在 `os`；设计的 `io` 以 `Stream` 为核心，流不在本项。

**选项。**
(a) 第 4 步用终端/控制台算子重建 `io`（raw mode、`has_input`、`get_char`、`get_chars`、`clear_input_buffer`），`eprintln` 进前奏；`Stream` 以后（推荐）。
(b) 终端算子留在 `process`，`io` 等流任务再建（改动小，但 `process` 名不副实）。
(c) 等流任务，`io` 先空着（不行，空壳就是问题）。

**推荐 (a)**；`io.basic` 不重建（终端交互不进 CI），靠 `inspect --ops`/`resources` 覆盖。

**影响。** 第 4 步的文件与测试；不影响其它步。

### 问题 6：`bench` 是否在 H19 合并

**背景。** `perf` 的模块名是 `util`（错），`sim` 是独立工具；设计叫 `bench`。

**选项。**
(a) 第 3 步合并：`bench_kernel`（C++，协议 `bench`）+ `bench.cml`（Camel 计时 + 原生 `pi_hits`/`asian_call_price`），不进公开参考（推荐，条件于问题 1）。
(b) 只把 `module util` 改成 `module perf`，`bench` 合并留给工具任务（改动最小）。
(c) 把 `perf`/`sim` 移到 `benchmarks/` 目录（会破坏 `examples/perf` 与 `examples/numeric/monte_carlo_pi` 的导入和计划入口）。

**推荐 (a)**；如果问题 1 选 (b)，自动退到 (b)。

**影响。** 第 3 步；不影响公开库的核心。

### 问题 7：`fs` 的错误种类是否在 H19 统一

**背景。** 现在 `FileOpenFailed`/`FileSystemError`/`RuntimeError` 混用；错误种类目录要建；把 errno 映射成 `NotFound`/`PermissionDenied` 是另一层工作。

**选项。**
(a) 目录冻结 7a 之后仍在用的种类；7a 的新 `T!` 函数用 `NotFound`/`PermissionDenied`/`IOError`/`EncodingError`；errno 的彻底分类以后再统一（推荐）。
(b) H19 一次统一 `fs` 的种类并改所有测试（更干净，风险更大）。

**推荐 (a)**；目录让债务可见，不挡迁移。

**影响。** 第 7 步的大小与 golden 变化。

---

## 8. 提交序列（建议）

每个提交都构建、相关计划绿；消息用英文，`git commit -F`。顺序即第 3 节。

1. `docs: add the standard library conventions page`
2. `feat(lang): declare native functions without a body`
3. `feat(module): read a module's declarations from the .cml beside its .cmo`
4. `refactor(modules): bind bytes to its declarations`
5. `test(compile): cover generic native declarations and mixed modules`
6. `refactor(stdlib): remove this and para::rendezvous from the public surface and drop the empty io test`
7. `refactor(stdlib): merge perf and sim into bench`（条件于问题 1/6）
8. `refactor(stdlib): rename sys to env and os to process`
9. `refactor(stdlib): give the terminal its own io module and remove the global sleep`
10. `refactor(stdlib): make get_env nullable`
11. `refactor(stdlib): merge algo into array`
12. `refactor(stdlib): rename statistics to stats`
13. `refactor(stdlib): write stats in Camel`
14. `refactor(fs): return error values from the file operations`
15. `feat(fs): add list_dir and remove_all`
15b. `refactor(time): return an error value from strptime`
16. `feat(tools): add the error kind catalog`
17. `refactor(stdlib): give each operation one home`
18. `refactor(stdlib): name resources after what they are`
19. `feat(stdlib): add eprintln`
20. `refactor(modules): declare str's operators in Camel`
21. `refactor(modules): declare math's operators in Camel`
22. `refactor(modules): declare fs's operators in Camel`
23. `refactor(modules): declare time's operators in Camel`
24. `refactor(modules): declare env's and process's operators in Camel`
25. `refactor(modules): declare json's operators in Camel`
26. `refactor(modules): declare re's and random's operators in Camel`
27. `refactor(modules): declare hash's and io's operators in Camel`
28. `docs: regenerate the standard library reference and update the examples`
29. `docs: report H19`

`crypto`→`hash` 的改名并入第 8 条（系统/基础改名同一提交）或单独一条，两者都可以；不要与第 28 条的声明化混在一起。

---

## 9. 文件与符号索引（实现时按这个找）

- 装载器：`src/core/context/context.cpp:538`（`getModulePathAndKind`，`.cmo` 优先）、`:687`（`tryLoadModule`）、`:696`（`loadCmoModule`）、`:717`（`UserDefinedModule::fromFile`）。
- 模块注册：`include/camel/core/module/module.h`、`include/camel/core/operator.h`（`OperatorGroup`/resolver）、`include/camel/core/operator_table.h`（traits/资源/语义）、`modules/camel_module.cmake`。
- 内建：`src/builtin/modules/globals.cpp`（前奏、HOF、`exit`）、`src/builtin/operators/higher_order.cpp`（四个调度器的 `map_arr` 等）、`src/builtin/operators/builtin_semantics.cpp`。
- 签名与参考：`stdlib/signatures/*.cml`、`scripts/stdlib-reference.mjs`、`docs/stdlib-reference.md`、`scripts/collect-out.js:273`（拷贝 `stdlib` 与 `.cmo`）。
- 文档：`docs/design/stdlib/README.cn.md`、`docs/design/stdlib/inventory.cn.md`、`docs/technical/09_operator_registration.md:272`、`docs/technical/41_resources_and_definitions.md`、`docs/technical/18_sync_control_semantics.md`。
- 计划：`test/plans/feat/modules/{std,stdlib_registry,resources,stdlib_faults,stdlib_values,bytes,random,tree,rules}.plan.toml`、`test/plans/feat/examples.plan.toml`、`test/plans/feat/trans/cpp.plan.toml`、`test/plans/feat/compile/*`。
- 用例：`test/cases/modules/std/{algo,crypto,fs,io,json,math,os,re,sim,statistics,sys,this,time}`、`test/cases/modules/stdlib/*`。
- 示例：`examples/{algorithms,cli,data,files,functions,ml,numeric,parallel,perf,text,tty}`。
- 错误：`include/camel/core/error/*`、`modules/fs/operators.cpp:125`（`makeError`）、`stdlib/signatures/globals.cml:220`（`error`）、`docs/diagnostics.md`/`scripts/diagnostics-catalog.mjs`（目录的样板）。

---

## 10. 复核清单（给 reviewer）

- [ ] 第 1 步的装载规则是否区分了“有 `.cmo` 的声明模块”和“无 `.cmo` 的普通/混合模块”；有没有可能出现声明被静默忽略？
- [ ] `inspect --ops` 的每一条是否能追到一个执行器和一个 trait；有没有两个重载共用一个 URI 时被去重？
- [ ] `array::sort` 的原地形式是否仍在图上；`sort_by` 是否仍只承诺 rank 1？
- [ ] `stats` 的 6b 前后 golden 是否逐位相同；空数组/NaN/大整数/quantile 边界是否覆盖？
- [ ] `fs` 的每个失败路径是否都有 `T!`（`mkdir`/`mkdirs` 是记录在案的例外），有没有一条仍走故障；`remove_all` 是否确实不跟随符号链接？
- [ ] `get_env` 的约 160 处是否逐处有 `??`/`!!`；有没有把缺失当空串的新语义悄悄写进断言？
- [ ] `resources.plan` 的每个 URI 是否与新名字一致；`eprintln` 是否只写 stderr？
- [ ] `tree`/`rules`/tensor 栈是否真的没被 H19 编辑；`MODULES` 的冲突是否只有一行？
- [ ] 旧名门禁是否只漏了历史文档；`docs/stdlib-reference.md` 是否由 `--write` 生成？
- [ ] H17-2 的复查清单是否写进 `H19.md`，而不是在代码里留 TODO？

---

## 11. 附：本计划不做的事

- 网络、HTTP、流、日志、压缩、CSV、Arrow、数据加载、优化器（H19 正文第 4 条）。
- `tensor_definitions` 并入 `tensor`（等 H17-2/动态声明）。
- `tensor`/`nn`/`onnx`/`tiles`/`autodiff`/`graph`/`rewrite`/`para`/`globals` 的声明化。
- `Map`/`Set` 搬出前奏；`min`/`max`/`abs` 搬进前奏（除非问题 3/4 另定）。
- `random` 的 `choice`/`sample`/`shuffle` 改写（序列是契约）。
- 完整 `re`（match/find/replace/groups）、完整 `io`（`Stream`/stdin 值）、`hash` 的 md5/hmac。
- 错误种类的 errno 全面统一（只建目录 + 新函数用约定名）。
- 扩展库与 `showcase/`。