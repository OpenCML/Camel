# 每日审查 11-27：develop 过去 24 小时（553f4d231..develop）

三个只读子代理分头审内核、标准库与模块、编译器与工具，下面是决策者逐条核实成立的。每条写清在哪、为什么不对、改成什么形状。行号是 develop 尖端的。

第 1、3 两条并进正在做的格（C1、C7）；其余排给缓冲或杂项车道，不占 CC 六格，截稿前做完即可。

## 1. 遍内核有两张登记表（第二份真相）——并进 C1 第 2 步

`src/kernel/execute/pass.cpp:360-393` 新加了按 `OperId` 登记的 `registerPassKernel(OperId, …)`、`findPassKernel`，在仓库里没有一个调用者；真正解析遍的仍是按路径字符串的那张（`registration.cpp:13,20`、`registration.h:41-58`，`rewrite/module.cpp:1868` 也在用）。两个同名重载键不同。C1 第 2 步要做的就是让解析改走按 `#op` 的 URI 驻留成 `OperId` 的那张，同一步删掉按路径的那张，重载随之只剩一个。判据：`git grep -n "registerPassKernel(const std::string" -- src` 为 0，`findPassKernel` 有调用者。

## 2. "程序写出的算子名"有两份实现，已经分叉

`src/kernel/runtime/operator_reuse.cpp:60` 的 `operatorName` 是 `storage_reuse.cpp:85` `updateName` 的复制，少了 `plainForm` 那一步，于是 `--reuse` 与 `--copies` 对同一个节点会打出不同的名字。注释里说"两份读法回答不同问题、谁都不拥有那张表"，但被问的是同一个事实：一个算子在程序里怎么写。改成算子表上的一个函数（例如 `OperatorTable::writtenName(uri)`），两处都调它，删掉两份局部实现。判据：两个读法对同一个程序打出的名字一致，加一条用例。

## 3. ONNX 导出器从 URI 的拼写推出算子关系（按名字）——并进 C7

`src/stdlib/onnx/exporter.cpp:552-560`：复合赋值跟的是哪个普通算子，靠把组名 `__add__` 切成 `add`、在 URI 里 `rfind` 它、取后缀当类型标签，再在组里按后缀匹配。这个关系该由内核的算子表直接答（复合赋值算子 → 它跟随的普通算子的 `OperId`），导出器读它。同一处 `appendInPlaceOper()` 插在 `isLocalVariable` 的文档注释与函数之间，把那段注释挂错了函数；那一行缩进也错了。这一笔没有用例，补一条。

## 4. 卷积与池化的窗口参数：声明说必填，内核和推断各藏一份默认值

声明已改成 `with <stride: int, padding: int>` 必填，但 `src/stdlib/nn/tensor_ops.cpp` 的内核与推断函数里还有约十处 `optInt(norm, 3, 1)`、`optInt(norm, 4, 0)`、`optConstInt(ctx, 3, 1)` 之类（第 113–114、123、153–154、168–169、230、255–256、294–295、329、364 行），注释说"遍造的节点可能不写"。默认值只能有一处：要么写进声明，要么缺了就拒绝；内核和推断里的副本删掉，它们读实参。判据：`optInt`/`optConstInt` 在这些窗口参数上不再带默认值，仓库里造这类节点的遍（若有）照声明写全。

## 5. 命令行记着一张"已删除选项"表

`src/tools/camel-cli/config.cpp:608` 的 `kRetired` 列了二十多个删掉的写法，原因写在注释里：选项解析器按前缀匹配，`--gc-nursery-mb` 会被读成 `--gc` 加尾巴。这是在用一张永久的旧名单去补解析器的行为，等于命令行有第二份选项表，也违背"旧写法干净删除、不留记录"。改法是让解析器只认整词（关掉前缀与粘连匹配），删掉的选项自然成为普通的未知选项，然后删掉 `kRetired` 与 `rejectRetiredOptions`。判据：`cli_removed_options` 那份计划照样全绿，代码里没有这张表。

## 6. 子命令按下标分派

`config.cpp:1029-1083` 用 `kSubcommands[0].name` … `[4].name` 写成两串 if。表里本来有 `command` 字段，分派应按表项走；现在调换表里的顺序就会悄悄换掉行为。改成按表项分派，表是唯一的来源。

## 7. 资源修饰词在解析层列了两遍

`src/parse/src/syntax/parser.cpp:1277-1278` 与 `src/parse/src/ast/builder.cpp:732-740` 各列一遍五个修饰词（`unordered` 等），AST 上五个布尔字段、打印顺序又是一份；内核的记录（`resource_semantics.cpp:321-325`）是语义的那一份。解析层放一张词表，解析器、建树器、打印器都读它；解析层到内核记录的映射只在一处。加第六个词只改两处：词表与内核记录。

## 8. 注释里的过程痕迹与过期事实

改成只说代码做什么与技术理由，删掉的不留"以前如何"：
- `test/tools/header-weight.mjs:149`（"another lane can pick it up"）、`test/tools/backend-differential.mjs:46`（"a lane's temporary reading"）：去掉角色词。
- `scripts/stdlib-reference.mjs:1011`、`:705`：讲的是这个读法以前怎么错的；改成规则本身（"结果写在 `=>` 之后"、"声明包括后面换行的 `when` 一行"）。
- `src/kernel/include/camel/core/operator_table.h:301`（"the copies had already drifted apart"）、`mm.h:83`（"the ledger of environment switches this project keeps"）、`context/frame.h:452`（CI 运行编号与那次的调用栈）、`runtime/node_kind.h:44`（"plan 2.3"）。
- `src/stdlib/test/contexts.cpp:22-31`（"It read 2 while…""Before the fix…"）、`src/stdlib/graph/resolve.h:10-12`（"The read operators learned that first…"）。
- `src/kernel/include/camel/core/resource_semantics.h:443-445` 说标准资源是 `stdout`、`stderr`、`stdin`、`clock`、`env`、`fs` 六个，代码里已是 `console`、`clock`、`env`、`fs` 四个；改成与代码一致，同文件与 `resource_semantics.cpp` 里把 `stdout` 当资源名的几处一并改。

## 不成立、不派的

内核里 `writesSomething` 与 `writesForTheRegion` 是否会分叉（举不出实例）、内核注释举库里的算子作例子、`AppendInPlace` 是否只为一个库进内核表（待证）、`stdlib-reference.mjs` 的例外表（那是检查工具明写的已知偏差登记，两个方向都比对）、测试遍 `loop_collect` 把拒绝改成跳过（理由是原则）。
