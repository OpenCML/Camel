# 只在 no-JIT / arm64 构建里才跑得到的用例，算不算把这条码按名字钉住了？

## 问什么

`RuntimeDiag::JitWithoutCodeGenerator`（`0x04100018`）说的是"流水线点名了 `std::jit`，但这个构建没有
代码生成器"。它**由编译期宏决定存不存在**：`src/execute/pass.cpp#jitWithoutCodeGeneratorWarning`
在 `ENABLE_FASTVM_JIT` 为 1 时整个函数体就是 `return std::nullopt`，只有 `#else` 分支才去扫
`resolvedPassList`，把 `resolveAlias(specName(spec)) == "std::fastvm::jit"` 的那一条变成警告
（`std::jit` 指向它，见 `src/execute/pass.cpp#passAliases`）。`cmake/Options.cmake#add_platform_option`
只对 `x86_64|AMD64|amd64|X86_64` 打开这个宏，其余处理器默认关；也可以手写成 `OFF`——CI 的 nojit 腿
就是这么做的（`.camel-opts.json` 里 `{"ENABLE_FASTVM_JIT": "OFF"}`，见 `.github/workflows/ci-leg.yml`
的 "Switch the JIT off"）。

**结论先说：没有哪个程序能走到它，能走到它的是构建。** 闸门这台 linux-x64 的构建里这个码被编译掉了，
所以程序、遍参数、`--log` 级别、环境变量、`-Werror` 都没有任何一条能让它出声。

于是"一条判据没有可测对象"这件事要按构建拆开问。`feat/linear/jit_without_codegen.plan.toml` 一直
有两条用例：一条要它出现（句子），一条要 `-Wno-jit-without-code-generator` 把它静音。两条都靠
`skip_unless = ["node", "test/tools/have-native-jit.mjs", "--absent"]` 挑选构建，因此在 x86-64 闸门上
恒为 skip。`056a8bf0a` 在那条用例上补了 `expect_diagnostic_name`，`--name-coverage-check` 从
`1 of 44` 变成 `0 of 44`。**这个 0 是文本上的 0：它证明有人点名，不证明闸门这一轮的运行里有人点名。**

要作者裁的就是口径：名字计数该不该收一条只在非默认构建里执行的断言？

## 读数（可复核）

同一棵树 `056a8bf0a`，闸门 `-Plan test/plans/feat/linear/jit_without_codegen.plan.toml -Tier fast -Jobs 4`：

- 默认构建（linux-x64，`ENABLE_FASTVM_JIT=ON`）：`total: 2, pass: 0, fail: 0, skip: 2`，两条都是
  `precondition not met (node test/tools/have-native-jit.mjs --absent): exit 1`。
- 同一棵树把 `~/camel-work/jitcase/.camel-opts.json` 写成 `{"ENABLE_FASTVM_JIT": "OFF"}` 再走同一个
  闸门（构建日志里 `-DENABLE_FASTVM_JIT=OFF`、`-- ENABLE_FASTVM_JIT=OFF`）：`total: 2, pass: 2, fail: 0`。
- 在**同一个** no-JIT 安装上把断言改成错的码（`RuntimeDiag::PassesNeverRun`）：`[FAIL]
  without_codegen.warning`，`[INFO] expected diagnostic RuntimeDiag::PassesNeverRun`，`pass: 1, fail: 1`；
  把名字改回来立刻 `pass: 2`。所以这条断言真的在执行、真的有牙，不是被 skip 挡在位子上的摆设。
- 反方向，在闸门的 JIT 安装上把最狠的要求给它：`camel -Werror hello.cml std::jit` → 退出 0、stderr 空；
  `camel -v hello.cml std::jit` 的日志里没有 `no native code generator`。它被编译掉了，不是被静音了。
- `test/tools/run-tests.mjs#buildHasNoCodeGenerator` 的 `CAMEL_TEST_NO_JIT` 只喂 `silenced_warnings`
  （默认把这条通知静音，见 `40b3bf4b6`），skip 的前置条件是另一个进程
  `test/tools/have-native-jit.mjs --absent`，那个环境变量管不到它：没有"用环境变量掰过去"的第二条路。
- 计数器本身是静态的：`--name-coverage` 读 `test/plans/**` 的文本，改断言前后（0 of 44）与 skip 无关；
  `--text-coverage-check` 读 `expect_stderr_contains` 的句子，这条码原本就是 `28 of 44 (recorded 27)`
  里的一个，加名字断言不动它。

## 三条路，各带代价

**A. 收它（当前树就是这个答案）**
- 计数器的口径是"树里的文本点名了这个码"，与哪台机器、哪个构建无关；`expect_diagnostic_name` 挡的是
  **改名**，而改名跟构建无关——arm64 与 nojit 腿每次跑都会执行它。
- 代价：x86-64 闸门的日常读数里，这个码的"可测对象"是**别处**的构建；读 `0 of 44` 的人不会知道这一点，
  除非有人把这件事写下来（本页就是那块牌子）。

**B. 让计数器认 `skip_unless`，把构建里跑不到的断言排除在分母外**
- 好处：`0 of 44` 只在真的会执行它的机器上出现，数字与"跑过什么"一致。
- 代价：计数器得会解释前置条件（至少认识 `have-native-jit.mjs --absent` 这一个），还得起子进程问本机；
  于是**同一个提交在不同机器上读出不同的数**（x86-64 是 `1 of 44`，arm64 是 `0 of 44`），而这张表
  现在的整个设计是"在一个树上量一次、把数记下来当棘轮"。这条码也就永久留在"没被按名字钉住"那一列，
  直到有人把闸门挪到 arm64 上。

**C. 删掉这个码，或者降回 `-v` 的日志行**
- 这是它加进来之前的样子（`36b526c7b`：原来只有 `-v` 才看得到的日志行），也是唯一能让 `1 of 44` 直接
  归零的另一条路。
- 代价：arm64 / nojit 构建的用户写 `std::jit` 又拿不到任何说明；`docs/cli.cn.md`、`docs/README.cn.md`
  已经承诺了这条警告，要一起改；而且这正是 `36b526c7b` 当初修的那个问题（"等于没告诉任何人"）。

## 默认（本轮按这个开工，不等回复）

**走 A**，`056a8bf0a` 就是它。理由：这条断言的用途是挡**改名**，改名与构建无关；它在 arm64 与 nojit
腿上每次都在跑（本页第二节量到 `pass: 2`，且改成错码会红），所以它不是自证——真正缺的只是"读到
`0 of 44` 的人要知道这 0 从哪里来"，那一句写在用例的注释里，也写在这里。B 会让同一个提交在两台机器上
读出两个数，与这张表"记一次当棘轮"的用法冲突；C 要同时收回两份文档的承诺，超出这条车道的范围。

作者若裁 B，改动只在 `scripts/diagnostics-catalog.mjs` 的名字计数与它上面那段说明（本车道没有碰
`scripts/**`）；若裁 C，用例里那条 `expect_diagnostic_name` 与两条 `skip_unless` 一起删。
