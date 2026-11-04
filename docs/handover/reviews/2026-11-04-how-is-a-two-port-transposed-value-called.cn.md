# 两端口（值 + 余切）的转置值，怎么被调用？

**状态**：待裁；我方默认见第 6 节（**承诺先留 1 端口**，树保持基线不动）。

这页接着 `docs/handover/questions/2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md`
（下称"甲那页"）写。甲那页问的是**语义**：转置成员要不要给"被读到的参数自己的值"留端口。我按它的甲
（留，且只给律真的读了值的参数留）动手，落地到一半发现**顺序问题不是这道题的难点**，难点是一堵更深的墙：
**只要 `transpose(f)` 的类型真的写成两个参数，任何把这个值当函数调用的程序都会让编译器在建图时不变量中止。**
这不是拒绝、不是错数，是 `Camel Invariant Violation`。

## 1. 读数（树、时间、负载）

树 `D:\Projects\Camel-wt\valports` @ `1c36c282e`（=`34b659107` 实时 `origin/develop` + `e16923e03` 的
cherry-pick），2026-11-04 05:2x-06:0x +08:00，宿主 88zjwei（32 核），1 m load **15.2 / 15.5 / 20.2**
（满载区间：数字只用于判形态，不用于比性能）。探针在 `/tmp/` 与 `D:\Temp\valports\`（不入 git）。

1. **顺序问题已经解决，"同类型不同体"不是死路。** `TransposeResolver::resolveWith` 在编译期拿得到实参：
   `normStatics[0].value` 是 `StaticFunction`，它带的是**本程序的 compile 图**。用
   `include/camel/compile/gir/encode.h#encodeToRuntimeProgram` 把它编码进
   `Context::reserveProgram()`（`jvp` 也这么给自己造程序），再跑一次 `transpose.cpp#buildTransposeMember`
   的 probe，就在**写返回类型之前**拿到账。实测印出
   `[probe] norm0=(x: float) => float pure graphId=54 graphName=read_outer needs=1 0`
   —— `read_outer` 要参数 0 的值，`read_one`（同类型、值没被律读）是 `needs=0`。所以"账"能在解析期算出来，
   不需要把发现挪成一次前向预扫，也不需要全参数都留端口（`..ParamsOf<F>` 的 98 个调用点不用动）。
2. **第二道墙：两端口的值一旦被调用，建图中止。** 按第 1 条把类型写成 `(float, float) => (float,)` 之后，
   两种写法、两个后端（`std::nvm` / `std::fvm`）全一样：
   ```camel
   let back = transpose(read_outer)
   println(back(1.0, 1.0))                    // 这条
   println(transpose(read_outer)(1.0, 1.0))   // 和这条
   ```
   ```
   Camel Invariant Violation:
       Location   : ../src/compile/gir/builder_operators.cpp(3923)
       Function   : node_handle_t camel::compile::gir::Builder::visitLinkNode(const GCT::node_ptr_t &)
       Guarantee  : dataType->code() == TypeCode::Function
       Reading    : Target node of LINK must be a function or operator node.
   ```
3. **只落"造图一侧"（needs 通道 + 成员真取到值端口，不动承诺）也不是免费的**：
   `test/plans/feat/compile/transforms.plan.toml` 105 条实测 **97 pass / 4 fail / 4 known**
   （基线 99 / 0 / 6）。四条是同一件事的两面 —— 拒绝**位置变好但变了**：
   - `transpose_hand_closed_lambda.same_answer_as_the_cut`：`TRANSPOSE function=__g_322__ graph=368
     type=(float, float) => (float,)` 之后 `ports=2 promised=1 cause=needs-a-value-outside-the-closure`，
     原来钉的是 `skip=cotangent-needs-a-value uri=:op/mul_d position=0`。**新句子更对**：这条形状要的值
     是闭包成员（正是 `2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` 那一页）。
   - `transpose_many_parameters`：变成 `TRANSPOSE function=mt skip=call-value-missing param=3 node=4 arg=3`
     —— 走得更远，停在"这个调用没把成员要的值传进来"。
   - `pow_gradient_refused_by_the_library`：从 `skip=condition` 变成
     `skip=call-no-transpose node=6 callee=rulesets.jvp::pow_tangent<float>`（真正的墙）。
   - `transpose_refusal_is_one_cause_in_four_places`：`NeedsAValueOutsideTheClosure` 会变成"有词有句没有
     站点"，`test/tools/transpose_refusal/check.py#WAITS_FOR_A_SITE` 要跟着说。
   这四条都是"拒绝更准"，但都不答 29，且要改 4 条 pin —— 单独落它等于用 churn 换措辞。它们也是下一轮
   做 2 端口时会被撞到的清单。

## 2. 1 端口 vs 2 端口：差在哪一步（`std::gir` 两次读数）

同一个程序（用例那条，只是调用点实参不同）。**1 端口（今天，能跑）**：

```
N0D [label="Function(graph=read_outer, tupleSlots=0)", type="DATA", value_type="(x: float) => float pure"];
N0E [label="transforms:transpose", type="OPER", value_type="(float) => (float,)"];
N0F [label="1.0", type="DATA", value_type="float"];
N10 [label="CALL", type="CALL", value_type="(float,)"];
N0D -> N0E [label="0|0", type="NORM"];      // 参数：被转置的函数
N0E -> N10 [label="0|0", type="WITH"];      // 被调值：算子返回的那个函数
N0F -> N10 [label="0|0", type="NORM"];      // 实参：余切
```

**2 端口**：`std::gir` **没有读数** —— 建图在 `#Builder::visitLinkNode` 就中止了，pass 根本没开始跑。
能读到的是**同一个算子节点**改成 `value_type="(float,)"`（答案元组），而不是 1 端口那行的
`"(float) => (float,)"`（成员函数）。所以差别不是 arity，是**把节点类型写少了一层**：

| | 算子节点 `N0E` 的值类型 | 调用怎么建 |
| --- | --- | --- |
| 1 端口 | `(float) => (float,)`（= `transposeSignature` 的返回，即成员） | 另一个 `CALL` 节点，被调值走 **WITH** 边，实参走 NORM —— **间接调用**，arity 在运行时对 |
| 2 端口 | `(float,)`（= 答案元组，少了一层 `=>`） | 没有 `CALL` 可建，落进 **LINK**，目标类型不是 `Function` → `GUARD` 中止 |

`TransposeResolver::resolveWith` 交出去的确实是
`(f: (x: float) => float) => ((x: float, ct: float) => (float,))`
（`transposeSignature` 结尾用的是 `FunctionType::create(withParams, params, TupleType(cotangents))`），
所以少掉的那一层不在它的返回值里，而在**算子节点的类型从哪里取**这一处：今天它等于解析结果的
`exitType()`；两端口时它等于 `exitType()->exitType()`。这是 LINK 那条车道要读的第一处
（`builder_operators.cpp` 里把 `resolveWith` 的答案落到节点类型上的那一步，以及同一处是否有 type-fold 的
第二次取值）。

## 3. 修在哪

第 1 节第 2 条的墙在 `src/compile/gir/builder_operators.cpp`（车道范围外，我按规矩没动）。两种修法：

| 修法 | 做法 | 代价 |
| --- | --- | --- |
| **A：把节点类型写对** | 让一个"返回值是函数"的算子调用节点，`nodeTypeOf` 拿到的就是那个函数类型（`exitType()`），而不是它自己的答案元组 | 要看那一步为什么在两端口时多取了一层。`jvp` 的同类节点是 `(float, float)`、`jvp(f)(3.0, 1.0)` 今天能跑，说明 LINK 对"两实参调一个算子返回值"本身是支持的 —— 差别在节点类型，不在 arity |
| **B：不走 LINK** | 被调值来自一个"返回值是函数"的算子调用节点时，按**间接调用**建（走函数槽），由运行时检查 arity | 与 `6560eb334` 的方向相反（那条 commit 把运行时 arity 失配换成了一句人话），所以 B 只在**类型确实对上**时可用 |

## 4. 这条用例要的东西，语法上是不是允许

`Transpose<F>` 的声明（`stdlib/types.cml#Transpose`）写的是 `(..ParamsOf<F>, ct: …) => …`，甲那页的注释也把
`(x, ct) => (float,)` 当作目标形状。所以**语言层面**"两实参调一个转置值"是允许的。今天不允许的是**编译器**：
它连图都建不出来就中止了。这一句要作者确认，因为它决定第 3 节修哪一侧。

## 5. 面有多大（量出来的）

`transpose(` 的调用点在 `test/cases/**/*.cml` 里 98 处、66 个文件；`type=(float) => (float,)` 的 pin 4 处。
**按第 1 节的账（只给被读到的参数留端口）这些都不用动** —— 这也是为什么第 3 节的墙值得修：修完是
"1 条用例（`two_values_of_two_kinds` 的调用点改 `back(1.0, 1.0)`）+ 4 条 pin"，不是 98 处。

## 6. 我方默认

**承诺先留 1 端口（乙的形状），本车道不落两端口。** 理由：29 要的端口能在解析期算出来（第 1 节已实测），
但算出来之后**没有程序能调到它**（第 1 节第 2 条），落地只会把 1 条用例的 `known_fail` 换成一次中止和
4 条 pin 的 churn。所以本轮 revert 了全部改动，树回到 `1c36c282e` 的基线，回退后逐项与基线相同：

| 计划 | 回退后 | 基线 |
| --- | --- | --- |
| `test/plans/feat/compile/transforms.plan.toml` | 105 total / 99 pass / **0 fail** / 6 known（build 2.7 s，test 3.0 s，load 20.22） | 105 / 99 / 0 / 6 |
| `test/plans/feat/compile` | 2243 total / 2231 pass / **0 fail** / **12 known**（test 51.4 s，load 17.42） | 2243 / 2231 / 0 / 12 |

顺带把甲那页的阈值说清：**它的"超过 20 条用例就改乙"今天不该触发** —— 按第 1 节的账，改的只有 1 条用例的
调用点和 4 条 pin；挡路的是缺陷，不是代价。所以下一泳道该做的是修
`builder_operators.cpp#Builder::visitLinkNode` 的节点类型（第 3 节 A），修完再回来落甲，
那时 `transpose(read_outer)(1.0, 1.0)` = 29、`known 12 → 11` 一起成立。

## 要作者答的一句

**「`transpose(f)` 的返回值（带值端口时的 `(x, ct) => (float,)`）被当函数调用，应该是 LINK 到那个算子节点、
还是走间接调用？我默认『节点类型该是函数类型（第 3 节 A）』，并在 `builder_operators.cpp` 里修；在修好之前
转置承诺保持 1 端口」**