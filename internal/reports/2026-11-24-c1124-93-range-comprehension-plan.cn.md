# `C-1124-93` 施工计划：推导式与数组名字对 `Range` 的解析

本页是施工方的计划（`internal/README.md`：`reports/` 由施工方写）。**先给判据，动的是语言语义，写完等你/作者点头再动手。**

## 一、现状：三处，实测（基 `1798450b30`，`lane/front-facts`）

**① 推导式自己没有一份「来源必须是数组」的判断。** 诊断注册表原文就写着它的意思：
`A list comprehension '[e for x in xs if c]' is 'xs->filter<(x) => c>->map<(x) => e>': 'xs' must be an array (or another value 'map' and 'filter' accept)`
（`src/kernel/diagnostics/registry.tsv:124`）。拒绝发生在**算子组没有接受 `Range` 的候选之后**：`src/compile/src/gir/builder_generics.cpp:2131` 只是把组里的「无候选」写成 `ComprehensionSourceRefused`。

⇒ **这里没有第二份遍历可以删，也没有第二份遍历要写**：推导式本来就复用算子组，所以「与语句的遍历共用一份」这件事在这一层是**已经成立的**，本笔要补的是组里的规则，不是新写一条走 `Range` 的循环。

**② 组里为什么没有接受 `Range` 的候选。** `src/stdlib/prelude.cml:1090-1125` 的 `map`/`filter`/`reduce`/`len`/`print`（`Range` 版）是**普通函数**，不 `#op`；组里只有 `:mark/map_arr`（`prelude.cml:1278-1286`，数组）与 `stream.cml:83/103` 的 `T..` 版。prelude 自己的注释（`prelude.cml:1084-1089`）已经写明：让 range 在这些名字下可达「是组里的一条规则，是算子的活」。

**③ 模块里连 `to_array` 都到不了。** `random.cml:114`、`str.cml` 的 `split`/`repeat` 报 `Unresolved reference: 'to_array'`。按 `D-1124-18②` 记录，这一半的实现是 `9e7717001b`（在 `lane/red-b-fixes`、未合进 `origin/develop`）——**我没有验过那条分支**，本页只把它当前置条件。

## 二、三笔，改哪几处

**笔一（`to_array` 可达）**：不重做。先确认 `9e7717001b` 是否已在 `origin/develop`；不在则本车道**依赖**它，不复制它的改法。验收若因此红，记成「前置未合」而不是「本笔失败」。

**笔二（组里给 `Range` 一条规则）**：两种写法，**这一条要你/作者定**，因为它是「算子组解析范围」的语义：

- **(a) 新增 URI**：`map`/`filter`/`reduce`/`len`/`print` 的 `Range` 声明各带 `#op(':mark/map_range')` 一类新 URI，实现体仍是今天的 `to_array(r)->map<f>`。局部、不牵动别的名字；代价是多五个 URI，且「同名」的表面分裂成两组。
- **(b) 让组的候选包含同名的普通声明**：`601b89e8bb` 已经把同名有体声明合并进一个重载集，缺的只是「组的候选集合也看这一份」。一处改、五个名字同时可达、不新增任何 URI；代价是改解析层，可能影响所有同名重载的选择。

我倾向 **(b)**，但它正是「把缺陷写成性质」最容易翻车的地方：候选变多之后，数组 / `Range` / 流三者的选择可能与今天不同。若选 (b)，必须用 `feat/compile` 全量比对基线（见验收五）。

**笔三（源改回 `0 .. n`）**：`random.cml` 的 `uniform`/`normal`、`str.cml` 的 `split`/`repeat` 四处，把 `to_array(0 .. n)` 换成 `0 .. n`；删掉 `to_array` 中转，以及注释里「今天被拒」整段（`824610ca0d` 写的那段），因为今天不再是那天。

## 三、共用的那一个入口（这一笔的要害）

`Range` 的整数只有一条生成路径：`operator for`（`prelude.cml:1128` 起的 `upto_round` 生成器）与 `to_array`（同一个 `[start, stop, step)` 语义）。`map`/`filter` 的实现体今天写的就是 `to_array(r)->map<f>` —— **本笔不新增遍历**，只让这个实现能被组的解析找到。

判据（可检查的）：本笔改完，`src/stdlib` 与 `src/compile` 里**没有**任何一处新写的「按 `start`/`step` 走 `Range`」循环；`git grep -n 'to_array' -- src/stdlib` 的命中数**只减不增**。

## 四、验收（四条 + 一条口径）

1. `test/plans/feat/modules/random.plan.toml` 13/13 全绿（今天 0/13）。
2. `test/plans/feat/modules/stdlib_values.plan.toml` 59 条里那 18 红转绿。
3. `test/plans/feat/compile/comprehension_context.plan.toml` 3/3 转绿。
4. `test/plans/feat/compile/stream.plan.toml` 的 `stream.range_value_map_no_instance`：`expect_exit = 1` → `0`，并改它的 `desc` 与用例注释 —— 它今天钉的是「没有候选」这个缺口（用例首行就写着「a range's own `map` would be a member of that group and is not this round's」），改后要钉**结果**（`map<dbl>(1..5)` 打出 `[2, 4, 6, 8]`）。
5. 口径：同一棵树、`-Jobs 8`，改前改后各量一次，合并说明里**逐条列名字**；合计是 75 条里 34 条败要转绿，且 `feat/compile` 不许出现新的红法。

## 五、不做 / 风险

- 不碰 `EffectSummary`、不碰 onnx、不碰 `kinds`；不带任何编号进注释。
- 若选 (b)：候选变多的风险由「验收五」兜住，出现新红法就回退到 (a)。
- 若笔一迟迟不合：只做笔二 + 笔三，验收 1/2 仍红，报「前置未合」，不把 `random`/`str` 改回 `to_array` 假绿。

## 六、待定（等你或作者一句）

笔二选 **(a)** 还是 **(b)**。这是 `D-1123-81` 落在哪一层的问题：`Range` 的这些名字是「组的成员」还是「同名的普通声明被组看见」。定了我按计划动手，且按笔一、笔二、笔三分三笔提交。
## 七、落账（`fad4492092` 之后）

笔二已落：`preludeOperatorOverloadsOf` 是 `operatorOverloadsOf` 合并的第四层（prelude 自己带体的同名声明）；`$builtin::` 名字也读这一层。读数见提交。

**③ 没通，原因换了一处**：推导式的元素函数是开放类型变量，判定「算子接受它吗」的探针是
`settleGenericOperatorArguments` 里的 `accepted()`（`src/compile/src/gir/builder_generics.cpp:1949`），它问的是 `ops->resolve(...)` —— **只有规则，没有体**。而 `Range` 的 `map` 正是「体」。所以：名字形式（`map<dbl>(1 .. 5)`）通了，推导式（内部是 `(0 .. n)->map<fn>`，元素函数开放）仍拒。

两条路，都是语义决定，不是实现细节：

1. **让探针问整份集合**（规则 + 体）：`accepted()` 之外还要把「体接受这个开放元素函数」变成一次实例化（元素类型代入 `int`、`U` 代入表达式类型）。改动在 `settleGenericOperatorArguments` 与它的调用点（`builder_call.cpp:1500` 只有 `ops`，要能拿到 DREF 的整份集合）。
2. **让元素类型从来源的 `operator for` 流里来**（页里说的「同一个遍历」）：`x` 的类型不由数组形状推、而由来源的 `__iter__` 元素类型给出，元素函数就成了具体的 `(int) => c`，`map` 的 `Range` 版本按模板实例化即可 —— 但 `accepted()` 仍会拒，因为模板不是规则。⇒ 2 单独不够，**1 是必须的**，2 决定 `x` 的类型怎么来。

⇒ 结论：③ 需要 1（探针问集合），2 是可选的进一步收敛。笔三（`random`/`str` 改回 `0 .. n`）依赖 ③，**不依赖笔一**：那四处改完，模块里不再有 `to_array` 调用。
