# 标准库评审：`array`（决策者 2026-10-22）

按 [`docs/stdlib-conventions.cn.md`](../../stdlib-conventions.cn.md) 6.5 "写法"。目标形态（示意，具体以实现为准）：

```camel
export func reverse(xs: T[]): T[]               { return xs[::-1] }          // 若负步长不支持，补进切片
with <n: int> export func take(xs: T[]): T[]    { return xs[:clamp(n, 0, len(xs))] }
with <n: int> export func drop(xs: T[]): T[]    { return xs[clamp(n, 0, len(xs)):] }

with <x: T> export func index_of(xs: T[]): int? {
    let i = loop i = 0 while i < len(xs) && xs[i] != x do i + 1
    return if i < len(xs) then i else null
}
with <pred: (x: T) => bool> export func find(xs: T[]): T?     { return xs->index_where<pred>->map_opt<(i) => xs[i]> }  // 或同样的 loop
with <pred: (x: T) => bool> export func any(xs: T[]): bool    { return loop hit = false for x in xs while !hit do pred(x) }
with <pred: (x: T) => bool> export func all(xs: T[]): bool    { return loop ok = true for x in xs while ok do pred(x) }
with <pred: (x: T) => bool> export func count(xs: T[]): int   { return len([x for x in xs if pred(x)]) }

export func sum(xs: T[]): T                     { return xs->reduce<(a, x) => a + x, 0> }      // 0 在实例里取 T
export func min(xs: T[]): T?                    { return if len(xs) == 0 then null else xs->reduce<math::min, xs[0]> }
export func max(xs: T[]): T?                    { return if len(xs) == 0 then null else xs->reduce<math::max, xs[0]> }

with <key: (x: T) => K> export func sort_by(xs: T[]): T[]  { return xs[argsort(xs->map<key>)] }
with <f: (a: T, b: U) => V> export func zip_with(xs: T[], ys: U[]): V[] { return zip(xs, ys)->map<((a, b)) => f(a, b)> }
```

逐条：
1. 去掉全部 `typeas` 与 lambda 参数注解（H229）。
2. `take`/`drop` 用切片加 `clamp`，不写三层 `if`；`reverse` 用负步长切片——不支持就补进切片语义。
3. `index_of`/`find`/`any`/`all` 用 `loop … while` 提前结束，删去 `first_true` 与"None of them stops early"的说明；模块注释改写：谓词必须是纯的，实现可以提前结束，也可以由 `#impl` 内核并行求值——两者结果相同。
4. `sum` 一个泛型定义（H228 的字面量缺口修好后）；`min`/`max` 用 `math::min`/`math::max` 归约（NaN 与 ±0 遵守 7.7）。数值数组上这几个配 `#impl` 归约内核。
5. `sort_by` 用 `xs[idx]` 取位置；`argsort`、`flatten`、`unique` 随 H19 第 5 步成为 `array` 自己的私有原生声明，删去 `algo::` 前缀。
6. `zip_with` 的 lambda 若不支持元组形参解构 `((a, b)) => …`，补进语言（与 `let (a, b) = p` 同一规则）。
7. 上面示意里用到、而语言还没有的（负步长、元组形参解构、可选值上的 `map`），各自记为语言缺口补上，或换成已有写法；不在库里绕。
