# 事实查询的实参是节点还是值？兼问遍名前缀

分支 `handover/h570-rules-chapter`。写语言参考第 9 章"规则与事实"的 9.6 节时，`with` 实参的示例本来照设计稿
7.5 写成普通函数体里的查询（`capped(y)`，`y: int`），主机上一编就报 `NoMatchingFunction`。查下去，症结不在
"查询写在哪里"，在**实参的类型**：查询的签名是 `(v: Node) => 事实类型`，而设计稿的头写的是 `fact range::<T>(v: T)`。

## 1. 一句话问题

一项事实的查询 `名字(实参…)`，收的是一个**节点**（今天的实现：`名字: (v: Node) => 事实类型`），还是一个**值**
（设计稿 7.5 的头 `fact range::<T>(v: T)` 读起来是值）？前者把查询限定在有节点可用的地方（规则的 `when`、
另一项事实的主体、或任何拿到 `Node` 的函数）；后者意味着普通函数体里的 `range(y)` 也是一次查询。

## 2. 逐字证据

主机 `88zjwei`，`out/latest`，基线 `1c3417e12` 加本分支。

事实没有 `with` 参数，查询写在普通函数体里（`func at_default(y: int): bool { return capped(y) == 5 }`）：

```text
$ camel ./fq_d.cml std::gir
exit=1
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (int).
   --> ./fq_d.cml:12:18
    |
 12 |     return capped(y) == 5
    |                  ^^^
help: Check the number and types of arguments to ensure they match one of the available overloaded function signatures. Available overloads:
          capped: (v: Node) => int

camel: 1 error
```

事实带 `with <cap: int = 5>`、查询同样写在函数体里（`capped(y)`），读数只多出实参那一项：

```text
exit=1
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <int> (int).
   --> ./fq_b.cml:13:18
help: ... Available overloads:
```

**同一次调用写在规则的 `when` 里就通过**（`rule reads_three(x: int) when capped<3>(x) == 3 { x + 0 => x }`）：

```text
$ camel ./fq_c.cml std::gir
exit=0
```

**实参换成节点，写在普通函数体里也通过**——这是关键的一条，它说明限制来自实参类型而不是书写位置：

```text
$ camel ./fq_node.cml std::gir     # func probe(n: Node): int { return capped(n) }
exit=0
```

补一句今天的写照：查询在帮助文本里的签名是 `capped: (v: Node) => int`，参数的静态类型就是 `Node`。

## 3. 这件事卡住了什么

- 9.6 的 `with` 实参示例：写成函数体今天编不过，只能写成两条规则（`when capped(x) == 5`、`when capped<3>(x) == 3`）。
  若裁定是"实参是值"，函数体才是更自然的教法，我会把示例改回去。
- 9.6 那条 bullet 的措辞，以及 9.8 表格里"事实查询的实参是值"那一行的去留。
- 诊断措辞：今天一个值实参得到的是 `NoMatchingFunction`（读起来像"没有这个重载"，而事实明明存在），而不是
  一条说清"事实的实参是节点"的诊断。

## 4. 选项与代价

**A. 实参是节点（今天的行为）**。查询只写在哪有节点的地方。
代价：普通函数体里读不到事实（想按"这个值非负吗"分支，只能写成规则的 `when`）；诊断措辞要改成说清原因。
好处：事实的答案本来就按"值在图上是怎样构造出来的"定义（`docs/technical/46_rules.md` 第 6 节：引擎回答的是
这些部分所属的那个值的事实——第一个部分的使用者里，某个 case 匹配、操作数正是这些部分的那个值），节点实参
与这套定义严格一致，不引入新语义。

**B. 实参是值（设计稿 7.5 的头 `fact range::<T>(v: T)`）**。`range(y)` 在普通函数体里也是一次查询。
代价：要为"一个值的事实"下定义——引擎按节点后序传播、事实挂在节点上，值层面的读取要落回它指认的那个节点；
`check` 的采样路径要给采样值求事实（`docs/technical/46_rules.md` 第 7 节记着今天只物化 `const` 参数）；
多参数形态 `fact pair(a: Node, b: Node)` 的参数是部分（节点），泛型头 `(v: T)` 的参数是值，一条声明里两种
参数类别的规则要写明；`when` 里的 `range(x)`（`x` 是节点）与函数体里的 `range(y)`（`y` 是值）要么统一成一条
规则，要么保留两套解析。
好处：与设计稿的头一致，教程与语言参考都能自然地写"这个值的事实"，读者不必先学会把问题改写成规则。

**C. 两种都收，按实参类型选**。节点给节点、值给值，重载解析按参数类型挑。
代价：要定义 `Node` 与 `T` 同时可接受时的优先级（例如 `T` 本身就是 `Node` 时），以及混合参数
（`pair(x, y)` 的两个部分 + 一个 `with` 实参）的选择顺序；诊断要能指出"这个实参两种读法都不成立"。
好处：不破坏今天的写法，也不挡住设计稿的目标形态。

## 5. 我只做了让页面今天能编过的事

9.6 的示例改成两条规则；9.8 增加一行，把"实参是值"列成今天的读数（`(v: Node) => …` 签名，值实参报
`NoMatchingFunction`）。这两处都不替作者选口径：裁定成 A，删掉 9.8 那一行、bullet 里设计稿头的那句；裁定成
B 或 C，示例改回函数体、9.8 那一行改写成"值实参今天不成立、目标是成立"。

## 6. 第二个问题：遍名前缀来自文件名还是 `module` 名

`std-opt-and-module-rules` 那节回答的是"规则集永远显式交进去"，没有回答注册名字这件事，所以在这里问一句。

同一条遍，四种写法（文件是 `counts.cml`，里面第一行写的是 `module not_the_file_name`）：

```text
$ camel counts.cml counts::simplify std::nvm
exit=1
stderr 第 1 行: Pass <counts::simplify> not found, available passes are:
stdout 第 1 行: error[RuntimeDiag::UnrecognizedGraphPass]: Unrecognized graph rewriting/scheduling/translation pass 'counts::simplify'.

$ camel ./counts.cml counts::simplify std::nvm
-7
exit=0

$ camel ./counts.cml not_the_file_name::simplify std::nvm
exit=1
stderr 第 1 行: Pass <not_the_file_name::simplify> not found, available passes are:

$ camel /tmp/h570/counts.cml counts::simplify std::nvm
-7
exit=0
```

两种失败写法走的是同一条路，逐字都是"未知的遍"，不是"模块在、但这个遍它没有"：

```text
$ camel counts.cml counts::simplify std::nvm
stdout 第 1 行: error[RuntimeDiag::UnrecognizedGraphPass]: Unrecognized graph rewriting/scheduling/translation pass 'counts::simplify'.

$ camel ./counts.cml not_the_file_name::simplify std::nvm
stdout 第 1 行: error[RuntimeDiag::UnrecognizedGraphPass]: Unrecognized graph rewriting/scheduling/translation pass 'not_the_file_name::simplify'.
```

读数：入口文件自己的遍用**文件名**（stem）注册，只有入口写成路径（`./` 或绝对路径）时才注册得上；前缀写声明的
`module` 名也不行。语言参考 9.4（`docs/language/09_rules_and_facts.md`）已经按"名字是文件名"写示例命令
（`camel ./counts.cml counts::simplify std::nvm`）。`docs/cli.md` 的"Pass parameters"一节讲的是 `module::pass`
形式在**别的**模块上如何解析（模块在、遍不在 → `RuntimeDiag::PassNotProvidedByModule`），没有讲入口文件自己
的遍该怎么称呼，所以这里两种失败都落到 `RuntimeDiag::UnrecognizedGraphPass` 也没有文档可对。

问题：裸文件名应当也能解析出同一个遍吗（即这是个缺陷），还是"前缀是文件名、入口要写成路径"就是规定、文档要
写清楚？若前者成立，9.4 的措辞与本页示例命令里的 `./` 都要跟着改；若后者成立，9.4 与 `docs/cli.md` 要各加
一句说明入口的写法。