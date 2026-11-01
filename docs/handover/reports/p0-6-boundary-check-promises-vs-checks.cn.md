# P0-6：遍边界检查比它声称的弱——承诺与实际逐格对照

> 2026-11-02。审查清单第 6 条。这一页是**对照表 + 这一笔做了什么 + 还差什么**，不是台账，也不改任何读数口径。
> 代码位置一律以 `src/execute/pass.cpp` 为准，行号是写这一页时的行号。

## 一、注释承诺 × 实际检查

| # | 注释承诺（原话在哪） | 实际检查（行号） | 判定 |
| --- | --- | --- | --- |
| ① | 旧注释说**被内联复制过的效应不比较**（`matchedEffects` 上方，旧 `:1990-1994`）；审查清单照此记为“只配对遍前后各出现一次的效应” | 匹配键是 **origin + 内联拷贝点 + 操作符**（`effectKey` `:1981-1993`，`inlinedCopySite` `:1749-1764`），所以**不同调用点上的拷贝是参与比较的**；被略过的只有“同 origin、同拷贝点、同操作符”的克隆（`:2009` `found->second != 1`） | **注释比实际弱**（实际比审查清单的写法强）→ 已改注释，写清它实际配什么、什么不进任何配对 |
| ② | 原则 3.3：**每个资源**上两个效应的先后都要保持 | `collectEffects` 只收 `ResourceClass::Ambient`（`:1958-1962`）；`var`、句柄、元组/对象一律跳过，理由写在原处（身份只在一张图里有意义） | **承诺更强** → 未补，见第三节 |
| ③ | 管线注释：没有一个遍——**嵌套的或列出的**——结束时不查（旧 `:3108-3111`） | `entryExitViolation` 只查 `program.root()`（`:2301-2305`），它自己的注释写了理由（模块自留的图不必能单独跑） | **承诺更强**（对非根图）→ 管线注释改成实际做的事（“程序自己的那张图”）|
| ④ | 同上：“嵌套的”也要查 | `applyRegisteredPass` 直接 `pass->apply(program, os)`（旧 `:2606`），**完全没有边界检查**——`std::opt::saturate`、`rules::simplify`、`opt::saturate` 都从这条路走 | **承诺更强，且是最严重的一格**：不是“子步之间不查”，而是这条路一次都不查 → **已补**（见第二节） |
| ⑤ | 原则 3.3 对 `var` 与句柄同样有效 | 与 ② 同一处、同一行；没有单独的检查 | **承诺更强** → 未补，与 ② 同一条 |

一句话：**五格里三格是“注释/原则比代码强”，一格是“注释比代码弱”（①），一格是最严重的“那条路完全没有检查”（④）。**

## 二、这一笔做了什么

### 1. ④：检查抽成一个函数，两个调用点

`passBoundaryViolation(ctx, spec, program, effectsBefore)`（`:2525-2543` 起）现在装着原来那段内联的五项检查（效应先后、资源集合、操作符端口、入口出口、效应凭据），被两处调用：

- `applyPassesDetailed`（`:3195`）——管线列出的遍，行为与之前完全一致；
- `applyRegisteredPass`（`:2704-2707`）——**一个遍按名字跑另一个遍**的路，这是 `std::opt::saturate`、`rules::simplify` 与 `opt::saturate` 的入口。这一笔之前它一次都不查。

**它换来的不是“更严的拒绝”，而是“拒绝得早一层”：** 之前嵌套遍破坏的图要等到外层遍返回时才被拒，诊断点名的是**外层**那个遍；现在在嵌套遍返回的那一刻就拒，点名的是**真正改坏图的那一个**。用例 `pass_guard.nested_break`（`test/plans/feat/opt/pass_params.plan.toml`）断言的正是这条点名的差别。

### 2. A/B：关掉这条检查，坏图被晚一层拒、点名错人

两个跑法只差 `applyRegisteredPass` 里那四行：

```
（关掉 / A）[FAIL] pass_guard.nested_break
  output does not contain 'Pass 'test::break_exit' returned a program whose graph has no exit node…'
  [stdout] test::nested_break: the nested pass returned a program
  [stderr] [Error]: Pass 'test::nested_break' returned a program whose graph has no exit node…
  [SUMMARY] total: 60, pass: 59, fail: 1
（打开 / B）[PASS] pass_guard.nested_break
  [stderr] [Error]: Pass 'test::break_exit' returned a program whose graph has no exit node…
```

注意 A 里 `test::nested_break` **先打印了“the nested pass returned a program”**：坏图确实交回到外层手上，外层的检查随后拦住了它——所以这一格不是“漏过去”，而是“**晚一层、且点名错人**”。这正是审查清单里“子步之间不查”的真实代价：坏图在两层之间是活的，下一层若先解引用它，就是崩溃而不是诊断。

### 3. ①：注释改成实际做的事

`matchedEffects` 上方现在是：“键是 origin + 拷贝点 + 操作符，所以内联出来的拷贝**是**参与比较的；不进任何配对的是同 origin、同拷贝点、同操作符的克隆”。旧注释说“拷贝不比较”，比代码弱，且把审查清单引到了错误的方向。

## 三、还没补的，以及为什么

### ②/⑤ `var` 与句柄（原则 3.3 说每个资源都要保持顺序）

**做不到的原因不是“暂时没做”，而是身份：** 环境资源的身份是名字，两张图上的 `"stdout"` 是同一个资源；`var`、句柄、对象的身份是**位置/指针**，只在**一张图内**有意义。跨图比较时把两个不同图的 `var` 当成同一个资源，会报出程序从来没有排过的顺序——那比不查更糟。

**能做的窄版本**（下一笔，未做）：只在同一张图内比较非环境资源——`waitsFor` 已经要求 `earlier.graph == later.graph`（`:2044`），所以把非环境资源的键改成 `图 id + 资源名`，配对就自动限制在一张图内。**代价**：`collectEffects` 要为每个效应点收集全部访问（不再只收环境），全量快层的耗时会上涨，需要先量再加。

### ③ 非根图的入口出口

`entryExitViolation` 只查根图，理由是模块自留的图（规则库、抽出的体）**不必能单独跑**。要查“所有图”得先有“哪些图必须能跑”的判据——那是设计问题，不是补一行循环能解决的。注释已经写清它查哪一张。

### ④ 剩下的部分：`std::opt` 的 7 个子步

`std::opt` 今天的子步数与检查次数（`plain.cml`，`-vv` 实测）：

```
<Opt> std::opt::fold: 2 rewrite(s)      <Opt> std::opt::cse: 0 rewrite(s)
<Opt> std::opt::dce: 5 rewrite(s)       <Opt> std::opt::fold: 0 rewrite(s)
<Opt> std::opt::cse: 0 rewrite(s)       <Opt> std::opt::dce: 0 rewrite(s)
<Opt> std::opt: fixpoint after 2 round(s)
<Opt> std::opt::order: 0 rewrite(s)
```

**7 个子步（2 轮 × fold/cse/dce + order），检查跑 1 次**（`std::opt` 返回时）。这 7 步走的是 `simplifyGraph` 内部的循环，不是 `applyRegisteredPass`，所以第二节的修补**不覆盖它们**。要覆盖，得让 `simplifyGraph` 每轮返回时也走 `passBoundaryViolation`——代价是每轮一次 `collectEffects`（这棵树上一个程序约 40 个效应点），在 `std::opt` 的 16 轮上限下最多 16 倍。**这一笔没做，因为它需要先量全量耗时；先补的是那条“一次都不查”的路。**