# 标准库评审：`rules`（决策者 2026-10-22，H230 的一部分）

```camel
export rule int32_sub_self(x: i32) { x - x => i32(0) }
export rule int32_combine(x: i32, const c1: i32, const c2: i32) {
    x * c1 + x * c2 => x * (c1 + c2)
} when isI32(c1) && isI32(c2)
```

1. **按类型复制的规则**：`int32_*`、`int64_*`、`float32_*`… 每条定律为每种元素类型写一遍，与 `array::sum` 的四个重载是同一个问题。规则的形参也按 5.5 自动绑定类型变量，一条规则覆盖所有类型：`rule sub_self(x: T) { x - x => 0 }`，字面量在实例里取 `T`。只对某类类型成立的（浮点要 `#requires<no_nans, no_infs>`，整数回绕下精确成立），用 `#requires` 或类型谓词表达，不靠复制。
2. **`when isI32(c1) && isI32(c2)` 是多余的守卫**：形参已经写了 `const c1: i32`，"这个常量是 i32"应当由匹配器按形参类型检查，不由每条规则自己再查一遍。`isI32` 一类的辅助函数随之删除；"畸形图（`add_i` 带 `int` 常量）不匹配"由匹配器的类型检查保证。
3. **`i32(0)`**：随 H228 的字面量取型写成 `0`。
4. 重写后规则条数应大幅下降，`check` 的随机检验按实例类型覆盖每种类型（检验的覆盖不随条数减少）。

## 补充：守卫函数（`combineSameSign32` 一类）

```camel
func combineSameSign32(c1: Node, c2: Node): bool {
    return match constant(c1, f32(0.0)) {
        case null => false
        case a => match constant(c2, f32(0.0)) { … finite32(a) && … } } }
```

5. **`when` 里的 `const` 形参应当就是那个常量值**，类型是形参声明的类型，而不是 `Node`。这样守卫直接写在值上，不需要 `constant(c, f32(0.0))` 取值、也不需要两层 `match … case null`：
   ```camel
   #requires<reassociate>
   export rule combine(x: T, const c1: T, const c2: T) {
       x * c1 + x * c2 => x * (c1 + c2)
   } when is_finite(c1) && is_finite(c2) && c1 != 0 && sign(c1) == sign(c2) && is_finite(c1 + c2)
   ```
   需要节点本身（读事实、读形状）时另用 `node_of(c1)` 一类的显式取法。这是规则语言的缺口，补进规则引擎。
6. **`constant(c, f32(0.0))` 用一个哑值告诉函数"按什么类型取"**：类型应当用 `::<T>` 给（设计原则 5.9），`constant::<f32>(c)`；有了第 5 条，大多数守卫不再需要它。
7. **`finite32(x) = x - x == 0.0` 这类辅助函数**：标准库已有或应有 `math::is_finite`（泛型，按 7.7），规则库不自造，也不按类型分名。
