# Issue: 编写语言参考与教程时发现的实现缺陷与可疑行为

> 记录日期：2026-10-12。来源：逐条运行语言参考与教程的示例时发现。
> 状态：**未修复**，全部只作记录（不在文档工作中修改编译器）。每条给出最小复现、当前输出与期望；“期望”是记录者的看法，凡涉及语义的以语言作者的裁定为准。
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

## 12. 尖括号的歧义（语法层面，一组）

这一组是同一个语法问题的几种表现：`with` 实参的尖括号与比较运算符 `<`、`>` 用同一对记号，换行又不结束语句，解析器在几种写法下把尖括号读成了比较。它不是某个遍或某个算子的缺陷。

**(a) 以 `>` 结尾的语句，下一行以字符串字面量开头：**

```cml
func main(): int sync {
    let total = [1, 2]->reduce<(acc: int, x: int): int => acc + x, 0>
    'total = {}'->format<total>->println
    return 0
}
```

当前：`[Error]: No matching function found for argument types: <> (i64, string); operator '>' is not defined for these operand types.`（`0> 'total = {}'->format<total>` 被读成比较。）

**(b) 同上，下一行以标识符开头：**

```cml
func main(): int sync {
    let total = [1, 2]->reduce<(acc: int, x: int): int => acc + x, 0>
    total->format<total>->println
    return 0
}
```

当前：`(main)t.cml:3:5: [Error]: Unresolved reference: 'total'.`（第 3 行被并进了第 2 行的初值，此时 `total` 还没有声明。）下一行不含尖括号时（`total->println`）没有问题。

**(c) 同一个实参表里有两个带尖括号的调用：**

```cml
with <k: int>
func scale(x: int): int { return x * k }
func main(): int sync {
    println(scale<2>(1), scale<3>(1))
    return 0
}
```

当前：`[Error]: No matching function found for argument types: <> (<k: i64> (x: i64) => i64, i64); operator '<' is not defined for these operand types.`

**(d) 带尖括号的调用后面再接带尖括号的管道：**

```cml
import { zip_with, all } from array
func main(): int sync {
    println(zip_with<(x: int, y: int) => x == y>([1, 2], [1, 2])->all<(e: bool) => e>)
    return 0
}
```

当前：`[Error]: The generic function 'zip_with' is used as a value ... (name=SemanticDiag::GenericFunctionAsValue)`。拆成两条语句（先 `let equal = zip_with<...>(a, b)`，再 `equal->all<...>`）可以通过。

期望：需要裁定换行是否结束语句，以及 `with` 实参的尖括号如何与比较区分；在此之前，这几种写法至少应得到指向歧义的诊断。**与未决问题有关**：设计原则 2.9 认定“类型位置上的尖括号与 `with` 参数的尖括号没有歧义”，没有涉及表达式位置上尖括号与比较的歧义。

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

## 15. `camel --help` 的文字

- `-S` 的占位符拼作 `<schedular type>`。
- `check -i, --lexical-only` 的说明是“indentation size in spaces”，与选项无关。
- `-P, --profile` 在 release 构建里被接受但不产生报告，帮助里没有说明。

期望：帮助文字与行为一致（`docs/cli.md`、`docs/cli.cn.md` 已按实际行为写）。

## 16. `?->` 右侧直接写 lambda

```cml
func main(): int sync {
    let r = parse_int('4') ?-> (x: int): int => x + 1
    println(r)
    return 0
}
```

当前：输出一行裸文本 `bad any_cast`，退出码 1，没有诊断名与位置。右侧写函数名（`?-> inc`）正常。与第 5 条是同一类表现。

期望：`r` 是 `int!`，打印 `5`。

## 17. 把无参的库常量当值用

```cml
import { pi } from math
func main(): int sync { println(pi); return 0 }
```

当前：`Compile graph 'main' still contains a live DREF node at draft ref 1.`（内部错误文本）。写成调用 `pi()` 正常。

期望：`SemanticDiag::FunctionNameAsValue`，提示写 `pi()`。

## 18. 库算子不能直接作函数值

```cml
import { trim } from str
func mytrim(s: string): string { return trim(s) }
func main(): int sync {
    println([' a ', ' b']->map<mytrim>)
    println([' a ', ' b']->map<trim>)
    return 0
}
```

当前：第一个 `map` 可以；第二个是 `[Error]: No matching function found for argument types: <void> (string[]). ... <:mark/map_arr>: <f: (item: T) => U> (collect: T[]) => U[]`。

期望：只有一个重载的库算子与用户函数一样可以当值用；有多个重载的给出 `FunctionNameAsValue`。

## 19. `match` 的一个臂是值、另一个臂是 `throw`

```cml
type Row = { n: int }
func parse(k: int): Row! {
    return match k {
        case 0 => { throw error('Zero', 'k is zero') }
        case _ => { n: k }
    }
}
func main(): int sync { println(parse(1)); return 0 }
```

当前：`[Error]: Return type mismatch between branches (in function 'parse: (k: i64) => { n: i64 }!'): expected '{ n: i64 }!', found '{ n: i64 }'. (name=SemanticDiag::BranchReturnTypeMismatch)`。把第二个臂写成 `{ return { n: k } }` 可以通过。

期望：在返回 `T!` 的函数里，作为返回值的分支的值臂与 `return v` 一样进入 `T!`。**与未决问题有关**：`DESIGN-QUESTION(nullable-error)` Q1（`T` 是否隐式流入 `T!`）与 Q9（两臂形式不同）。

## 20. `camel check` 的退出码与 `-i`/`-s`

```cml
func main(): int sync {
    let x = undefined_name + 1
    return 0
}
```

当前：`camel check -O short t.cml` 打印 `UnresolvedReference` 错误，退出码为 0。`camel check -s`（只做语法检查）、`-i`（只做词法分析）仍然报告这条语义错误：两个选项被解析但不起作用。

期望：发现错误时退出码非 0；`-i`、`-s` 按说明限制检查范围，或者去掉。

## 21. `std::inline std::jit` 下很深的原生递归段错误

```cml
func depth(n: int): int {
    return if n == 0 then 0 else 1 + depth(n - 1)
}
func main(): int sync { println(depth(100000)); return 0 }
```

当前：`camel --max-recursion-depth 1000000 t.cml std::inline std::jit` 以段错误结束（core dumped），没有诊断。默认上限下同一个流水线给出正常的 `MaxRecursionDepthExceeded`；`std::nvm`、`std::fvm` 在调高上限后由原生栈检查给出诊断。

期望：原生栈将满的检查覆盖已编译的代码，给出带位置的 `MaxRecursionDepthExceeded`。

## 22. 入口的两处细节

```cml
func main() sync {
    println('printed by the program')
}
```

当前：`camel t.cml -o out.txt` 退出码为 0，`out.txt` 的内容是 `printed by the program`：没有声明返回类型、函数体以 `println(...)` 结尾的 `main` 不被当作不返回值。声明为 `: void` 时，`-o` 是 `RuntimeDiag::EntryResultNotWritten`。

期望：两种写法一致。

```cml
module my_module
func main(): int sync {
    let x =
    return 0
}
```

当前：`(main)t.cml:4:5: [Error]: Unexpected 'return'; expected an expression. (name=SyntaxDiag::UnexpectedToken)`：语法错误诊断里的模块名是 `(main)`，不是文件声明的 `my_module`（语义错误的诊断用的是声明的名字）。

期望：用声明的模块名。

---

## 缺少的能力

不属于缺陷，单列在这里，供排期参考。

### 导入时不能给名字起别名

```cml
import { find as str_find } from str
```

当前：`[Error]: Expected '}' or ',' before 'as'. (name=SyntaxDiag::MissingComma)`。

两个模块有同名函数时只能用到其中一个：

```cml
import { find } from str
import { find } from array
func main(): int sync {
    println('hello'->find<'l'> ?? -1)
    println([1, 2, 3]->find<(x: int): bool => x > 1> ?? -1)
    return 0
}
```

当前：第二个调用是 `NoMatchingFunction`，可用的重载只列出 `str:find`；把两条 `import` 换个顺序，则第一个调用失败。两条 `import` 本身不报错。可行的写法是只导入一个，另一个用限定名（`import array` 后写 `array::find`）。

期望：支持 `import { a as b } from m`；同名的导入要么合并为重载，要么在第二条 `import` 处报告冲突。

---

## 已不成立、从清单中去掉的

- 字符串下标 `s[i]` 运行时故障：现在按字节取出单字节字符串（结果是字符串还是数值，尚待裁定）。
- `stdlib/signatures/globals.cml` 列出不存在的函数：签名文件现已与实现对齐，并由 `scripts/stdlib-reference.mjs --verify-ops` 约束。
