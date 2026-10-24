# 具名实参能不能在圆括号里命名 `with` 形参

（本文件按 handover 规则写在 `docs/handover/questions/`，等语言作者答复；答复后本文件删除，答复进
`docs/handover/reviews/`。提出者：H391 named-args-groups，2026-10-24。）

## 问题

裁定的验收写法里有一条 `get(j, key: 'port')`：`key` 是 `get` 的 **`with` 形参**，却写在圆括号里。
按设计原则 4.4（名字写在持有那个参数的那张表里）这不是具名实参，今天的语言也是这样：

| 写法 | 今天的报告 |
|---|---|
| `scale(4, k: 3)`（`func scale(x: int): int` + `with <k: int>`） | `error[SemanticDiag::ParamArgumentUnknown]: 'k' is not a parameter of 'scale'.` |
| `scale<k: 3>(4)` | 编译通过，打印 `12` |
| `get(j, key: 'a')`（`json.get` 的 `with <key: string>`，本切片之后） | `error[SemanticDiag::ParamArgumentUnknown]: 'key' is not a parameter of 'get'.` |
| `get<key: 'a'>(j)` / `j->get<key: 'a'>`（本切片之后） | 绑定，与 `get<'a'>(j)` 打印同一个值 |

也就是说：`key: 'a'` 在圆括号里**找不到** `with` 参数 `key`，因为圆括号里的名字只在普通参数表里查。
Camel 自己的函数（`scale`）与 C++ 算子组（`get`）现在是同一个答案，这正是裁定要的"规则统一"；但裁定
文字里点名的那个拼写因此仍然不绑定。

## 选项与代价

**选 1（保持现状，推荐）**：`with` 参数只在尖括号里按名给，圆括号里按名给普通参数。代价：验收文字里的
`get(j, key: 'port')` 要写成 `get<key: 'port'>(j)`（两者都指向同一个位置，只是写法不同）；判据落在
"名字写在持有它的表里"这一条上，不改语义、不改测试。可以另做一件小事：`ParamArgumentUnknown` 在这个
名字确实是 `with` 参数时补一句提示（"`key` 是 `with` 参数，写在尖括号里"），但那要动诊断话术，由作者定。

**选 2**：圆括号里的具名实参也在 `with` 表里查，两表同名时报 `OverloadParameterNames` 一类的歧义。
代价：与 4.4 的"名字写在持有它的表里"冲突，等于同一件事有两种写法；`f(k: 3)` 在普通参数 `k` 与
`with` 参数 `k` 同时存在时需要一个消歧规则；有函数体的函数（`scale(4, k: 3)`）今天报错的行为要一起改，
相关测试与文档要改；尖括号写法仍必须保留（位置写法不能只有一个）。

**选 3**：只给"算子组"开这个口（C++ 规则的 `with` 名可在圆括号里给），Camel 声明不开。代价：两条规则，
正是裁定要消灭的不统一。

## 建议

选 1。它把本切片的目标（"一个调用能不能按名给实参，不取决于被调者是 Camel 声明还是 C++ 算子组"）
完整地立住：`get<key: 'port'>(j)`、`j->get<key: 'port'>`、`len(x: m)`、`items(m: s)`、`as_int(j: j)`
都绑定，而 `get(j, key: 'port')` 的拒绝理由与 `scale(4, k: 3)` 完全相同，一句话可以写清。若作者要
选 2，本切片的绑定实现（`declarationsOfCallee` 把算子组的每条规则当声明、名字取自规则签名）不需要改，
只在 `bindArguments` 的普通参数表查找失败后加一次 `with` 表查找。

## 出处

- 裁定：`git -C D:\Projects\Camel show c8782c36d`（"named arguments reach operator groups, and a
  built-in's signature has one source"）；`docs/handover/reviews/questions-2026-10-23.md` 的
  `named-arguments-at-operator-groups` 条目（选 1）。
- 设计原则 4.4；设计原则 5.9（`with` 名字给 lambda 的形参类型）。
- 代码：`src/compile/gir/builder_calls.cpp` 的 `bindArguments`（`isWith` 分别读两张表）、
  `declarationsOfCallee`；`include/camel/core/type/resolver.h` 的 `parameterNames`。
- 实测：`test/plans/feat/compile/param_defaults.plan.toml` 的
  `param_defaults.stdlib_with_name_in_parens`（`get(j, key: 'a')` → `ParamArgumentUnknown`）与
  `param_defaults.stdlib_named_args`（`j->get<key: 'a'>` 绑定）。