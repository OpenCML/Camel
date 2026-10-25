# 教程集中改写：章节提纲（交决策者审批，批了再写正文）

## 问题

教程（[tutorial.cn.md](../../tutorial/tutorial.cn.md)、[tutorial.en.md](../../tutorial/tutorial.en.md) 与它旁边的
`docs/tutorial/examples/`）是新读者见到 Camel 的第一份文档。它今天停在早期的语言上：例子里的注释还说
"尾递归就是 Camel 写循环的方式"、没有多维数组与维度名、没有 `===`、没有规则与改写、从来没有用 Camel 写过
一个遍，第 15 节的张量仍写 `import { Tensor }`；而 Camel 最不同的"按依赖求值"被放在第 6 节硬讲，
招牌（规则、遍、图）压在最后几节点到为止。逐句修补不够，要整体改写。**本页只交提纲**：新的章节表、
贯穿的主线例子、依赖张量收尾的章节、写作纪律、落地顺序与代价。正文与例子一个都不动。

---

## 一、新的章节表

四部分、正文二十章，加序章与结语。每章一行：**教什么 · 用哪个例子 · 读者读完能做什么 · 这一章回答"这段代码
在图上是什么"的哪一面**。例子里 `待写` 表示今天不存在，改写时新建；"今天有"表示
`docs/tutorial/examples/` 下已有一个同主题文件，按新章号改名并用现在的语言改写。

### 序章

| # | 章标题 | 教什么 | 例子 | 读者读完能做什么 | 图上哪一面 |
|---|---|---|---|---|---|
| 1 | 五分钟见到图 / Five minutes to see the graph | 写三行函数、`camel f.cml std::gir` 看它的图；再写 `x * 2 + x * 2`，加上 `std::opt` 看它被改写成一份乘法（以 `std::opt` 今天真能做的改写为准，换一个同样一眼能看懂的例子也行） | `01_hello.cml`（今天有）、`01_rewrite.cml`（待写） | 会从仓库根跑一个 `.cml`，会打印一张图，并在第一屏就看到"编译器改了程序而结果不变" | 钩子：**函数是一张子图，调用是一个节点；改写让图变小、结果不变** |

### 第一部分 写程序 / Part 1: Writing programs

贯穿例子：**词频统计命令行小工具**（`examples/wordcount/stepN.cml`，待写），第 8 章写完。

| # | 章标题 | 教什么 | 例子 | 读者读完能做什么 | 图上哪一面 |
|---|---|---|---|---|---|
| 2 | 值、类型与 `let` / Values, types and `let` | 字面量、基本类型、不隐式数值转换、字符串与格式化、元组与结构体字面量；`let` 不可重绑定、`var` 与给字段/元素赋值；类型标注与推断 | `02_values.cml`（今天有）、`wordcount/step2.cml`（待写：读一行、`split`、计数） | 会声明不可变的值、会用类型标注解释推断结果、知道什么时候才需要 `var` | 值在图上就是节点（常量、形参端口）；`let` 只是给一个节点起名字，没有"变量槽" |
| 3 | 函数与管道 / Functions and pipes | `func`、返回类型与 `return`、`->` 管道、lambda；`with` 参数只露一面（"配置"，第 11 章展开） | `03_functions.cml`（今天有：**删掉"尾递归就是 Camel 写循环的方式"，先给一次 `loop`**）、`wordcount/step3.cml`（待写：切词与计数的函数化） | 会把一段计算拆成函数并接成管道 | 调用是图上的一个节点，`->` 是图上的一条数据边：数据流写成一条链，就是图上的一条路径 |
| 4 | 分支与 `match` / Branches and `match` | `if` 是表达式、`match` 匹配名字、模式与 guard、`case _` 的作用；块里 `return` 的坑（用编译器原话与改法讲） | `04_branches.cml`、`04_block_return.cml`（今天有）、`wordcount/step4.cml`（待写） | 会用表达式式的分支写完一个函数体，不再写"提前返回" | 分支是它自己的子图；臂的块里写的 `return` 给的是**臂**的值 |
| 5 | 数组与循环 / Arrays and loops | 数组与区间 `a..b`、`loop … for … do`、`while`、`map`/`filter`/`reduce`/`foreach`、推导式；什么时候用循环、什么时候用高阶算子；元素之间没有顺序（用 `std::prl` 演示） | `05_arrays.cml`、`05_map_order.cml`（今天有，补 `loop`）、`wordcount/step5.cml`（待写：把词表算出来） | 会用 `loop` 写下带状态的重复计算，并知道哪些重复计算该交给高阶算子 | `loop` 是图上带状态回边的节点；高阶算子的元素之间没有顺序边，所以能并行 |
| 6 | 结构体、元组、映射与集合 / Structs, tuples, maps and sets | 给数据建模：结构体、元组、`Map<K, V>`、`Set<T>`、`as` 转换；`==` 按内容整体比较 | `11_collections.cml`（今天有）、`06_records.cml`（待写：`==` 与结构体）、`wordcount/step6.cml`（待写：`Map` 计数、`sort_by` 取前几名） | 会给一份数据选结构体还是 `Map`/`Set`，并知道整体比较的代价 | 结构体是若干节点按字段打包，字段访问是一个 `ACCS`；`Map` 与 `Set` 是值，不是"容器对象" |
| 7 | 缺失与失败 / Null and failure | `T?` 与 `T!`、`match` 拆开、`??`/`!!`/`?->`；**故障与错误值的区别**（除零、越界、递归过深带位置终止） | `10_nullable.cml`、`09_errors.cml`、`09_fault.cml`（今天有）、`wordcount/step7.cml`（待写：文件读不到、行数不对） | 会把"可能没有"和"可能失败"写进类型里，并分清它和故障 | 失败的值是一张有两条出边的节点；`match` 把它拆开就是一个分支 |
| 8 | 模块、标准库与一个完整的小工具 / Modules, the standard library and a complete little tool | `module`、`import`（含 `{ ... } from` 与 `::`）、`export`、`--arg` 入口参数、文件与 `json`；标准库怎么查 | `shapes.cml`、`12_modules.cml`、`12_stdlib.cml`（今天有）、`wordcount/step8.cml`（待写：成品；行为与今天被计划跑着的 [word_frequency.cml](../../../examples/text/word_frequency.cml) 对齐） | 能把前七章的东西合成一个能跑的命令行工具，会自己查标准库参考 | `import` 不改变图；库算子就是普通节点（`OPER`），不因为是库就特殊 |

### 第二部分 Camel 的想法 / Part 2: Camel's ideas

读者已经有手感，这里才讲最不同的地方：先给直觉，再给规则。

| # | 章标题 | 教什么 | 例子 | 读者读完能做什么 | 图上哪一面 |
|---|---|---|---|---|---|
| 9 | 按依赖求值 / Evaluation by dependency | 没人用的结果不算、互不依赖的东西可以并行；用 `std::gir` 看依赖边，解释为什么；与"一条接一条执行"的差别 | `09_dependency.cml`（待写：同一段程序加/不加依赖）、`13_graph.cml`（今天有）、[parallel_map.cml](../../../examples/parallel/parallel_map.cml)（今天有） | 能指着图说出"这两件事为什么可以同时做"，也能说出"我少写了一个依赖" | **依赖边就是一切**：程序是"谁要谁的结果"，没有人要的节点不在可达部分里 |
| 10 | 效应与顺序 / Effects and order | 同步块与 `sync` 函数体、同步块不向内继承、`var` 与写入的顺序、"读用到最近一次写"、函数的行为由它自己决定；常见错误与诊断原话 | `06_unused_effect.cml`、`06_sync_block.cml`、`06_sync_body.cml`、`06_callee_decides.cml`、`06_var_order.cml`、`06_let_to_var.cml`（今天有，整组搬到本章） | 能判定一段代码"该不该加 `sync`"，能看懂 `EffectNotExecuted` 与 `IgnoredSideEffect` | `sync` 是控制边；`var` 的读写在图上被排成先后；读到的永远是最近一次写 |
| 11 | 函数是值 / Functions are values | 闭包与捕获、函数值、重载名什么时候能当值用、`with` 参数（上下文）与偏应用；为"遍就是函数"埋伏笔 | `07_closures.cml`（今天有）、`11_with_params.cml`（待写：把配置从对象成员搬到 `with`） | 会把函数当数据传、会用 `with` 参数给函数配上下文 | 闭包是图上带捕获的图引用；`with` 实参是一条 `WITH` 边，第 16 章的遍正是"把图当值的函数" |
| 12 | 泛型 / Generics | `::<T>`、`when` 条件、类型函数；进阶小节：**按结构展开**（`match type`、`static for`、`static loop`），用"把结构体的每个字段都加一"引出 | `08_generics.cml`（今天有）、`12_structural.cml`（待写） | 会写泛型辅助函数，会写一个按字段展开的泛型（第 15 章的参数树就靠它） | 泛型函数是一张**子图模板**，每用一组具体类型实例化一张；`match type` 与 `static for`/`static loop` 在编译期展开，展开后图上没有循环节点 |

（流 `T..`、`yield`、`tee` 落地后在本部分加一章；资源推断落地后加进第 10 章。）

### 第三部分 数值与模型 / Part 3: Numbers and models

贯穿例子：**拟合一条直线**（`examples/fit/stepN.cml`，待写）。本章与第 14、15、19 章的正文按"张量并进数组"
收尾后的写法写，见第三节。

| # | 章标题 | 教什么 | 例子 | 读者读完能做什么 | 图上哪一面 |
|---|---|---|---|---|---|
| 13 | 多维数组与形状 / Multi-dimensional arrays and shapes | `f32[n, k]` 与维度名、逐元素运算与广播、矩阵乘 `@`、下标与切片取行、`shape`；**`===` 与 `==`**（逐元素 vs 整体）；形状写错时编译器怎么说 | `13_shapes.cml`（待写）、`15_tensor.cml`（今天有，按新写法改写）、`fit/step13.cml`（待写：把数据读成 `f32[n, 2]`） | 会用形状写类型、会说出两个形状不同的数组相减时编译器会报什么 | 形状是类型的一部分，数组节点带它；`===` 给一个同形状的 `bool` 数组，`==` 给一个 `bool` |
| 14 | 求梯度 / Taking gradients | `grad` 把函数的图改写成导数的图、`with_grad`、第一个 `with` 参数是权重；用 `std::gir` 看一眼导数图；用 `loop` 写梯度下降把直线拟合出来 | `16_grad.cml`（今天有，改写）、`14_grad_graph.cml`（待写）、`fit/step14.cml`（待写） | 会求一个函数的梯度，并把它接进一个 `loop` 里的下降 | **`grad` 是程序变换**：它把一张图改写成另一张图，改完仍然只是普通的图 |
| 15 | 训练一个小模型 / Training a small model | 参数写成结构体、`tree_map`、`nn` 的层与优化器；从直线走到一个两层网络 | [train_linear.cml](../../../examples/ml/train_linear.cml)（今天有，被计划跑着）、`fit/step15.cml`（待写） | 会写一个训练循环，会说出参数树在图上展开成什么 | 参数树按字段展开成直线代码；一次训练步就是"梯度 + 更新"两张图接起来 |

### 第四部分 变换程序 / Part 4: Transforming programs

招牌。同一个模型一路用到底。

| # | 章标题 | 教什么 | 例子 | 读者读完能做什么 | 图上哪一面 |
|---|---|---|---|---|---|
| 16 | 看图与换遍 / Looking at the graph and changing passes | 遍的流水线、`std::gir`/`std::rgir`/`std::stats`、`std::opt`、换后端（`std::nvm`/`std::fvm`/`std::jit`/`std::prl`）、`std::profile` 看时间与分配 | `14_passes.cml`（今天有）、`16_profile.cml`（待写）、`fit/step16.cml`（待写） | 会为一件事挑一组遍，会用 `std::stats` 说出前一节和后一节差在哪 | 同一张图，换一组遍就是另一张图；后端是图的执行者，不是语言的一部分 |
| 17 | 教编译器一条代数律 / Teaching the compiler an algebraic law | 写一条 `rule`；`std::opt` 在模型上用上它；`rewrite::check` 验证它；`std::opt::saturate` 看等价图（一个值有几种写法同时存在） | `17_rule.cml`（待写）、`fit/step17.cml`（待写：把模型的某处交给规则改写） | 会为自己领域的代数律写一条规则，并证明它在模型上真的被用上 | **规则是图的局部改写**；`saturate` 之后图上同一值有多种写法（一个虚线簇），抽取只取其中一种 |
| 18 | 用 Camel 写一个遍 / Writing a pass in Camel | 用 `graph` 模块把图当值：数一数、删掉一类节点、改一处；挂进流水线，用在模型上 | `18_pass.cml`（待写）、`fit/step18.cml`（待写） | 会写一个自己的遍，并把它接进流水线 | **遍就是函数**：输入一张图，输出一张图，语言里没有别的魔法 |
| 19 | 导出与嵌入 / Exporting and embedding | 导出 ONNX、导出 C++；从 Python 里调用 Camel 模块（数组零拷贝） | [export_onnx.cml](../../../examples/ml/export_onnx.cml)（今天有，被计划跑着）、[rules.cml](../../../examples/python/rules/rules.cml)（今天有，但**没有任何计划跑到它**，见第六节）、`fit/step19.cml`（待写） | 会把训练好的模型导出，会从别的语言里调用它 | 导出就是**把图翻译成另一种形式**；零拷贝是同一块内存在两边都能看见 |
| 20 | 上 GPU / Going to the GPU | `#gpu` 标签与放置：同一份程序、同一张图，节点带上"在哪台设备上"的事实 | 待写 | 会为一段计算指定设备，并知道主机与设备之间什么时候发生搬运 | 放置是图上节点的**事实**，图还是那张图；搬运是显式的节点，不是隐藏的转换 |

**这一章只在标签与放置机制落地后写**；落地前不写、不占位。

### 结语 / Closing

接下来读什么：语言参考、标准库参考、示例集、命令行、设计原则。

---

## 二、一条贯穿的主线例子

设计成一条线：**数据处理 → 训练 → 用规则优化 → 写一个遍 → 导出**。每一步都是 `examples/` 里的一个文件，
每一步的输出都由计划逐字核对，读者看着同一段程序被一层层变换。

| 步骤 | 文件（`待写` = 今天不存在） | 补到哪一步 |
|---|---|---|
| 第 2 步 | `examples/wordcount/step2.cml` | 读输入、`split` 切成词 |
| 第 3 步 | `examples/wordcount/step3.cml` | 切词与计数各自成函数，用 `->` 接起来 |
| 第 4 步 | `examples/wordcount/step4.cml` | 用 `match` 给行分类 |
| 第 5 步 | `examples/wordcount/step5.cml` | 用 `loop` 与高阶算子算出词表 |
| 第 6 步 | `examples/wordcount/step6.cml` | `Map` 计数、`sort_by` 取前几名 |
| 第 7 步 | `examples/wordcount/step7.cml` | 文件读不到、行数不对成为 `T!`/`T?` |
| 第 8 步 | `examples/wordcount/step8.cml` | **成品**：`--arg`、`json`、模块划分。行为与今天被计划跑着的 [word_frequency.cml](../../../examples/text/word_frequency.cml) 对齐 |
| 第 13 步 | `examples/fit/step13.cml` | 数据读成 `f32[n, 2]`，写下形状 |
| 第 14 步 | `examples/fit/step14.cml` | `grad` 求出梯度，用 `loop` 做梯度下降，把直线拟合出来 |
| 第 15 步 | `examples/fit/step15.cml` | 参数写成结构体、`tree_map`、`nn` 的层与优化器，走到两层网络 |
| 第 16 步 | `examples/fit/step16.cml` | 同一份程序换一组遍，用 `std::profile` 看时间与分配 |
| 第 17 步 | `examples/fit/step17.cml` | 写一条 `rule`，用 `std::opt::saturate` 看它在模型上用上了 |
| 第 18 步 | `examples/fit/step18.cml` | 自己写的遍挂进流水线，用在模型上 |
| 第 19 步 | `examples/fit/step19.cml` | 导出 ONNX，并从 Python 调用它 |

第二部分（第 9–12 章）不在这条线上：它们讲的是 Camel 的想法本身，各章用各自的小例子。第二部分的结尾把
第 11 章的"函数是值"指向第 16–18 章，让读者在进第三部分之前就知道这条路通向哪里。

---

## 三、依赖"张量并进数组"收尾的章节

**第 13、14、15 章与第 19 章**（张量、求梯度、导出）的正文，按那一步收尾之后的写法写：类型写成 `f32[n, k]`
与 `f32[..]`，**不再 `import { Tensor }`**。这一步合入之后再补齐这四章的正文。

今天 `Tensor` 仍是 [stdlib/tensor.cml](../../../stdlib/tensor.cml) 里的一个类型名，示例与用例仍写 `import { Tensor }`——现在写这四章，
收尾之后要再写一遍。数组的形状语法 `f32[n, k]` 与维度绑定**已经**在树上（语言参考的数组一行已经写
`T[]`/`T[4]`/`T[n]`/`T[..]`），所以第 13 章的形状部分是今天就能写的；但为了不写两遍，仍按上面这条一起等。

其余各章现在就写。

---

## 四、写作纪律

1. **只写现在的语言**，不写"以前怎样"，不写"暂不支持"之外的历史。未落地的特性不写进正文（流、资源推断、
   GPU 各自在落地时补章）。
2. **每个例子是 `examples/` 里的一个文件**：章例子 = `docs/tutorial/examples/NN_slug.cml`（页内相对路径写
   `examples/NN_slug.cml`，与今天一致）；主线每一步 = 仓库根 `examples/wordcount/stepN.cml`、
   `examples/fit/stepN.cml`。**每个例子都在计划里跑、输出逐字一致**：今天的机制是
   [docs_links.plan.toml](../../../test/plans/feat/docs_links.plan.toml) 里的片段检查（`cml` 代码块后面紧跟
   的 `text` 块会被真跑并逐字比较）；章例子文件本身另加一个快速计划，按四个后端跑并录 golden。
3. **不出现假的输出**：图（DOT）与测量结果的块要按计划记下来的文本写，并在页上写明它是哪条命令的输出；
   节选要加代码块标记写清理由。
4. **中英文两版同步，同一次提交改。** 两版的节号、标题、例子、命令、输出一一对应；只改一版不算改完。
5. **每章末尾一个小练习**（附参考答案文件），读者与 AI 读者共用；练习的期望输出同样由计划核对。
6. **精确规则交给语言参考**：教程给直觉和最常用的形式，每章末尾链接语言参考的对应节，不复述全部规则。
7. **错误是教材**：常见错误写成"这样写会看到哪条诊断、怎么改"，用编译器的原话（诊断带改法的，展示改法）。
8. **本页与教程正文无关的页面不动**：`docs/design-principles*`、`docs/design/**`、`docs/handover/reviews/**`、
   `docs/language/**` 都不在这条车道里。

---

## 五、落地顺序建议

1. **样章**：第 1、5、9、17 章（钩子、基本功、最不同的想法、招牌各一章），交决策者看文风和节奏。
2. 样章批了之后写不依赖"张量并进数组"收尾的其余各章：2、3、4、6、7、8、10、11、12、16、18。
3. **那一步合入之后**写第 13、14、15、19 章，按 `f32[..]` 的写法。
4. 第 20 章在标签与放置机制落地后写。
5. 每写完一章就补它的例子、计划项与 golden；两版同一次提交。

---

## 六、代价与风险

- **四章的正文要等**：13、14、15、19 章写不了现在；等待期间它们的例子文件可以先设计、不能先录输出。
- **新例子是净增量**：章例子约 30 个（今天 27 个，多数要按新章号改名并用现在的语言重写），主线 14 步。
  每个都要在四个后端上跑并录 golden，这是新的计划与新的 golden 数据集。
- **主线例子今天不存在**：`examples/wordcount/` 与 `examples/fit/` 都要新建；成品可与
  [word_frequency.cml](../../../examples/text/word_frequency.cml)、
  [train_linear.cml](../../../examples/ml/train_linear.cml) 对照，避免同一件事写两遍。
- **第 19 章引用一个没有计划跑到的例子**：[rules.cml](../../../examples/python/rules/rules.cml) 今天只在 README 与
  `basket.py` 里手动驱动，**没有任何计划跑到它**；引用它之前要么给它一个计划，要么在章里写成手动步骤并
  写明前置（装了 Python 包、不设环境变量）。
- **读者测试与既有评测共用脚本**：那套评测的脚本与任务集要先存在，否则本车道的"读完每章末尾的小练习"没法
  记录卡住与写错的地方；这是外部前置，不是本车道能自己补的。
- **两版同步让每次提交翻倍**，且两版的命令与输出必须逐字一致——改写期间任何例子输出变化都要同一次提交改四处
  （中版正文、英版正文、例子文件、golden）。
- **图不在逐字核对的范围内**：片段检查对 `std::rgir` 这类转译遍的块只编译、不比较文本；第 13 节今天的 DOT
  块因为中间隔了一段散文，连配都没配上。教程里的图要么另加一份计划记录的文本，要么加标记写明是节选。
- **章例子住哪会牵动闸门**：见第七节问题 1 的两个选项，选错会让片段检查或计划选择规则变红。

---

## 七、请决策者明确回答的问题

1. **章例子放在哪。** 倾向：**留在 `docs/tutorial/examples/`**（页内 `examples/…` 的命令与片段检查的
   "把页面旁边的 `examples/` 拷进临时目录"这一约定都不动），另加一个快速计划逐字核对这批文件。
   另一个选项是搬到仓库根 `examples/tutorial/`，好处是与 [examples/README.md](../../../examples/README.md) 的组织方式
   一致、与 [examples.plan.toml](../../../test/plans/feat/examples.plan.toml) 是同一种东西，代价是页内的命令块要
   改成"整段程序 + 行内命令"，且要动计划选择规则。
2. **主线例子的粒度。** 上面给了 14 步（每章一个版本）。要不要减到只在部分章给"长大后的版本"（例如
   4 步 + 4 步），少 6 个新文件？
3. **第 13 章要不要提前写形状部分。** `f32[n, k]` 与维度绑定今天已经在树上，只有张量写法要等收尾；
   允许提前写形状那半章，还是整章一起等？
4. **新例子要不要进 [examples/README.cn.md](../../../examples/README.cn.md) 与
   [examples/README.md](../../../examples/README.md) 的表格**（读者可见的索引），还是只在教程里出现？

## 倾向

先把第 1、5、9、17 章四篇样章按上面的提纲写出来交审；样章通过后按第五节第 2 步铺开，四章等收尾之后再写。
本页只交提纲，正文、例子与计划在本步一律不动。