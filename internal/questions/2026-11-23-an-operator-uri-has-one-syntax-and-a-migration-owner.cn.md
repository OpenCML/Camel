# 一条算子 URI 只有一种写法，而把 2,677 处改成它归谁

## 事实

作者 2026-11-23 的第五条已经把**规则**裁掉了，不用再问：

> 算子有两层名字，各管一件事（负责人 2026-11-23）：Camel 名字（`std::add`、`array::min`）给写程序的人…URI 是每个具体实现的唯一身份…URI 只有一种语法 `模块:路径`，路径里可以用 `/` 分组：核心内建算子写 `std:op/add`，标准库模块写 `math:sqrt`，第三方写 `mylib:linalg/solve`；`std` 留给核心，登记时检查冒号前的模块就是正在登记的模块。URI 在加载时换成编号，长短不影响运行。诊断、调试器与默认的图打印显示 Camel 名字与类型，URI 只出现在面向实现者的视图里。
>
> —— `docs/design-principles.cn.md:658`

没有裁的是**这件事由谁在什么时候落**，以及迁移期允不允许旧写法继续被读出来。以下是这一条今天在树里的读数（2026-11-23 09:10，`ft1`，基线 `697309132`）：

```
:op/ 在 src/          799 处   92 个文件
:op/ 在 *.cml         446 处  127 个文件（stdlib 的律、rulesets、用例）
:op/ 在 docs+internal 1432 处 222 个文件
```

「核心内建算子没有模块」不是一句注释，而是两处判据在用它：

```
src/kernel/core/operator_table.cpp:67   if (uri.starts_with(':')) { family |= kFamilyBuiltin; }
src/kernel/core/operator_semantics.cpp:325  if (key.uri().starts_with(':')) { ... .commit(key.uri()); }
```

第一处把「空模块」当成「核心内建」的家族位（`operator_table.h:28` 的注释也这么写）。第二处更直接：`SemanticDiag::OperatorAccessesUndeclared` 把 **URI 本身**印给写程序的人——这与同一条裁定的最后一句（诊断显示 Camel 名、URI 只出现在面向实现者的视图里）相冲突，而且它不是一处措辞问题：改成 Camel 名要算子表能反查「URI → Camel 名」，而「只经 C++ 登记、没有 Camel 声明的算子」根本没有名字。

登记的入口也在这条线上：

```
src/stdlib/std/builtin/executors/builtin_semantics.cpp:122   const std::string uri = ":op/" + name + "_" + t.suffix;
src/stdlib/std/builtin/executors/builtin.cpp:57              {"op/atoi", __atoi__, ...}
```

`builtin.cpp` 的表按 `"op/..."` 认名字，`builtin_semantics.cpp` 拼 `":op/"`；`declareSemantics` 目前不检查冒号前那个模块（它今天是空的）。

最后，两个活着的车道正在同一个位置上写字：`lane/oper-identity-write`（33 ahead / 55 behind）与 `lane/stage2-frontend-oper`（8 ahead / 92 behind）。两者都在改算子身份、算子表和「按 URI 还是按 id」的读法，也就是这 799 处里的同一批字面量。

## 要裁的三件

### ① 这次改名由谁落，排在哪两个身份车道的前面还是后面？

`:op/add_l` → `std:op/add_l` 是一次**硬切**：登记处一旦检查「冒号前的模块就是正在登记的模块」，空模块的 799 处就在同一刻全部失效，没有中间态。可选的三条：

- 甲：本车道只交语法侧（①–④），⑤ 单独一条车道，排在两个身份车道**之后**，等它们落地再一刀切。
- 乙：⑤ 排在**之前**，先把 URI 写法定下来，两个身份车道在新写法上改身份。
- 丙：两个身份车道各自顺手改自己碰到的那部分，本车道不碰——代价是同一批字面量被三方各改一次，合并时逐处冲突。

本车道按甲交付：`<>` 与 `..` 用的是编译器内部名字 `$comp::<>` / `$comp::..`（`src/kernel/include/camel/core/operator_names.h`），不是 URI，所以 ⑤ 不在本车道的 ①②③④ 里，也不挡它们。

### ② 迁移期有没有兼容别名？

设计说「URI 在加载时换成编号」「Camel 名字改名或加别名不影响已编好的模块」，但没有说 **URI 改名**时已编好的模块怎么办——而预编译模块里存的正是 URI。两种答法：

- 有别名：`declare`/`intern` 把 `:op/x` 读成 `std:op/x`，别名活到某个版本为止；好处是 `.cml` 与预编译模块可以分批迁，坏处是「一种语法」这句话在迁移期不成立，而且 `starts_with(':')` 那两处判据要多一个分支。
- 无别名：一刀切，旧写法在加载时就是「没有这个算子」。那么 `.cml` 446 处必须在同一笔里改完，预编译模块必须重编，且要有一次明确的「旧模块失效」读数（是报错还是静默查不到）。

### ③ 诊断里到底印什么？

`OperatorAccessesUndeclared` 今天印 URI。裁定说印 Camel 名，那么：

- 有 Camel 名的算子（`std::add`）印 `std::add`；
- 只经 C++ 登记、没有 Camel 声明的算子（`builtin.cpp` 里那一整张表）印什么？留 URI 等于这条裁定只管一半，编一个名字等于凭空造一个写程序的人查不到的词。

这一条不定，⑤ 改完诊断仍然是两套写法并存，只是从 `:op/` 变成 `std:op/`。

## 不裁会怎样

- 本车道照甲交付，`feat/parse` / `feat/compile` / `feat/trans` 的读数不受 ⑤ 影响；⑤ 留在待裁，URI 仍只有 `:op/` 一种写法，裁定里那句「有一种语法」只是文档上的一句话。
- 反过来，若两个身份车道先按自己的读法改了 `:op/`，而 ⑤ 最后落在别的写法上，799 处要再改一遍。
- `OperatorAccessesUndeclared` 继续把 URI 印给写程序的人：凡是被这条诊断挡下的程序，读者拿到的名字在 `.cml` 里搜不到。