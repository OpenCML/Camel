# ASan 腿的 SEGV 在 develop 上复现：站点是 `fastvm.cpp:1025:30`，行号零漂移

**日期**：2026-11-22。**基线**：`develop = 3cf717a42`（`git fetch` 后 `merge --ff-only`，无变化）。
**工作树**：`D:\Projects\Camel-wt\asan`（`lane/asan`，本线**不产提交**，产物是读数）。
**CI 对照**：run `44630939267`、分支 `lane-win-s1-6`、commit `e7e006d1`，`ASan + UBSan (linux-x64, Debug, smoke)`。
**状态**：① SEGV **复现**，站点与 CI 一字相同；② 行号**未漂移**；③ 22 条红，其中 21 条带 ASan 站点、1 条是 Debug 断言（真错）。**未修、未提交。**

## 1. 怎么建的这一份 ASan 构建

`.github/workflows/ci.yml` 的 `sanitizers` job 的参数照搬到 88 主机（本机不构建）：

```bash
export CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
export CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
export LDFLAGS='-fsanitize=address,undefined'
export CAMEL_TARGET_ARCH=x86-64-v3
export CAMEL_CONAN_BUILD_TYPES=Release,Debug,RelWithDebInfo
node scripts/install.js && npm run debug          # npm run debug = Debug + collect-out debug
```

核过编译行（`build/compile_commands.json`）：**2130/2130 条都带** `-fsanitize=address,undefined` 与
`-march=x86-64-v3`；`runFramedJit` 自己那个 `.o` 有 541 处 `__asan` 引用。跑计划时的环境与 CI 同：
`ASAN_OPTIONS=detect_leaks=0:symbolize=1`、`UBSAN_OPTIONS=print_stacktrace=1`、`CAMEL_TEST_BUILD=debug`。

**与 CI 的实际差别只有两处**：clang **23.1.2**（CI `clang-19`）、`llvm-symbolizer` **14**（CI `llvm-symbolizer-19`）。
架构同为 `x86-64-v3`（`-march=x86-64-v3` 在缓存里记成 `native`，但编译行是 v3）。

## 2. 三个计划的读数（develop，ASan Debug，`--jobs 8`）

| 计划 | total | pass | fail | 备注 |
|---|---|---|---|---|
| `smoke` | 10 | 10 | 0 | 绿 |
| `feat/compile/stream.plan.toml` | 147 | 132 | 15 | `killed by a signal: 1` |
| `feat/compile/transforms.plan.toml` | 216 | 207 | 7 | `known failures: 2` |

CI 的 `[SUMMARY] total: 147, pass: 133, fail: 14, killed by a signal: 1` 是 **stream** 那一次
（`smoke` 只有 10 条、`transforms` 216 条，只有 stream 是 147）—— 所以那条 SUMMARY 不是 smoke 的。
develop 的 stream 是 **132/15**，比 CI 多一条红。

## 3. 栈（用例 `stream.for_loop_jit`，`${case} std::cxx::inline std::opt std::jit`）

```
==4037746==ERROR: AddressSanitizer: SEGV on unknown address 0x7aeaab99fff8 (pc 0x7aeaa2ea894c ... T0)
==4037746==The signal is caused by a READ memory access.
    #0 runFramedJit(unsigned long (*)(unsigned long*, void*), Frame*, void*) fastvm.cpp:1025:30
    #1 invokeOwnedJitFrame(...)            fastvm.cpp:879:16
    #2 invokeCallOrJit(...)                fastvm.cpp:1500:20
    #3 callBorrowed(unsigned long, Frame*) gotovm.cpp:557:45
    #4 call(...)                           fastvm.cpp:1435:25
    #5 apply(Program const*, ostream&)     fastvm.cpp:605:25
    #6 applyPassesDetailed(...)            pass.cpp:3463:33
SUMMARY: AddressSanitizer: SEGV .../src/passes/sched/fastvm/fastvm.cpp:1025:30 in FastVMSchedPass::runFramedJit(unsigned long (*)(unsigned long*, void*), camel::core::context::Frame*, void*)
```

同站点、同一用例族的还有 **13 条** `stream.generator*.jit` 与 transforms 的 **7 条** `vjp_*/transpose_*.jit`。
21 次崩溃的两个常数：故障地址**恒为 `<页边界>-8`**（`…0ff8`/`…9ff8`/`…dff8`），`r12–r15` **恒为**
`0xf8f8f2f8f2f8f2f2 / 0000f2f8f2f2f8f8 / f8f8f2f20000f2f2 / f2f2f2f8f8f8f2f2`——即 ASan 的红区毒值
（`0xf8` 栈右红区、`0xf2` 堆右红区）。**这是帧边界外的 8 字节读，不是空指针跳转**：非 ASan 构建读到的
是相邻内存，所以那条腿一直“绿”。

## 4. 行号（判据 ②）

`develop:1025` 与 `e7e006d1:1025` 是**同一条**：

```
1025:            slot_t result  = curFn(slots, jitCtx);
```

列 30 正是 `curFn(` 这个间接调用点。`runFramedJit` 在**两棵树里都起于 967**，所以 1025 是函数第
**59** 行（签名算第 1 行）。两树 `fastvm.cpp` 的唯一差异是 1160 行注释里被删掉的一个日期
（`…, 2026-11-02,` 被去掉），**漂移 = 0**。`e7e006d1` 那一行在 develop 上不必换算，就是 `:1025`。

## 5. 22 条红的分类（判据 ③）

- **21 条 = sanitizer 站点**（判据：输出里有 `SUMMARY: AddressSanitizer`）：stream 14 + transforms 7，
  全部 `:1025:30`，只在 `std::jit` 后端上；非 jit 的同类用例（`*_fvm`/`*_prl`/`*_nvm`）同批全绿。
- **1 条 = 真错**，不带任何 sanitizer 行：`stream.generators_match_binding_across_yield`，退出
  `SIGABRT`，输出是 Debug 断言

  ```
  Camel Debug Assertion Failed:
      Location   : include/camel/compile/gct/node.h(564)
      Function   : void camel::compile::gct::Node::check(NodeKind) const
      Expression : kind_ == expected
  ```

  它跑的是 `std::nvm`（`camel <case> std::nvm`），**与 JIT、与 ASan 都无关**，是编译器自己在 Debug 下
  断言了。该用例文件自 `e7e006d1` 起没被改过，所以是这两天里 28 笔动过 `src/compile` 的提交之一把它带红的
  （多为 builder 拆分 + 两笔“一个值在一处交付”的行为改动）。
- CI 的 `fail: 14` 与这 **14 条 SEGV** 同站点；develop 多出的第 15 条最可能是**多一条 SEGV**：ASan 的 SEGV
  在这份构建里以 `exit 1` 收场（`meta.json` 里 `signal: null`），而 CI 仍报 `killed by a signal: 1`，
  正对上我们这条 `SIGABRT` 的断言用例 —— 也就是说 CI 那 14 条里已含这条断言。

## 6. 复现物

主机 88（`~/camel-work/asan`，`lane/asan`，HEAD `3cf717a42`）：`asan-build-asan.sh`、`asan-build.log`、
`asan-run-plans.sh`、`asan-plans.log`；失败 artifact 在
`test/results/log/20261121_163852_4035224`（stream，15 个）与 `..._163919_4041911`（transforms，7 个）。
构建树留着（`out/debug` 已是 ASan 版），重跑只需 `bash ~/camel-work/asan-run-plans.sh`。

**本线不产提交**：改 `fastvm.cpp`、改测试、`known_fail` 掉用例三件都按禁令没做。