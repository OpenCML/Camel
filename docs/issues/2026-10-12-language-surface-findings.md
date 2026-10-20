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

## 3.（已解决，见文末）

## 4. `int('4x')` 接受数字前缀

```cml
func main(): int sync { println(int('4x')); println(float('abc')); return 0 }
```

当前：第一行打印 `4`；第二行是故障 `RuntimeDiag::InvalidNumberText`。

期望：两者一致，`int('4x')` 同样是 `InvalidNumberText`（`parse_int('4x')` 已经给出错误值）。

## 5.（已处理，见文末）

## 6.（已修复，见文末）

## 7.（已解决，见文末）

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

期望：联合类型要么可用，要么在声明处被拒绝。**与未决问题有关**：设计原则 3.8“不做通用联合类型的运行时分辨”、第 15 节“类型表达式语言的具体内容”。

## 10.（已处理，见文末）

## 11.（已处理，见文末）

## 12.（已解决，见文末）

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

## 21. `std::opt::inline std::jit` 下很深的原生递归段错误

```cml
func depth(n: int): int {
    return if n == 0 then 0 else 1 + depth(n - 1)
}
func main(): int sync { println(depth(100000)); return 0 }
```

当前：`camel --max-recursion-depth 1000000 t.cml std::opt::inline std::jit` 以段错误结束（core dumped），没有诊断。默认上限下同一个流水线给出正常的 `MaxRecursionDepthExceeded`；`std::nvm`、`std::fvm` 在调高上限后由原生栈检查给出诊断。

期望：原生栈将满的检查覆盖已编译的代码，给出带位置的 `MaxRecursionDepthExceeded`。

**已修复（2026-10-12，`lane/jit-coverage`）。** 已编译的代码在每次原生调用之前，除了递归计数（r15），还把栈指针与 `JitContext::stackLimit` 比较；后者由 `invokeOwnedJitFrame` 在进入原生代码前按当前线程的栈下界加保留量（`execute::nativeStackFloor()`，与解释器的 `nativeStackNearlyExhausted` 同一个保留量）写入。低于它就走故障出口，诊断与解释器相同：`Maximum recursion depth (70103 open calls: the native stack of the thread, 8188 KiB, is full) exceeded when evaluating graph 'depth'`，位置是那次递归调用。实测（8 MiB 主线程栈）：上限一万时各后端都在第一万次调用上报计数超限；上限十万、一百万时，`std::opt::inline std::jit` 下 `depth` 算到约七万层后报栈满（每层 112 字节），`std::fvm` 与不加 `std::opt::inline` 的 `std::jit` 的调用不占原生栈，能算完九十万层；包在 `attempt` 里时故障在边界上变成错误值，之后同一个函数照常运行。测试：`test/plans/feat/consistency/limits.plan.toml` 的 `limit.10000.*`、`limit.raised.native.*`、`limit.raised.isolated`。

## 22.（已处理，见文末）

---

## 缺少的能力

不属于缺陷，单列在这里，供排期参考。

（“导入时不能给名字起别名”已解决，见文末。）

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
- 第 12 条 尖括号的歧义（四种表现）：由两条已裁定的排版规则解决（设计原则 4.1；`docs/technical/34_handwritten_parser.md`）。尖括号实参必须紧贴前一个记号，比较运算符 `<` 前要有空格；换行结束语句。(a)(b) 以 `>` 结尾的语句后面跟一行以字符串或名字开头的语句，现在是两条语句；(c) `println(scale<2>(1), scale<3>(1))`、(d) `zip_with<f>(a, b)->all<p>` 现在按写出来的样子读。`i<n` 这样紧贴的比较是 `SyntaxDiag::ComparisonNeedsSpace`。测试：`test/plans/feat/compile/surface.plan.toml` 的 `surface.angle_then_*`，探针 `test/tools/parsecheck/probes.py`。
- 第 7 条 `^` 的优先级：按设计原则 4.2，`^` 比乘除高一级、从右到左结合（`2 * 3 ^ 2` 是 18，`2 ^ 3 ^ 2` 是 512），一元运算符比它松（`-n ^ 2` 是 `-(n ^ 2)`）。紧贴数字的负号是字面量的一部分，`-2 ^ 2` 仍是 4（待问语言负责人）。
- 第 3 条 带指数的字面量：按 5.1，带指数或小数点的字面量一律是浮点数，`1e-2` 是 `0.01`。
- 导入改名：`import { find as str_find } from str` 可用；`ImportNameConflict` 的建议里给出这个写法。整模块改名 `import m as n` 未裁定。

## 待裁定（修复这批缺陷时遇到的）

- 带符号的数字字面量是一个操作数：`-2->f` 是 `(-2)->f`，而 `-a->f` 是 `-(a->f)`；`-2 ^ 2` 与 `-a ^ 2` 都是先取负。前者是否改成与一元 `-` 一致。
- `1e-2` 这类带负指数、没有小数点的字面量：现在是 `int`（值 0）加警告；定为浮点数，还是编译期错误。
- `println(x)` 的值是 `x`，而签名文件写的是 `=> void`；函数体没有 `return` 时取最后一条语句的值，而语言参考此前写的是“没有 `return` 的函数返回 `void`”。两者合起来使 `func main() sync { println('a') }` 把 `a` 输出两次。
- `inc<1>`（给没有 `with` 形参的函数绑定 `with` 实参）作为值使用时，绑定被静默丢掉：`let f = inc<1>; f(2)` 得 3。是报错，还是定义为部分应用。
