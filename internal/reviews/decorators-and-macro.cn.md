# 装饰器的声明要不要带 `#macro`？

**状态**：按默认建议 B2 处理（2026-11-22 21:50，算子与事实车道 `lane/stage2-opsfacts` 撞点）。作者可见此页改判。

## 问

2.2 的第 ② 项要把「图上的 `#macro` 标志」从副本改成**推出**。五个写点、四种原因，前四格推出式与今天**逐格等值**，第五格不等：

`compile/src/gir/builder_decorators.cpp:98` **无条件**写 `#macro`，没有任何谓词。

三条已量的事实决定了它为什么没有见证：

1. **`@d f` 不是构造，就是一通普通调用。** `Builder::applyDecoratorAnno`（`builder_decorators.cpp:44-67`）造一个 link 节点、头是装饰器表达式、尾是函数的 dref，然后走 `visitLinkNode(linkNode)` —— 与任何 `d(f0)` 同一条路。编译器自己的诊断词就是这么写的（`kernel/core/error/diagnostics/messages/semantic.cpp:324`：`'@d func f' is 'let f = d(f0)'`）。全树没有名为 decorate 的算子。
2. **只有「按运行期应用的装饰器」才建工厂图。** `compile/src/gir/builder.cpp:1183-1189`：授权类装饰器在建体时已贴到体上，「the others make the decorated function」。所以第五格触发的就是 `@plain` / `@doubled` 这类**真的返回一个函数**的装饰器。
3. **语料里的装饰器绝大多数是 Camel 函数，没有算子记录。** `test/cases/compile/decorators/blocks.cml:14` 的 `func doubled(f: () => int): () => int`、`test/cases/compile/generics/err_decorated.cml:1` 的 `func deco(...)`。有记录的只有 `test_attrs:{mark,flags,level,plain}`（内核 `attrDecoratorIdentity`，`lib/std/builtin/modules/test_attrs.cpp:54-63`）与 `tiles:kernel`，而它们的记录是 `attrDecoratorTraits() = {.pure = true, .mayFault = false}`（`kernel/core/module/attr_decorator.cpp:36`）—— **不说 macro**，且它们是既可被装饰、也可被正常调用的恒等函数。

所以第五格标 `true` 不是**内容事实**，是**阶段事实**：「装饰器在宏期运行」。阶段没有内容见证，推不出来。

## 摆出来的三条路

| | 做法 | 代价 |
|---|---|---|
| **B1** | `attrDecoratorTraits()` 加 `.macro = true` | **创造一个新真相**：一次正常调用 `test_attrs:plain(f)` 也会变成宏调用，`std::macro` 的展开面变大。而且只覆盖「装饰器恰好是算子」的几个，`@doubled` / `@deco` 仍是 false ⇒ 第五格**半等** |
| **B2** | 装饰器的**声明**带 `#macro`，工厂图的 exit 于是是「macro 被调者的 CALL」⇒ 第五格并进第三格 | **一条规则、零新真相、零新内核**（`#macro` 已在 66 个 `.cml` 里写过）。代价：每个装饰器声明加一个词 + 一条「装饰器没写 `#macro`」的诊断；**要改 `test/cases/compile/decorators/**` 的用例写法** |
| **B3** | 留成「标志只在一处存活」的豁免 | 已被否决：完成判据是「各项只剩一处」，豁免就是把它改成「一处半」 |

## 默认建议：**B2**

理由：B1 为了让一格里有个见证而**造一个今天不存在的真相**，那比留一个已知的不等更坏。B2 用的是已经存在的词，只是要求写出来 —— 「装饰器在宏期运行」这句话今天只写在 `builder_decorators.cpp` 的一行代码里，B2 把它**搬到声明上**，与其他四处由声明推出。

B2 需要语言层多一条规矩：

> **声明了 `#macro` 的函数可以在装饰器位置使用；在装饰器位置使用的函数应当声明 `#macro`，否则诊断。**

这条不改变任何**现有**行为的求值结果（装饰器今天就在宏期运行），它只是要求那件事**被写下来**。`test/cases/compile/decorators/**` 的用例要加这个词 —— 那是用例写法的改动，不是断言的改动。

## 在这一条裁下来之前

- `of1` 的第二项停在第五格，**不造内核、不改 `builder_decorators.cpp`、不改语义**。
- 第五格今天仍然写 `true`（行为不变），只是**不能被推出式取代**。这记为一个已知的不等格，不是缺陷。
- 改前基线已备好，B1/B2 一旦落地即可逐字节对比：`std::macro` 提前求值 → `feat/trans/macro`；往返拒绝 `lib/std/cml/cml.cpp:768` → `feat/modules` 的 cml 往返用例；`lib/autodiff/pullback.cpp:1530/2648/2863` 的跳过 → `feat/trans/autodiff`。
## 决策者答（2026-11-22 22:05）

**三条都不选：删掉 `builder_decorators.cpp:98` 那一处无条件的 `#macro`。** 设计原则已经定了这件事：
- 6.1：`@d f` 就是 `let f = d(f₀)`，`d` 是普通函数；
- 第 1 节："简写（如装饰器）……只能是同一语义的另一种写法，不得另带规则"。

"装饰器在宏期运行"正是简写另带的一条规则，没有写在任何原则里，只写在建图器里。所以第五格不是"推不出来的阶段事实"，而是一处偏离原则的特例，删掉之后，它落进第三格的推出式：被调者是 `#macro` 的，这次调用才在 `std::macro` 里提前求值，否则就是普通调用。想让某个装饰器提前求值，就在它的声明上写 `#macro`——这是 `#macro` 本来的含义，不新增"装饰器必须带 `#macro`"的规矩，也不加诊断。

删掉之后，`test/cases/compile/decorators/**` 等用例里装饰器提前求值的读数会变，按原则那是改正：逐条看变化，求值结果应不变，变的只是在哪一遍求值；若有结果变了的，单独报给我。`macro` 的第二项照此收尾，"各项只剩一处"成立。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

已答（决策者 2026-11-22 22:05）：删掉无条件写的 `#macro`。依据原则 6.1"`@d f` 即 `let f = d(f₀)`"与第 1 节"简写……不得另带规则"。归档。
