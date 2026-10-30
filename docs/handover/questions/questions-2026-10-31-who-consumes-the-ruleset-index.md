# 索引建好了、打印得出来，可今天没有消费者：`jvp`/`transpose` 这两个变换算子什么时候落地

日期：2026-10-31　车道：`lane-recheck`

**要作者答的一句：索引已经建好、`CAMEL_RULESET_INDEX=1` 就能打印出十条规则的键，但今天没有任何
pass 查它——`jvp`/`transpose` 这两个变换算子（设计 9.9）什么时候落地？在那之前，"缺规则"的报错该由谁
发出（还是不报）？**

## 现象（读数与代码出处）

按 22:10 的裁定搬完规则后，两个规则集模块一共十条规则，索引的键是"模块 + 运算符 + 元数"：

```
RULESET rulesets.jvp +/2 -> jvp_add
RULESET rulesets.jvp -/2 -> jvp_sub
RULESET rulesets.jvp */2 -> jvp_mul
RULESET rulesets.jvp //2 -> jvp_div
RULESET rulesets.jvp -/1 -> jvp_neg
RULESET rulesets.transpose +/2 -> add_t
RULESET rulesets.transpose -/2 -> sub_t
RULESET rulesets.transpose */2 -> mul_t
RULESET rulesets.transpose //2 -> div_t
RULESET rulesets.transpose -/1 -> neg_t
```

（主机 `88zjwei`，`LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib`，一个 `import
{rulesets.jvp, rulesets.transpose}` 的程序加 `CAMEL_RULESET_INDEX=1`；不设该变量时导入完全静默。）

索引是建在模块编译时的：`src/compile/gir/program.cpp:658` 起，只对名字以 `rulesets.` 开头的模块建，
键在 `src/compile/gir/program.cpp:674`（`rule.head + "@" + arity`），`RuleHead` 带 `arity` 在
`include/camel/parse/ast/rule_head.h:24-33`。

**但 `takeRuleHeads` 在整棵树里只有两处调用，都在同一个函数里，就是"建索引并打印"这一处：**

```
$ git grep -n "takeRuleHeads" -- src
src/compile/gir/program.cpp:665:  camel::parse::ast::takeRuleHeads(ruleset);
src/compile/gir/program.cpp:667:  rules = camel::parse::ast::takeRuleHeads(unit->diags->moduleName());
src/parse/ast/rule_head.cpp:95:   std::vector<RuleHead> takeRuleHeads(const std::string &module) {   // 定义
```

**查询本身现在是真函数了**（`include/camel/parse/ast/rule_head.h`）：

```cpp
std::string indexRule(const std::string &family, const RuleHead &rule);       // 建表（也是查重）
RuleMatch lookupRule(const std::string &family, const std::string &head, uint32_t arity);
std::vector<std::string> ruleFamilies();
```

`RuleMatch` 带 `RuleLookup` 三态：`Found` / `NoRuleForPrimitive`（这族在，这个原语没有规则）/
`NoSuchFamily`（没有这个规则集）。三种答案都有读数（用例 `rulesets_lookup_answers_and_says_why_not`）：

```
LOOKUP rulesets.jvp -/1 -> jvp_neg
LOOKUP rulesets.jvp -/2 -> jvp_sub
LOOKUP rulesets.jvp -/3 -> no rule
LOOKUP rulesets.nope -/1 -> no such family
```

建表与查重现在是同一个动作（`indexRule` 返回同键下已有的另一条规则名），所以索引不可能为一个原语
存两条规则。**但 `lookupRule` 今天的调用者只有那个探针**（`CAMEL_RULESET_LOOKUP`，在
`src/compile/gir/program.cpp`）：**索引建出来了、打得出来、也查得动了，却仍然没有一个 pass 去查它。**
`jvp` / `transpose` 这两个变换算子
（设计 `docs/design/program-transforms/README.cn.md` 第 4 节"三个图算子"）还没有实现：`git grep
"func jvp" -- stdlib` 无结果，库里只有 `grad`、`with_grad` 两个宏（`stdlib/signatures/autodiff.cml:45-68`），
它们展开宏、不查规则集。

**一条判据（写在这里，因为它是这一页的由来）**：**一个索引建好了却不能查，与没建的区别只在于
前者让人以为有。** 建好、打得出来的索引一样回答不了"这个原语有没有规则"，可它看起来像能回答。

由此，设计第 3 节末那条**"缺规则必须拒绝（原则 9.9）：变换遇到一个没有对应规则的原语，在那个原语处给
编译期诊断，点名原语与缺的是哪种规则"**——今天无处发出：诊断属于"查索引的人"，而查索引的人还不存在。

## 为什么不能自己定

诊断挂在哪个阶段、由谁发出，取决于变换本身怎么实现（是在 `gir` 之上遍历时查，还是在 `rewrite` 的
规则库里查），而那是设计 9.9 的算子、尚未实现。施工方 ad hoc 造一个"没有变换也报缺规则"的检查，会
把诊断钉在错误的阶段，并预先约束变换将来怎么查表；「未决问题归语言作者」（`docs/design-principles.cn.md`）。

## 两条路（至多三条）的代价

- **(A) 等 9.9 的算子落地，诊断与那个算子同一笔提交（我方建议，没裁定前按它做）。** 索引保持"建好、
  可打印、有人查才查"；在那之前"缺规则"不报任何错。留下的限制：今天一个程序即使缺规则也静默——因为
  它根本没请求变换，没有东西可缺。
- **(B) 在规则集模块编译时就检查"每个原语都有规则"。** 做不到：一个规则集只看得见自己那个模块，不知道
  哪个程序会用到哪些原语；`rulesets.transpose` 里就没有 `matmul` 的规则，而它并不是错的。
- **(C) 在程序编译期，拿"程序里出现的原语"去对比它导入的规则集，缺了就在原语处报。** 这会把"这个程序
  根本没请求变换"也报成错误：一个只用 `grad`（宏）的程序不需要任何规则集，它导入 `rulesets.jvp` 只是
  为了用 `lift`。

## 我方默认建议

按 (A) 做：这一笔只把索引建好、把"谁查"留给 9.9 的算子；在那之前不造"缺规则"的诊断，验收里那一条
按"今天不可能"记，与索引非空、`lift`/切线函数能跑这两条并列。

**在那之前，"标量 `jvp` 跑通"这件事的替代证明是 `mul_tangent(2,3,0.5,0.25)=2.0` 与
`lift(3.0)->3.0/0.0`——它们证明的是切线函数对，不是 `jvp` 对。** 前者是库里那条切线数学（`2*0.25 +
0.5*3`），后者是提升后的值与零切向量；两者都绕过了规则与变换，所以它们说明"这份数学对"，不说明
"有一个 `jvp` 在跑"。

**要作者答的一句：索引已建好并打印得出来，但今天没有消费者——`jvp`/`transpose` 这两个变换算子
（设计 9.9）什么时候落地？在那之前，"缺规则"的报错该由谁发出（还是不报）？**