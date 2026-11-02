# `transpose` 怎么知道一个函数的参数是切向（半身），而不是普通参数？

**一句话**：`transpose(lin)` 在调用点该收**一个**实参（cotangent）还是**一串**（值…，cotangent），取决于 `lin` 是"闭包半身"还是"普通函数"——而这两者今天的**静态类型是同一个字符串**。算子解析器只看类型，所以它分不出来。四条路，代价在下面。

## 读数：解析器手里是什么（2026-11-02 18:33，`lane-resid`）

临时打印加在 `modules/transforms/transpose_operator.cpp`（`TransposeResolver::resolve` 入口与 `transposeKernel` 的既有读行），跑的正是 `residuals_in_the_closure.cml` 那条程序（f(x,y)=x\*x\*y 在 (3,2)）：

```
TRANSPOSE function=autodiff::mul_tangent<float> graph=72 type=(float, float, float) => (float, float) nodes=7 arms=0 rhs=add_t,mul_t
TRANSPOSE function=split_of_jvp_of_f_backward graph=71 type=(float, float, float) => (float, float) nodes=9 arms=0 rhs=-
TRANSPOSE resolve with=0 norm=1 value=none graph=none
TRANSPOSE op signature=(f: (float) => float | ((..Residuals), float, ...) => float) => (knowns.., cotangent) => (cotangents..)
  resolved=(f: (float, float) => float pure) => (float, float, float) => (float, float)
  transposed=(float, float, float) => (float, float)  graph=(float, float, float) => (float, float)  same=yes
```

三件事因此是确定的：

1. **解析器只有类型**。`FuncTypeResolver::resolve(const type_vec_t &with, const type_vec_t &norm)`（`include/camel/core/type/resolver.h`），调用点是 `builder_operators.cpp:1107` / `:2618` 的 `.ops->resolve({}, normTypes, statics)`——那里 `normTypes` 是各实参的 `dataType()`。**没有节点，也没有值，更没有图**。所以"从值上读它有没有闭包"这件事，在类型必须被定下来的那一刻拿不到材料。
2. **内核看得见图，但来不及**。`transposeKernel` 里有 `function->graph()`（上面那行 `captures=` 就是它的 `closureNodes().size()`），可它跑在类型定型之后。
3. **内核那条自检把"类型说一套、图做一套"钉死了**：`same=yes` 是 `resolved`（静态类型算出来的）与 `graph`（真造出来的图）的 `toString()` 比较。也就是说即便内核造出只有一个参数（cotangent）的图，静态类型仍说三个参数，`same=no`，用例当场红。这一条不是障碍，是护栏：它要求"类型与图同形"，而这正是本页要裁的那个形状。

## 四条路

| # | 路 | 代价 | 读数后的判断 |
| --- | --- | --- | --- |
| 1 | **类型上加一个标记**（"参数是切向"），`transposeSignature` / `Transpose<F>` 读它 | 改类型系统一个位；打印不变（仍 `(float, float) => float pure`）；不用放宽任何守卫 | 见下"三问" |
| 2 | **另一个算子名**（例如 `transpose` 只管普通函数，闭包半身用新名） | 不加类型改动；多一个算子，且 `vjp` 要记得用哪个 | 次优：把一个类型问题换成一个命名问题，库里两条路并存 |
| 3 | **保留切向实参**，`vjp` 传占位值 | 零改动 | **不该选**：那是拿一个假数字去满足守卫。今天 `residuals_in_the_closure.cml` 就是传 `0.0` 才验证了"闭包才是来源"——把它变成库里的常态，等于让每个 `vjp` 都靠一个假值过守卫，而这个假值一旦哪天被某条规则读到就是静默错数 |
| 4 | **从值的闭包读**（值有闭包 ⇒ 是半身） | 不加类型改动，与 09:50 的闭包一致性校验同向 | **读数是"拿不到"**：解析器只有类型（上面第 1 条）。要用它，就得让算子解析多一个"实参节点"入参——那本身就是一笔比选项 1 更大的机制改动，而且解析发生在建图之前，节点的图未必已经定下来 |

## 选项 1 的三问（作者对"新字段/标志/变体"的规矩）

**为什么推不出？** 因为 `lin` 的类型与一个普通双参函数的类型**逐字节相同**：`(float, float) => float`。`is_same` 是结构比较（`stdlib/types.cml:119` 用它测），残差的个数与类型不在类型里（这是 08:30 那条裁定的结论：`lin` 的静态类型是普通函数类型），而 `Tangent<float> = float`、`Tangent<float[]> = float[]`，所以连"参数的类型是不是切向"都推不出来。**能用的信息只有"这个值有没有闭包"，而它在值上，不在类型上。**

**为什么不能用已有机制？**

- `with <...>`（函数类型的捕获位）：`lin` 的闭包**正是**它的捕获，但 `with` 要求**声明处**写出成员类型与个数，而残差个数是图的答案（5 个、3 个、4 个，逐程序不同）——这正是 08:30 否掉的"arity 未知的类型"，而且 `transposeSignature` 今天第一条就拒绝 `withTypesCount() != 0`（`transpose.cpp:44`）。
- `ResidualsOf<F>` / `TupleType::residuals(...)`：残差已不再是类型，两张空表已随 `split` 那一笔删掉。
- `param_t` 的 bool：`include/camel/core/type/composite/func.h:49` 写着它是 `var`（可变参数），不是"切向"。
- 静态实参（`static_args_t`）：`transpose(lin)` 的实参是运行期值（`Fill(FunctionClosure)`），没有静态实参可用。

**通用在哪？** 它说的是"这个函数值的参数是**被求导的那个量**，值由闭包提供"——`split` 的后半段是第一个这样的值，但 `vjp`/`jvp` 之外任何"把图连捕获一起交出去"的算子都会用到同一位。换句话说，这个位描述的是**函数值的种类**（开/闭），不是 `split` 的私有形状。

## 要作者答的一句

**「`transpose` 分不出"闭包半身 vs 普通函数"（静态类型同一个字符串、解析器只有类型），按上面读数该走选项 1（类型上加"参数是切向"一位），除非你另有裁定。」**

若走选项 1，还差一个**写法**问题要一并定：声明处怎么拼这个位。`split` 今天在 `stdlib/signatures/transforms.cml` 写的是
`export func split(f: ...): (v: float, d: float) => (float, (float, float) => float)`
——里层那个函数类型要么需要一个能拼出"切向参数"的类型构造（打印仍是 `(float, float) => float`），要么由 `split` 的 kernel 在造 `lin` 时置位、而声明处只写普通函数类型（那时静态类型与运行期标记必须一致，`same=` 那条检查会替我们看着）。

**我方默认**：按读数走 **1**；写法上取后者（kernel 置位 + 声明写普通类型），因为它在类型字符串上零改动、且与 `same=` 那条自检同向——除非作者要类型构造那一版。

**若作者选 (a)（类型构造），打印会不会变、`is_same` 会不会劈开，届时需重新量**：(b) 只多一位，(a) 多一个类型构造，两者的兼容性代价不同，不能拿 (b) 的读数替 (a) 担保。

## 这条堵着谁

`stdlib/vjp.cml` 的 `transpose(linear)(res, ct)` 要改成 `transpose(lin)(ct)`，`vjp` 及全部 `vjp_*`/`grad_*`/`split_branch_author_vjp` 的用例迁移（`feat/transforms` 那 15 条红）都等在它后面。`transpose` 的**图侧**已经通了（残差从闭包进、转置图自己的捕获口，读数 `back dx=12.0 back dy=9.0`，`residuals_in_the_closure.cml` 已绿），堵住的只有"收几个实参"这一条类型法。