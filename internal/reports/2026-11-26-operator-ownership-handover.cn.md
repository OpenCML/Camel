# `O-3` 的归属：四个事实、一张表、两笔提交

这一页是「去掉前缀判断」（路线图 item O 的第三条）开工前的交接。写它的原因是执行这一格的车道在动手前读到上下文余量尽头、主动停手；停手前查实的四件事如果只留在对话里，下一位要重推一遍。**代码零改动，`lane/facts-one` 停在 `ef3d40af2e`，`git status` 空。**

## 一、要改的七处

`src/kernel/execute/pass.cpp` 里，「内核自己的 pass 住在 `std::` 下」这条事实被写成字符串判断七遍：

| 行 | 现在判什么 | 改成问什么 |
|---|---|---|
| 365 | `alias.starts_with("std::") \|\| passAliases.contains(alias)` | 这个名字是不是登记过的内核 pass |
| 427 | `written.rfind("std::", 0) == 0` | 同上 |
| 634 | `resolvedName.rfind("std::", 0) == 0` | 同上 |
| 856 | `path.rfind("std::",0)==0 \|\| path.rfind("test::",0)==0` | 归属是不是 Core 或 Test |
| 883 | `path.rfind("test::", 0) != 0` | 归属**不是** Test（方向相反，取反须保持） |
| 991 | `sep == npos \|\| sep == 0 \|\| name.substr(0, sep) == "std"` | `a::b` 的前半段归属是不是 Core（无 `::` 的名字也归 Core） |
| 2533 | `moduleName == "std" \|\| moduleName.starts_with("std::")` | 这个模块的归属是不是 Core |

## 二、四个事实（都查过原文，不必重推）

1. **`registerPassKernel` 不是内核专用。** `src/stdlib/rewrite/module.cpp:1868` 的 `rewrite::extract` 就调它。于是「走这个入口 ⇒ 内核归属」会把一个模块 pass 静默标成内核的——这是错的，而且是静默的。`registration.cpp:2` 是两个重载的定义，`registration.h:4` 是声明，库调用点只有 rewrite 那一处。
2. **内核的 `std` 不在 `builtinModuleFactories` 里。** `src/kernel/core/module/builtin.cpp:17` 只有 `""`、`test_attrs`、`profiler`；`std` 是从内核自带的 stdlib 根**解析**出来的普通模块。所以「在不在 builtin 集合里」回答不了这一格的主要对象。
3. **`Context::builtinModules_` 是私有成员，没有公开访问器。** 见 `src/kernel/include/camel/core/context/context.h:136`。即使只想判 builtin 那三个，也要先给它加一个访问器——而它回答不了 `std`。
4. **`test::` 的归属由注册者在登记期标。** 那些 pass 在 `src/stdlib/test/*.cpp` 与 `src/stdlib/std/selftest/params_test_passes.cpp` 里显式注册，既不是 builtin 工厂，也不是「从 `-I` 根找到的」。所以 856/883 问的是「归属是不是 Test」，不是「名字是不是以 `test::` 开头」。

## 三、归属表

```
位置    Context 私有段（context.h:136 旁），与 builtinModules_ / loadedModules_ 同一所有者
键      Module*（稳定身份：不用名字，不用解析根字符串）
值      归属枚举：Core / Test / Module
写入    ModuleResolver 解析成功之后写一次
        重复解析同一模块 ⇒ 重复写同值（不依赖「只写一次」这个未验证的假设）
读取    Context 上一个 const 访问器，pass.cpp 七处只问它
寿命    表与模块同属一个 Context（module.cpp:153 说明归属），模块随 Context 销毁 ⇒ 键不会悬空
```

## 四、两笔提交

```
第一笔  解析期登记归属（context.h + context.cpp）
        跑 smoke 取 [SUMMARY] 作基线；报 build exit 与 git diff --numstat
第二笔  pass.cpp 七处改问登记表
        报：七行原文（不再有 "std" / "std::" / "test::"）；
            991 的反例——短名仍可用，贴 `camel xxx.cml nvm` 的 stdout 原文；
            registerPassKernel 三个点（registration.cpp:2 定义、registration.h:4 声明、rewrite/module.cpp:1868 库调用点传 Module）；
            smoke 与 feat/compile 的 [SUMMARY]，与上次晋级红表逐名比，新增 0
```

分两笔的理由：审的人能分别验；第二笔出问题时不必回滚第一笔。

## 五、这一格不该做的事

- 不在 `pass.cpp` 里引入另一个字符串比较（换 `"std::"` 为别的写法是搬家不减法）。
- 不给 `PassDescriptor` 加归属字段：它被九个文件提到，其中 `onnx`、`tensor`、`tiles`、`transforms` 是库的构造点——那等于把内核的账摊给四个库。
- 不动 `GATE` 的布局（那是 item O 的另一条），不动计划与金标。