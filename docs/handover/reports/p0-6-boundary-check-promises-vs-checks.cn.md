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
## 四、第二笔（2026-11-02，同一页往后写）

### ④ 的第二半：`std::opt` 的每个子步现在都查

第一节量过：`std::opt` 一个程序跑 **7 个子步**（2 轮 × fold/cse/dce + order），边界检查**跑 1 次**（`std::opt` 返回时）。第二笔把检查接在**每个子步返回的地方**——`rewriteReachableGraphs`（`src/passes/opt/generic/rewrite.cpp`），fold、cse、algebra、dce、order 全部从这里过，`session.commit()` 之后调 `checkPassBoundary(context, passName, *program, *committed)`。同一个函数，**第三个调用点**；它自己只多做一件事：把“改动前的程序”走一遍 `collectEffects`（管线那两处已经有这份读数，所以不重算）。

**代价**：每步多一次程序遍历（效应点收集），`std::opt` 一轮多 5 次。全量 fast 层仍然 `fail 0`（`[SUMMARY] total: 8358, pass: 8346, fail: 0`），门没有放宽任何一格。

**没有用例能红它，这句话要写在明处**：这个检查拦的是 `std::opt` 自己的重写步（fold/cse/dce/order），而它们由规则与 draft 事务产出，**case 文件里没有入口能喂给它一张排错顺序的图**——能触发它的只有“某条规则把效应挪过了另一个效应”，那不是用例，是规则自己的 bug，届时诊断会以 `PassInternalError` 点名 `std::opt::fold`/`cse`/`dce`/`order`。所以这一格我给的是**检查 + 无红可用例的解释**，而不是假装有一个红。第一节那条 `pass_guard.nested_break` 仍是 ④ 前半（`applyRegisteredPass` 那条路）的红。

### ②/⑤ `var` 与句柄：补不了，理由写在这里（不是“暂时没做”）

身份问题，不是工作量问题：环境资源的身份是**名字**，两张图上的 `"stdout"` 是同一个资源；`var`、句柄、对象的身份是**位置**，只在一张图内有意义。要在跨图的效应表里比较它们，得先有“哪两个 `var` 是同一个变量”的判据，而那正是 `108` 这类指针身份在图之间**不存在**的东西——拿它去配对，会报出程序从来没排过的顺序，比不查更坏。

**窄版本可以做的**（但我没做，因为要设计而不是补一行）：只在同一张图内比非环境资源，键写成“图 id + 资源名”（`waitsFor` 已经要求 `earlier.graph == later.graph`）。它要改 `collectEffects` 的取种条件（现在只收 `Ambient`），全量会变慢，并需要一个新的“同图资源”判据；这是下一笔的事，页里先记着。

### ③ 非根图的入口出口：补不了，理由在 `entryExitViolation` 原处

模块自留的图（规则库、抽出来的体）**不必能单独跑**，所以“所有图都要有入口出口”这条承诺本身就是错的；能查的是“程序自己的那张图”，代码一直是这么做的，注释现在也这么说。要查全部图，得先有“哪些图必须能跑”的判据——那是设计问题。

### 这一轮的一处自伤（记在这里，因为它和“仪器”有关）

`std::opt` 那一笔第一次进闸门时红了 `docs.headers.canonical`：我把 `#include` 插在版权头**上面**，两行头后面没有空行。同时 `scripts/headers.mjs --check` 还报了两份我上一轮用 PowerShell 追加过的文件——**CRLF**：Windows 上 `Add-Content` 写的是 CRLF，仓库要 LF。`node scripts/headers.mjs --fix` 之后 `--check` 全绿（“every one of 1525 member files carries the two-line header”）。**教训**：在这台 Windows 上改 C++ 文件，行尾与文件头都得当作被检查的东西，不是编辑器的事。
