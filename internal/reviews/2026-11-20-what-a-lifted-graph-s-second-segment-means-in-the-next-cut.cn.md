# 一张提升图的第 2 段，在下一刀 `split` 里是切向还是值

**问句（一句）**：一张**提升图**（`jvp` 的产物，签名是 `Lift<F>`，入口是 `(值, 切向)` 两段）作为下一刀 `split` 的输入时，它入口的第 2 段在**这一刀里**算**切向**还是算**值**？两条法则在同一个入口上撞：`split` 的「值半不声明切向端口」与「`jvp` 的输出第 2 段按类型就是切向」。**这一句该由类型法则回答，还是由 `split` 的入口划分回答？**

## 默认建议（本轮按此开工）

**「是切向」—— 类型已经说了。** 决策者 `283abc77b` 的裁定是 *no lifted flag, **the type carries it***：一张 `Lift<A>` 的入口第 2 段，按类型就是 `Tangent<A>`，下一刀**不该把它重读成值**。所以 `split` 该做的是**把那个提升调用的切向实参留在切向半**，而不是把它拉进值半去复制。

⇒ 若这条默认成立，落点在 `split` 的**边划分**：**切向入口的读者若落在值半，那个读者本身要出现在切向半** —— 一处，仍在一格内。这与本页 §二 的读数一致：那个读者本来就是法则层的切向算子，它没有「值版」。

## 一、读数一：读切向入口的那个节点

`nested` 的三条失败图（被割的图就是上一刀的前向半步），逐字：

```
TANGENTPORT graph=jvp_of_split_of_jvp_of_cube__first_with_as_param_forward ref=1 index=1 half=1 norm=2
             tangent=0 portOfThisSlot=0
TANGENTREADERS graph=jvp_of_split_of_jvp_of_square__first_with_as_param_forward port=1
   reader=3 kind=Func atInput=2 copiedYet=0 type=float
   reader=3 kind=Func atInput=3 copiedYet=0 type=float
TANGENTREADERS graph=…_quartic__…_forward port=1
   reader=5 kind=Func atInput=3 · reader=7 kind=Func atInput=3
```

⇒ 那个切向入口**不在值半的起点集合里**（`portOf[index] == kInvalid`）⇒ 起点划分不是根因；它是被**一条边**接进来的。读到它的位置，正是提升调用实参表 `(v0, v1, d0, d1)` 的**后半**。

## 二、读数二：那个调用的被调方是法则层算子，不是用户函数

同一批图的 `PAIRQ` dump（逐字）：

```
PAIRQ graph=jvp_of_cube__first_with_as_param ref=3 half=1 pair=0 pairAt=0
      callee=autodiff::mul_tangent<float>  calleeNorm=4  ownNorm=2
PAIRQ graph=jvp_of_f__first_with_as_param ref=35 …
      callee=autodiff::add_tangent<f32>    calleeNorm=4
```

⇒ 被调方 `autodiff::mul_tangent<T>` 的签名是 `(x, y, dx, dy)`，**它就是为了拿切向才存在的**：它没有「值版」可还原。`answersAPair`/`answersAPairAt` 对它都为 0，也是同一件事的一侧 —— 它不是「答一个对」的函数，它是法则的切向那一半。

## 三、于是撞的是两条法则，而不是一处实现

- 作为 `jvp` 的结果，那张图的第 2 段按类型是**切向**；
- 作为 `split` 的输入，值半**不许声明切向端口**。

两条都对，而它们在同一个入口上重叠。⇒ 这不是「`split` 找到切口」的问题，而是**「下一刀怎么读上一刀的输出」**的问题。

## 四、这一格排除掉的、以及对下一格的约束

本页之前的七步（同一格）已排除：位置不可依赖（三次偏移三次破同一对闸）· 位图存在但生产者不填、填上之后墙不动（`45369b138`）· 调用是满的（不是少实参）· 那句拒绝的数字来自图签名端口号（`halfInputsOf` 永远改不动它）· 切向入口不在值半起点集合里。这些都写在
`2026-11-20-the-tangent-argument-record-is-not-written-by-the-transform.cn.md`。

而本页要写死一句，防重走：

> **「不许拿切向」不是一条无条件的复制法则 —— 它只对**用户函数那一层**成立；法则层算子（`*_tangent`）
> 生来就是拿切向的。把这两层混成一条，会得出「复制时还原一切提升调用」这个错的法则。**

⇒ 所以「`split` 复制提升调用时把它还原成原调用」这个提法是**错的**：`mul_tangent` 无值版可还原。正确的落点是**边划分**：谁读切向入口，谁就在切向半。

## 五、开工程序

这一句是**给决策者**的：`lift.h` 里已有 `Lift<A>` 的读法，`split` 的入口划分也在用 `parameterOfPort` 数「值半 / 切向半」，两处对**同一张图**给出相反的答案。若决策者批「是切向」，本线的开工是 `split` 的边划分一处；若批「是值」，则要改的是 `Lift` 在第二刀里的读法，落点不在 `split`。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

是切向。依据 9.9"按类型解读"。外层 `split` 中，读外层切向入口的节点（包括 `mul_tangent` 这类法则层算子）落在线性半，它读到的值成为残差（核心抽象第 6 节："切口上的值成为残差"）。
