# 生成器里一个闭包的两个类型：绿在别处

`test/cases/compile/stream/generators.cml` 的 `reading()` 今天编译得过去，`stream.generator*` 五条在
`origin/develop` 上是 147/147 里的 5 条绿。**这不是因为捕获判定对了，是因为程序里恰好有另一个叫 `x`
的参数被交给了调用。** 本页记录两份 trace 的原话、根因那一句、按名字匹配这一段，以及一条最小复现的
形状。作者裁定（2026-11-16）原文：**「`NoMatchingFunction` 是两份真相，不是语义问题」**。

## 1. 两条 trace 原话

同一个闭包在同一个程序里被建了两次：`typeof` 在临时图里重建成 `__g_69__`，语句处真建成 `__g_71__`。
两次都问同一个函数 `decideCaptureByValue`，答案不同。

**gate5 在（今天的 `origin/develop`）**：两次都答「格子」，类型相等，所以绿。

```
[TYPESRC] inferTypeOf begin outer=reading exprOrigin=7366 src=() => x
[TYPESRC] decide graph=__typeof__ outer=reading origin=0x6 name=x nameIsLocalOrigin=true gate=gate5-handed-to-call answer=false
[TYPESRC] capture graph=__typeof__ outer=reading origin=0x6 name=x byValue=false originKind=11 portType=var int
[TYPESRC] capture graph=__g_69__ outer=__typeof__ origin=0x6 name=x byValue=false originKind=11 portType=var int
[TYPESRC] inferTypeOf end outer=reading src=() => x -> () => int uses capture 0
...
[TYPESRC] capture graph=__g_71__ outer=__gen_step__ origin=0x6 name=x byValue=false originKind=11 portType=var int
[TYPESRC] reach capture-pos=0 body=__g_71__ capturedNode=0x0 isVariable=true
```

**删掉 gate5（`3773eed5c` 之后的世界）**：重建答「值」，真建答「格子」，两句话对不上。

```
[TYPESRC] decide graph=__typeof__ outer=reading origin=0x6 name=x nameIsLocalOrigin=true gate=rule-by-value answer=true
[TYPESRC] capture graph=__typeof__ ... portType=int
[TYPESRC] inferTypeOf end outer=reading src=() => x -> () => int pure
...
[TYPESRC] captureByValue graph=__gen_step__ name=x answer=0 via=machine-closure maker=true
[TYPESRC] captureByValue graph=__g_71__ name=x answer=false via=cache
[TYPESRC] capture graph=__g_71__ outer=__gen_step__ origin=0x6 name=x byValue=false portType=var int
[TYPESRC] inferTypeOf end ... -> () => int uses capture 0
```

于是格子的类型是 `() => int pure`，赋进去的值是 `() => int uses capture 0`：

```
No matching function found for argument types: <> (() => int pure, () => int uses capture 0);
operator '=' is not defined for these operand types.        (SemanticDiag::NoMatchingFunction, 0x0300000F)
```

## 2. 根因那一句

**不是两条规则，是同一条规则的两次不同输入（谁在问）＋「一格一个答案」把机器的答案粘住。**

`decideCaptureByValue` 只看「正在问的是哪个图」：临时图 `__typeof__` 不是机器造的，走按值那条规则；
机器自己造的 `__gen_step__` 是，直接答「格子」。`captureByValue` 里那句 `captureByValueOf_[origin] = false`
把它记成**这一格的总答案**，于是程序的闭包 `__g_71__` 从缓存里拿到 `false`，自己一次都没被判过。

这也说明核查该往哪儿看：**类型不是一个闭包算出来的，是两次建图各算一次**。作者给的修法（①取建图时那个
闭包的类型、②让重建走同一条捕获判定）都是在收掉第二次建图。实测（2026-11-16，`lane-typesrc`）：

- ①：「不重建」在生成器里做不到——`typeof (() => x)` 跑在 `__gen_step__`／`__g_71__` 建身之前，
  语句处那个闭包此刻还没有类型；
- ②：把「机器自己的捕获不写进这一格的总答案」加上，五条的 `NoMatchingFunction` **确实消失**，但
  21 条生成器用例全部撞同一句图不变量（`passes a string as 'with' argument 2 of '__gen_step__', which
  takes a var string`）：外层拿格子、内层拿值，图不合法。② 成立的前提是作者第 ④ 条——生成器跨
  `yield` 的状态改成按值传递——先落地。

## 3. 按名字匹配：今天的绿是巧合

`handedOver_` 是 `scanProgramWriters` 扫出来的「被交给调用的名字」集合，**按名字**匹配，不看作用域。
`reading()` 的 `x` 从来没有被交给任何调用；它答「格子」，是因为**程序里别处**有一个 `x` 坐在调用的参数
位置上（同一个文件里的参数、或标准库里的同名参数都算）。换一个名字就没有这个巧合。

**今天的绿是这个巧合，不是判据。** 判据只有一条：同一个闭包只该有一个类型。今天它有两个，只是恰好
相等。

## 4. 最小复现的形状

```cml
func reading(): int.. sync {   // 生成器体里造闭包，机器会在同一次拉取之外写这个格子
    var x    = 0
    let read = () => x
    yield 0
    x = 5
    yield read()               // 按引用读 5，按值读 0：两个意思
}

func unrelated(x: int): int { return id(x) }   // 这一行不是它的事：同名参数被交给了调用
```

只要程序里有别的 `x` 被交给调用，`read()` 就是 5；把那一处改成 `y`，同一个 `reading()` 的 `read()`
就变成 0（或者在没有 gate5 的世界里报 `NoMatchingFunction`）。**同一条源码，意思取决于一个无关的
名字**——这是潜伏的错编译，不是排版问题。

这条现在**还绿**（`stream.generator*` 五条在 `origin/develop` 上 147/147）。作者裁定过这个形状的正确
读法：闭包按值捕获，`read()` 是 0，`x = 5` 要么落在闭包最后一次使用之后、要么这个程序该被拒（3.5，
198）或改用 `Ref`。所以**将来它必须红（被拒）或者必须对（按值读到 0）**，不能像今天这样，靠一个同名
参数决定是 5 还是 0。

## 5. 给下一个人

**`147/147` 不构成「捕获判定正确」的证据。** 这条线上通过的用例，可能通过的理由是错的；要判这一处，
看的是一个闭包有没有**两个**类型，不是五条用例是不是绿的。

配套：作者第 ④ 条（生成器状态作为值在拉取之间传递）落地前后，`scripts/check-capture-census.mjs` 的
`capture_ports_generator` 应该归零（口径见 `61_capture_census.md`）；这一栏不归零，② 就还不成立。