# `FirstOf` 交出一个成员，`(W, ...Rest)` 要交出两个：泛型里「其余」那半没有写法

**判据一句话**：`FirstOf<P>` 顶上的是「取一个成员」——同一个 `F`，`<w: FirstOf<WithOf<F>>>(...) => float` 在泛型声明里判 `true`，exit 0；顶上不的是「把包拆成两半、两半都要用」——只要把「其余」那半写回列表，泛型声明就报 `no case selects float[2]`，exit 1，而把 `F` 绑成具体函数类型、一字不改，同一条就判 `true`。所以缺的不是「取头」这个通道，是**一次解构要交出两个答案**的那条路。

状态：待裁。基线 `origin/develop` `e6148acfe`。读数取自 `88zjwei` 的 `~/camel-work/Camel`（HEAD `e6d15a2a2e`，干净树，二进制自报 `Camel v0.2.44 (Build 261105_131711_e6d15a2a2e)`）；`git diff --stat e6148acfe..HEAD` 只有 1 个文档文件（40 行），`src/` 与 `stdlib/` 与车道基点逐字相同。探针在机器的 `~/probe-typepat/`，不入树。

同一目录的 [`2026-11-05-a-type-position-pattern-is-not-expressible-only-inside-match-type`](2026-11-05-a-type-position-pattern-is-not-expressible-only-inside-match-type.cn.md#量出来的边界) 量的是**模式能站在哪几个位置**；这一页量的是**模式绑定的两个名字能不能分开用**——`FirstOf` 只有一个答案，所以它们不是同一个问题。

## 要你裁的是什么

**`Gradient<F>` 这类声明，要按设计原则 5.6 的写法把类型包「拆成第一个成员与其余」，还是 `FirstOf<P>` + 整包 `...WithOf<F>` 就是可接受的最终写法？**

这不是口味：拆法今天**在泛型声明里写不出来**（下面第 2 节的报错），而设计原则说它写得出来。

## 今天的事实

### 1. 把 `(W, ...Rest)` 写进类型位置：报错原文

`~/probe-typepat/p5_pattern_outside_match.cml`，全文：

```camel
import { ParamsOf, Tangent } from types

type Loss = <w: f64[2], k: int> (x: float) => float

// The ruling's text written where a type expression is expected, with no `match type` around it.
type Grad2<F> = <(W, ...Rest)>(...ParamsOf<F>) => Tangent<W>

func main(): int sync {
    return 0
}
```

（`ParamsOf` 与 `Tangent` 从 `types` 导入；那行注释是探针里的原文。）

`./out/latest/bin/camel ~/probe-typepat/p5_pattern_outside_match.cml`：**exit 1，stdout 1381 字节，stderr 0 字节**（诊断走 stdout），原文：

```
error[SyntaxDiag::UnexpectedToken]: Unexpected '('; expected a name.
  --> /home/zjwei/probe-typepat/p5_pattern_outside_match.cml:6:18
   |
 6 | type Grad2<F> = <(W, ...Rest)>(...ParamsOf<F>) => Tangent<W>
   |                  ^
help: The token fits nothing that can stand where it is. What the parser expected instead follows the semicolon above: a list whose items have no ',' between them, a bracket that is not closed and a reserved word where a name belongs are the usual causes.

error[SyntaxDiag::MissingToken]: Expected ':' before ','.
  --> /home/zjwei/probe-typepat/p5_pattern_outside_match.cml:6:20
   |
 6 | type Grad2<F> = <(W, ...Rest)>(...ParamsOf<F>) => Tangent<W>
   |                    ^
help: One token is missing at the caret and the message names it; nothing else in the file has to change.
help: insert ':'
   |
 6 | type Grad2<F> = <(W: , ...Rest)>(...ParamsOf<F>) => Tangent<W>
   |                    ++

error[SyntaxDiag::MissingComma]: Expected ',' or ')' before '...'.
  --> /home/zjwei/probe-typepat/p5_pattern_outside_match.cml:6:22
   |
 6 | type Grad2<F> = <(W, ...Rest)>(...ParamsOf<F>) => Tangent<W>
   |                      ^^^
help: The items of a list (parameters, arguments, elements) are separated by ','.
help: insert ','
   |
 6 | type Grad2<F> = <(W,, ...Rest)>(...ParamsOf<F>) => Tangent<W>
   |                     +

camel: 3 errors
```

**读它**：`<(` 被读成 `with` 参数表的开头（那里要 `名字: 类型`），所以第一条是「期望一个名字」。后两条 help 建议插 `:`（`<(W: , ...Rest)>`）——建议之所以荒谬，是因为**这个位置上没有模式可读**，解析器只能把它当参数表掰。这就是「类型位置缺模式语法」的字面读数：不是语义上拒绝，是**语法上那个位置根本没有模式的入口**。位置选在 `with` 列表不是随手：裁定的写法（拆开 `WithOf<F>`，再把两半拼回 `with` 列表与结果）第一步就撞在这里。

### 2. 拆两半：泛型里报错，具体里为真

`~/probe-typepat/p13_rest_only_spliced.cml`：

```camel
import { ParamsOf, WithOf, is_same } from types

type RestOf<P> = match type P { case (W, ...Rest) => Rest }

type Loss = <w: f64[2], k: int> (x: float) => float

// Only the rest, spliced into the with list.
type RestOnly<F> = <...RestOf<WithOf<F>>>(...ParamsOf<F>) => float
type RestOnlyShape = <k: int> (x: float) => float

func main(): int sync {
    println(is_same::<RestOnly<Loss>, RestOnlyShape>())
    return 0
}
```

**exit 1，stdout 822 字节，stderr 0 字节**，原文：

```
error[SemanticDiag::TypeFunctionNotApplicable]: The type function 'match type float[2]' is not applicable: no case selects float[2].
   --> /home/zjwei/probe-typepat/p13_rest_only_spliced.cml:12:23
    |
 12 |     println(is_same::<RestOnly<Loss>, RestOnlyShape>())
    |                       ^^^^^^^^^^^^^^
help: A type function computes a type from types at compile time (design principles 21): `ElementOf<T>` is the element of an array or a set, `PayloadOf<T>` the `U` of `U?` or `U!`, and `KeyOf<T>` and `ValueOf<T>` the key and value types of a map, and `RowsOf<T>` the array whose first-dimension elements are `T`; a program writes its own with `type F<T> = ...`, whose body may be a `match type`. It is called with `F<...>` where a type is written, and it accepts only the types it is defined for.

camel: 1 error
```

`match type float[2]` 就是 `RestOf` 绑上 `P = float[2]` 之后的写法——`float[2]` 正是 `Loss` 的第一个 `with` 参数 `w` 的类型。所以 **`...RestOf<WithOf<F>>` 被读成「对包内每个成员各算一次 `RestOf`」**：`RestOf` 的 case 要的是一个**包**，收到的却是一个类型（`float[2]`），于是「没有 case 可选」。

`~/probe-typepat/p18_generic_rest_in_tuple.cml` 把同一个表达式放进**元组**类型（`type InTuple<F> = (...RestOf<WithOf<F>>)`），同样 exit 1、同一句报错——**所以这不是 `with` 列表的锅，是泛型里「其余」这个拼接本身**。

把 `F` 绑成具体的 `Loss`，一字不改就过：`~/probe-typepat/p16_rest_in_with_concrete.cml`（`<...RestOf<WithOf<Loss>>>() => float` 判成 `<k: int>() => float`）**exit 0，印 `true`**；元组那半 `~/probe-typepat/p17_rest_in_tuple.cml` 也 `true`（`test/cases/compile/type_packs/pack_taken_apart.cml#RestOf` 早已钉着这一头）。

### 3. `FirstOf<P>` 今天怎么顶上

`stdlib/types.cml#FirstOf` 逐字：

```camel
export type FirstOf<P> = match type P { case (W, ...Rest) => W }
```

`stdlib/types.cml#Gradient` 今天的声明（两行，逐字）：

```camel
export type Gradient<F> =
    <...WithOf<F>> (...ParamsOf<F>) => Tangent<FirstOf<WithOf<F>>>
```

它顶上了什么：结果那一格要「第一个 `with` 参数的类型」，`FirstOf` 就是它，而且 **`F` 还是类型参数也对**——`~/probe-typepat/p11_named_head_only.cml` 把 `FirstOf<WithOf<F>>` 写成具名 `with` 参数的类型（`<w: FirstOf<WithOf<F>>>(...ParamsOf<F>) => float`），**exit 0，印 `true`**。

正写也绿：`test/cases/compile/type_packs/gradient_type.cml` 在这棵树上 exit 0，四行判据 `true / true / true / false`，末行 `gradient_type g=[2.0, 2.0]`。

### 4. 它顶不上什么（顶上与顶不上的分界）

两边都是**同一个泛型**、同一个 `Loss`，只差「要头」还是「要其余」：

| 写法（`F` 都是类型参数） | 读数 | 探针 |
| --- | --- | --- |
| `<w: FirstOf<WithOf<F>>>(...ParamsOf<F>) => float` | **exit 0，`true`** | `p11_named_head_only.cml` |
| `<...RestOf<WithOf<F>>>(...ParamsOf<F>) => float` | **exit 1，`no case selects float[2]`** | `p13_rest_only_spliced.cml` |
| `(...RestOf<WithOf<F>>)`（元组里） | **exit 1，同一句** | `p18_generic_rest_in_tuple.cml` |
| `F` 绑成 `Loss`：`<...RestOf<WithOf<Loss>>>() => float` | exit 0，`true` | `p16_rest_in_with_concrete.cml` |
| `F` 绑成 `Loss`：`(...RestOf<WithOf<Loss>>)` | exit 0，`true` | `p17_rest_in_tuple.cml` |

一句：**取头不需要知道包的其余，所以泛型里也行；取其余要经过 `...`，而 `...` 在泛型里先把包按成员摊开（`src/core/type/typevar.cpp#expandPack` 与 `#PackScope`：先量长度，再按 index 把包换成第 i 个成员），`RestOf` 拿到的就是成员。**

## 问题的确切形状

`FirstOf<P>` 的答案**是一个类型**：`case (W, ...Rest) => W` 里 `Rest` 被绑上，然后被丢掉——名字出不了 case 体。所以：

- `FirstOf<P>` 是「**取一个成员**」的通道：`P` 有几口都行，一次调用只交出**一个**答案。
- `(W, ...Rest)` 是「**把一个包拆成两半**」：一次解构交出**两个**名字，`W` 去结果、`Rest` 去参数表剩下的位置。它要的不是「哪个成员」，是「剩下那些」。

用一个一元的通道扮演二元的语法，缺的那一半只能靠**第二个** `match type` 补（`RestOf`，与 `FirstOf` 同一个 case 形状）——而第二个 case 一旦要写进列表（`...RestOf<...>`），就撞上 `...` 的成员展开（第 4 节的表）。**差在哪：不是语法糖多少，是一次解构要产生两个答案，而 `FirstOf` 一次只产生一个。**

`Gradient<F>` 之所以「顶上」了，是因为它**根本不需要 rest**：`...WithOf<F>` 把整包原样带上，`FirstOf` 只管结果那一格。这也正是这个默认今天零代价的原因——直到第一个「要 rest」的库类型出现。

## 默认

**先用 `FirstOf<P>` 这条既有通道，等有模式语法再换写。**

代价（都是上面量出来的）：

1. **5.6 只对上了一半。** `docs/design-principles.cn.md#5.6 类型函数与原语` 逐字写着：「类型层的包同样能拆：值层能写 `<w, ...rest>`，类型层也能把一个类型包拆成"第一个成员"和"其余"……由参数算出的函数类型（`Gradient<F>`、`ValueAndGradient<F>` 等）都用这种写法在标准库里声明，不由 C++ 另算一份。」今天能对上的只有「第一个成员」那一半；「其余」落在具体形状上（p16/p17），泛型里是 p13 那句报错。所以这不是"已按通道解决"，而是**留下半条**：问题页该记的就是这半条，别把 `FirstOf` 读成 5.6 已经落地。
2. **库里只有 `FirstOf`，没有 `RestOf`**（`git grep -n RestOf -- stdlib/` 空；`RestOf` 只活在 `test/cases/compile/type_packs/pack_taken_apart.cml#RestOf`）。第一个要 rest 的库类型出现时，要么临时加一条库函数，要么绕开——绕开就是"每个具体形状写一条"。
3. **声明少了一句话**：今天 `Gradient` 的 `with` 列表写 `...WithOf<F>`（整包），"第一个 `with` 参数是被微分的那个"只在结果那一格出现一次。类型是对的，但读的人看不出 rest 被谁带上了，也看不出这两处来自同一个包（今天靠 `F` 相同保证，不靠语法）。
4. **报错不点名**：p13 的 `match type float[2]` 既不写 `RestOf` 也不写 `FirstOf`，第一次撞上的人要从 `float[2]` 反推是哪个类型函数收到的（空包那条同病：`~/probe-typepat/p6_empty_pack.cml` 点名的是 `pack<>`）。照默认用 `FirstOf` 的人不会撞上它；一旦有人去写 "其余"，撞上的就是这句。

## 判据

1. 今天：`FirstOf<WithOf<F>>` 与模式拆出的 `W` 是同一个类型（`gradient_type.cml` 的 `true`、`pack_taken_apart.cml#Head` 的 `true`），**泛型里也成立**（p11）。
2. 「其余」有写法的那天：p13 与 p18 从 exit 1 变 exit 0，`RestOnly<Loss>` 与 `<k: int> (x: float) => float` 判成同一个类型，且 p16/p17 仍是 `true`（具体形状的读法不变）。
3. 在那之前，库里的类型一律整包 `...WithOf<F>`；**要「其余」的形状不进泛型声明**——它今天只能在具体形状上写（第 4 节表格的最后两行），写进泛型就是 p13 那句报错。