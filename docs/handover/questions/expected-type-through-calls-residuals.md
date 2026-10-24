# 期望类型穿过调用：本 lane 修完两半之后仍红的三处（实测，2026-10-24，h429 lane）

裁定的出处：`docs/handover/reviews/questions-2026-10-23.md` 的
`## expected-type-through-literals-and-calls（期望类型穿过调用）` 一节（选 (a)、否 (b)、两处同族缺陷按 5.1 修）。

本文件只报**实测**到的残留，三处都需要作者裁定或另开一片，所以 h429 只做了裁定点名的那两半
（① 位置的期望类型经类型变量送到实参：字面量据此取类型；③a 参数位置带计算元素的复合字面量；
③b 声明位置的元组只圆整一次），**没有自己发明语义**。

## 一、位置与「值」谁先说了算：本 lane 保留「值先绑」，于是 `f64` 值仍在声明处报错

裁定说「先把被调函数的返回类型与期望类型对齐」，并且「一个已经有类型的值不会被转换（`f64` 变量
进 `f32` 位置仍是类型错误）」。本 lane 的实现把这两句读成：**字面量让位给位置，值保自己的类型**——
先让有自己类型的实参绑定它提到的变量（与改动前完全一致），再让位置解**剩下**的变量，字面量最后绑。
实测（`keep::<T>(xs: T[], n: int): T[]`）：

```
let v: float[] = [1.0, 2.0]
let xs: f32[] = keep(v, 2)        // 仍红，但落在声明处：
error[SemanticDiag::DeclaredTypeMismatch]: This value has type 'float[]',
    which is not the declared type 'f32[]'.
```

即「一个 `float[]` 值进 `f32[]` 位置仍是类型错误」成立，但错误**不在调用处**（不是
`GenericArgumentsMismatch`），因为位置不能改写一个值给出的绑定。

**要裁定的问题**：位置对**值**是否也说了算（错误落在调用处）？本 lane 不动这一条，因为实测把
位置放到值之前会弄红一条既有的绿用例：`test/cases/modules/tree_operations.cml` 第 11 行
`tree_map<twice>({ count: 2, weight: 1.5 })`（`stdlib/tree.cml:58` 的 `tree_map`，同一个模板实例化出
int 叶与 float 叶）会报

```
error[SemanticDiag::GenericArgumentsMismatch]: The generic function 'twice' cannot be called with
  argument types (float): argument 1 has type 'float', but its parameter 'x: T' is 'int' here (T = int).
```

也就是说，`f(t, ..)`（`stdlib/tree.cml:62`）那个内层调用的「位置」在 float 叶上被读成了 `int`
（叶片的类型由 `static for` 展开，位置来自模板推断出来的返回类型）；位置上先绑就把 `T` 钉成 `int`。
这看着像另一个缺陷（同一模板的返回类型在逐叶实例化时不是逐实例的），但本 lane 没有测到它的
成因（只能测到「位置先绑 ⇒ tree_map 红」），所以不敢据此改值一侧的语义。

## 二、带「名字（计算值）」元素/字段的复合字面量：位置仍整体让位

裁定 ③a 说的是「带计算元素的复合字面量在参数位置拿不到参数类型」。本 lane 修好了**写下来的元素**
（含嵌套一层、含洞保留），但下面三种形状仍红——它们共同点是：**外层复合字面量里有一个「名字」
或一个洞，而字面量元素躲在更深一层的复合字面量里**。实测（本 lane 与 develop 的 `out/latest` 二叉
逐条对照；develop 侧是主机 `~/camel-work/Camel` 的 checkout）：

```
type P = { n: int, t: (f32, f32) }
let i: int = 2   let f: f32 = 1.0   let g: float = 1.0

本 lane 仍红：
  let p2: P = { n: i, t: (f, 2.0) }        // { n: int, t: (f32, float) }，不是 P
  let c:  P = { n: k, t: (1.0, f32(k)) }   // { n: int, t: (float, f32) }，不是 P
  takeT((g, 1.0))                          // (float, float)：这条红是对的（值不转换）

本 lane 已修（develop 上还红）：
  let t1: (f32, f32) = (f, 1.0)                    // develop: (f32, float)
  let p1: P = { n: i, t: (1.0, 2.0) }              // develop: { n: int, t: (float, float) }
  let d:  (f32, f32) = (1.0, f32(k))               // develop: (float, f32)
  takeT((f, 1.0)) / takeArr([f, 1.0]) / takeP(…)   // develop: (f32, float) / ElementTypeMismatch
```

成因（本 lane 用临时 `fprintf` 探针实测到，探针已删）：`visitStructInto`/`visitTupleInto` 里那条
「字段/元素写的是变量就整体回退」的判据用的是 `extractData` 给出的 data，而
`extractStaticDataFromNode` 对 `DREF` 返回的是 `RefData`（`Ref` 类型），`Ref` 不可能被任何字段类型
接受，于是**只要有一个名字，整个字面量就回退到无位置的建立**；回退后 `declaredValue` 用
`convertTo` 补救，而 `TupleData::convertTo` 遇到洞（`Ref` 元素）按既有语义返回 `nullptr`
（「带洞的元组不是值」），于是更深的字面量元素就丢掉了位置类型。

**要裁定的问题**：「回退判据」是否应当按元素的**值类型**判（而不是按 `extractData` 的 `Ref`），
即洞保留、写下的字面量取位置类型？难点是 GCT 里拿不到 `DREF` 的类型（类型在 GIR），
而若简单地「有洞就不回退」，一个类型不符的值会在洞里被静默接受（洞的填充值不再被检查），
会削弱 5.1「值不转换」的判据。所以这不是一行能改的，需要作者定：是在 GCT 补一次名字的类型查询，
还是在 GIR 对洞补一次类型检查。

## 三、算子路径没有期望类型：`reshape` 仍红，与 P4 一致

裁定自己说了这条（「今天按实参推断的 `OpDefResolver` 不经过类型变量，`tree_training` 要红到
P4 + 算子签名单一来源落地」）。本 lane 实测修前修后**逐字相同**：

```
let a: f32[2] = reshape([1.0, 2.0], [2])
error[SemanticDiag::DeclaredTypeMismatch]: This value has type 'Tensor<float64, [2]>',
    which is not the declared type 'f32[2]'.
```

所以本 lane 没碰 `tree_training`、没加 `known_fail`，也没有让 `reshape` 变绿。

## 四、实测到的一件附带事：文本/布尔字面量现在与数字字面量同组，以及没有标注的 `let` 把 `void` 当作位置

「字面量在位置之后绑定」这条实现里，`literalArgument` 认的是「那个实参是**写在那里的字面量**」
（数字、文本、布尔、复合字面量、只由字面量折出来的表达式），不是「它的类型是某种默认」。
文本与布尔因此也进了「位置先、字面量后」这一组，理由是：它们的类型**不由自己决定而由位置决定**
（一个文本字面量只能进文本位置，别处都报错），而若把它们留在「值」那一组，实例名里变量的次序会
跟着实参的分组走、不再按实参次序排列——实测 `test/cases/compile/generics/basic.cml` 第 31 行
`let p = pair_of(7, 'seven')`（`pair_of<T, U>`）的实例名会从 `pair_of<int, string>` 变成
`pair_of<string, int>`（`instances_are_concrete` 因此红；那是文本字面量在 `7` 之前绑了 `U`）。

**代价（实测，需要作者确认）**：字面量落在**位置**里不能进去时，报的位置从声明处挪到了实参处。
以 `ident::<T>(x: T): T` 为例：

```
let t: int = ident('a')
  改前：error[SemanticDiag::DeclaredTypeMismatch]: This value has type 'string', which is not the
        declared type 'int'.                       （落在声明处）
  改后：error[SemanticDiag::GenericArgumentsMismatch]: ... argument 1 has type 'string', but its
        parameter 'x: T' is 'int' here (T = int).  （落在实参处，与数字字面量完全一致）
```

数字字面量在改动前后都是实参处这一条（`let n: int = ident(1.5)` 实测就是
`GenericArgumentsMismatch`），所以这是「统一」而不是「新增一类错误」；但若作者认为文本字面量
应当按「值」处理（保自己的类型、报在声明处），本 lane 的这条实现要一起回退，并把实例名的
变量次序另行固定。

## 五、顺带实测到的另一件事：没有标注的 `let` 把 `void` 当作那个位置的期望类型

`stdlib/array.cml:228` 的 `sum::<T>(xs: T[]): T` 的 `@example` 是 `sum([1, 2, 3])`，文档检查器把它装进
`func __doc_example(): void { let __doc_example = sum([1, 2, 3]) }`（`scripts/stdlib-reference.mjs:1199-1205`）。
本 lane 的「位置解类型变量」落上去以后，实测：

```
error[SemanticDiag::WhenConditionUnmet]: No candidate of 'sum' applies to (void[]):
    every candidate's 'when' condition is false.
```

即位置被读成 `void`，于是 `T = void`、参数成了 `void[]`（这条红过 `registry.docs_complete`）。
本 lane 的处置：在「位置解类型变量」两处都把 `void` 当成「没有要求」跳过（一个被丢弃的值的
位置不是要求），于是该用例恢复绿。**但位置本身为什么是 `void`** 值得另记：一个不绑定名字的 `let`
的期望类型看上去应当是「未知」而不是 `void`；本 lane 只挡掉了它的后果，没有改位置的产生处。