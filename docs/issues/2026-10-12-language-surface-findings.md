# Issue: 编写语言参考与教程时发现的实现缺陷与可疑行为

> 记录日期：2026-10-12。来源：逐条运行语言参考与教程的示例时发现。
> 状态：**未修复**，全部只作记录。每条给出最小复现、当前输出与期望；“期望”是记录者的看法，凡涉及语义的以语言作者的裁定为准。
> 复现方式：把片段存为 `t.cml`，运行 `camel -E short t.cml`（默认后端 `std::nvm`）。

与设计原则未决问题的关系在各条末尾标出；没有标的属于普通的实现缺陷。

---

## 1. 函数组合 `f..g` 的结果不能调用

```cml
func inc(x: int): int { return x + 1 }
func dbl(x: int): int { return x * 2 }
func main(): int sync {
    let f = inc..dbl
    println(f(3))
    return 0
}
```

当前：`[Error]: Function of type 'i64' cannot be called with argument types: <> (i64). (name=SemanticDiag::ArgumentsMismatch)`。`println(inc..dbl)` 打印 `0`。加 `std::macro` 结果相同。

期望：`f` 是函数 `(x: int) => int`，`f(3)` 得 8；或者在含义确定之前，`..` 与 `?..` 一样给出 `ConstructNotSupported`。

## 2. 超出范围的整数字面量静默回绕

```cml
func main(): int sync {
    println(9223372036854775808)
    println(1e400)
    let b: i32 = 3000000000
    println(b)
    println(i32(3000000000))
    return 0
}
```

当前：

```text
-9223372036854775808
-9223372036854775808
-1294967296
-1294967296
```

期望：字面量超出其类型的范围是编译期错误（设计原则 2.7 对“编译期能知道的越界”的态度是编译期错误）。`i32(x)` 对运行期的值是截断还是故障，需要裁定。

## 3. 带负指数的整数字面量是 0

```cml
func main(): int sync { println(1e-2); return 0 }
```

当前：`0`（`1e-2` 按词法是带指数的整数，向零截断）。

期望：编译期错误或警告，提示写 `1.0e-2`；或者把带负指数的字面量定为浮点数。

## 4. `int('4x')` 接受数字前缀

```cml
func main(): int sync { println(int('4x')); println(float('abc')); return 0 }
```

当前：第一行打印 `4`；第二行是故障 `RuntimeDiag::InvalidNumberText`。

期望：两者一致，`int('4x')` 同样是 `InvalidNumberText`（`parse_int('4x')` 已经给出错误值）。

## 5. 直接调用括号里的 lambda；把内建算子当值

```cml
func main(): int sync { println(((x: int): int => x + 1)(3)); return 0 }
```

当前：输出一行裸文本 `bad any_cast`，没有诊断名与位置。`3->(x: int): int => x + 1` 相同。

```cml
func main(): int sync { println(println); return 0 }
```

当前：`Compile graph 'main' still contains a live DREF node at draft ref 1.`（内部错误文本）。

期望：前者得 4；后者是 `SemanticDiag::FunctionNameAsValue`。两者都不应露出内部文本。

## 6. `n-1` 是语法错误

```cml
func main(): int sync { let n = 5; println(n-1); return 0 }
```

当前：`[Error]: Unexpected '-1'. (name=SyntaxDiag::UnexpectedToken)`；`n -1`、`3-1` 相同。原因：词法把正负号并入数字字面量。

期望：`n-1` 是减法。至少诊断应提示“在 `-` 两边加空格”。

## 7. `^` 与 `*` 同优先级

```cml
func main(): int sync { println(2 * 3 ^ 2); let n = 2; println(-n ^ 2); return 0 }
```

当前：`36` 与 `4`。文法里 `^` 与 `*`、`/`、`%`、`@` 同属一级、左结合，一元 `-` 比它们都紧。

期望：`^` 高于 `*` 且右结合（`18`），`-n ^ 2` 为 `-4`；需要语言作者确认。

## 8. `===` 对字符串字面量为假

```cml
func main(): int sync { println('a' === 'a'); println('a' == 'a'); return 0 }
```

当前：`false`、`true`。`===` 对数值比较值，对数组、元组、结构体、字符串比较是否为同一个对象。

期望：`===` 的含义需要裁定；若是同一性比较，它对不可变的字符串没有可预期的结果，宜对字符串按值比较或拒绝。

## 9. 联合类型的形参不接受成员类型的值

```cml
func show(u: int | string): string { return 'u' }
func main(): int sync { println(show(2)); return 0 }
```

当前：`[Error]: No matching function found for argument types: <> (i64). ... show: (u: Union<string, i64>) => string`。`let a: int | string = 1` 是 `LiteralStaticCastFailed`。

期望：联合类型要么可用，要么在声明处被拒绝。**与未决问题有关**：设计原则 2.9“不做通用联合类型的运行时分辨”、第 6 节“类型表达式语言的具体内容”。

## 10. 没有 `else` 的 `if`：诊断里的类型不对

```cml
func main(): int sync {
    let a = 1
    if a < 2 then println('lt')
    return 0
}
```

当前：`[Error]: Return type mismatch between branches (in function 'main: () => i64'): expected 'string', found 'void'. (name=SemanticDiag::BranchReturnTypeMismatch)`。写成 `if a < 2 then sync { println('lt'); return null }` 可以通过。

期望：臂的值是 `void` 的单臂 `if` 作语句应当被接受；诊断里的 `expected 'string'` 与程序无关。

## 11. 两处诊断没有位置

```cml
func main(): int sync { println([1, 2.0]); return 0 }
```

当前：`(main)t.cml:?:?: [Error]: Cannot insert data of type 'f64' into a(n) Array with element type 'i64'. (name=SemanticDiag::ElementTypeMismatch)`。

`import nosuch`（模块不存在）的 `SemanticDiag::ModuleNotFound` 同样是 `?:?`。

期望：定位在那个元素、那条 `import`。

## 12. 以 `>` 结尾的语句与下一行并成比较

```cml
func main(): int sync {
    let total = [1, 2]->reduce<(acc: int, x: int): int => acc + x, 0>
    'total = {}'->format<total>->println
    return 0
}
```

当前：`[Error]: ... operator '>' is not defined for these operand types`（`0> 'total = {}'->format<total>` 被读成比较）。同一个实参表里写两个带尖括号的调用 `f<a>(x), g<b>(y)` 也被读成比较。

期望：需要裁定换行是否结束语句，或 `with` 实参的尖括号如何与比较区分。

## 13. 带类型的声明：函数类型与可空元素

```cml
func inc(x: int): int { return x + 1 }
func main(): int sync {
    let f: (x: int) => int = inc
    println(f(1))
    return 0
}
```

当前：`[Error]: The feature 'dynamic type casting' is not supported yet. (name=SemanticDiag::FeatureNotSupported)`。`let xs: (int?)[] = [1 as int?, null as int?]` 相同；去掉类型标注都可以通过。

期望：类型一致的带类型声明被接受。

## 14. `string(3.5)` 与 `println(3.5)` 的文本不同

```cml
func main(): int sync { println(3.5, string(3.5), string(1.0)); return 0 }
```

当前：`3.5 3.500000 1.000000`。

期望：两者一致，或在标准库参考里写明 `string` 对浮点数固定 6 位小数。

## 15. 命令行与文档的出入

- `camel --help` 里 `-S` 的占位符拼作 `<schedular type>`；`check -i, --lexical-only` 的说明是“indentation size in spaces”。
- `docs/cli.md`“Pass pipelines”说默认遍是 `std::nodevm` 的别名，别处与 `AGENTS.md` 写 `std::nvm`。
- `docs/cli.md`“What `std::macro` reports”末段含提交哈希与“此前的行为”；“Resource limits”表中“`std::jit` 编译后的递归不计数、不检查”一行与现有测试（`limit.direct.exceeds.native`）不符。
- `docs/cli.md` 的 inspect 一节没有列出 `camel inspect` 实际接受的 `--color`、`--max-errors`、`-N`。

---

## 已不成立、从清单中去掉的

- 字符串下标 `s[i]` 运行时故障：现在按字节取出单字节字符串（结果是字符串还是数值，尚待裁定）。
- `stdlib/signatures/globals.cml` 列出不存在的函数：签名文件现已与实现对齐，并由 `scripts/stdlib-reference.mjs --verify-ops` 约束。
