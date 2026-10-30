# 规则集模块里一条规则都没有：索引按"规则集模块"建，今天索引到的是空的

日期：2026-10-31　车道：`lane-recheck`

**要作者答的一句：`autodiff` 就是 `jvp` 那一族的规则集模块，还是把那几条前向规则搬进
`stdlib/rulesets/jvp.cml`？**

## 现象（读数，全为原文）

按 20:50 的裁定（`0b2cdc996`）建好了索引：一条规则属于哪一族，看它声明在哪个规则集模块里；它是哪个
原语的规则，看它左边的头。索引本身是对的，三条形状都读出来了（用例 `rulesets.probe_rule_heads`，nvm/fvm 同读数）：

```
RULESET rulesets.probe_rule_heads + -> add_rule
RULESET rulesets.probe_rule_heads * -> mul_rule
RULESET rulesets.probe_rule_heads - -> neg_rule
```

但**规则集模块里是空的**：

```
$ grep -c "^export rule\|^rule " stdlib/autodiff.cml stdlib/rulesets/jvp.cml
stdlib/autodiff.cml:10
stdlib/rulesets/jvp.cml:0
```

`stdlib/rulesets/jvp.cml` 里是 `Lift<T>`、`lift`、零切向量这些东西（共 4449 字节），**没有一条 `rule`
声明**；四条前向规则（`add_jvp`、`sub_jvp`、`mul_jvp`、`div_jvp` 一族）在 `stdlib/autodiff.cml` 里，共 10 条。

## 与裁定的一处不一致

裁定（`0b2cdc996`，20:50）说过：

> **"`rulesets/jvp.cml` 里已有那四条前向规则不搬"**（17:50 那条"叫你们搬"的随之作废）。

**这句话的前提今天不成立**：那个文件里没有规则可"不搬"——不是作者记错，是文件与记忆不一致：那些规则
一直在 `autodiff` 里。所以"搬不搬"这个结论的前提需要重新看一眼。

## 两条路，各自的代价

**(a) 把 `autodiff` 认作 `jvp` 这一族的规则集模块。**
- 代价：`autodiff` 同时是"库"（`grad`、`tangent` 等）和"规则集"，一个模块两种身份；索引要按"模块"建，
  而这份模块里的规则与它的函数混在一起。另外 `autodiff` 现在是 `import` 进来的普通模块，名字不带
  `rulesets.` 前缀——今天读索引的分支只认 `rulesets.*`（见下）。
- 好处：零搬迁，今天就能让索引非空。

**(b) 把那些 `rule` 搬进 `stdlib/rulesets/jvp.cml`（或新建 `rulesets/jvp_forward.cml`）。**
- 代价：一次搬迁 + 一次回归（`autodiff` 里谁 `import` 这些规则要跟着改），`docs/stdlib-reference.md` 与
  规则相关用例要跟着重新生成；且 `autodiff` 仍要能 `import` 到它们。
- 好处：与裁定的结构一致——"规则集是模块，标签只管程序自己写的函数"。

**我不会替你挑**：这条要裁的是"规则集模块的身份"，不是实现细节。

## 附：两条与这页无关、但下一个读这页的人会撞上的事实

- **索引只对名字以 `rulesets.` 开头的模块读**（`src/compile/gir/program.cpp` 里那一处判断）。所以 (a) 成立
  的前提是先把这个判断改成"按被声明出来的规则集"而不是按名字前缀——今天没有"声明一个模块是规则集"的
  写法，只有名字。
- **裁定要对齐的英文设计文档不存在**：`docs/design/program-transforms/` 下只有 `README.cn.md`（全仓
  `*program-transforms*` 只命中这一个目录）。所以那一笔的"英文版同段"没有目标文件，我没有造一个。
- **索引的键是模块名，没有环境状态**：`visitRuleDecl` 直接把 `diags_->moduleName()` 传进去，不用
  `thread_local`，所以将来并行编模块也不会把规则记到别人的模块名下。