# 声明写下的 extent 到不了调用点，`constas` 值也管不住数组参数的 extent

## 现象（都用构建出的二进制复现过，H17 第 2 项施工中）

`f32[4]`、`f32[n]` 这些写法（H17 P2/P3，已在 develop：`ecaa31f58`、`fee698bb9`）今天只有一种实参
能让调用点真正读到 extent：用 `as` 明确写出来的值。另外三种同样自然的写法都读不到。

1. **声明写下的 extent 既不检查，也不进入值的类型**

```camel
func show(x: f32[4]): int { return len(x) }
func main(): int sync {
    let a: f32[3] = [1.0, 2.0, 3.0]
    println(show(a))      // 报 NoMatchingFunction，实参类型印成 `f32[]`
    return 0
}
```

同一个声明写成 `let x: f32[4] = [1.0, 2.0, 3.0]` 会直接跑起来并印 `3`：声明处的长度没有被检查，
字段和实参都拿不到它写下的 3 或 4。

2. **`constas` 值不固定数组参数的 extent**（同一件事在 `Tensor` 写法上是被拒绝的）

```camel
func total(constas IN: int, x: f32[IN]): int { return len(x) }
func main(): int sync {
    println(total(4, [1.0, 2.0, 3.0]))        // 印 3
    let v: f32[] = [1.0, 2.0, 3.0]
    println(total(4, v as f32[3]))            // 也印 3
    return 0
}
```

`test/plans/feat/compile/constas.plan.toml` 的 `constas_argument_shape` 用 `Tensor<f32, [n]>` 写同
一件事，那里是被拒绝的（`GenericArgumentsMismatch`）。

3. **空字面量可以站在固定长度的位置上**：`func count(x: f32[4])`，`count([])` 编译通过，运行时长度 0。

## 为什么需要裁决

`docs/design-principles.cn.md` 5.4 与 `docs/design/arrays-memory/plan.cn.md` P3 都把"调用点绑定维度"
当作形状检查的主场：实参的 extent 静态已知时绑定，未知时由运行时的转换补检查。今天第一条几乎没有
可查的东西（只有 `as` 写出的值带 extent），第二条没有实现——extent 未知的实参进 `f32[n]` 参数既不
绑定也不补检查，直接放行。于是下面这个程序今天可以通过：

```camel
func dot(x: f32[n], y: f32[n]): f32 { return x[0] + y[0] }
let a: f32[3] = [1.0, 2.0, 3.0]
let b: f32[2] = [4.0, 5.0]
println(dot(a, b))        // 印 5.0
```

## 选项与代价

- **(A)（建议）让声明写下的 extent 成为值的类型**：`let a: f32[3] = …` 之后 `a` 的类型就是 `f32[3]`，
  调用点、`as`、返回处都自然读到。代价：`f32[3]` 按 5.4 是不可追加的，所以 `var a: f32[3] = …` 之后
  `a->append(x)` 会从合法变成编译错误——"声明写下长度"是否等于"值就是那个长度"，需要作者判一次。
- **(B) 只在声明处检查长度，值的类型不变**：`let x: f32[4] = [1, 2, 3]` 报错，但 `show(a)` 仍然因为
  实参是 `f32[]` 而无法参与绑定。代价：规则还是只剩下 `as` 一条路，普通程序里 P3 的绑定查不到东西。
- **(C) 明确 `as` 是唯一写法并写进文档**：代价最小，但等于承认声明写下的维度只是给读者看的，与 5.4
  "形状检查由核心负责"的意图相反。
- **(D) 单独补 `constas` 的 extent 检查**（与上面三条独立）：把 `substituteExtentValues` 之后的参数
  类型也用于字面量与实参的匹配。代价：动的是泛型调用路径，`Tensor` 与数组两条路要一起看。

## 影响

这是 H17 第 2 项内部的一致性问题，不牵动 P4（`Tensor` 别名）和 P5（按第一维的高阶函数）；P4 之后
`Tensor` 与数组是同一种类型，(A)/(B) 的边界会更清楚。本分支上每条现象都有判别用例或复现记录。

## 来源

- `handover/h348-h17-dims`（基点 `3ee0447f7`，release `261023_141104_3ee0447f7_dirty`）施工中的探针。
- 正向与反向用例：`test/cases/compile/array_shape/err_call_mismatch.cml`、`err_call_linear.cml`
  （`as` 写出的 extent 之间矛盾时，核心确实报 `SemanticDiag::DimensionMismatch`）、
  `unknown_not_inferred.cml`、`constant_dimension.cml`。
- 判定矛盾的那一行：`src/compile/gir/builder_operators.cpp` 的 `bindNamedExtents`（诊断处）→
  `Type::bindExtents`（`src/core/type/base.cpp:207`）→ `ExtentForm::bind`（`include/camel/core/type/extent.h:105`）。