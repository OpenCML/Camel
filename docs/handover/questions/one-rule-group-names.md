# 一条规则的算子组要不要接受具名实参

（本文件按 handover 规则写在 `docs/handover/questions/`，等语言作者答复；答复后本文件删除，答复进
`docs/handover/reviews/`。提出者：H391 named-args-groups，2026-10-24。）

## 问题

本切片让算子组按名绑定：一个名字的每条规则都是它的一条声明（`declarationsOfCallee`），名字取自规则
签名的 `with` 表与普通表。但"一个名字的规则读成一组声明"仍沿用旧条件：**两条以上**。一条规则的算子组
走旧的签名路径，而签名路径只在规则**固定了签名**时才知道名字（`signatureForArgumentContext`）：

| 调用 | 今天的结果 |
|---|---|
| `len(x: m)`（3 条规则） | 绑定 |
| `items(m: s)`（2 条规则） | 绑定 |
| `j->get<key: 'a'>`（`json.get` 8 条声明） | 绑定 |
| `trim(s: …)`（1 条规则，静态签名） | 绑定（旧路径，签名记录了名字） |
| `filter(collect: xs)`、`apply(collect: xs)`（各 1 条规则，结果类型取自实参，没有固定签名） | `ParamArgumentCallee`：签名在按名字写出的调用里定不下来 |
| `m->get<key: 'a'>`，程序**没有**导入 `json` 时（`get` 只剩内建的 `:coll/map_get` 一条规则） | 同上，拒绝 |

所以：**同一个名字能不能按名给，取决于它的规则条数**（1 条拒绝、2 条以上绑定），而裁定要的是
"不取决于被调者是 Camel 声明还是 C++ 算子组"。一个 Camel 声明（1 条）能按名给，一条 C++ 规则不能。

## 为什么当时没有一起做

旧条件的另一半是**默认值**：一条规则的名字组里出现空洞时，签名路径会用这条规则登记的默认值补上
（`defaultsOfCallee` + `nativeDefaults_`，H276），而集合路径遇到空洞一律拒绝（"默认值写在一条声明里，
调用没说它指哪一条"，H376）。把 1 条规则也交给集合路径，就要先把默认值的填充搬进集合路径；`onnx` 的
`export_model(out: string = "")`、`std` 的 `growth_budget: int = 4096`、`rewrite` 的 `rounds`/`strict`/
`explain` 都是"一条规则 + 默认值 + `with` 名字"，今天按名调用还能填默认值。

这不是推测：本切片先按"一条规则也算一组声明"实现过，`stdlib/rules.cml:522` 的
`r.result->run<rules, rounds: rounds, strict: strict>`（`explain` 有默认值、这里留空）当场编不过——
`rules` 模块加载失败（`InvalidPassParameter: its library 'rules' does not compile`），根因是
`NoMatchingFunction: <Rule[], int, bool> (Program)`：空洞处不再补 `false`，调用落不到任何一条规则上。
改回"两条以上"后恢复。这条调用就是选 2 要付的代价本身。

## 选项与代价

**选 1（保持现状，本切片的写法）**：名字在"一组声明"里查，一组声明至少两条；一条规则的名字由签名路径
负责，而签名路径要求规则先固定签名。代价：`filter(collect: xs)` 这类调用不接受具名实参，理由写在参考页
（"签名在按名字写出的调用里定不下来"）；`get` 在导入 `json` 前后行为不同（内建那条一条规则）。

**选 2（推荐，随下一个切片）**：一条规则也算一组声明，名字一律取自规则签名；把默认值的填充从
`bindArguments` 的签名路径提出来，集合路径遇到空洞时用注册表里的默认值补（URI 就是键），补不了才拒绝。
代价：动 `bindArguments` 的两条路径（默认值补位要在名字定位之后按位置做），`param_defaults` 里几条
"一条声明 + 默认值"的用例要重跑确认；收益是"名字与规则条数无关"这条规则彻底成立，`filter`/`apply`/
`format` 也能按名给普通参数。

**选 3**：一条规则的名字也接受，但空洞一律拒绝（不搬默认值）。代价：`export_model(out: 'x')` 这类今天
能填默认值的调用会退化成拒收，是可见的回归。

## 建议

选 1 落地本切片（验收里的绑定都满足），选 2 排下一个切片。选 3 不可取。

## 出处

- 裁定：`git -C D:\Projects\Camel show c8782c36d`；`docs/handover/reviews/questions-2026-10-23.md`
  的 `named-arguments-at-operator-groups`（选 1）。
- H276（`with` 参数的默认值）、H376（一组声明不填默认值）、H235。
- 代码：`src/compile/gir/builder_calls.cpp` 的 `declarationsOfCallee`（`size() < 2` 的返回）、
  `bindNamedArgumentsToDeclarations`（空洞处 `anyDeclarationDefaults`）、`defaultsOfCallee`（一条固定签名
  的规则组按 URI 找 `nativeDefaults_`）。