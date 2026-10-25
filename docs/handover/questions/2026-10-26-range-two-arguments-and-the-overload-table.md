# `range(a, b)` 的两实参形态与重载表 · 2026-10-26

> 说明（协调者）：裁定「**`RangeTwoArgumentsRemoved` 删掉，`range(a, b)` 就是普通的"没有适用的重载"（`range` 只有三个参数的那一种）**」在落地时撞到一个机制问题，报给你定。落地方（切片车道）已按"保留名字判别但报普通重载错"临时做，页面里请确认这就是你要的效果，还是要把机制改掉。

## 现象

`range` 组只有**一条** resolver：`src/builtin/modules/globals.cpp` 的 `"range"` → `":struct/range"`，签名 `(start: int, end: int, step: int) => int[]`，但**变量表**写作 `{{0,{}},{-1,{}}}`——`-1` 表示**不约束普通实参个数**，所以两实参在这一层是**被接受**的（它自己的注释就写着「`range(a, b)` is the interval of the integers from `a` to `b - 1`」）。

今天「两参数 `range` 不许写」实际只由 builder 的一处**按名字拒绝**兜着：`src/compile/gir/builder_operators.cpp:2967-2975`（按 DREF 名 `range` + 2 个普通实参命中，报具名 `SemanticDiag::RangeTwoArgumentsRemoved`，码 `0x0001E5`）；`builder_static.cpp:985-989` 的静态序列读法那一处同理。

## 机制（为什么不能简单地"把变量表收窄"）

登记机制**能**表达「恰好 3 个」：把 `{-1,{}}` 换成 `{3,{false,false,false}}`（下标切片那条就用 `{6,{false×6}}`）。

但 **`a..b` 的降低走的是同一个 resolver**：`builder_operators.cpp:1583-1602` 的 `intervalValue()` 造一个 LINK、callee 是 `$builtin::range`（`kRangeCall`，`:1379`），随后 `visitLinkNode(call)` 走**普通调用路径**（含重载解析）。而 `builder_when.cpp:59` 明说「`$builtin::not` 和 `not` 是同一个算子」——两个名字落在**同一个组**上，resolver 看不到 callee 名。所以把表收窄成 3 个，会**把编译器自己那次两实参调用一起拒掉**，`a..b` 就废了。

## 两条备选

1. **收窄变量表 + 让 `a..b` 的降低绕开重载解析**：这才是"`range` 只有三参数那一种"的字面实现（求重载时 `range(a,b)` 自然落空、报普通 `NoMatchingFunction`）。代价：要动 `intervalValue`/`$builtin::range` 的降低路径（语义/管线改动），且要让"程序写的 `range`"与"编译器写的 `$builtin::range`"在解析层可分。
2. **保留按名字的判别，但报普通那条**（本次做法）：`range(a,b)` 的报错换成与正常失败路径**同一条** `SemanticDiag::NoMatchingFunction`（同样的 `argTypesStr`/`overloadsStr` 构造），具名诊断 `RangeTwoArgumentsRemoved` 与其码 `0x0001E5` 一并删；`builder_static.cpp:985-989` 让两实参**落空**交给调用路径去报。程序看到的错误与"没有适用的重载"**逐字相同**，但它仍是**解析期的一次特判**，不是重载表的自然结果。

## 建议

先按**备选 2** 落地（错误文本与普通重载错逐字一致、具名诊断删净、不留过渡），把**备选 1**（收窄表 + `a..b` 绕开解析）记成一条待办；若你要的是"表里就只该有三参数那一臂"这种机制上的干净，请点头做备选 1，我另派一条车道（它动的是降低路径，与该批的用例改写不该混在一起）。

**要你一句话**：备选 2 的效果是否就是你要的？还是备选 1？