# 混合汇合取哪个类型？`concat` 为什么比兄弟算子严？

（这是 `examples/tty/snake.cml` 编译不过的根因，属你保留的 C4/C22 范围，我们没动核心。）

## 症状（原文，`camel check examples/tty/snake.cml`，exit 1）

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ({ x: int, y: int }[], Point[]).
   --> examples/tty/snake.cml:39:18
 39 |     return concat([new_head], snake->slice<0, len(snake) - 1>)
help: … Available overloads:
          <:struct/concat_arr>: <> (xs: T[], ys: T[]) => T[]
```

## 例子本身没错（判据，不是"改到过为止"）

第 32 行 `let new_head = match dir {…}` 的四臂是匿名 `{x, y}`，`case _ => now_head` 是 `Point`；按 `docs/language/03_data_model.md:170-175` 与既有用例 `test/cases/loops/struct_state.cml` 的 `arm_mixed`（注释逐字："the branch joins them at the named type and copies the unnamed one"），**混合汇合应当取命名类型** ⇒ `new_head` 该是 `Point`，例子合法。

## 实现是顺序相关的（两个探针）

```cml
type Point = {x: int, y: int}
func f(p: Point, c: bool): Point[] {
    let q = if c then { x: 1, y: 2 } else p ;   // q 是匿名类型 ⇒ concat 报上面那条错
    return concat([q], [p])
}
```
把两臂**交换**（`if c then p else { x: 1, y: 2 }`）⇒ `q` 是 `Point`，exit 0。同一种值、不同书写顺序，两种结论。

## 两处根因

1. `joinTypes`（`src/core/type/composite/tuple.cpp:470-528`）先试 `entersImplicitly(a,b) ? a`，而匿名结构体能被命名类型"进入" ⇒ **匿名在前就赢**；
2. `:struct/concat_arr` 的 C++ 解析器要求 `elemType()->equals()`（`src/builtin/modules/globals.cpp:2581-2600`），而**兄弟算子** `append`／`contains` 与普通形参传递用的是 `assignableFrom`／`entersImplicitly`（同文件 2617 起）——同一个值，不同算子给不同回答。

## 请裁三点

1. **混合汇合取命名类型**（与文档、与 `struct_state.cml` 一致）——确认后汇合的规则按"命名优先"钉住；
2. **`concat` 的实参检查与兄弟算子同一条**（`assignableFrom`／`entersImplicitly`），而不是 `equals`；
3. 这两条是否落在你的 **C4/C22**（"一个类型一个身份、补 `unify`"）里——你来做，还是交回我们做（给出你要的口径即可）。

## 同源的第三处（顺带）

命名/匿名结构体做相等比较，`q == p`（`q` 是匿名结构体值、`p` 是 `Point`）报 `operator '==' is not defined for these operand types` —— 看起来与上面同一个根因：**结构体转换规则现在写在三处**（汇合、算子实参检查、算子解析）。

## 顺带更正一处

你那条工具缺口说"`--verify-ops` 跳过只有私有核函数的模块（`hash`）"：在 `ab3a58d9e` 上 **`hash` 并没有被跳过**（它的 3 个算子都是公开声明；更早的 `4454f9631^` 也是导出 `sha256`／`sha256_rounds`）。当前树里"只有私有核函数"的唯一实例是 **`array` 的 22 个核函数**，我们按它复现并修好了两处工具（`inspect --ops` 867 → 889 行、私有核函数带 `"private":true`；`--verify-ops` 比对的重载 615 → 637、exit 0），并给那 22 个声明补了 `/// @pure` 文档标记（因为签名子句那套迁移还没做；迁移落地后应换成签名子句）。