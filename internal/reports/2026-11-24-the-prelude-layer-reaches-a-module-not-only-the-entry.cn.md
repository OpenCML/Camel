# prelude 的导出层：① 实测成立 —— 每个模块都用程序共享的那张表

- 日期：2026-11-24
- 来源：`lane/red-b-fixes`，回答 `internal/reviews/2026-11-24-prelude-functions-with-bodies-and-layer-5.cn.md`
  （负责人裁决 `fsh-dev 238927c3b`）
- 结论：**①**。标准库模块这一侧没有洞，不改编译器。

## 1 读数（先量后改）

三个探针点（临时仪器化，未提交）：两个构造器各打一行，`Builder::preludeEntity` 在
`to_array` 上打一行「表是谁的、表里有几个 prelude 导出、找到没有」。被量的是一次编译：
模块 `prelude_caller`（非入口，按 library 编）调 `to_array`，不写 import。

```
[TBL] Builder(shared) this=0x57156c16fb80 table=0x57156c16ab60
[TBL] preludeEntity(to_array) unit=prelude_caller table=0x59ff303d7510 \
      ownExports_=0x59ff303dc850 sharedTable=1 preludeExportsInThatTable=11
[TBL]   to_array -> FOUND (prelude=prelude)
```

三件事一起读，缺一件都不成结论：

1. **`Builder(single-arg)` 一次也没被构造。** 它不是「被构造了但表是空的」，是**没走到**。
2. 模块用的表 `sharedTable=1`，即 `exports_ != &ownExports_`。
3. 那张表里 prelude 名下有 **11** 个实体，`to_array` **FOUND**。

## 2 为什么单参数构造器不在路上

引用（`src/compile/src/gir/builder.h`）：

```cpp
/// A builder for a program of its own: it owns the program's export table (ProgramExports).
explicit Builder(const camel::core::context::context_ptr_t &context)
    : context_(context), exports_(&ownExports_) {}
```

成员同一文件写着它的语义：

```cpp
/// The program's export table (see the constructors): `exports_` points at `ownExports_`
/// for a builder of a program of its own, or at the context's table for the context's
/// program. Never at a module's table: that is the module's own (native) declarations.
camel::core::module::ProgramExports ownExports_;
camel::core::module::ProgramExports *exports_;
```

它唯一的构造点是 `src/compile/src/module_source.cpp:253`（`buildProgramOfItsOwn`，只走
`UserDefinedModule::compileAsProgram`，即把模块当**独立 program** 编的 pass 模块）。
标准库模块走的是另一条：`buildLibrary` 在 `module_source.cpp:225` 用
`Builder::of(module.context())`，而 `Builder::of`（`program.cpp:230`）是

```cpp
session = std::make_shared<Builder>(context, context->programExports());
```

即 `exports_ = &context->programExports()`，程序内每个模块共用这一张。

## 3 这一笔换来了什么

- 新用例 `test/cases/compile/stream/prelude_in_module.cml` + 模块 `prelude_caller.cml`：
  非入口模块不写 import 调 `to_array`，普通体与泛型体各一次（泛型体在**声明时**就被检查，
  比普通体严）。两个文件都不 import `prelude`。**它在 `9e7717001b` 之前在泛型体上报
  `Unresolved reference: 'to_array'`（`stdlib/random.cml:114:62`），现在是绿的。**
- `test/cases/compile/stream/interval.cml` 删掉 `import { Range, to_array, is_empty } from prelude`
  （`e49bdeae88` 写在 `bc90856729` 之前的基点上）。**删前删后三次打印一字不差**，所以那行
  在今天**没有作用**；删它是这一层的回归测试，不是改写用例。

## 4 读数与它的条件

`feat/compile/stream.plan.toml`，158 条：**157 pass / 1 fail**，commit `d72f86cfe7`，主机
load 10.7→9.5（脚本自报「readings taken now are not comparable to quiet-host readings」）。

那一条红是 `stream.interval_loop_not_materialised`：CPU 5600 ms 对 20 s 超时的 1/4 线（5 s），
红在 runner 的超时余量规则，不是用例行为（`[stdout] 2000000`，`[stderr] (empty)`）。它是
两百万轮的循环，与本笔改动无关（本笔只删了一个用例的 import、加了两个用例）；**基线读数应在
load < 8 的安静主机上取**。

## 5 记下的一处文档漂移（未改）

`test/plans/feat/compile/prelude_signatures.plan.toml` 的 `unimported_function_is_in_scope`
的 `desc` 仍写着「The body of a generic function is the one place this does not hold yet」，
而 `9e7717001b` 已经补上 `valueNameExists` 那一行、泛型体也成立。desc 是文档，不是断言，
所以它不红——但那句话现在是错的。