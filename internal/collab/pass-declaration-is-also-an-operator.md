# 一条遍声明怎样同时是"遍名"和"算子"（C1 ② / C2 的钥匙）

来源：`lane-editview-a` 在 2026-11-28 01:3x 定位（`[provenance] tip 8ce4bd4e4e` 一系）。此前多轮把这一步当成"C1 ② 必须动编译器"，是错的。

## 机制（原文位置）

```
src/compile/src/gir/builder.cpp:943-959  visitDeclNode：
    if (isPassDeclaration(gct)) {                 // 形状 (f: F) => F! + when is_function::<F>()
        unit_->passDeclarations.insert(gct->name());
        if (!declarationHasOperator(gct) && !nativeOperatorUri(gct).has_value()) { return nullptr; }
    }
    declareNativeOperator(gct);                    // 带 #op 的同一条声明落到这里
src/compile/src/gir/builder.cpp:906-921  declarationHasOperator：存在 #op('uri') ⇒ true
```

⇒ **同一条遍形声明加 `#op('std:cse')` 后，两个身份同时成立**：它仍是"遍名"，同时成为已登记的算子。

## 顺序被写死

`#op` 指向的算子**必须有人实现**，否则在声明处就被拒绝（"an operator nothing implements is refused below"）。

⇒ **先注册算子（模块执行器的 `kernels()`），再给声明加 `#op`**。反过来就是载入期拒绝。

这也说明为什么"只写算子入口、暂不动命令行"（曾拟的 2a）是坏切法：没有 `#op` 时程序里写不出这个名字，那份登记**没有任何执行路径**，只能证明编译过。

## 命令行那一侧的缝（`lane-editview-a` 原文）

```
src/kernel/execute/pass.cpp:189-196   findImplementation(path) = 路径 → OperatorTable::lookup(name) → findPassKernel(id)
src/kernel/execute/pass.cpp:2295-2309 applyRegisteredPass = resolvePassWithModule → factory → pass->apply(program, os)
```

`applyRegisteredPass` 拿到的是「遍」，不是「函数值」；命令行那句名字要变成"算子"，靠的是**声明到算子的映射**，不是遍内核表。所以"命令行改走同一算子 + 同笔删 `generic.cpp:2195` 的 `PassKernel`"不能单独落地 —— 删了就找不到遍内核，命令行立刻不认识这个名字（C1 ② 的未解析引用换个位置出现）。

## 落地顺序（必须原子成笔）

```
① module.cpp 的 kernels() 加 {"cse", cseKernel} + 访问声明（读 graph，记录带 readsTags）
② std.cml:402 的 cse 声明加 #op('std:cse')（照 onnx.cml:48 形状），其余不动
③ applyRegisteredPass 的 cse 分支改走同一算子
④ 同笔删 generic.cpp:2195 的 PassKernel
```

判据：命令行 `std::jgir canonical=true` 与改动前逐字节相同；程序里调与命令行跑逐字节相同；`readsTags` 原文；快层逐名新增红 0。
## 第三个消费者：导出那一步（2026-11-28 01:43 d1 读数钉死）

加了 `#op` 之后名字仍 `Unresolved reference: 'std::cxx::cse'`。原因不在载入、不在 `#op`、也不在"安装根的 `std.cml` 是旧的"（d1 三行打印：`module="std"` 解析到 `out/latest/stdlib/std.cml`、`hasOp=yes`、`exportDeclarations reached` 多次 ⇒ 那份是新的、builder 确实编译了模块）。

真因是**名字形状两侧不一致**：

```
src/compile/src/gir/builder_modules.cpp:1351-1358  导出循环
    const std::string name = ref.toString();               // "cxx::cse"（限定名）
    if (unit_->passDeclarations.contains(name)) continue;  // passDeclarations 里也是 "cxx::cse"
    而声明的裸名是 "cse"（builder.cpp:949 插的是 gct->name()）
⇒ 判定时拿"限定名"和"裸名"比 ⇒ 恒不相等 ⇒ 例外永远不成立
```

所以例外要**按末段比**（`cxx::cse` → `cse`），注释写："the export walks qualified names while a declaration's own name is bare"。

## 三个消费者，一个都不能漏

```
① builder.cpp:943-959        载入/声明：带 #op 的遍形声明同时登记为算子
② builder_modules.cpp:1351    导出：名称要按末段比，否则带 #op 的也被当"纯遍"跳过
③ pass.cpp:615-637           命令行：按文本读 std.cml 的遍声明（第二个消费者，与编译期不同源）
```

**检查清单**：将来任何"两条身份合一"的活，先把 `passDeclarations` 的**所有**消费者 `git grep` 出来，逐个问"它认的是裸名还是限定名"。
