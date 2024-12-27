## Bend

最近有个比较离谱 [Bend 语言](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=Bend+语言&zhida_source=entity), 就是纯函数式的, 号称**所有能够并行的一定会[并行执行](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=并行执行&zhida_source=entity)**

[https://github.com/HigherOrderCO/Bendgithub.com/HigherOrderCO/Bend](https://link.zhihu.com/?target=https%3A//github.com/HigherOrderCO/Bend)

语法类似 Python, 但是加入了一些纯函数式的特性, 基于 HVM2([第二代高阶虚拟机](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=第二代高阶虚拟机&zhida_source=entity), Higher-order Virtual Machine 2).

![img](https://picx.zhimg.com/80/v2-b3487f912cca030ace3f8b6e1c503cc1_1440w.webp?source=2c26e567)

可以用 AVX 或者 [CUDA](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=CUDA&zhida_source=entity) 作为后端, 自然地在 CPU 和 GPU 上大规模并行上运行，基于核心数实现近乎线性的加速.

**且没有任何显式并行注释, 不需要线程, 也没有锁、互斥、原子等等过程式的糟粕。**

而且 Bend 并不局限于特定计算范式, 如[张量计算](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=张量计算&zhida_source=entity)或者[多面体循环优化](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=多面体循环优化&zhida_source=entity)这些, 用户压根不需要懂优化。

任何并发系统，从 Shader 到 Actor 再到 Stackless Coroutine，都可以在 Bend 上自动并行, 即便是复杂的算法也能跑的很好。

背后这个逆天的理论被称为 **[交互式组合子](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=交互式组合子&zhida_source=entity) (IC, Interaction Combinators**[[1\]](https://www.zhihu.com/question/646234682/answer/3517299156?utm_campaign=shareopn&utm_medium=social&utm_psn=1849756974457171969&utm_source=wechat_session#ref_1)**)**, 该理论可以追溯到 1997 年的一篇论文.

而且按照这篇论文的描述, 它是一种[组合子逻辑](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=组合子逻辑&zhida_source=entity), 也就是说这个东西可以简单地就扩展到无状态[分布式网络](https://zhida.zhihu.com/search?content_id=669579638&content_type=Answer&match_order=1&q=分布式网络&zhida_source=entity), 可以说这场革命才刚刚开始.

Bend offers the feel and features of expressive languages like Python and Haskell. This includes fast object allocations, full support for higher-order functions with closures, unrestricted recursion, and even continuations.
Bend scales like CUDA, it runs on massively parallel hardware like GPUs, with nearly linear acceleration based on core count, and without explicit parallelism annotations: no thread creation, locks, mutexes, or atomics.
Bend is powered by the [HVM2](https://github.com/higherorderco/hvm) runtime.

我的评论：他直接面向GPU底层做高并发，粒度非常细，并不开放底层。语法像python是想拉拢现有生态，和nodejs作比较，那我就加速nodejs。

## OCaml

OCaml，这门充满魅力的编程语言，以其独特的特性在编程世界中独树一帜。它摒弃了monads的复杂性，让编程回归本质，清晰明了。在OCaml的世界里，程序员能够直接与类型系统对话，无需通过monads的迷雾来理解程序的流程控制。这种简洁性使得代码更加直观，易于理解和维护。

OCaml的纯[函数式编程](https://zhida.zhihu.com/search?content_id=325419118&content_type=Answer&match_order=1&q=函数式编程&zhida_source=entity)范式，让开发者有机会深入挖掘程序的性能潜力。在不引入副作用的情况下，OCaml的编译器能够进行更加激进的优化，从而生成更加高效的机器码。这种对极致性能的追求，使得OCaml在需要高性能计算的领域，如金融、科学计算和系统编程中，展现出了其独特的价值。

- 函数式语言，ML系主力
- 模式匹配很好用，自动类型推导底子深厚



## Python



## JS/TS

- 对象类型写起来很爽
- 类型系统不如C++，但很够用

## Java

- 完全地面向对象，冗余臭长，适合严谨的工业界

## Dart

## Kotlin

## C/C++

- 底层语言，不必和它比表达力

## Golang

- 天然协程
- 错误处理机制值得借鉴

## Erlang



## Rust

- Option设计值得借鉴
- Rust 是一门系统编程语言，但它也汲取了许多函数式编程语言（如 Haskell、Scala 等）的特性

## MoonBit

[Introduction — MoonBit Document v0.1.20241216 documentation](https://docs.moonbitlang.com/en/latest/language/introduction.html)

自称从全方位角度考虑和AI的结合，更好地支持AI编程。

从语法上看从rust借鉴了很多。是rust，js，go的混合物（和我审美很像），可能是rust with gc。

明确提出综合考虑IDE开发工具的需求来设计语法特性。

诞生2年左右，从GPT出现后开始研发。

团队效率很高，已经开始打造全球影响力。3个月2k提交，逆天。

其主要推动者似乎也是Rescript的作者。OCaml前核心开发成员。

说的很玄学，所谓“用静态分析加快开发过程，提高安全性”，强调编程语言与AI的交互及其安全性。

全程强调所谓的“容易被机器理解”，其实就是静态类型分析。

好像底层是基于wasm？？？解决问题的出发点是在wasm平台上实现物联网的开发？？

原来是wasm gc语言，同一生态位的还有dart，kotlin。

看样子是个多后端的转译语言。这不是kotlin吗？然后来蹭AI热度？

## Clojure

没搞懂 ，语法难看。