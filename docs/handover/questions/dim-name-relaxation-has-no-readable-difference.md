# 维度名放宽为 `?` 在图上没有可读差异（有一处读数请你定）

你 2026-10-25 的裁定（`docs/handover/reviews/questions-2026-10-24-evening.md` §recursion-specialization-fixed-point-followups）要求：沿调用环同一个函数以**相同的键**再次被实例化就用环上已有的实例（读法 E）；键不同时 extent 与维度名**放宽为 `?`**；元素类型不同则报编译错误。按第 7 条，我们补了形参写 `T[n]` 的多态递归用例并取读数，读数如下。

## 用例与读数

`test/cases/opt/generic/specialize_dim_name_recursion.cml`（3 条，注册在 `test/plans/feat/opt/generic.plan.toml:819-880`）：

```camel
func walk(n: int, xs: f32[n]): f32 {
    let h = len(xs) / 2
    return if n <= 0 then xs[0] else xs[0] + walk(n - 1, xs[0 .. h])
}
println(walk(3, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]))
```

- stdout `4.0`；stderr 只有既有的 `warning[SemanticDiag::DimensionNameUsedOnce]`，**放宽路径不产生任何诊断**。
- `std::opt::specialize std::stats std::nvm` → `GRAPH_STATS {"graphs":4,"nodes":48,"edges":59,"opers":19,"direct_calls":3,"indirect_calls":1,"closures":0,"branches":2,"loops":0,"bytes":1948,"specializations":1}`。
- `std::opt::specialize std::jgir`：`walk | (n: int, xs: f32[n]) => f32`、`walk$spec0 | (n: int, xs: f32[n]) => f32` —— **实例签名就是声明本身**，没有被绑成 `f32[8]`；实例内唯一调用指回基函数 `walk`，main 内唯一调用指 `walk$spec0`。
- 上限 8 与 4096 下 GRAPH_STATS、签名、`std::opt::fold std::opt::specialize` 读数**逐字节相同**（4 图 / 1 实例 / 48 节点），所以用例把"有界"写成实测值 + 这个对照，不靠计数上限。

## 要你定的两件事

1. **放宽这一步在语法内读不到**：这个用例里"每层取一半"用的是 `len(xs)/2`，那是**运行时值**、不是类型里的静态 extent，所以静态键**自始不动**，"键不同 → 放宽为 `?`"从未被走到；实例的 extent 文本始终是声明的维度名 `n`，不是 `?`。我们没能写出"递归参数的静态 extent 每层为不同常量"的写法（`T[n]`/`f32[n]` 只能带名字与静态式，`n - 1` 不是合法 extent 实参）。请给一条**能读到 `?`** 的写法（或确认这条路径在今天的语法下不可达、只作内部防卫）。
2. **这条用例分不出读法 E 与放宽**：它对 h463 的提交、h463 的父提交（`fe9e17133`）以及把 `instanceRoot` 早返回子句开/关，读数都相同。它钉住的是"实例签名 = 声明本身 + 调用不造新实例 + 总数 1"，这是真的、也需要钉；但"是哪条在起作用"没有读数。若要区分，同样需要上面那种写法。

另：**元素类型不同的递归**（按 5.5 应报"递归调用改变了元素类型"）我们还没写用例，正在另派一条车道补，读数会另报。