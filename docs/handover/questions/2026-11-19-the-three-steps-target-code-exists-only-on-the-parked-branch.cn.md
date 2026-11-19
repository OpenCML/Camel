# 三步裁定的目标代码只在 lane-ssa 上；而 develop 上没有打包

**问句（一句）**：`3a58a22a1`「答三页」第一条给的三步（先让打包不再覆盖 `funcType()` → 四读者改问 → 一起删）在 `develop` 上**无处可做**——`packsVarParameters` / `exitTypeIsPacked` / `callHandsBackResult` 在 `develop` 全是 0 命中，目标代码只在 `lane-ssa` 上，而那条分支自带 104 条红、已判不能合。请问是**先修 `lane-ssa` 的 104 条红再执行三步**，还是**在 develop 上放弃三步、只做第三步里能在 develop 落地的部分**？

**默认建议（本轮按此开工）**：**在 develop 上只做「删 `declaredFuncType`」**（它在 develop 上有 5 处代码命中、4 个文件，删完 grep 归零）；三步里的前两步随 `lane-ssa` 一起等。

## 一、实测：develop 上没有打包

```
`packsVarParameters` / `exitTypeIsPacked` / `callHandsBackResult` 在 `origin/develop`（`061011af6`）：**0 命中**
`graphExitTypeOf`：只有 2 处注释（`draft.h:466`、`builder.h:2338`）
`builder_control.cpp` 只有 **4546 行**；打包那两行在 `lane-ssa` 的 `builder_control.cpp:~4625`
   （`graph->setFuncType(funcType->withExitType(exitType)); graph->setPacksVarParameters(true);`）
`var_params.*` 用例在 develop 上是「按引用传参」，全绿
```

⇒ **⇒ 所以「打包覆盖 `funcType()`」这件事在 develop 上不存在。** 作者三步描述的是 `lane-ssa` 的现状。

## 二、那个「唯一的写回点」不是死代码：删它会掉 23 条

develop 上 `withExitType` 只有一处会写图的函数类型：`src/compile/gir/draft_graph_builder.cpp:96` 的 `else` 分支
（`draft().setFuncType(funcType->withExitType(exitType))`）。另外三处是声明（`func.h:179`）、实现（`func.cpp:327/439`）、
运行时故障隔离（`fault_isolation.cpp:67`），都不在打包路径上。

删掉那个 `else` 分支的实测（补丁留档 `D:\Temp\pack2-step1-EXPERIMENT-withExitType-removed.diff`，已回退）：

```
`feat/linear` **864 → 841 pass / 0 → 23 fail**（`decorator_*` 7 条、`order_edges_regions` 5 条、`same_resource_order*` 6 条等）
第一条红 `test/cases/linear/nvm/decorator_bare.cml:17`：
   `[Error]: Function of type 'decorated graph should return function' cannot be called with argument types: () => <null>`
```

⇒ **⇒ 「声明里没写结果类型」的图（lambda / 装饰器），出口类型**只能**由体写回 `funcType()`，调用方读的就是它。**
⇒ **⇒ 所以那处写回不是多余的；删它需要一个「出口类型住在别处」的家，而 `graphExitTypeOf(声明)` 对这类图给不出东西。**

## 三、这是设计缺口，作者没有裁到

作者 `3a58a22a1` 只写了「`funcType()` 永远是声明」；作者 07:35 第 2 款只裁了「入口图仍是声明」。
**「lambda / 装饰器图的出口类型住在哪」没有裁** —— 而这恰恰是删掉那处写回的前提。

⇒ **⇒ 请裁定**：decoration/lambda 这类**声明不写结果**的图，其出口类型由谁承载？
　**甲（默认建议）**：写回 `funcType()` 保留（即那处 `else` 分支留下），因为它本来就是「声明没写结果时由体补全」的机制，不是打包；
　**乙**：另立载体（例如 encode 时的冻结图），则那处写回可以删。

## 四、`lane-ssa` 的 104 条红与这条的关系

`lane-ssa`（`4d308fc72`）自带 104 条红（linear −93 / opt −4 / tools −1 / modules −5 / trans −1），
合前合后 `feat/linear` **逐字相同**（`719/93`），红的形状集中 `var_*` 且是**编译期**错：
`Return type mismatch between branches (in function 'count_up: (var acc: int, n: int) => int'): expected 'int', found 'int'`。

⇒ **⇒ 三步裁定的前两步要在那条分支上执行，先得把那 104 条红做完；那不是「执行裁定」，那是「重做整套打包」。**

## 五、本轮处理

```
① develop 上**只做**「删 `declaredFuncType`」（5 处代码命中 / 4 文件，删完 grep 归零），六计划与 modules/trans 不许差
② 三步的前两步**随 `lane-ssa` 一起等**本条裁定
③ `withExitType` 那处写回**不动**（删它会掉 23 条）
```