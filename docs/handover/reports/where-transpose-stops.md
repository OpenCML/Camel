# 转置今天停在哪儿：四道墙的顺序

这张图给下一个人用，省掉重走一遍的四个下午。四道墙按**被撞到的顺序**排，每道三句：**症状**、**当时为什么以为它是终点**、**挪开它之后露出的是什么**。

被量的东西一直是同一件：`grad(f)([3.0, 2.0])` 对 `f(xs) = xs[0] * xs[1]` 打印 `[2.0, 3.0]`，以及反向那一半能不能把余切送回 `xs`。下面每一道墙的读数都来自这同一个探针（`vjp(f)([3.0, 2.0])` 的前向半边已经打印 `6.0`）。

---

## 第一道：`unbound`（不是墙，是测量造成的假象）

**症状**
```
TRANSPOSE function=rulesets.jvp::index_into<float> skip=rule uri=:op/idx_arr reason=unbound
```
`unbound` 出自 `modules/transforms/transpose.cpp:1296–1301` 的那个三元式：`!rule.has_value()` → `unbound`，也就是"`transposeBindings()` 里没有这个 uri"。

**当时为什么以为它是终点**：因为它看起来是一个"绑定没登记"的确定结论，于是下一步自然是查注册有没有跑、键有没有写错，甚至怀疑规则名要不要 `export`。**这三条都查了，都不是。**

**挪开它之后露出的是什么**：换一个 pass 量同一条探针（`std::rulesets` 而不是纯 `std::nvm`），同一条规则读成
```
RULESET   rulesets.transpose  index_into/2 -> idx_arr_t (rhs scatter_into)
TRANSPOSE function=rulesets.jvp::index_into<float> skip=law rule=idx_arr_t uri=:op/idx_arr
```
**绑定在、规则在、查找命中**——`unbound` 是"用哪个 pass 量"造成的，与 import 漏掉造成 `no-such-family` 是同一类：**测量程序本身也是被测量的一部分**。

---

## 第二道：`skip=law`（按名字分派）

**症状**
```
TRANSPOSE function=rulesets.jvp::index_into<float> skip=law rule=idx_arr_t uri=:op/idx_arr
```

**当时为什么以为它是终点**：`rule=idx_arr_t` 已经在读数里，说明规则被找到了，只差"让 `transpose` 认得这个名字"——于是最省事的做法是加第六个 `law` 分支。**那是第二个真相：规则集里写一遍，C++ 里再写一遍。**

**挪开它之后露出的是什么**：`transpose.cpp:1314–1360` 那五条分支不是同义重复——`mul_t`/`div_t` 依赖 `knownValue(另一个操作数)`、`wantsCotangent`、`isConstant`、`inputs.size()==2`，`div` 第 1 位还要 `-(ct*a/(c*c))`。而**库里的 `*_cotangent` 只写了第一个名字的那一半**（`sub_cotangent(x, c, ct) = ct`，而 `c` 的余切应是 `-ct`），所以 C++ 补的是**库缺的那一半**。补完库那一半（`add_cotangent_c`/`sub_cotangent_c`/`mul_cotangent_c`/`div_cotangent_c`），才轮到"转置去调库"——**而"调库"要一个调用节点。**

---

## 第三道：被调函数的图引用（`addCallNode` 要 `GCCallBody`）

**症状**：`primitive(uri, args)` 只发**算子节点**（`":op/add_d"` 这种），转置整个文件里没有一处发过**调用节点**。要发，就撞上
```
include/camel/runtime/draft.h:573   gc_node_ref_t addCallNode(
                                        camel::core::type::Type *type, const GCCallBody &body = {},
                                        gc_slot_idx_t dataIndex = 0);
```

**当时为什么以为它是终点**："同一个 `draft` 就行"——而这一条**是对的**：`transpose.cpp:233` 取 `auto &draft = group.transaction(member).edit();`，`272` 的 `primitive` 捕获 `&draft`，整段构建一个事务。**同址，不需要探测。**

**挪开它之后露出的是什么**：`addCallNode` 的第一个参数是**类型**（`jvp.cpp:964` 传的 `lifted` 是 `TupleType::create(...)`，不是图引用），**真正缺的是"这个库函数在图里是哪张图"**。转置今天**从不进 program 的函数表**，所以它没有这个答案。

---

## 第四道：实例（`GenericRuleInstance`）

**症状**：唯一"把规则右侧变成图"的函数是
```
modules/transforms/jvp.cpp:131   gc_node_ref_t expandRuleRightSide(
                                     camel::runtime::GraphDraftGroup &group,
                                     camel::runtime::GraphTransaction &transaction,
                                     const camel::compile::gir::GenericRuleInstance &instance,
                                     const std::vector<gc_node_ref_t> &values,
                                     const std::vector<gc_node_ref_t> &tangents,
                                     std::string &why);
```

**当时为什么以为它是终点**：`jvp` 手里有它、调得通，那"转置调到同一个函数"看起来只是传参；而且转置确实有 `group`（`transpose.cpp:202`）与 `draft`（`233`）。

**挪开它之后露出的是什么**：**第三个参数转置没有**。`GenericRuleInstance`（`include/camel/compile/gir/rule_templates.h:52`）是"某条规则在某个算子上的一次实例化"，而转置今天**拿到规则名就按名字分派，从不实例化任何规则**——它手里根本没有那个对象。所以卡住的不是一堵墙，是**一个对象**：要先有实例，"按规则展开"才有一半可用；而实例化的入口在今天只在 `reinterp` 那一遍里。

---

## 一句话

**今天真正卡住的是一个对象（`GenericRuleInstance`），不是一堵墙。**

四道墙的顺序与措辞取自实际读数；与派工时的记法相比，第一道这里明确写成**"不是墙，是测量造成的假象"**，第四道明确写成**"卡的是一个对象"**——这两处是记录时改的，不是事后改的口径。