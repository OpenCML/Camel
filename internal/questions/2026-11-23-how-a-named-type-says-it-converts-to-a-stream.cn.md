# 命名类型怎么声明「我能变成流」，以及 `..` 换了结果类型之后 golden 算谁的

## 事实

**`..` 不是库函数，是内置算子。** `intervalValue`（`src/compile/src/gir/builder_composition.cpp:277`）把
`a..b` 写成 `$builtin::range` 的调用（`core::compilerNamedOperator`，`operator_names.h:68`：带
`$builtin::` 前缀、**在程序之外的内核模块里解析**），解析到算子 `:struct/range`；这个算子的规则就是
`stdlib/prelude.cml:1031` 的声明 `#op(':struct/range') export func range(start: int, end: int, step: int): int[] panic`，
实现是 `__range__`（`stdlib/std/builtin/operators/struct.cpp:279`），造一个 `int[]`。
所以「`..` 建 `Range`」不是加一条库声明：它要换**这个内置算子的结果类型与实现**，而程序写下的
`range(a, b, s)` 与 `a..b` 是同一个算子，两处一起变。

**这个算子还有三个读者，都按 `int[]` 读它**：

1. `src/stdlib/transforms/transpose/walk.cpp:2701`：反序与转置的 backward 循环用它造
   `[lastIndex, …, 0]` 的下标序列 —— 节点类型写死 `ArrayType::create(Type::Int64())`，输入是
   `(lastIndex, -1, -1)`。`range` 不再给数组，这条循环就没有序列可走。
2. `src/stdlib/std/cpp/operator_rules.cpp:1545`：C++ 后端把它的调用发成 `rt::range(...)`，
   返回数组（`std/cpp/include/camel/standalone/array.h:1521`）。
3. `src/stdlib/std/cml/cml.cpp:4008`：step 为 1 的调用打印回 `a..b`（`caml`/`fmt` 的往返靠它）。

**`static for` 不读这个算子的结果类型**：它走 GCT 层的结构识别（`gir/builder_static.cpp:1067` 的
`isIntervalExpr` 与 `:1113` 的 `range` 名字），按常量参数直接数整数。所以 `..` 换结果类型不会动
`static for` 的展开路径（D 才动那个名字）。

## 要裁的两件

### ① 一个命名类型用什么写法声明「我能变成流」

原则 5.4 只写「`for` 除数组、字符串、映射、流之外，也接受能转成流的值」，没写这条转换在库里长什么样。
两条候选，改动的地方不同：

- **甲：算子组 `__stream__`**，随类型走（5.7「命名类型的运算符随类型走」）。`Range` 的模块导出
  `#op(':struct/range_stream') export func __stream__(r: Range): int..`，`for` 按序列的类型解析这个
  算子组（与今天 `__iter__` 的找法同一段代码，`gir/builder_control.cpp:2615` 的 `iterableElementType`），
  拿到 `T..` 后走已有的 `buildStreamLoop`。
  - 影响：现有用例一条不变（今天没有任何类型声明它）；新增用例是「用户自己的类型写一条
    `__stream__`，`for` 能遍历它」；`LoopSequenceNotIterable` 的措辞要加一句。
  - 代价：多一个保留的算子组名；与 `__iter__` 并列，读者要分清「交数组」与「交流」。
- **乙：类型所在模块里的一个普通函数**（如 `to_stream(r)`），`for` 按 5.7 在该类型声明所在模块里按
  **名字**找它。
  - 影响：名字成了语言的保留名（与保留算子名 `__add__` 同类，但普通函数不参与算子表，`--verify-ops`
    看不到它）；用户类型要与声明写在同一模块；显式调用 `to_stream(r)` 也成了程序可写的写法。
  - 代价：正是这条车道在删的「按名字特判」，只是把 `range` 换成了 `to_stream`。

第三个小问题随①一起裁：这条转换**只许交 `T..`**，还是也允许交数组（那与现有的 `__iter__` 重合）？

### ② `..` 换了结果类型之后，golden 与 ast 记录算谁的

今天没有任何 snapshot golden 钉住范围节点：`git grep -n 'range' -- 'test/plans/**/golden/**'` 命中的
全是诊断文字（`IndexOutOfRange`、`TimeFormatFailed` 一类），不是图转储；`smoke` 的
`inspect.ast_snapshot`/`gir.*_snapshot` 钉的是 `test/cases/smoke/core/fib10_pipeline.cml`，那份程序
没有 `..`、也没有下标。

会动的记录是另一本：`feat/parse` 的 `frontend.ast_snapshot` 按哈希核对**每个 `.cml` 的 AST**
（`golden/ast_hashes.txt`，现约 4483 个文件）。改 `prelude.cml`（加 `Range`、改 `range` 的返回类型）
就换掉 `src/stdlib/prelude.cml` 那一行；B 把下标里的 `range(...)` 语法特判删掉后，**每个写下标的
用例**的 AST 都会变，那些行整批过期。这本记录由 `-UpdateAstHashes` 重录（不是 `-UpdateGolden`）。

两条候选：

- **甲：改形状的那一笔自己重录**，diff 进提交说明。读数属于它的位置：形状变了、记录跟着变的那一笔
  才说得清哪一行为什么变。代价是这笔提交同时含代码与记录，评审要看两样。
- **乙：记录单独一笔、在形状稳定之后重录**。代价是中间那一笔的 `feat/parse` 红着，而红的原因不在
  它自己的代码里 —— 「一个绿必须带着它的范围」要写在车道表那行。

顺带一条要写进同一笔的账：`-UpdateGolden` 至今是禁的，而这里用的是 `-UpdateAstHashes`，两者不是一回事
（前者重写 snapshot golden，后者只重录 AST 哈希表）。请求确认：**本车道是否可以在改形状的那一笔里用
`-UpdateAstHashes`**（只重录 AST 哈希表，不碰 snapshot golden）。

## 被上面第一条逼出来的第三件

### ③ 编译器自己造下标序列用什么

反序/转置的 backward 循环要 `[n, n-1, …, 0]` 的 `int[]`（`walk.cpp:2701`）。`range` 不再交数组后：

- **甲：给 `Range → int[]` 一条转换算子**（原生，`:op/range_array`），transform 按 URI 把 Range 节点
  接过去。最小；代价是算子表里多一个只给编译器用的算子（它有声明，`--verify-ops` 的账不变）。
- **乙：让循环驱动接受 `Range`**（那是 C 的事），transform 只造 Range 节点。语义上最一致；代价是
  A 必须等 C，而 A 今天就能落。
- **丙：transform 另造这个数组**（再一个原生算子或一段库函数）。等于把甲换个名字。

本车道倾向甲（A 能单独落），但这条一旦定成乙，A 与 C 就得合成一笔。

## 第四件（不是问，是一条该被记住的编译器规则空档）

`Range` 的定义域函数（`map`、`filter`、`reduce`、`len`、`print`）本可以是普通函数 —— 参数是 `Range`，
不撞任何已有类型。但它们**同名于算子组**，而这五个名字在 `stdlib/prelude.cml` 里**全是 `#op` 声明**：

- `map` → `:mark/map_arr`（`:1116`），`filter` → `:mark/filter_arr`（`:1122`），
  `reduce` → `:mark/reduce_arr`（`:1128`），`len` → `:struct/len_arr`（`:911`）/ `:struct/len_str`（`:905`）/
  `:struct/len_bytes`（`:918`）/ `:coll/len`（`:1179`），`print`/`println` → `:io/println`（`:858`）。
- **一条普通函数都没有**：四条 `len` 全部 `#op`，`len` 的 map/set 那条（`:1180`）也带 `#op(':coll/len')`。

于是「给 `Range` 加同名重载」只有两种写法，而它们的**分界不在库里，在编译器**：

- **甲：无 `#op` 的声明与同名算子组共存，按参数类型分派**。成立的话，`Range` 的五个函数就是
  `prelude.cml` 一笔：`with <f: (item: int) => U> export func map::<U>(r: Range): U[] { … }`。
- **乙：每条一个新的 `:mark/map_range` 之类算子**，C++ 注册加转调，与「换 `:struct/range` 的结果类型」
  同一级的工作量。

**要知道的是「甲允许不允许」这条规则本身**：一个无 `#op`、有函数体的声明，能否与一个已注册的算子组
同名，并由重载解析（参数类型）而不是由名字优先次序来选。树上**没有先例**（`len` 的四条都是 `#op`），
所以它只能靠一次编译验证，验证不出就只能默认乙 —— 而乙会把「库里加一个类型」变成「算子表加五条」。
**验过了：甲不成立**（读数与红文见下），答案落在乙。

这条空档不属于本车道：任何「给已有算子名的概念加一个库类型重载」的车道都会再撞上它。若答案是甲，
值得在 `docs/design-principles.cn.md` 的命名一节写一句（算子名与普通函数名谁先、重载怎么分派）；
若答案是乙，则「库里加类型」的代价要按算子计。本车道按「先试甲、红了转乙并把红文贴出来」走。

### 那次跑：甲不成立

在 `prelude.cml` 的 `range` 声明之后加了 `export type Range`、`to_range`、`to_array` 与七个无 `#op`
的普通函数（`map`、`filter`、`reduce`、`len`、`is_empty`、`to_array`、`print`，转调同名的数组重载），
在构建主机上跑 `test/plans/feat/compile/stream.plan.toml`：**既有用例一条都没有变红**（147/150，红的
三条是为这条问题新加的探针），所以「同层声明共存」本身不炸；但**同一个模块自己的函数体里，普通函数
把同名算子组整个顶掉**，`prelude` 于是自己编不过：

```
(prelude)out/latest/stdlib/prelude.cml:1075:15: [Error]: No matching function found for argument types: <> (int[]).
    prelude::len: (r: Range) => int
 (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
(prelude)out/latest/stdlib/prelude.cml:1090:12: [Error]: No matching function found for argument types: <(item: int) => bool> (int[]).
    prelude::filter: <f: (item: int) => bool> (r: Range) => int[]
(prelude)out/latest/stdlib/prelude.cml:1103:17: [Error]: No matching function found for argument types: <> (int[]).
    prelude::print: (r: Range) => int[]
(range_value)test/cases/compile/stream/range_value.cml:6:1: [Error]: Cannot import module 'prelude': it has errors
    (out/latest/stdlib/prelude.cml).
```

读法：`len(to_array(r))` 里的 `len`（参数是 `int[]`）解析到的是新的 `prelude::len: (r: Range) => int`，
不是 `:struct/len_arr` —— 层 2（本模块自己的名字）里同名普通函数盖住了同名算子组。而**对外**（层 5，
ambient）盖的方向相反：没有被导入的 `len(xs)` 仍是算子组（既有 147 条全绿就是这一条）。两个方向都不是
「按参数类型分派」，而是「先按层选一个实体，选了就不再合并」（`names.cpp` 的五条层规则，只有层内才成
重载集）。所以 `Range` 的同名函数只有乙：五条各是一个 `:range/…` 算子，`#op` 声明加内核，与 `len` 已有
的四条 `#op` 声明同一个机制。代价：`map`/`filter`/`reduce`/`print` 的内核要么把 `ArgsView` 转调现成的
数组内核，要么各写一份；`print` 还要内核走与 `:io/print` 同一条写路径。

顺带两条同源的读数，都属同一次跑：`export type Range` 不在 ambient 层 —— 本车道那次的探针在没导入时
报 `Unresolved type reference: 'Range'`，程序要写 `import { Range } from prelude` 才看得见它；而一个
模块只要有错，导它的每个程序都拿到 `Cannot import module 'prelude'`，所以这条路上「库里有类型、程序里
写不出来」和「库自己编不过」是同一个读数。
## 决策者答（2026-11-23 18:40）

**① 甲，并且只有一个名字：`__iter__`，只交 `T..`。** 建图器今天找字符串、映射的元素时已经按 `__iter__` 这个算子组找（`builder_control.cpp:2598`），所以不另起 `__stream__`：`for` 的规则是"数组按第一维走；其余的值按它的类型解析 `__iter__`，拿到流再走 `buildStreamLoop`"。`__iter__` 随命名类型走（5.2），`Range` 的模块导出一条 `__iter__(r: Range): int..`；字符串与映射那几条重载同一笔改成交流（不计兼容），于是"交数组"与"交流"不再并存。乙不取：按名字找普通函数就是这条车道在删的东西。`LoopSequenceNotIterable` 的措辞改成"既不是数组，也没有 `__iter__`"。

**② 甲。** 改形状的那一笔自己用 `-UpdateAstHashes` 重录 AST 哈希表，diff 写进提交说明。它只记录 AST 的哈希，不是快照基准，`-UpdateGolden` 的禁令管不到它。

**③ 乙。** 转置与反序的反向循环造一个 `Range` 节点，循环按 ① 遍历它；不为编译器另加只给自己用的 `:op/range_array`。A 与 C 合成一笔。C++ 后端的 `rt::range` 与 `cml` 的打印跟着改：打印把步长为 1 的 `Range` 写回 `a..b`。

**④ 甲成立才对，那次读数是名字解析的缺陷，不是规则。** `names.cpp` 开头自己写着"同一层里同名函数组成一个重载集"，而 `#op` 声明就是函数声明，只是函数体在 C++（语言参考 7.4.4"C++ 实现的函数写成无函数体的 `export func` 声明"）。prelude 里的 `#op` 的 `len` 与有函数体的 `len(r: Range)` 同在 prelude 这一层，应当合成一个重载集、按参数类型选；现在后者把前者整个顶掉，是 `#op` 声明没有作为本模块的函数进入第 2 层。改法：让 `#op` 声明与同名的有体声明在同一层合成一个重载集。同源的第二处：prelude 导出的函数不用导入就看得见，而它导出的类型要 `import { Range } from prelude`，同一个模块导出的东西可见性不一致；prelude 导出的类型同样不用导入。两处改完后，`Range` 的读法（`len`、`map`、`filter`、`reduce`、`print`）就是 prelude 里几条普通函数，不加算子。
