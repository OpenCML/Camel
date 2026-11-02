# `#op` 通配：一处生效、一处失效，同一个写法两个效果

> **裁定关系（显式作废）**：作者 23:20 与 00:10 的裁定**取代**了 `87754891f` 那一笔的结论方向。`87754891f` 说的是"`*tos` 这 8 行靠一条通配声明覆盖、不必逐条写"；作者裁定的是**"这 8 行本来就不该按类型各有一行"**——它们是同一个算子按类型分的 kernel，应当**合并成一个泛型算子**，内建组的通配展开随之删掉。本文按后一条走，前一条的方向作废。

## 一、同一个写法，两个效果（可跑出来的证据）

两条最小程序，**同一个通配 URI**，只有声明名不同：

```
#op(":op/*tos")
export func dtos::<T>(x: T): string
```
```
error[SemanticDiag::NativeDeclarationNotLoaded]: The declaration 'dtos' names the operator
':op/*tos', which no loaded module implements.
```

```
#op("tensor:from_bytes_*")
export func from_bytes::<T>(b: Bytes, s: int[]): T[..]
```
```
error[SemanticDiag::NativeDeclarationNotLoaded]: The declaration 'from_bytes' names the operator
'tensor:from_bytes_*', which no loaded module implements.
```

而 `stdlib/prelude.cml` 里**同一串** `#op(':op/*tos')` 却生效——因为它的声明名是 `string`，是内置算子组。代码里那条判据就在 `src/compile/gir/builder_modules.cpp`：

```
if (context_->preludeModule() != nullptr && unit_->module == context_->preludeModule()) {
    const auto matches = [](const std::string &pattern, const std::string &candidate) {
        const size_t star = pattern.find('*');            // 只有前奏这一支认得 *
        ...
const bool ownRule = !claimed && uri.find('*') == std::string::npos && ...   // 带 * 就不自成规则
```

**通配不是"语言的一条规则"，是"前奏模块里的一段特例"**：`*` 只在"声明所在模块 == 前奏"时是模式，别处就是一串普通的字符。作者说的"同一类事一条规则"正是这里被破坏的地方。

## 二、删之前先 `git grep`：靠这 8 行的不止一处

`git grep -n "\*tos"`：

```
stdlib/prelude.cml:815:#op(':op/*tos')
```

通配本身只有前奏这一处。但**这 8 个 URI 的名字在 C++ 侧被直接点名**（`git grep -c "op/dtos\|op/itos\|op/ftos\|op/ltos\|op/stos\|op/ctos\|op/ztos"`）：

| 文件 | 命中 |
|---|---|
| `src/builtin/executors/builtin.cpp` | 7（`{"op/itos", __itos__}` … 每个 URI 一个内核） |
| `src/builtin/modules/globals.cpp` | 7（每个 URI 一条规则） |
| `src/passes/trans/cpp/operator_rules.cpp` | 3（`:op/stos` → `identity()`，`:op/ctos`、`:op/ztos` → `runtimeFunction("toText")`） |

第三行是关键：**C++ 后端按 URI 名给算子指定实现**，删掉这 7 个 URI 就同时删掉了后端的映射。

## 三、试着合并：注册表侧成立，别处断

**做得成的部分**（本轮实际改过、编过）：把 `string` 组的 7 条静态规则删掉，只留那条**本来就是泛型**的

```
{":op/atos",
 DynamicFuncTypeResolver::create({{0, {}}, {1, {false}}}, "(x: T) => string",
     [](const type_vec_t &, const type_vec_t &norm) -> optional<Type *> {
         if (!isConcreteValueType(norm[0])) { return nullopt; }
         return Type::String();
     })},
```

它的内核 `__atos__` **已经与类型无关**：

```
slot_t __atos__ (ArgsView &with, ArgsView &norm, Context &ctx) {
    std::ostringstream oss;
    printSlot(oss, norm.slot(0), norm.type(0));      // 打印任意 slot
    return toSlot(String::from(oss.str(), mm::autoSpace()));
}
```

也就是说"一个算子、一个 URI、kernel 按类型选"在注册表层**是有的**——`string` 组里早就有这条泛型规则。

**断掉的部分**（同一份改动，全量 5 个失败）：

```
[FAIL] print_and_recompile            std::cml cannot print describe: the operator ':op/atos' ...
[FAIL] registry.signatures_match      --verify-ops: expected exit 0, got 1
[FAIL] registry.result_types_match
[FAIL] registry.parameter_types_match
[FAIL] registry.migrations_stay_migrated
```

`print_and_recompile` 说明 C++/CML 那条打印路径**按名字找具体的 `*tos` 算子**，泛型规则替不了它；`registry.*` 三例是 `--verify-ops` 的签名比对（声明与注册规则的参数表对不上）。所以"只删 7 条规则 + 留一条泛型"不成立——**合并必须一起改掉那些按 URI 点名的位置**，那不是签名层的一处改动。

## 四、这一笔的读数

- **代码没有留在树上**：改动已全部撤回，树回到 `8376/8364/0`（`inspect --ops` 655 行、`--verify-ops` 559 agree 不变）。
- **不删任何东西**：8 行仍在，"只删不并"没有发生。
- **合并要做的事**（下一步的清单，按上面的证据排）：
  1. 注册表：`string` 组留一条泛型规则（`(x: T) => string`），7 条静态规则删掉；
  2. 内核：`__atos__` 已通用，`builtin.cpp` 的 7 条映射删掉、留 `{"op/atos", __atos__}`；
  3. **C++ 后端**：`operator_rules.cpp` 的 `:op/stos`/`:op/ctos`/`:op/ztos` 三条映射改成指向剩下的那条 URI；
  4. **CML 打印路径**：`std::cml` 打印时按什么名字找算子，要跟着改（`print_and_recompile` 就是它）；
  5. 前奏：`#op(':op/*tos')` → `#op(':op/atos')`；
  6. 加载器：删掉前奏那一支的 `matches`（`*` 一律不是模式），`ownRule` 的 `uri.find('*')` 判据随之只作防御；
  7. `from_bytes_*` 同形：`modules/tensor/ops/binary_ops.cpp` 的 `{"tensor:from_bytes_f32", FromBytesResolver(TypeCode::Float32)}` 表与 `add("from_bytes_f32", …)` 内核登记同样要合成一条读类型实参的规则；
  8. 文档：`docs/stdlib-reference.md` 与 `--verify-ops` 的计数要重生成（合并后 `inspect --ops` 从 655 降到 **648**：8 行并成 1 行）。

**三栏的影响**（做到第 1–6 项之后）：655 → 648 行；"已覆盖" 487 → **480**（8 行进 1 行，仍算覆盖）；"真缺" 22 与"签名表达不了" 146 不变——因为它们本来就按 `name` 计，合并只改 URI 的条数，不改"哪些调用形状有声明"。

## 五、关键发现：CML 打印路径按**名字**找算子

`print_and_recompile` 的那条红不是副作用，它是这一笔最重要的读数：

```
std::cml cannot print describe: the operator ':op/atos'
```

`std::cml` 把一段程序打回文本时，遇到"把值变成文本"的调用，它要认出这是不是一条算子、叫什么符号。**合并把这 8 条规则变成一个算子之后，打印路径认不出来了**——因为**它认的是"哪条 kernel"（按 URI），而不是"哪个算子"（按名字/符号）**。同一段程序，规则是 8 条时它认得出，是 1 条时认不出——这说明**打印路径的事实源是 URI 的条数，不是算子的身份**。

这与今晚那三处"同名不同义"是一家：**名字（URI）被当成事实源，而它只是实现的一个名字**。合并一个算子的规则时，凡"按 URI 数条目"的地方都要跟着改。

**要回答的一句**：**打印路径该按算子还是按名字找？** 我的判断是**按算子**——打印器要写的是 `string(x)` 这个**符号**（`prelude.cml` 里 `string` 就是它的名字），不是 `:op/dtos` 这条 kernel；kernel 是运行时按类型选的东西，打印器不该知道它有几个。但这条判断牵动 `cml.cpp` 的符号表（`operatorSymbol(uri)`）与 `operator_names.cpp` 的 `kForms`（`string` 不在可重载清单上，它**不是符号**，是转换调用），所以按你的第 ③ 条**写成页、请作者裁**：

- **A. 打印器按名字找**：认 `string(x)` 的调用为转换调用，写 `string(x)`，完全不看 kernel 有几条；（我倾向的）
- **B. 打印器按 kernel 找**：保留今天的做法，那 8 条规则就不能合成 1 条——合并与打印相斥；
- **C. 别的**：例如打印器把 `string(x)` 当作普通直接调用打印，不管它是不是算子。

**默认（作者未裁时按此办）**：**A**，但**这一笔不做**——`cml.cpp` 的符号表与 `kForms` 的边界要作者定，改错了会让 `print_and_recompile` 从"拒一条"变成"打错一段"。

## 六、这一笔为什么停在这里（按第 4 条纪律）

试做时走的顺序是上面的 1 → 2 → 5 → 6，结果红了 5 条（`print_and_recompile` 与四条 `registry.*`）。第 4 项（`cml.cpp`）不是"改一处调用"，而是**要先答上面的问题**；第 3 项（`operator_rules.cpp`）依赖第 4 项的答案（后端与打印器必须认同一个名字）。**所以第 9 件事出现了：`cml.cpp` 的符号表要按哪个名字认算子，得作者定。** 按第 4 条纪律停手报，不把范围扩到 `cml.cpp` 的符号表与 `kForms` 的边界。

代码已全部撤回，树是绿的；`inspect --ops` 655、`--verify-ops` 559 agree、全量 `8376/8364/0`。

`等点名`。