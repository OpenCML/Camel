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
include/camel/core/error/diagnostics/messages/semantic.h:389   CompOperandNotInterval
include/camel/core/resource_semantics.h:269                    resourceDisplayName
include/camel/core/type/tree.h:62                              displayPath
include/camel/execute/pass/base.h:122                          applyPassesDetailed
include/camel/parse/ast/data.h:263                             tagsName
include/camel/passes/test/keepshape.h:5                        kept-shape pass
modules/tensor/ops/registry.cpp:326                           printed parameter count
src/builtin/modules/globals.cpp:1949                          `__idx__` arity
src/compile/gct/builder.cpp:365                                `never`
src/parse/syntax/parser_type.cpp:208                           `constas` scope
src/passes/trans/cml/cml.cpp:3380                              printer's name table
src/passes/trans/cml/cml.cpp:3613                              `with` parameters of a call
```

K 的 49 处不再列出：判据 grep 的读数减去上面 15 处就是它们，逐条的归类理由在本节第一段。

## 四、与 108 的差

**64（命中）− 108 = −44；15（该改）− 108 = −93。** 解释只有两类，都成立：

1. **108 这条交代没有字面量**，它不是从树上量出来的，所以它不是一个可复现的读数；我在七种候选字面量上都得不到 108（上表）。
2. **我的判据比那句口头交代窄**：我只算「主语是写法」的那些行，并把「写法就是事实」的整族排除在外；把那些也算进来是 111（例子式）或 257（历史式），仍然不等于 108，而它们改了会让准确的注释变差。

判据与名单先落这一笔，不改任何注释。