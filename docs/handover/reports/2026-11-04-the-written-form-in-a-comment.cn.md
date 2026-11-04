# 注释里那句「它写成什么」：判据、条数，与 108 的差

## 一、交代给的是一个数，没有字面量

任务说的是一批 C++ 注释「用语法形式描述规则」，已统计为 **108 处**，而 `agents/` 已有一套写法（讲要达成什么，不讲它长什么样）。交代里没有可复现的 grep，我在 `src/ modules/ include/` 的 C++ 注释行上量了下面这些候选（全部是注释行数，`ca464bca2`）：

| 字面量（注释行内） | 条数 |
|---|---|
| `\bsyntax\b` | 56 |
| `is/are written` / `spelled`，且同行有一个反引号包住的写法 | 64 |
| `e.g.` / `for example` / `such as`（用例子说规则） | 111 |
| `used to`（历史） | 152 |
| 历史标记合并（`used to`/`no longer`/`is gone`/`previously`…），按注释块 | 257 |
| `src/parse` 里形如 `/// name : production` 的文法行 | 105 |
| `Wrong:` / `Right:` | 0 |

`Wrong:`/`Right:` 是 0，这条读数有用：这一族**不是**对照式写法。

## 二、落成的判据

采用协调者给的那条，并收紧成一行可复现的 grep（注释行，`src/ modules/ include/`）：

```
git grep -n -E '`[^`]+`[[:space:]]*(:|,)?[[:space:]]*(is|are)[[:space:]]+(written|spelled|spelt)\b|\b(is|are|was|were|be|been)[[:space:]]+(written|spelled|spelt)[[:space:]]+`|\bwritten[[:space:]]+as[[:space:]]*`' -- src/ modules/ include/
```

**读数：64 行。** 判据的意思是：一个被反引号包住的写法在句子里作**主语**（`X` 是写成…的），或作「written as `Y`」的**宾语**——即这句话讲的是它长什么样，而不是它为什么这样。

## 三、64 处的判定

逐处读了上下文，分两类：

- **K（49 处）不改**：写法本身就是读者要的事实。解析器要知道接受什么形状、打印器要知道输出什么文本、诊断词条要知道在哪触发、文本格式要知道输出什么字符（`0o77`、`<root>`、`-0` 写成 `0`）；另一部分是同一段注释里**已经**先说了目的、写法只是它的一个实例。把这些「写成什么」改成「要达成什么」，事实会丢，句子反而更空。
- **M（15 处）改**：句子以写法作主语，而读者需要的是这条规则要保证什么。

M 的 15 处：

```
include/camel/compile/gct/node.h:318                          enumStructPattern
include/camel/core/error/diagnostics/messages/semantic.h:247   HandleTypeNotNominal
include/camel/core/error/diagnostics/messages/semantic.h:329   TypeOfNotWholeType
include/camel/core/error/diagnostics/messages/semantic.h:389   the interval form (above `RangeStepZero`)
include/camel/core/resource_semantics.h:269                    resourceDisplayName
include/camel/core/type/tree.h:62                              displayPath
include/camel/execute/pass/base.h:122                          requireKnownPasses
include/camel/parse/ast/data.h:263                             tagsName
include/camel/passes/test/keepshape.h:5                        kept-shape pass
modules/tensor/ops/registry.cpp:326                           printed parameter count
src/builtin/modules/globals.cpp:1949                          `__idx__` arity
src/compile/gct/builder.cpp:365                                `never`
src/parse/syntax/parser_type.cpp:208                           `constas` scope
src/passes/trans/cml/cml.cpp:3380                              printer's name table
src/passes/trans/cml/cml.cpp:3613                              `with` parameters of a call
```

## 三之二、剩下的 49 处：按理由分类

这 49 条不是「没数到」，是逐条读过之后决定**不改**，理由分三类。下一个人拿同一个 grep 再命中它们时，看这一段就不必再读一遍上下文。

### K1 写法本身就是读者要的事实（13 条）

这些注释的读者是解析器、词法、打印器和诊断词条：它们要保证的正是「接受/输出哪一个形状」，换成一句目的，可判定的东西就没了。

```
include/camel/core/error/diagnostics.h:212        dumpIdentity：快照里未解析的 range 写 `?`
include/camel/utils/number_text.h:711             complexPartText：零的部分不显示符号，`-0` 写成 `0`
src/core/error/diagnostics/diagnostics.cpp:507    place：同一份 dump 的 `?`，理由同上
src/core/type/composite/array.cpp:317             类型打印：开放秩的尾部包打印成 `...`
src/parse/syntax/lexer.cpp:616                    数字 token：八进制写 `0o77`
src/parse/syntax/parser.cpp:957                   包谓词诊断：`...` 是包，`..` 是区间
src/parse/syntax/parser.cpp:1042                  同上，`::<` 收尾处
src/parse/syntax/parser.cpp:1410                  同上，参数包
src/parse/syntax/parser_type.cpp:162              维度表的尾部包 `T[...]`
src/compile/gct/builder.cpp:4433                  `typeof e` 是类型写法，要在这里登记
src/compile/gct/builder.cpp:6078                  `return` 只能站在 step 函数自己的 body 里
src/compile/gir/builder.h:2639                    isIntervalExpr：`expr` 是否写成 `a..b`
src/passes/trans/cml/cml.cpp:403                  打印器：模块算子打印成 `module::name`
```

### K2 同一段注释已先说目的，写法只是它的一个实例（28 条）

```
include/camel/core/forward.h:12               先说「没有按算子注册」，再举 `+`
include/camel/core/forward.h:13               同上，`matmul(...)`
include/camel/core/operator_table.h:461       先说键取自声明，再给 URI 的写法
include/camel/core/type/composite/struct.h:140 先说值就是句柄，再给 `#resource`
include/camel/core/type/composite/struct.h:182 先说两个命名类型互不赋值，再给 `as`
include/camel/core/type/enum.h:101             同 struct.h:140
include/camel/standalone/array.h:582           讲下标次序的语义，写法只是例子
include/camel/standalone/num.h:99              例子是程序里的一句检查
modules/tensor/kernels/elementwise.h:84        先说 `!` 为何存在，再给 `!(a === b)`
modules/tensor/ops/elementwise_ops.cpp:113     讲转换由程序写，例子是给程序作者的
modules/tensor/ops/elementwise_ops.cpp:273     先说分解，再给写法
modules/transforms/module.cpp:7                引的是设计稿里那句今天写不出来的签名
modules/transforms/module.cpp:59               先说索引存什么，再给 `a + b`
modules/transforms/module.cpp:330              `split` 切出 `vjp` 用的两半
modules/transforms/split.cpp:1568              先说闭包留在类型外，再给配对的写法
modules/transforms/transpose.cpp:378           先说前提（至少一个操作数、至少一个类型）
modules/transforms/transpose.cpp:1137          先说参数为何留下
modules/transforms/vmap.cpp:81                 键从算子读，`matmul(x, w)` 是例子
src/builtin/modules/globals.cpp:179            先说这是兜底臂，再给 `a = a op b`
src/builtin/operators/derivative.cpp:509       同 forward.h:12
src/compile/gir/builder_expr.cpp:1356          先说 `typeof` 的类型在这里推出来
src/compile/gir/builder_expr.cpp:1506          先说 `as` 是程序显式写的转换
src/core/forward.cpp:16                        先说表里只放有切空间的算子
src/core/forward.cpp:123                       先说名字说明是哪个运算
src/core/module/module.cpp:475                 先说标签不是值，所以不按名导入
src/parse/syntax/parser_expr.cpp:1193          先说 lambda 不带修饰符
src/passes/test/writings.cpp:327               测试要的就是「两处写法」
src/passes/test/writings.cpp:413               同上，节点号最大者
```

### K3 句中的 written 指代码里的写入或图的先后，不是源写法（8 条，grep 的假命中）

```
include/camel/core/source/manager.h:204        `main` 在源里的位置（入口图会被改名）
include/camel/runtime/draft.h:690              链上排在 `tail` 之后
include/camel/runtime/draft.h:717              `newId` 之后的先后
src/compile/gir/builder.h:2319                 programWrites：哪个函数写这个变量
src/compile/gir/builder_operators.cpp:3002     `decoratedOf_` 只在一处被写入
src/passes/opt/inline/engine.cpp:86            originKeyOf：源位置记在哪里
src/passes/sched/taskflow/handoff.h:99         每次 hosted call 记一个计数
src/runtime/draft.cpp:1525                     替换后语句的先后
```

## 三之三、15 处的迁移：两批，读数

- **批 1（8 处，`63e03b34c`）**：`gct/node.h#enumStructPattern`、`semantic.h` 三条（`HandleTypeNotNominal`、`TypeOfNotWholeType`、区间词条）、`resource_semantics.h#resourceDisplayName`、`type/tree.h#displayPath`、`execute/pass/base.h#requireKnownPasses`、`parse/ast/data.h#tagsName`。`git diff -U0` 的增删行 29 行，其中注释外的 **0** 行。
- **批 2（7 处，`c33d8b1cb`）**：`passes/test/keepshape.h`、`tensor/ops/registry.cpp`、`builtin/modules/globals.cpp`、`compile/gct/builder.cpp`、`parse/syntax/parser_type.cpp`、`passes/trans/cml/cml.cpp` 两条。`git diff -U0` 的增删行 27 行，其中注释外的 **0** 行。
- 每批之后 `smoke -Tier fast -Jobs 8`：**10 / 10 pass / 0 fail**，两次 `RUN_EXIT=0`。

## 四、与 108 的差

**64（命中）− 108 = −44；15（该改）− 108 = −93。** 解释只有两类，都成立：

1. **108 这条交代没有字面量**，它不是从树上量出来的，所以它不是一个可复现的读数；我在七种候选字面量上都得不到 108（上表）。
2. **我的判据比那句口头交代窄**：我只算「主语是写法」的那些行，并把「写法就是事实」的整族排除在外；把那些也算进来是 111（例子式）或 257（历史式），仍然不等于 108，而它们改了会让准确的注释变差。

判据与名单先落这一笔，不改任何注释。