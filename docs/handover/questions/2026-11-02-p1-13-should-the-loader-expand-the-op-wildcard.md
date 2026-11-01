# `#op` 的通配，该由加载器展开，还是每个名字各写一条？

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

1. **加载器不展开 `*`**：它把那串字符当成一个 URI 去找，找不到就报上面那句。所以通配只是参考文件里的**书写约定**，不是机制。
2. **要覆盖这 13 个 URI，只有两条路**：写 13 条各自命名的声明（`from_bytes_f32`、`load_npy_bf16`……，等于给用户新增 13 个模块级的名字），或者给加载器加通配展开。
3. **通配会让"声明覆盖了哪些算子"不再能一眼数清**：P1-13 的三栏表是按"一条声明 ↔ 一个调用形状"配对的（`docs/handover/reports/2026-11-02-p1-13-three-columns-call-shape.cn.md`）；一条通配背后是 5 个还是 50 个 URI，只有跑一遍加载器才知道，表也就没法静态数。

## 问题

`#op` 的通配该由加载器展开（一条声明覆盖一族），还是每个名字各写一条声明？

- **A. 每个名字各写一条，加载器不认通配**：声明与 URI 一对一，三栏表能静态数；代价是 13 个新名字，且以后每加一个元素类型就要加一条。
- **B. 加载器展开通配**：一条声明覆盖一族，参考文件的写法就是机制；代价是"声明覆盖了哪些算子"要跑 `inspect --ops` 才知道，三栏表的配对也得改成"一族 ↔ 一批"。
- **C. 别的**：例如通配只允许在声明文件里、由另一个工具展开成生成物。

## 默认（作者未另裁时按此开工）

**A：不给加载器加通配**，这 13 行（`from_bytes_*` 8 + `load_npy_*` 5）记为**"签名表达不了（无通配）"**，不进"真缺"，也不写 13 条新名字。理由：三栏表是 P1-13 的底数，一对一才数得清；把 13 个新名字摆上台面是更大的语言决定，不该由这一点顺手做掉。

`等点名`。