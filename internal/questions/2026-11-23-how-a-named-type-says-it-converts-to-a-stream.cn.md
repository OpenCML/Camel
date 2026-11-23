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