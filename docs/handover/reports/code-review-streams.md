# 流（stream）实现的代码评审

范围：`git log -60 -- src include stdlib/stream.cml` 里与流有关的提交（主要是 `bf9a4208e`、`afa52e16e`、`d09604821`、`f1fa2bd23`、`124317bd3`/`eb391cd10`），以及现在树上的代码。读的是代码，不是提交信息。没有编译、没有运行用例；下面每条"失败场景"都是从代码推出的，写明了"需先写用例确认"的除外。行号是当前树（`2c0f893d8`）上的。

涉及的文件：`src/compile/gir/builder_control.cpp`（`buildStreamLoop` 3485–3815、`consumeStream` 3819、`checkStreamConsumption` 3841–3908、`visitLoopNode` 2660 起）、`src/compile/gir/builder_operators.cpp`（3280–3290、3387–3425）、`src/compile/gir/builder.cpp:378`、`src/core/type/composite/stream.cpp`、`func.cpp:378`、`include/camel/core/type/composite/stream.h`、`src/core/type/typevar.cpp`、`src/compile/gct/builder.cpp`（2041、5424、5962、6015）、`src/builtin/operators/stream.cpp`、`stdlib/stream.cml`、`src/core/error/diagnostics/messages/semantic.cpp:1576–1610`。

## 总评

`for` 展开成已有的 `while`、不新增节点种类，这个方向是对的，生成图的形状也有测试钉住。问题集中在三处：（1）"每条路径一个消费者"只检查了两种使用（`for` 和 `s()`），线性是靠**枚举使用方式**实现的，不是类型的性质，漏洞很多；（2）`buildStreamLoop` 把数组循环的一整套机件复制了一遍，又加了新的几份"建元组"；（3）几处绕开既有机制的做法（按名字认库函数、运行时读静态类型、用对象地址当键）。

## 严重（会让线性规则静默失效，或让别的错误被盖掉）

### 1. 线性只检查 `for` 与 `s()` 两种使用，其余使用全部放过
`builder_control.cpp:3499`（`for`）与 `builder_operators.cpp:3285`（调用）是 `consumeStream` 仅有的两个调用点。下面这些都是同一条流被第二次消费，却不报：

- 作实参传给函数：`let a = total(s); let b = total(s)`（`total` 的参数是 `int..`）。`tee` 本身就是这样用 `s` 的。
- 返回、放进元组/结构体：`let t = (s, s)`，再 `t.0`、`t.1` 各消费一次——`accs` 节点是新节点，`second.value != first.value`，逃过。
- 赋给函数类型：`func.cpp:378` 允许流进入 `() => (T, T..)?` 的槽，之后 `f()` 是普通函数调用，不走 3285 的流分支。`let f: () => (int, int..)? = s; f(); f()` 重放同一个元素（值语义下每次都是同一个）。
- 被 lambda 捕获：`let g = () => s()`，`g` 可以调用任意多次；设计原则 5.4 写明"lambda 不能捕获流"，代码里没有任何检查（`grep -i capture` 流相关为空）。

失败场景：`let (a, b) = (count(3), count(3))` 没问题；但 `let s = count(3); let x = sum(s); let y = sum(s)` 编译通过，`x == y`，而语义上第二次读的是已经读过的流（对有效应的来源，如 `fs::read_lines`，是把文件读两遍，或在来源已关闭时读到错误）。

修法：线性应当是**类型的性质**，不是对使用方式的枚举。给流类型（以及包含流的元组/结构体）标 linear；建图完成后统一数每个 linear 类型节点的**出边**（每条路径上的数据使用：实参、返回、字段、捕获、循环状态），多于一次即 `StreamConsumedTwice`；捕获一律拒绝。现在的 `consumeStream` 两个调用点和 `streamConsumptions_` 的手工登记就可以删掉，`arm` 互斥的判断改成"出边在不同分支臂里"，用图上已有的支配/互斥关系，不另维护 `armGroups_`。

### 2. 循环状态里把流原样交回，不算"没前进"
`checkStreamConsumption` 对循环区域只检查"被消费的值是区域的端口"（3855–3866），不检查下一轮状态里放回去的是**拉取答复里的 rest**，而不是原来的流。`test/cases/compile/stream/loop_state_consumes.cml` 的 `case null => (round, rest)` 就是把已经拉过的 `rest` 放回状态；对非空分支写成 `(round + 1, rest)`（笔误，忘了 `p.1`）会编译通过，运行时每一轮都读同一个第一个元素，条件靠 `round` 才终止。
失败场景：`loop round = 0, rest = first while round < 2 do match rest() { case null => (round, rest)  case p => (round + 1, rest) }`——两轮都拿到同一个 `p.0`，且没有任何诊断。
修法：同第 1 条——这是同一个线性问题：`rest` 在拉取后已被消费，再作为下一轮状态使用就是第二次使用。统一的出边计数会直接抓到。

### 3. `leaveScope` 的异常路径调用 `checkStreamConsumption`，会盖掉原来的错误并报假的"消费了两次"
`builder.cpp:378–382`：`leaveScope()` 现在无条件调用 `checkStreamConsumption`，而 `builder_calls.cpp:568`、`builder.cpp:1120/1133`、`builder_operators.cpp:755` 等处在 `catch (...)` 里调用 `leaveScope()`。检查可能 `throw BuildAbortException`，于是它在 catch 块里抛出，**替换**正在传播的异常。
- 被盖掉的包括 `MemoryExhausted`（`builder.cpp:1120`：先 `leaveScope()` 再 `throw e.during(...)`，内存耗尽的报告被一条流诊断替代）。
- 假阳性：`if c then s() else <这里编译出错>` ——互斥分支是在 join 建完后才登记的（`noteArmGroup`，`builder_control.cpp:2507`），中途出错时第一臂的使用已经记下、分组还没有，检查看到"同一条流在两处被消费"，报 `StreamConsumedTwice`，真正的错误丢了。
修法：检查只在**正常完成**的图上做（放到函数体建完的那一处，不放进 `leaveScope`）；若保留在 `leaveScope`，异常路径跳过它。这条与第 1 条的修法一并解决（图建完后的统一遍）。

### 4. `StreamInContainer` 只检查写出来的类型，推断出的容器不检查
`gct/builder.cpp:5424`、`5962`、`6015` 都是在语法层看"写在类型位置上的 `T..[3]`、`Map<K, T..>`、`Set<T..>`"。数组字面量与泛型实例没有经过这些点：`let xs = [count(3), count(3)]`（推断 `int..[]`）、`ys->map<(n: int): int.. => count(n)>`（结果是 `int..[]`）都构造出"流的数组"，`xs[0]` 可以读任意多次。
修法：在类型的构造处拒绝（`ArrayType`/`CollectionType` 的元素类型检查，或统一的"元素类型合法性"函数），三个语法层检查随之删除；一条规则一个地方。

## 高

### 5. 编译器按名字认库函数：`tee` 的特判
`builder_operators.cpp:3387–3425`：在一般调用路径里写了 `baseGraphName(targetGraph->name()) == "stream::tee"`，还靠 `name.find('<')` 剥掉实例化后缀，再用 `dynamic_pointer_cast<LongData>/<IntData>` 判常量，报 `TeeHintNotPositive`。
- 这是绕开机制：库函数的前提应当写在库里（`n: int` 的约束，或函数体里 `fail<'InvalidArgument'>`），或者用语言的通用常量检查，不该让通用的调用构造器认识 `stream::tee`；同名的用户函数在另一个模块里叫 `stream::tee` 时也会被误伤（名字前缀相同的用户模块）。
- 语义不自洽：`stdlib/stream.cml:57` 只钳上限（`if n > cap`），`n < 1` 的非常量实参静默通过，常量 0 反而编译错误；诊断文本自己写着"它是个提示，永不故障"（`semantic.cpp:1604`）。
- 更根本：`n` 对行为**没有任何影响**（只进性能报告 `tee_note`），却成了公开参数、专属诊断、`StreamConsumedTwice` 帮助文字里的推荐写法（`tee(s, n)`）。
修法：删除特判和 `TeeHintNotPositive`，删除 `n`（缓冲本来就无界），或者让 `n` 真的成为背压（缓冲满了领先方等待）再谈参数。

### 6. `tee` 的缓冲只增不减，是内存泄漏
`stdlib/stream.cml:72`：`buffer = buffer->append<p.0>`，没有任何地方丢弃两边都读过的前缀（`taken` 记了位置，只用来算 `peak`）。两边步调一致读一个 10 GB 的文件，缓冲会长到 10 GB。设计稿写的是"背压是拉取模型自带的"，这里恰好破坏了它。
修法：按 `min(taken)` 丢弃前缀（用偏移量表示 `at`），或用环形缓冲；加一个"长流 + 两边同步读，内存不增长"的用例。

### 7. `FunctionType::assignableFrom(Stream)` 的子类型方向看起来反了
`func.cpp:383`：`return stepType()->assignableFrom(this)`。要问的是"这个函数槽能不能装下这条流"，即 `this->assignableFrom(stepType)`；而 `StreamType::assignableFrom(Function)`（`stream.cpp:97` 附近）写的是 `stepType()->assignableFrom(sourceType)`，那个方向是对的。`func.cpp` 这一份像是照抄来的，方向没换。两个类型相等时看不出来，返回类型有隐式进入（`int` 进 `int?`）时结果会相反：`() => (int?, int..)?` 的槽装 `int..` 会被拒，而该槽的类型本来可以接受它。**需先写用例确认**（一个返回类型更宽的槽接收 `int..`）。

### 8. `buildStreamLoop` 复制了 `visitLoopNode` 的机件
`visitLoopNode` 本身有 800 行（2660–3462）；`buildStreamLoop` 又是 330 行，其中大量是数组循环的复制：
- `refreshPorts` / `declarePorts`（2892/2906 对 3581/3594）逐行平行，只差端口列表；
- 状态强转（`intoState`，3535 对 2965–2977 的 `entersImplicitly + coerceArmExit`）同一规则两份实现，且 `intoState` 返回 `nullptr` 后 3688 又发一遍同样的 `LoopBodyTypeMismatch`（同一诊断两处发出）；
- "用静态空元组做源、再把各分量接到 `Fill`"这段（建元组）在本文件出现 **6 次**：2649、3217、3398、3555、3788、4194（`makeFillPayload(runtime::GCFillKind::Tuple` 搜索）。应当是一个 `makeTuple(type, components, origin)`；
- 循环节点的组装（`addRegionLoopNode` … `setRegionOwner` … `mergeArmVariables` … `pendingIgnoredWrites` … 同步链）3720–3790 与数组形式 3330–3400 平行。
修法：把"两种迭代源"的差别（每轮怎么取元素、状态怎么前进）收成一个小接口，`visitLoopNode` 负责骨架一次；把 `visitLoopNode` 按阶段拆开（解析头、建条件区、建体区、装配节点、拆状态）。

## 中

### 9. 魔数与字符串名耦合状态布局
状态机布局是 `($stream, $done, state...)`，代码里到处是 `i == 0 ? streamName : (i == 1 ? doneName : stateNames[i - 2])`（3586）、`addAccsNode(stateTypes.front(), 2)`（3790 附近）、`j + 2`、`addAccsNode(elementType, 0)`、`addAccsNode(stream, 1)`。改布局要同时改六处。应当有一个 `struct StreamMachine { enum Slot { Stream, Done, FirstState }; ... }`，或者把机器状态做成命名字段。`"$stream"`、`"$done"` 也是靠"程序写不出这样的名字"这句注释保证不冲突，应当用不可写出的内部名常量。

### 10. `consumeStream` 对 `nullptr` 静默返回
`builder_control.cpp:3820`：`value == nullptr || currGraph_ == nullptr || at == nullptr` 直接 `return`——一次消费没被记录，线性检查就悄悄漏过。三者都不该为空，应 `ASSERT`；真有合法的空路径要写在调用点。

### 11. 检查与登记的数据结构
- `streamConsumptions_`、`armGroups_` 是 `Builder` 的全局向量，从不清理，也不按图分桶；`checkStreamConsumption` 每离开一个图都扫一遍全部登记（O(图数 × 登记数)），再对同图登记做 O(n²) 配对。编译一个大程序时是二次的。
- 登记里存 `DraftGraphBuilder*` 裸指针当图的身份；图销毁后地址可能被复用，新图会读到旧图的登记。
- `regionOf` 用 `0` 当"没有"（3872–3880），区域编号若有合法的 0 就会误判为"不互斥"，报假的 `StreamConsumedTwice`。用 `std::optional<uint32_t>`。
- 登记里每个 `ArmUse` 复制整个 `ports` 向量。
修法：随第 1 条改成图上的出边计数后这些都不存在；若保留，按图存放并在检查后清掉。

### 12. 状态为空的 `for` 对流给出自相矛盾的诊断
`builder_control.cpp:2869`：`... == TypeCode::Stream && k > 0`。`k == 0`（`loop for x in s do …` 没有状态）落到数组路径，找 `__iter__`，找不到就报 `LoopSequenceNotIterable`，而这条诊断的文字（`semantic.cpp:603`）写的是"`for` 接受数组、字符串、映射、张量或**流**"——对一条流说"流可以"。要么 `k == 0` 也走流循环，要么给出专门的诊断。**需先写用例确认**（`loop for x in s do println(x)`）。

### 13. `tee_note`：运行时读静态类型，用对象地址当键
`src/builtin/operators/stream.cpp`：`norm.type(4)` 在运行时取实参的静态类型来算元素名与字节数——设计原则 5.5 写明"原生算子的内核不在运行时读取调用的静态类型；需要按类型变化的，在编译期展开"。又用 `reinterpret_cast<const void*>(norm.slot(4))`（流值，即闭包指针）当报告的键；分代 GC 会移动小对象（10.5），同一个 `tee` 在两次拉取之间可能换了地址，报告里变成两条。
另外 `stdlib/stream.cml:95` 的 `tee_note(...)` 每拉一个元素都调用一次（参数 `len(buffer)` 等先求值），报告关着时也付调用成本；"一次加载就返回"是 C++ 一侧的，调用本身不免。
修法：元素名与大小在 `tee` 实例化时作为编译期常量传入（`T` 已知）；键用 `tee` 调用处的稳定标识（调用点的源位置）而不是对象地址；整个报告钩子放到 `profile` 打开时才生成（`when` 或编译期开关）。`bytesOfElement` 重复了数组元素大小的已有知识（数据块的元素宽度），应调用那一份。

### 14. 两个类型遍历漏了流
`typevar.cpp:264` `writesResourceClause` 与 `:332` `findRepeatedMark` 对 `TypeCode::Array` 有分支、对 `Stream` 没有；其余 8 个遍历补了。后果：元素是带资源子句的函数类型的流（`(() => int uses fs)..`）不被识别为"写了资源子句"；`int??..` 这样的重复标记穿过 `findRepeatedMark`。更根本的是"每加一种复合类型，要在十几个 `switch` 里各补一条"（`TypeCode::Array` 出现在 44 个文件，`Stream` 在 13 个）——补漏就是这样漏的。给 `CompositeType` 一个"组件列表 / 映射组件"的虚函数，遍历就不再枚举类型。

### 15. `tee` 读到末尾后每次重新拉源
`stdlib/stream.cml:68–74`：`pull()` 在源结束（`src()` 为 `null`）时返回 `false`，但**不记录"已结束"**，`src` 不变。之后每次读都再次 `src()`。纯来源无所谓；有效应的来源（`read_lines`）会被再次打开读。应在 `var` 里记一位 `done`。

## 低

### 16. 诊断质量
- `StreamConsumedTwice` 的消息（`semantic.cpp:1586`）是 `'{0}' is consumed twice`，`{0}` 填的是**类型**（`builder_control.cpp:3902` 的 `valueType->toString()`），用户看到 `'int..' is consumed twice`，不知道是哪个变量。应当填源码里的表达式文本（`sourceTextOf` 已有）。
- 帮助文字写"The second consumer is here and the first is here."，位置靠 `relatedOrigin` 附带；文字里的 "here" 在纯文本输出里没有指向。
- `StreamConsumedInLoop` 与 `TeeHintNotPositive` 的帮助是整段设计说明（后者 600 余字符，还把"为什么 n 只是提示"讲了一遍）。诊断应当短：规则一句、改法一句，其余引用设计原则小节。
- 在 `for` 体里又拉同一条流报的是 `StreamConsumedInLoop`（"每轮消费一次"），不是 `StreamConsumedTwice`，对这种情形更准的话是"这条流已由外层 `for` 读取"。

### 17. 注释
- `stream.h:17` 与 `stdlib/stream.cml:6` 带日期（"the rule of 2026-10-24"、"the reading of 2026-10-26"），是在引用裁定记录；应写规则本身并引用设计原则小节。
- `gct/builder.cpp:2041–2050` 的 `yield` 拒绝注释讲"slice E … is not built yet"，是计划状态不是规则；留着的拒绝也应当只说"`yield` 的降低（状态机）见 `docs/design/io-concurrency-streams/`"。
- `builder_control.cpp:3470–3482` 的函数头注释对机器状态的描述（"state (next, acc)"）与实际布局 `($stream, $done, state...)` 已不符（早期版本留下的）。
- `buildStreamLoop` 内"A `for` over a stream is one consumer of it (design principles 5.4 …)"之类对原则的引用是对的，保留。

### 18. 死代码
- 3726/3734 的 `if (condPart)`：本函数里 `condPart` 一定非空（3640 起必建），两个分支判断是复制数组形式时带过来的。
- `refreshPorts` 只被 `declarePorts` 调用一次，可内联。
- 测试文件里仍有 `module stream_consumed_twice` 这样的 `module` 行（文件即模块，名字来自文件名）。

### 19. 性能（记录，不要求现在改）
每个元素：一次拉取的结果元组（`(T, rest)`）、`unfold` 里新建的闭包、循环每轮重建机器状态元组（`machineStateOf`）——至少三次分配；`unfold` 的 `match step(state)` 对同一条流值每次拉取重新计算（值语义的代价）。`stdlib/stream.cml` 的 `tee` 每元素约十个节点加若干 `var` 读写。设计稿说"生产者可见时融合（把 `step` 内联进循环体）"，现在没有；先量再做，但用例里应当有一个"一百万个元素的 `unfold` 求和"的基线，别等别人量出来。

## 缺的测试（按上面的发现）
1. 同一条流传给两个函数；放进元组后两侧各读一次；赋给函数类型后调用两次；被 lambda 捕获。
2. 循环状态里把已拉的流原样放回。
3. `[count(3), count(3)]`、`xs->map<…返回流…>` 的容器。
4. 分支另一臂出错时的诊断仍是那个真正的错误（第 3 条）。
5. `loop for x in s do …`（无状态）。
6. 返回类型更宽的函数槽接收流（第 7 条）。
7. `tee` 两边同步读长流的内存、源结束后的再拉取次数。
