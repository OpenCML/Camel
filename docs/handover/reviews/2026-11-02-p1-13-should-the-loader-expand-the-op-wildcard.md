# `#op` 的通配，该由加载器展开，还是每个名字各写一条？

## 先说更正：通配是有效的，只是**只看内置组**

`stdlib/prelude.cml#bf16` 今天就有一条**能生效的**通配声明：

```
#op(':op/*tos')
export func string::<T>(x: T): string
```

它覆盖 8 个 URI（`:op/itos`、`:op/ltos`、`:op/ftos`、`:op/dtos`、`:op/stos`、`:op/ctos`、`:op/ztos`、`:op/atos`），每个都在 `camel inspect --ops` 里以 `name: "string"` 出现。机制在 `src/compile/gir/builder_modules.cpp`：

```
if (const auto builtins = context_->importModule("")) {
    if (const auto entity = builtins->getNativeEntity(Reference(name));
        entity.has_value() && entity->isOperGroup()) {          // 名字必须是内置组
        for (const auto &[ruleUri, rule] : entity->operGroup()->resolvers())
            if (rule != nullptr && matches(uri, ruleUri)) { claim... }
const bool ownRule = !claimed && uri.find('*') == std::string::npos && ...   // 带 * 就不能自成规则
```

也就是说：**带 `*` 的 `#op` 只能靠"声明名是内置算子组、组的规则表里有匹配"来绑定**；名字若不是内置组（`tensor` 模块的 `from_bytes`、`load_npy`），`claimed` 为假，`ownRule` 又被 `*` 否掉——那条通配**没有生效的路**。所以问题不是"加载器会不会展开"，而是"**只有内置组能展开**"。

## 仍然存在的问题

`stdlib/signatures/tensor.cml` 用**一条带通配的声明**覆盖了 13 个算子（`tensor:from_bytes_*` 8 个、`tensor:load_npy_*` 5 个）：

```
#op('tensor:from_bytes_*')
export func from_bytes::<T>(bytes: Bytes, shape: int[]): T[..] when is_plain::<T>()
```

这份文件自己的头写着 "not compiled"，所以这行通配**从来没被加载器读过**。把同样两行搬进被编译的 `stdlib/tensor.cml` 之后，加载器直接拒绝：

```
error[SemanticDiag::NativeDeclarationNotLoaded]: The declaration 'from_bytes' names the
operator 'tensor:from_bytes_*', which no loaded module implements.
```

## 事实

1. **通配只对内置组生效**：`:op/*tos` 能用是因为 `string` 是内置算子组（`src/builtin/modules/globals.cpp` 里 `OperatorGroup::create("string", {...})` 的规则表），而 `tensor:from_bytes_*` 的名字 `from_bytes` 是模块算子，不在内置组里；带 `*` 的 URI 又被 `ownRule` 直接排除（`uri.find('*') == std::string::npos`），于是它没有绑定成功的路。
2. **要覆盖这 13 个 URI，只有三条路**：写 13 条各自命名的声明（`from_bytes_f32`、`load_npy_bf16`……，等于给用户新增 13 个模块级名字）；把这些规则改成"组 + 通配 URI"式的注册（像 `string` 那样，是 C++ 侧的改动）；或者放宽 `ownRule` 对 `*` 的排除。
3. **通配会让"声明覆盖了哪些算子"不再能一眼数清**：P1-13 的三栏表要按"前缀 + 后缀"去匹配通配 tag 才能数（这一轮就因此改了量法：原来按 URI 后缀当名字，把 `:op/dtos` 当成 `dtos`，漏了 `string` 那条通配声明）。

## 问题

`#op` 的通配该由加载器展开（一条声明覆盖一族），还是每个名字各写一条声明？

- **A. 每个名字各写一条，加载器不认通配**：声明与 URI 一对一，三栏表能静态数；代价是 13 个新名字，且以后每加一个元素类型就要加一条。
- **B. 加载器展开通配**：一条声明覆盖一族，参考文件的写法就是机制；代价是"声明覆盖了哪些算子"要跑 `inspect --ops` 才知道，三栏表的配对也得改成"一族 ↔ 一批"。
- **C. 别的**：例如通配只允许在声明文件里、由另一个工具展开成生成物。

## 默认（作者未另裁时按此开工）

**A：不改加载器、不加通配通道**，这 13 行（`from_bytes_*` 8 + `load_npy_*` 5）记为**"签名表达不了（无通配）"**，不进"真缺"，也不写 13 条新名字。理由：三栏表是 P1-13 的底数，一对一才数得清；把 13 个新名字摆上台面、或给模块算子开一条内置组才有的通配通道，都是更大的语言决定，不该由这一点顺手做掉。

`等点名`。

## 补一条 2026-11-02 的更正

上面第 1 条原来写的是"加载器不展开 `*`"，那是错的：`:op/*tos` 就是一条今天生效的通配声明（见 `docs/handover/questions/2026-11-02-p1-13-the-eight-tos-rows-serve-the-string-call.md`）。准确的机制是"**只有内置算子组能展开**"，`tensor` 的 `from_bytes` 不在内置组里。
