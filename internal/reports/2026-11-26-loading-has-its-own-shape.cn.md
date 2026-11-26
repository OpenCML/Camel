# 装入有自己的形状：哪个读者说哪个形状

基：`origin/integration` **e4ba71bc7**。读完这一页要知道的只有两件事：代码侧在基上**已经**没有把装入归进
Conversion 的地方（造形状的那一笔在基里），语句侧还剩一处，已改。

## 1 判据与两个读数

**代码侧**（把装入这个算子登记成 Conversion 的地方）：

```
$ git grep -nE 'op/enter[^;]*OperShape::Conversion' -- src
（基线 e4ba71bc7）    0 行
$ git grep -nE 'op/enter[^;]*OperShape::Conversion' 9145fff8f^ -- src
9145fff8f^:src/stdlib/std/builtin/executors/builtin.cpp:824:    registry.declareShape(":op/enter", camel::ops::OperShape::Conversion);
                      1 行
```

`9145fff8f`（2026-11-25 16:32，`feat(ops): entering and refining are shapes of their own…`）把 `:op/enter` 从
`Conversion` 改成 `Entering`、把 `:op/refine_extent` 改成 `Refining`，`OperShape` 由 4 项增到 6 项；它是基的祖先。
所以「把装入归进 Conversion」在代码侧于基上为 0，不是本车道改出来的。

**语句侧**（把装入的**形状**说成转换的形状）：

```
$ git grep -n "the same shape as a" -- src docs          # 基线 e4ba71bc7
src/stdlib/std/builtin/modules/globals.cpp:2141:        // Entering (…): the same shape as a
                      1 行
$ git grep -n "the same shape as a" -- src docs          # 改后
                      0 行
$ git grep -n "the same shape as a" -- src docs          # 再跑一次
                      0 行
```

## 2 改的那一处

`src/stdlib/std/builtin/modules/globals.cpp`，`OperatorGroup::create("enter", {})` 之前（本车道唯一的改动，
10+/6-，只有注释）：

- 改前：`the same shape as a conversion, declared the same way` —— 读这句话的人会把装入登记成 Conversion 的
  形状，而 `declareShape` 在 `builtin/executors/builtin.cpp:829` 写的是 `Entering`。
- 改后：两组建的是**同一个调用形式**（目标类型是调用的结果类型，所以两组都不需要声明里没有的参数）；**形状各自
  是自己的**，`:op/enter` 登记为 `OperShape::Entering`，理由写在旁边（转换按配对的安全表判、可故障；装入不问
  表、不故障）；再注明其余不同的两点（谁写这个调用、记录上有没有故障）。

## 3 其余问形状的读者，各自说的是哪个形状

`git grep -n 'OperShape::Conversion\|OperShape::Entering' -- src` 共 54 行 / 25 个文件。按它们各自说的事分四组：

| 组 | 说的是什么 | 站点 |
|---|---|---|
| A 两种形状并排写 | 同一处对两个形状分别回答，因为答案不同 | `graph.cpp:112/115`（故障：转换按配对、装入 false）、`writings.cpp:74/86`、`cml.cpp:4151/4152`、`function_emitter.cpp:645/646`、`fvm/compile.cpp:334/438`、`shaped_operators.cpp:138/149`、`graph_regions.cpp:358/359`、`test/oper_shape.cpp:40/48`、`node_operator.h:259-261`（三标签一体的注释写明「不是一家」）、`oper_shape.h:42-65`（词表，6 个值）、`exporters/pullback/rewrite/screen` 各一处 |
| B 只写 Conversion，理由写在旁边 | 问的问题只对转换存在 | `facts.cpp:1017`（配对的问题只属于转换，装入口永不故障）、`graph_edges.cpp:833`（禁配报告只属于安全表）、`taskflow.cpp:2013/2347`（故障模型只属于转换） |
| C 只写 Conversion，而该值已不可能出现 | kind 读者：`CAST`/`COPY` 两个 kind 已删，`shapeOfKind` 只答 `Fill`/`Accs` | `graph.cpp:124/206/227/285/371/2094`、`writings.cpp:324`、`writings_extract.cpp:167`、`draft_inline.cpp:498/1390`，以及**冻结**的 `std/cxx/opt/{generic/generic.cpp:1501, generic/rewrite.cpp:243, inline/engine.cpp:218}`、`fvm/compile.cpp:334` 的臂 |
| D 只问「操作数当自己的类型吗」 | 故意不分三者：`takesItsOperandAsItsType`（词表注释写明转换/装入/细化三者都答 true） | 11 处：`graph.cpp:195/273/1088/1914/1955/2063`、`graph_edges.cpp:1063`、`exporter.cpp:3426`、`tiles/compiler.cpp:706`、`verify/translate.cpp:217`、`builtin.cpp:827`（注释） |

登记点只有一处：`builtin/executors/builtin.cpp:821-833` 六个形状各写一次（`cast→Conversion`、`enter→Entering`、
`refine_extent→Refining`、`copy→Clone`、`fill→Construction`、`access`/`access_key→Access`）。冻结目录
（`src/stdlib/autodiff/`、`src/stdlib/std/cxx/opt/`）只读不碰。

## 4 看到但没改的（另一件事，留一句给下一车道）

`git grep -n "which of the four" -- src` = 7 行。其中 `facts.h:428`、`reduction_ops.cpp:446` 的「四」与本形状无关；
其余 5 行（`operator_table.h:569/574`、`kernel/.../runtime/oper_shape.h:14`、`execute/shaped_operators.h:15`、
`test/oper_shape.cpp:16`、`function_emitter.cpp:639`）在形状表 4→6、kind 4→2 之后确实旧了。它们不是把装入归进
Conversion，而是**根本没提装入**，且横跨内核头与后端；改它要按角色拆头并报重编译单元数，是独立的一笔，本车道不动。