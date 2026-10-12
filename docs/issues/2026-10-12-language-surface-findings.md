# Issue: 编写语言参考与教程时发现的实现缺陷与可疑行为

> 记录日期：2026-10-12。来源：逐条运行语言参考与教程的示例时发现。
> 状态：**未修复**，全部只作记录（不在文档工作中修改编译器）。每条给出最小复现、当前输出与期望；“期望”是记录者的看法，凡涉及语义的以语言作者的裁定为准。
> 复现方式：把片段存为 `t.cml`，运行 `camel -E short t.cml`（默认后端 `std::nvm`）。

与设计原则未决问题的关系在各条末尾标出；没有标的属于普通的实现缺陷。

---

## 1.（已处理，见文末）

## 2. `i32(x)` 对超出范围的运行期值截断

```cml
func main(): int sync { println(i32(3000000000)); return 0 }
```

当前：`-1294967296`。

期望：`i32(x)` 对运行期的值是截断还是故障，需要裁定。（本条原先还包括超出范围的字面量静默回绕，那一部分已修复，见文末。）

## 3. 带负指数的整数字面量是 0

```cml
func main(): int sync { println(1e-2); return 0 }
```

当前：`0`（`1e-2` 按词法是带指数的整数，向零截断），现在附带一条带位置的警告 `SemanticDiag::IntegerLiteralTruncated`，提示写 `1.0e-2`；值没有变。

期望：由语言作者裁定带负指数的字面量是浮点数还是编译期错误；在此之前保持警告。

## 4. `int('4x')` 接受数字前缀

```cml
func main(): int sync { println(int('4x')); println(float('abc')); return 0 }
```

当前：第一行打印 `4`；第二行是故障 `RuntimeDiag::InvalidNumberText`。

期望：两者一致，`int('4x')` 同样是 `InvalidNumberText`（`parse_int('4x')` 已经给出错误值）。

## 5.（已处理，见文末）

## 6.（已修复，见文末）

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

## 10.（已处理，见文末）

## 11.（已处理，见文末）

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

## 15.（已处理，见文末）

## 16.（已处理，见文末）

## 17.（已处理，见文末）

## 18.（已处理，见文末）

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

## 20.（已处理，见文末）

## 21. `std::inline std::jit` 下很深的原生递归段错误

```cml
func depth(n: int): int {
    return if n == 0 then 0 else 1 + depth(n - 1)
}
func main(): int sync { println(depth(100000)); return 0 }
```

当前：`camel --max-recursion-depth 1000000 t.cml std::inline std::jit` 以段错误结束（core dumped），没有诊断。默认上限下同一个流水线给出正常的 `MaxRecursionDepthExceeded`；`std::nvm`、`std::fvm` 在调高上限后由原生栈检查给出诊断。

期望：原生栈将满的检查覆盖已编译的代码，给出带位置的 `MaxRecursionDepthExceeded`。

## 22.（已处理，见文末）

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
- 第 6 条 `n-1` 是语法错误（更严重的表现是 `let b = a-1` 把 `b` 绑定为 `a`、静默丢掉 `-1`）：词法的数字记号不再带正负号，两个操作数之间的 `-`、`+` 总是二元运算符；在需要操作数的位置上紧跟着数字的符号由语法规则 `literal` 读作字面量的符号（`-1`、`case -1 =>`、`-9223372036854775808` 不变）。同一根源的 `t.0.1`（`0.1` 被读成一个浮点数）也已可用。测试：`test/plans/feat/consistency/surface.plan.toml`、`test/plans/feat/parse/numbers.plan.toml`。
- 第 2 条中超出范围的字面量静默回绕（`9223372036854775808`、`1e400` 得到最小整数，`let b: i32 = 3000000000` 截断，`1.0e400` 输出裸的异常文本）：现在是带位置的 `SemanticDiag::LiteralOutOfRange`；`-9223372036854775808` 仍然合法。测试：`test/plans/feat/compile/literals.plan.toml` 的 `literal_range.*`。
- 第 1 条 函数组合 `f..g`：在含义确定之前是 `SemanticDiag::ConstructNotSupported`（按原“期望”的第二种）；组合的语义没有实现，仍待裁定。
- 第 5 条 直接调用括号里的 lambda 与把内建算子当值：`((x: int): int => x + 1)(3)`、`3->(x: int): int => x + 1` 现在可用；`println(println)`、`println(len<1>)` 是带位置的 `SemanticDiag::FunctionNameAsValue`。
- 第 10 条 没有 `else` 的 `if`：规则未改（臂必须没有值），诊断改为 `SemanticDiag::IfArmValueWithoutElse`，说明原因与写法。是否允许臂不写 `return null` 仍待裁定。
- 第 11 条 两处诊断没有位置：数组字面量的 `ElementTypeMismatch` 定位在那个元素，`ModuleNotFound`（以及 `ImportedModuleHasErrors`）定位在那条 `import`。
- 第 15 条 `camel --help` 的文字：`<scheduler type>`、`-i`/`-s` 的说明、`-P` 在 release 构建不产生报告的说明已改。
- 第 16 条 `?->` 右侧直接写 lambda：现在可用（与第 5 条同一个根因）。
- 第 17 条 把无参的库函数当值用：`pi` 只有一个签名，现在与用户函数一样是函数值（`println(pi)` 打印函数，`pi()` 是调用）；不再有内部错误。
- 第 18 条 库算子作函数值：只有一个固定签名的库算子可直接作值（`xs->map<trim>`）；有多个签名的名字在被调用者的形参类型能唯一选出一个时取那一个，否则是列出各签名的 `FunctionNameAsValue`。
- 第 20 条 `camel check`：发现错误时退出码为 1；`-i` 只做词法分析，`-s` 做到语法树为止。
- 第 22 条 入口的两处细节：语法错误诊断里的模块名现在是文件声明的名字；未声明返回类型、以 `println(...)` 结尾的 `main` 返回被打印的值是现有规则的结果（函数体没有 `return` 时取最后一条语句的值，而 `println(x)` 的值是 `x`），已写进语言参考 7.4.1 与 4.1，行为未改，两条规则是否保留见下面“待裁定”。
- 其他工作线记录的几条：`zip` 传三个数组时只用前两个（并且按三元组的类型读两元素的元组）——现在按数组个数构造元组；`head([])` 返回零值——现在是 `RuntimeDiag::IndexOutOfRange`；模块导入自身时段错误——现在是 `SemanticDiag::ImportCycle`。
- 第 12 条 (a)(b)：语法未改；在被读成比较的那个 `>` 处增加了警告 `SyntaxDiag::ComparisonAcrossLines`，指出两条语句可能被连成了一条。(c)(d) 不变。

## 待裁定（修复这批缺陷时遇到的）

- 带符号的数字字面量是一个操作数：`-2->f` 是 `(-2)->f`，而 `-a->f` 是 `-(a->f)`；`-2 ^ 2` 与 `-a ^ 2` 都是先取负。前者是否改成与一元 `-` 一致。
- `1e-2` 这类带负指数、没有小数点的字面量：现在是 `int`（值 0）加警告；定为浮点数，还是编译期错误。
- `println(x)` 的值是 `x`，而签名文件写的是 `=> void`；函数体没有 `return` 时取最后一条语句的值，而语言参考此前写的是“没有 `return` 的函数返回 `void`”。两者合起来使 `func main() sync { println('a') }` 把 `a` 输出两次。
- `inc<1>`（给没有 `with` 形参的函数绑定 `with` 实参）作为值使用时，绑定被静默丢掉：`let f = inc<1>; f(2)` 得 3。是报错，还是定义为部分应用。
