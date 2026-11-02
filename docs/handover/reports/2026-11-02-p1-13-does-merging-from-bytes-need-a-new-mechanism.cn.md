# 合并 `from_bytes_*` 需要一个新机制吗

## 三条读数（原文）

**一、`DynamicFuncTypeResolver` 看不到调用点的显式类型实参。** 它拿到的只有参数的类型表：

```
using ResolverFunc = std::function<std::optional<Type *>(
    const type_vec_t &, const type_vec_t &)>;                 // include/camel/core/type/resolver.h:20
std::optional<FunctionType *> resolve(
    const type_vec_t &with, const type_vec_t &norm) const override;   // 同上 :259
```

`with` 是 `with` 参数的类型、`norm` 是普通参数的类型，**没有"这条调用的类型实参"这个入口**。`::<T>` 那一层在签名与调用节点上，但**解析器的回调签名里没有它**。

**二、`withStaticResult` 的判据是参数的常量值，不是类型实参。** 原文（`src/builtin/modules/globals.cpp:120`）：

```
/// The rule with a result callback that also reads the call's constants (`withStatics`: the path
/// for an operator whose result type follows the values the program wrote).
```

它给的是 `static_args_t`，而 `static_args_t` 的定义是**"编译期已知的某个参数的值"**（`value` 是常量槽、`elements` 是就地数组的长度），**不是类型**。

**三、注册表里没有一个条目在读静态类型实参。** `withStatics` 的使用点只有三处（`globals.cpp:714`、`2006`、`2035`），全部读的是**常量值**（长度、形状这类），**没有一处在读 `::<T>`**。

## 结论：今天做不到，按默认建议不合并

`from_bytes_*` 的 8 条规则靠**每个 URI 硬编码自己的 `TypeCode`** 才知道 `T` 是什么（`FromBytesResolver(TypeCode::Float32)`）。要合成一条，那条规则必须**从调用点读出类型实参**——而今天**没有任何机制把类型实参送到解析器手里**。

**默认建议**：**`from_bytes_*` 暂不合并**。理由是两条：

1. 合并它需要**新机制**（"读静态类型实参并据此分派"），今天没有，而造一个机制是作者的决定；
2. **作者 00:10 指出的那个病（通配一处活、一处死）已经随 `*tos` 的通配删除而消失**——那次合并要清的是"一条通配声明代表 8 条规则"；**`from_bytes_*` 从来没用过通配**，它是 8 条独立规则 + 8 条 TypeCode 驱动的解析器。**病已经治好，剩下的是"能不能更省"的整洁问题，不是缺陷。**

## 若将来要做，形状是这一个

加一个"解析器能看到调用点的类型实参"的入口（`resolve` 再加一个 `const type_vec_t &typeArgs`，或一个新的 `resolveWithTypeArgs`），然后 `from_bytes` 一条规则：

```
(::<T>)(bytes: Bytes, shape: int[]) => T[..]      // T 由 typeArgs[0] 决定
```

**判据会是**：注册表少 7 行（8 条 URI 并成 1 条）、`inspect --ops` 由 649 降到 642、`--verify-ops` 少 7、全量绿。**今天这三条一个都拿不到**，因为第一步（机制）不存在。

`等点名`。