# 让句柄的计数等于持有者的数目：加一加在哪里、减一减在哪里

- 日期：2026-11-15
- 状态：已量，未实现——只做「加一/减一」实测使每块的计数永远归不了零；在返回边界加清理点实测会打崩 defun
- 相关：原则 10.5（不可变值与 `Ref<T>` 句柄只按计数回收，计数归零立即释放）；裁定 `d052bcf23`、`bed72f456`

## 问题

原则 10.5 要求计数归零**立即**释放，且回收时机与后端无关。作者 `bed72f456` 的裁定把病因定在**计数不准**：
「存进第二个槽、被闭包捕获、放进聚合时计数没有加一」，所以计数从来不等于持有者的数目；返回边界的收集只是权宜
（时刻错、机制错——GC 只管图和快照，`Ref` 必须按计数）。修法是 Perceus 的准确计数：每多一个持有者 +1（槽存、
捕获、聚合存），持有者消失 −1（帧释放、槽覆盖、聚合释放），归零即释放；**不为每个槽分配 wrapper**；四个后端
调**同一组**核心内存层里的加减入口。

本页记两件事：**只做加一/减一实测是什么结果**，以及**在返回边界加收集点实测是什么结果**。

## 一、只做加一/减一：每块的计数永远归不了零

在核心内存层加了 `retainCountNow` / `giveBackCountNow`，在 `Frame::set` 里对句柄槽做「新的 +1、旧的 −1」，
在 `FramePool::release` 里对每个句柄槽 −1，没有为槽分配 wrapper。四后端读数（`--gc-check foreign`，返回时刻）：

| 用例 | 四后端一致读数 |
|---|---|
| `boxes_are_counted` | `created=1001 released=0 live=1001 finalized=0` |
| `boxes_loop_ten` | `created=11 released=0 live=11 finalized=0` |
| `boxes_loop_thousand` | `created=1001 released=0 live=1001 finalized=0` |
| `boxes_made_per_call_ten` / `_thousand` | `created=11/1001 released=0 live=11/1001` |

`value.counter_closure` 与 `value.ref_one_cell_two_holders` 同时变红（四个后端）。原因不是「减一不安全」，而是
**加一之后没有任何地方还把「产出这份值的那一个引用」还回去**：`ForeignResourceObject::create` 一出生就 retain 一次
（`refCount_` 起于 1），那份引用在值被存进槽之后就没有主了；槽上的 +1 只让它从 1 变成 2、3……，帧释放把它减回
出生那一份为止，永远到不了 0。也就是说：**加减的配对里缺的是「产出处的减一」，不是槽上的加一。**

要在运行时补上这一半，就得知道一次存进槽是**转移**还是**复制**——即编译器要按线性/唯一性发出 dup/drop。今天的
GIR/GCT 不发出这两种动作，运行时无法从一次 `Frame::set` 看出这一次存的是「新的一份」还是「原本那一份」。

## 二、在返回边界加收集点：defun 降级的程序第一次收集就崩

`NodeVMSchedPass::apply` 在入口图返回后请求并到达一次收集（`requestMajorCollection` + `safepoint`），三例读数
在四后端都变成 `released = created`、`live = 0`。但 `-Plan test/plans/feat/opt/defun.plan.toml`：

| 树 | defun 计划 |
|---|---|
| 基线 `6c64f2042` | 21 / 21 全绿 |
| 加了这个收集点 | 9 红、8 个 `killed by a signal` |

第一个红是 `defun.higher_order.lowered` 的 `std::nvm`，SIGABRT，诊断是参考追踪拒绝用一个 Variant 的类型：

```
[variant] the reference trace cannot use the type of a Variant: the type it was traced with has code
1610612746, which is not an enum's
[variant]   value=... type=... tag=2 fields=0
[variant]   the type reads as: (x: int) => int pure
```

即：defun 降级之后，一个声明为函数类型的槽里放的是带 tag 的 Variant，收集器按下沉到槽的类型（函数类型）去追踪它，
就撞上这条诊断。这个缺陷在基线上一句话也不说，因为基线在运行中没有收集点，这条路径从来没被走到——它被我的
收集点暴露出来，不是被我造成的。但它说明：**「在返回边界收一次」不是权宜，是危险**；而且任何让 defun 降级程序
真正发生一次收集的改动都会走到同一条路上。

## 今天的状态（已落盘）

- 返回边界的收集点**已撤掉**（`nodevm.cpp` 回到 `541ec5ff6`），退出时刻的收集也早在上一步撤掉（`loops.collect.oom`
  在堆耗尽时会从析构里抛 `MemoryExhausted`）。今天运行中没有回收点。
- 三例改断言**算式**：两个时刻都读，`created = released + live`、`created >= N`、`live <= 3`；每后端的读数写在用例
  头部：fvm/jit/prl 读 0，nvm 读 1 / 3 / 2（`boxes_are_counted` / `_ten` / `_thousand`）。nvm 的 1–3 就是「最后一个
  句柄随帧走了、之后没有任何收集点看过它」的那几块。
- 新加一对用例 `ref.boxes_made_per_call_ten` / `_thousand`（每次调用造一个盒子、调用返回时丢掉；入口在循环之后、
  返回之前读一次）：`created` 11 / 1001，`live` **2 / 2**（fvm、jit、prl），nvm 3 / 2——**不随调用次数增长**，这正是
  作者要的判据「live 为 0 或一个与次数无关的小常数」。

## 需要作者定的

1. 「产出处的那一个减一」由谁发：这是编译期的事（dup/drop，或一个「这一份值转移给槽」的分析），不是运行时能从
   `Frame::set` 推出来的。若按第 4 路排期，需要先定它落在一个 pass 里还是落在 GCT 的构造里。
2. defun 与收集器的这条冲突（函数类型的槽里放 Variant）本身要不要单独立项：它与计数无关，是「参考追踪按下沉的
   类型解释槽」的缺陷，任何一次运行中收集都会撞上。