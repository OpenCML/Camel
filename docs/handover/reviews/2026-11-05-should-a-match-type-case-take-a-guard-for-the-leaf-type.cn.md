# `match type` 的一条臂能不能带「这个叶子是不是数值」的守卫

**判据一句话**：`ShapeOf<T>` 的最后一臂是 `case _`，**于是 `ShapeOf<string>` 与 `ShapeOf<int>` 答同一个结果**——今天 `is_same::<ShapeOf<string>, int[0]>()` 读 `true`。这不是 `ShapeOf` 写错，是 `match type` 没有守卫位置。

## 现象到符号

- `stdlib/types.cml#ShapeOf` 三臂：`case E[...Dims] => int[Dims]`、`case E[...] => int[]`、`case _ => int[0]`。第三臂是**默认臂**，因为前两臂只认数组，「数组之外的任何类型」只能落到它。
- **实测**（`merge-train-2`，`2026-11-05`，探针跑完即删）：

  ```
  is_same::<ShapeOf<string>, int[0]>()   ->  true
  is_same::<ShapeOf<int>,     int[0]>()  ->  true
  is_same::<ShapeOf<float[2, 3]>, int[2]>() -> true
  ```

  第一行是那条判据的**实际读数**：一个字符串类型的「形状」与一个标量的「形状」被判为同一个类型。
- 想按类型的谓词分开，唯一在语言里的写法是 `case _ when is_numeric::<T>()`，**而 `match type` 的 case 没有守卫位置**：同一写法今天报 `Expected '=>' before 'when'`（`docs/handover/reviews/questions-2026-10-30-match-type-guard.md` 记的是同一件事的另一面——规则集要「一条臂覆盖任意秩张量」时也撞在这里）。
- 另一条看似可行的路是**把 `is_numeric` 的名单抄进类型层**（`match type` 里逐个列 `float`、`int`、`u8`…）。**那是同一件事的第二套真理**：值层有一份「哪些类型是数值」，类型层再抄一份，两份总有一天不同，而不同的那天谁也不算错。

## 为什么不能自己定

三条路各自由谁拍板不同：

1. **给 `match type` 的 case 加守卫位置**（`case _ when <pred>`）——**新语法，语言形状，作者裁。**
2. **把谓词名单抄进类型层**——不用新语法，但**造出第二套真理**，且每加一个数值类型要改两处；这与「按判定而不是枚举」那句话直接相悖。
3. **认下 `ShapeOf<string> == int[0]`**——什么都不用改，代价是「形状」这个名字对非数组类型说谎，而调用方无从知道。

## 默认建议（按它继续，除非作者另裁）

**走第 1 条：给 `match type` 的 case 加一个守卫位置 `case _ when <pred>`，与值层 `match` 的 `when` 同形**，谓词取类型层已有的类型函数（`is_numeric`、`is_array`…），守卫写在臂上、与臂同一次求值。

理由：**它让类型层问出值层已经能问的那件事，而不是把答案抄一份**；语法上与值层同形，学一遍就够；`ShapeOf<T>` 的第三臂届时写成 `case _ when is_numeric::<T>() => int[0]`，`string` 落到「没有臂选中」，那就是它应有的答案。

## 落地前仍然成立的事实

- `ShapeOf<string>` 与 `ShapeOf<int>` 今天同答 `int[0]`（上面第一行）。
- `test/cases/compile/match_type/array_dims.cml` 钉的是**标量那一条**（`ShapeOf<int>`），**没有钉 `string` 那一条**——守卫落地时，那条读数才会有一个可以被改红的用例；现在钉它只会把缺陷固定成期望。
- `stdlib/types.cml#ShapeOf` 的注释已经写明第三臂是默认臂、以及它为什么不能问叶子类型，**这一页是那条注释的问句形态**。
## 决策者（2026-11-13）

这是新语法，已列给负责人裁。决策者推荐第 1 条（`case _ when <pred>`，与值层 `match` 的 `when` 同形），理由同本页。裁定前不动 `ShapeOf`。

## 负责人定（2026-11-14）

加守卫，见 `reviews/questions-2026-10-29.md` 同日一节与原则第 5 节。
