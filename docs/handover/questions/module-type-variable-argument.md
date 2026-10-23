# 问题：类型变量能不能做模块类型的实参（H166，note ⟨26e8cc0d⟩）

## 问题
无体声明的签名里，是否允许把类型变量写成模块类型的实参？今天
`export func py_wrap(x: typeas T): PyObject<T>` 被拒绝，而同一棵树里 `Device<T>` 是接受的——
这是 `src/compile/gct/builder.cpp` 那条通用检查的结果，不是 `python` 模块的特例。

## 选项与代价
| 选项 | 代价（什么破、什么做不了、要重做什么） |
|---|---|
| A. 允许，但由模块自己声明"这是参数化模块类型"（例如覆写 `typeArguments()`） | 要在模块侧加一个可检查的标记并写进约定；不写标记的模块类型行为不变。收益：`Device<T>` 与 `PyObject<T>` 同一条规则，`python` 的两个名字可搬进声明 |
| B. 加专用语法（类型构造器/别名），让 `PyObject<T>` 成为合法的参数化类型名 | 改类型系统与 GCT 的检查点，面比 A 大；没有任何任务在等它 |
| C. 保持阻塞 | 零改动；`py_wrap`/`py_unwrap` 永久留在 C++ 计算签名（`stdlib/signatures/python.cml`），`Device<T>` 与 `PyObject<T>` 的不一致长期存在 |

## 倾向
选 **A**，并要求模块显式声明"可参数化"：`Device<T>` 已经接受类型变量，说明这条规则真正的例外点就是
"该模块类型接不接受变量"，把它写进模块的声明比留在 GCT 里的一条猜想要可检查。
**判断在语言作者**（声明语法的表面），决策者转呈。

## 出处
- note：`git notes --ref=handover show 26e8cc0d`，报告《H166》§6「待作者裁定：`PyObject<T>` 不能出现在声明里」。
- 现状：`camel check` 报 `SemanticDiag::InvalidTypeArgument`（`0x03000020`），消息
  "a type variable cannot be an argument of a module type"；检查在 `src/compile/gct/builder.cpp`
  的模块类型实参路径上，条件是"模块类型没有覆写 `typeArguments()`"——所以有覆写的一侧今天就是
  放行的：`DeviceType` 覆写了 `typeArguments()` 与 `cloneWithArguments`
  （`modules/tiles/include/tiles/device.h`），`PyObject` 没有。
- 同一缺口已记在 [`docs/stdlib-conventions.cn.md`](../../stdlib-conventions.cn.md) 第 8 节
  「只有语言作者能答的缺口」："模块类型的类型变量实参（`Device<T>` 接受、`PyObject<T>` 不接受，
  是 `typeArguments()` 覆写与否的问题）"。