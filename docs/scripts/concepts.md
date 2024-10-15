重点关注的关注：

## 深度学习编译器简述

[现在是不是编译器行业的风口时期？ - 知乎](https://zhuanlan.zhihu.com/p/674907063?utm_campaign=shareopn&utm_medium=social&utm_psn=1828848033145704448&utm_source=wechat_session)

>后期引入大量编译器的技术进行改进
>
>- • 表达上的改进([Pytorch](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=Pytorch&zhida_source=entity)/TorchScript、JAX)
>
>Pytorch的Eager Model是一种解决易用性的方案，虽然基本上还是图层和算子两层的抽象，但是整个语法基本上是Python Native的，让算法工程师比较容易上手；不过这个方案在运行的时候基于Python解释器的能力，不是一种高性能的解决方案，本身与神经网络的编译器关系不大；但是其表达的方式成为后面框架参考的标杆，图层的神经网络编译器主要就是考虑如何把这样表达转换到图层的IR进行优化，目前主要有两种方式：
>[AST](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=AST&zhida_source=entity)-Based：以Pytorch TorchScript为例，主要通过Python的修饰符，把Python代码的AST拿到，然后变换成图层的IR，进行[编译优化](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=编译优化&zhida_source=entity)。
>Tracing-Based：以JAX为例，主要把Python代码假执行一遍，保存[执行序列](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=执行序列&zhida_source=entity)，基于执行序列变换到图层IR进行编译优化。
>两种方案各有优缺点，第一种方案实现复杂，第二种方案在一些处理上有限制(比如控制流的处理)。
>
>- • 性能上的优化(XLA/TVM/TC)
>
>性能上的优化思路其实比较统一，就是打开图和算子的边界，进行重新组合优化。
>XLA：基本上的思路是把图层下发的子图中的算子全部打开成小算子，然后基于这张小算子组成的子图进行编译优化，包括buffer fusion、水平融合等，这里的关键是大算子怎样打开、小算子如何重新融合、新的大的算子(kernel)怎样生成，整体设计主要通过HLO/LLO/LLVM层层[lowering](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=lowering&zhida_source=entity)实现，所有规则都是手工提前指定。
>TVM：分为Relay和TVM两层，Relay主要关注图层，TVM主要关注算子层，总体思路与XLA是类似的，也是拿到前端给一张子图进行优化，Relay关注算子间的融合、TVM关注新的算子和kernel的生成，区别在于TVM是一个开放的架构，Relay目标是可以接入各种前端，TVM也是一个可以独立使用的算子开发和编译的工具（基于Halide IR，最新演进到自己定义的[TIR](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=TIR&zhida_source=entity)），TVM在算子实现方面采用了[compute](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=compute&zhida_source=entity)和schedule分离的方案，开发人员通过compute来设计计算的逻辑，通过schedule来指定调度优化的逻辑。
>TC(Tensor Comprehensions)：开发者发现算子的计算逻辑的开发是比较容易的，但是schedule的开发非常困难，既要了解算法的逻辑又要熟悉硬件的体系架构，更重要的是，前面提到图算边界打开后，小算子融合后，会生成新的算子和kernel，这些新的算子compute是容易确定的（小算子compute的组合），但是schedule却很难生成，所以传统的方法就是事先定义一大堆schedule模板，万一组合的新算子不在模板之内，性能就可能比较差，甚至出错；那TC则希望通过Polyhedra model实现[auto schedule](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=auto+schedule&zhida_source=entity)，降低开发门槛，当然这个项目基本已经停更了，但是类似的工作在MLIR、[MindSpore](https://zhida.zhihu.com/search?content_id=238109221&content_type=Article&match_order=1&q=MindSpore&zhida_source=entity)上还在不停发展。
>
>- • 图层和算子层的IR表达
>
>在神经网络编译器发展过程中，有多种IR的出现，各有特点：
>图层IR：朴素的DataflowIR、函数式IR、函数式图IR、SSA风格IR
>算子层IR：HalideIR、LLVM等
>图算融合表达：MLIR

## TVM简述

[【从零开始学深度学习编译器】一，深度学习编译器及TVM 介绍 - 知乎](https://zhuanlan.zhihu.com/p/358585143?utm_campaign=shareopn&utm_medium=social&utm_psn=1828855669954068480&utm_source=wechat_session)

## Torch编译的启发

### pytorch图分离技术

[【翻译】torch.compile 的详细示例解析教程 - 知乎](https://zhuanlan.zhihu.com/p/855291863?utm_campaign=shareopn&utm_medium=social&utm_psn=1828855602933276672&utm_source=wechat_session)

> 在我们理解了 `torch.compile` 作为Just-In-Time 编译器的全局图景后，我们可以深入了解它是如何工作的。与 `gcc` 或 `llvm` 等通用编译器不同，`torch.compile` 是一个领域特定的编译器：它只关注与 PyTorch 相关的计算图。因此，我们需要一个工具将用户代码分为两部分：纯 Python 代码和计算图代码。
>
> `Dynamo`，位于模块 `torch._dynamo` 中，就是用于完成这项工作的工具。通常我们不直接与这个模块交互。它在 `torch.compile` 函数内部被调用。
>
> 从概念上讲，`Dynamo` 做以下几件事：
>
> - 找到第一个不能在计算图中表示但需要计算值的操作（例如，`print` 一个张量的值，使用张量的值来决定 Pytho n 中的 `if` 语句控制流）。
> - 将之前的操作分为两部分：一个纯粹关于张量计算的计算图，和一些关于操作 Python 对象的 Python 代码。
> - 将剩余的操作作为一个或两个新函数（称为 `resume functions`）留下，并再次触发上述分析。
>
> 为了实现对函数如此细粒度的操作，`Dynamo` 在 Python 字节码级别上操作，这是比 Python 源代码更低的级别。

从前向图生成反向图

> 这个函数将从真实输入创建一些假张量，并仅使用元数据（形状、设备、数据类型）进行计算。因此，AOTAutograd 组件是提前运行的。这就是它得名的原因：AOTAutograd 是提前运行自动求导引擎。