# 类型表归谁：一个程序一份，还是进程一份？

- 时间：2026-11-27
- 缘起：`D-1127-1` ④（`--timeit` 每轮重建 `Context` 却留着上一轮的签名）。命令行车道读了实现后判断：这条**不能在命令行内闭环**。
- 相关：`internal/backlog.md`（`task-97`：同一进程第 2 次迭代红）、`src/stdlib/test/contexts.cpp:19-22`（"第二个 context 永远不会被销毁"）

## 读到的现状

```
src/kernel/include/camel/core/type/intern.h:116   class TypeTable
                                            :145   `static TypeTable &instance()` —— **进程一份、永不释放**
                                            :131   `TypeVarType::create` 按**名字** intern（Shape::TypeVar）
                                            :130   函数类型（"签名"）进同一张表（Shape::Function）
src/tools/camel-cli/timeit.cpp:450-453      每轮 `make_unique<RunInvocation>(…)`、`:243-264` 里 `Context::create` ⇒ Context 确实每轮重建
src/stdlib/test/contexts.cpp:19-22          项目自述：module 强指回 context ⇒ 第二个 context 永不销毁
```

⇒ 后果：**第二个 `Context` 必然继承第一个的名字**（按名字 intern 的类型变量、函数类型）。最小复现：`map<func>` 三行 + `camel --timeit std::nvm/2` ⇒ 第 2 轮 `NoMatchingFunction … <(i: int) => int pure> (Range)`。

## 两种修法为什么都不在命令行里

```
「整个复用」：`--timeit` 的语义是每轮从**一张全新的图**跑那一个 pass，而每轮结束后 program 已被 pass 消费
            ⇒ 复用同一 Context 就要复用同一张图，等于换掉 `--timeit` 的定义
「整个重建」：要求每轮把进程级类型表复位，而 `TypeId` 已写进 operand 表、内置类型、图里的类型引用
            ⇒ 那不是隐藏状态清理，是**内核生命周期重构**
```

## 问句

**类型表应当归谁所有？**

- 甲（默认）：**归 `Context` 所有，每个程序一份**。`TypeId` 的编号空间本来就随一次编译走；进程一份只是"省一次分配"的早期简化，代价是第二个程序继承第一个的名字。
- 乙：仍进程一份，但**按 (context, 名字) 复合键 intern**。改动小，可是"同一个名字在两个程序里是不同东西"这条事实被藏进键里，生命周期缺陷还在。
- 丙：进程一份 + 每轮显式 `reset()`。等于承认表是全局可变状态，谁忘了重置谁踩雷；`--timeit` 之外的第二个 Context（测试框架、语言服务器）仍会踩。

## 默认做法（未裁定前）

按 **甲** 走：内核车道把类型表改成随 `Context` 生灭；命令行格只保留"复现 + 两轮读数"，`perf-baseline` 那条红等内核修好再关。
如果裁定为乙或丙，只有内核那一层的落点变，判断与读数不变。

## 裁决（负责人，`D-1127-1` ④）

```
④ 同意甲，不改身份口径；真问题在 `--timeit` 每轮重建 Context 却留着上一轮的签名
   —— 一次迭代要么整个重建（不留任何跨迭代状态），要么整个复用，这是工具缺陷，
   归命令行那格（bench 子命令那步）一并修，perf-baseline 的红随之消
```
⇒ 但命令行车道读实现后确认：**"整个重建"要动进程级类型表，命令行内做不干净**。因此这条问句仍需一句落点确认：
要么按甲开内核车道（类型表随 Context），要么改成乙（复合键 intern）。在拿到落点前，命令行格只保留复现与读数。