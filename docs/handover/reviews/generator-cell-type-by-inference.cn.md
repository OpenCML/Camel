# 问题：单元的类型由「只求类型的推导」定，还是由运行时状态的首个写入定

## 现象

裁定把单元的创建顺序写成「GIR 先建 `step`，再建创建体和状态结构体，单元类型从 `step` 里对应的
写回填」。这一步在今天的图上办不到，三处都是实测或读码的结果：

- **名字在造内层图的那一刻就要解析。** 生成器机器全在 GCT 里搭（`src/compile/gct/builder.cpp:5683-5727`
  建创建体与单元、`builder.cpp:5906` 把 `step` 交出去）；把单元挪到 `step` 之后（只调创建体的子节点
  顺序），`test/cases/compile/stream/generators.cml` 逐字报
  `SemanticDiag::ImportNameNotExported: Reference '__gen_pc__' is declared as imported but is not
  exported by its module`。解析发生在 `src/compile/gir/builder_expr.cpp:698`（`visitDRefNode`）与
  `src/compile/gir/captures.cpp:42`（`captureInto`）：内层图引用外层名字的时刻，就是它自己被造出来的
  时刻，而 `captures.cpp:142` 那条「函数值只持有它被造出来时的那批捕获」把「事后补一条捕获」也堵住了。
- **单元的类型就是初值的类型。** `src/compile/gir/builder_expr.cpp:1429`（`visitVariNode`）经
  `builder_expr.cpp:1468`（`newVariable`）发 `:var/new` 作用于一个值；GIR 里没有「带类型、没有值」的
  单元。要的是 `T`，而 `T` 只能从值上取。
- **`typeof` 在声明类型的位置不求值。** 唯一表达「某表达式的类型」的写法是 `typeof`，它在
  `src/compile/gir/builder_generics.cpp:2465`（`resolveTypeNode`）里会**把表达式建出来**再取类型；
  但声明里的类型不求值：`let a: typeof(f()) = g()` 逐字报
  `SemanticDiag::DeclaredTypeMismatch: ... not the declared type 'typeof'`。所以 GCT 今天拿不到
  「第一次写那个表达式的类型」而不把它建出来。

三条候选机制各要动哪一层、代价与风险：

- **A. GIR 只求类型地推导一处表达式（建议）。** 动 GIR：给 `resolveTypeNode` 那条路加一个
  「推导完不留节点、不留效应」的入口，GCT 只把 `typeof <第一次写的表达式>` 放进单元的类型位置；
  单元仍是 `:var/new` 合成的零元算子（`resolveTypeNode` 的兄弟路径已能处理空实参）。代价是 GIR、GCT
  各一处，不碰 `step` 的构建顺序，单元的创建仍在创建体里。风险是要定义清楚「只推导」对期望类型、
  泛型实例化、诊断各意味着什么（同一个表达式推导一次、真正求值再报一次，要按哪一份报），以及
  「推导不求值」要不要因此不排 `sync` 边。
- **B. 创建体里放一个探针。** 几乎不动 GIR：`addCell` 把第一次写的表达式包成 `typeof` 就走今天
  `resolveTypeNode` 那条路。代价最小，但探针是**一个真的被建出来的值**：`f()` 这类第一次写会在
  创建时执行一次，与「调用生成器只求实参、函数体一句都不执行」冲突；探针若被算进 `sync` 链更糟。
  风险是这类「为了知道类型而造一个值」的做法会被后面的人当成先例。
- **C. 状态改成拉取沿上传值。** 动机器本身：`(T, S)?` 里的 `S` 带值而不是图里的一格，`yield` 的续延
  每次把活着的量传下去。代价是重写 `builder.cpp:5583` 那一整段（arm、循环、`match` 臂、局部量的
  提升全部换成值传递）。风险最大：`var` 是资源，捕获与效应规则要重新对；`std::jgir` 里「状态是
  这一格」的可读读数消失。

## 默认建议

按 A 做。它把「类型」留在类型层：不知道类型就不该为了知道类型而先造一个值——这与「不留限制版」是
同一件事的两面。B 会把一句「求值」留在创建体里，C 会把已经复核过的机器形状推翻，两者都不该为了
一条类型规则去付。按 A 落地后，`FeatureNotSupported('generators')`（内层 `pairs(n)`）与
`a local read after a 'yield'` 才删得掉；在那之前两处都仍可达，一处都不删。

## 需要作者一句话

单元类型是否应当由「只求类型的推导」决定，而不是由运行时状态的首个写入决定？