# 问：块的根从哪里拿到「声明它那个算子的模块」

写给负责人。

## 一、事实

`edit d = f { … }` / `view d = f { … }` 的 `d` 是块所工作的那张图，块头为此要一个调用
`graph:open(f)` / `graph:graph_of(f)`。这一条本身没有争议，问题在下游：

**块根是一个编译器自己造出来的引用，它按编号认那个算子**（算子表里 `graph:open` 有编号），
而算子的规则由 `graph` 模块的声明给出。实测（`ProgramExports::exportEntity` 上加一行探针，
在 `test/cases/probe/editview/` 的 15 个用例上跑）：

```
[probe] export module='prelude' ref='count'  kinds=graphset
[probe] export module='prelude' ref='filter' kinds=graphset
[probe]   template key='prelude:reduce' count=1
```

**`module='graph'` 的导出行一条都没有。** 同一批程序里再读模块本身：

```
[probe] graph module='graph' path='.../stdlib/graph.cml' openPresent=0 openIsOperGroup=0 templates=0
```

所以不是「登记点错了」，是**这些程序里 `graph` 的声明从来没有被建**：程序没有 `import graph`，
`graph` 只被 load 进来（源码 `graph.cml`，不是 `.cmo`），它的声明（`open`、`graph_of` 的模板）
没有被声明过，因此任何表里都查不到。

语言这一层没有第二条路：`src/compile/src/module_source.cpp:280-311` 里，声明期只沿
`module.importSites()` 递归声明被 import 的模块；`Module` 里也只有 `importSites_`
（`src/kernel/include/camel/core/module/module.h:238/339`），**没有「只依赖、不进名字作用域」的登记处**。
而块根的 DREF 在**函数体阶段**才建（`gct/builder_expr.cpp` 的 `visitBlockRoot`），那时声明期已经过去了。

## 二、两条不改可见性的路，都不存在

1. **在 import 路径上找不改可见性的入口**：没有。import 站点是唯一的「这个程序需要那个模块」的登记处，
   加一条就等于把 `graph` 的公开名字放进程序的作用域（用户此后不 import 也能写 `open(f)`）——
   这是可见性变化，不是登记。
2. **在声明层给块根更早的挂点**：没有。块根只在体访问器里被建，声明层不看函数体里的表达式。

## 三、所以这是一个设计缺口，请裁

块这个概念依赖 `graph` 的公开算子，而程序可以完全不 import `graph`。三种候选，代价各不相同：

| 候选 | 做法 | 代价 |
| --- | --- | --- |
| 甲 | 把 `graph` 变成程序的一条 import（隐式 import） | 用户程序的名字作用域因此多出 `open`、`graph_of`、`splice` 等；可见性变化，且是语言规则的变化 |
| 乙 | 给 `Module` 加一个「只依赖、不进名字作用域」的登记处，声明期沿它声明模块 | 新机制；`importSites` 目前一个概念承担两件事，现在要拆成两件 |
| 丙 | 块根的算子不从本程序的声明取，而是从一份「`graph` 作为库编译」的结果取（与定义库同一条路） | 要回答「库编译的结果怎么被程序按编号读到」，且不能让两个程序各建一份 |

请指示走哪一条（或第四条）。在此之前，块根的 DREF 会停在
`The module that declares the operator the compiler emits did not register it: graph:open`：
它报的是实话，缺的那一环在上面。