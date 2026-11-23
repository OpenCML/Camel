# 一个 Oper 的身份是 payload 里的 URI，还是算子表查出来的名字

## 事实

`lane/packed-view-lifetime` 合并 `bdd497756` 之后，`grad_captured_var_written` 的四条用例（`.default` / `.macro` / `.macro.fvm` / `.macro.opt`）稳定地停在：

```
../src/stdlib/std/builtin/operators/struct.cpp(1568)
Function: slot_t __access_value__(ArgsView &, ArgsView &, Context &)
Guarantee: norm.size() >= 2
Reading: a member read must have a position and a value.
```

12 次运行（四个 pass 组合各三次）逐字相同，不再出现跑一次 `abort`、跑一次算出 `70.0` 的摇摆。停在拒收上不是退步：摆动说明它读的不是这个值，而拒收是一条有名字的读数。

拒收发生在一个 `grad(loss)` 里的节点上：

```
kind 11 (Oper) · URI :op/access · 普通操作数 **1 个**（节点 15，数据槽 14）· 结果槽 6 · origin 0
```

同一个程序里 `main` 的同一种读带着它的位置常量、一路执行正确。

## 这个缺陷的证据是六个零读数

零读数的位置就是证据。为找出「2 个操作数在哪一步变成 1 个」，按 URI 与按输入个数在四条路上挂了钩，读数如下：

```
GraphDraft::addOperNode      URI = :op/access                          0 行
GraphDraft::addNode          payload URI = :op/access                 0 行
GraphDraft::rebuildNode      URI = :op/access  且「旧 2 → 新 1」      0 行（后者对整个程序的任意 Oper 都是 0）
GraphDraft::replaceUsesInList  改前/改后输入个数                      0 行差异
GraphDraft::createDecodedNode  冻结图 loss 的两个 access               **2 个**（kind 0 Data + kind 10 Func）
编译期 builder_control.cpp:162-165  addOperNode → place → value        **2 个**
```

这不是「没找到」，是把四条路排除了：**draft 里没有任何一个节点在建造时说过自己是 `:op/access`**，而它在执行期却按这个名字执行。于是名字不是建上去的，是**事后在原地写进 payload** 的（`draft_writes.cpp` 的 `case GCNodeKind::Oper` 一类路径；本次没有在那里读数，留给另一个车道）。

## 需要裁的三件

### ① 一个 `Oper` 节点的身份，是 payload 里的 URI，还是算子表按 `body.oper` 查出的名字？

两者可以不同：`writings_match.cpp` 造 Oper 节点时只写 `body.oper`，payload 里没有 URI。若身份是 payload 里的 URI，那这些节点在建造时**没有身份**；若身份是算子表查出的名字，那 payload 里的 URI 就是一份可能与它不一致的副本，而这份副本在执行期被当作权威读。

两者什么时候必须一致？不一致时以谁为准？

### ② `writings_match.cpp` 造 Oper 时只写 `body.oper`、不写 uri，是有意的还是漏的？

若是有意的，「按 oper id 活」是契约，那么「URI 为空」是一种正常状态，读 URI 的人必须先查表；若是漏的，那是一次静默的身份缺失，每个读到空 URI 的地方都在读一个未定义的值。

### ③ 运行时一个**成员读**要几个操作数？位置常量在运行期还要不要带？

```
编译期: 2 个（位置常量 + 值）
decode: 2 个（原样搬运）
执行期: 1 个，而 __access_value__ 要求 2 个,于是拒收
```

**而没有人报「少了」。** 从 2 变成 1 的这几步里，没有任何一处失败、断言或日志认为数量不对 —— 那说明有一处认为 1 个是对的，而它赢了。

同一条红里，四处认为该有 2 个，而有一处认为 1 个就够，而它赢了 —— 因为它掌握着重建。

这一点比「搬运丢了一个操作数」更值得先裁：前者是某一处写错了，后者是**两处对同一个约定的理解不一致**，而修法完全取决于哪一边是契约。若裁决是「运行时的成员读不带位置」，那 `__access_value__` 的 `norm.size() >= 2` 与四处「2 个」的读法都要跟着改；若裁决是「必须带」，那要在**造之前**拒绝一个 1 操作数的成员读，而不是在执行期拒收。

## 不裁会怎样

- 现在这条红是稳定的拒收，可以不修就交付一个「已知红」。
- 但同一个不一致会以别的形状再出现：任何**运行时新建/改写出来**的成员读都会走到这里，而它们的操作数个数由各自的构造者决定，没有任何一处断言把关。
- 而 `isView()` 静默读一个既不是 Tuple 也不是 Struct 的值的头字，是这条红的另一副面孔：一个值到了不该到的地方，没有人问它为什么在这。