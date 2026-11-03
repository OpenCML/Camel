# 闸门的 libstdc++ 路径带在哪一层——"只在 `-Check` 加"不成立，那么要不要收敛成一条？

日期：2026-11-03　状态：**前提已核实为否，脚本一个字未改；本条只报不改，等裁决**

## 问什么

有人报："`camel-remote.ps1` **只在 `-Check` 时**把
`/home/ycheng/.conda/pkgs/libstdcxx-16.2.0-h934c35e_4/lib` 加进 `LD_LIBRARY_PATH`（或等价变量）；
任何直接 ssh、或没有该路径就跑 `camel` 二进制的动作，都会得到
`GLIBCXX_3.4.31/32/35/36 not found`，于是每个测试都是 `0/N`、`pass: 0`，看起来像整批红。"

核实结果：**前半句为否**（带它的地方是三处，不是一处），**后半句为真**（下面第 4 条读数就是它）。
于是真正要问的只剩一层：既然那条全局导出已经够用，要不要把三处收敛成一处、以及
`-Changed` 与 `-Plan` 之间那处不一致补不补。

## 三处，原文（`camel-remote.ps1`，不在版本控制，`D:\Projects\Camel-wt\camel-remote.ps1` 是唯一副本）

1. **整个远程脚本的导出**，在 `$remoteScript` 里，构建、plan、checkers 全都排在它后面：

   ```
   export CC=clang CXX=clang++ LD_LIBRARY_PATH="`$C/lib"
   ```

   其中 `C=`$HOME/camel-eval/conda/envs/clang`。这一条对**每一次远程执行**都成立。
2. **plan 分支**（`$runPlan`，同一个值，换了个变量名）：

   ```
   "cd `$R/ && CAMEL_TEST_EXTRA_LIB_DIRS='$LibStdCxxDir' node scripts/test.js $planArgs --jobs $Jobs$tierArg"
   ```
3. **`-Check` 的 compiler 条目**（`$checkBlocks` 里的 `$libPrefix`）：

   ```
   $libPrefix = if ($entry.compiler) { "LD_LIBRARY_PATH='$LibStdCxxDir':`$C/lib " } else { '' }
   ```

另有一处**不对称**：`$runPlan` 的三个分支里只有 `-Plan` 带 `CAMEL_TEST_EXTRA_LIB_DIRS`，
`-Changed`（每次合并的闸门）与 `-UpdateGolden` 都不带。

## 读数（88 上，我这棵树的二进制 `~/camel-work/selfread/out/latest/bin/camel`，`261103_144957_1443eceec_dirty`）

| 环境 | 读数 |
| --- | --- |
| 不给 `LD_LIBRARY_PATH` | `version 'GLIBCXX_3.4.32' not found`（`camel` 三条：3.4.31 / 3.4.32 / 3.4.36，另加 `../libs/libcamel.so` 的 3.4.32） |
| `LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib` | `Camel v0.2.43 (Build 261103_144957_1443eceec_dirty)` ——**全局那一条就够** |
| `LD_LIBRARY_PATH=/home/ycheng/.conda/pkgs/libstdcxx-16.2.0-h934c35e_4/lib` | 同上，起得来 |
| 同一夹具 `test/tools/selftest/sweep` **不给导出**直跑 | `[SUMMARY] total: 13, pass: 0, fail: 13`、退出 1，stderr 第一行就是 `GLIBCXX_3.4.32 not found` |
| 同一夹具经闸门（`-Plan feat/transforms/split -Tier fast -Jobs 4`，19 条用例都起 `camel`） | `[SUMMARY] total: 19, pass: 19, fail: 0`、退出 0 |

两份 `libstdc++.so.6.0.36` 同是 23,874,544 字节、`strings` 最高都到 `GLIBCXX_3.4.36`，
但 md5 不同（`48bce475…` 与 `ed413e5d…`）：同版本号、不同次构建，两个都能满足这个二进制。

**结论**：camel 起不来只在"一条都没带"时发生，而闸门的每一段都在第 1 条之后。
第 2、3 条是**冗余**而不是**救命**：那三个读 `CAMEL_TEST_EXTRA_LIB_DIRS` 的工具
（`scripts/stdlib-reference.mjs#installEnvironment`、`test/tools/check-vjp-ruleset.mjs#installEnvironment`、
`test/tools/check-tuple-trace.mjs`）都是在**继承来的 `LD_LIBRARY_PATH` 前面追加**，不是替换，
所以它们对"手工跑"有用，对"经闸门跑"不改变结果。
另记一句**文档与代码的漂移**（只报不改）：`camel-remote.ps1` 文件头写
"`stdlib-reference --verify-ops` is the one entry that starts the tree's own camel binary"，
而 `$checkers` 表里 `compiler = $true` 的已经有三条（`stdlib-reference`、`conformance`、`gate-static`）。

## 三条路（第四条是本轮被点名要走的）

| 路 | 代价 |
| --- | --- |
| 1. **不动**（本轮默认） | 零成本，且每一段都实测能起；代价是三处各自要保持正确、`-Changed` 与 `-Plan` 的环境差异继续存在 |
| 2. 收敛成第 1 条（收掉 2、3） | 少两处要维护；但把整层押在 `~/camel-eval/conda/envs/clang/lib` 上——换一次 clang 环境就整层红 |
| 3. 反过来收敛成 `$LibStdCxxDir`（把全局那条换成它） | 与 `-Check` 一致、与题面那条路径一致；但那个目录属于另一个用户（`/home/ycheng/...`），被 conda 清理就整层红 |
| 4. 只给 `-Changed`/`-UpdateGolden` 补上同一变量 | 三行内，消掉唯一可见的不一致；但补的是一条今天量不出作用的冗余，且它改的是一份没有 diff 可审的脚本 |

## 默认

**走路 1：脚本一个字不改。** 理由是读数：题面那句"只在 `-Check` 加"不成立，而它想保证的
"每一次远程执行都带上"**已经成立**——第 1 条导出对构建、plan、checkers 都生效，且实测够用。
按一条不成立的前提去改一个没坏的东西，等于把"我以为它坏了"当成读数。

**该被写下来的不是"补脚本"，而是判据**：手搓 ssh 的人仍然会拿到 `0/N`——第 4 条读数就是
那个形状，而它**与负载无关**，重跑一百次也一样。所以先看 stderr 第一行是不是
`GLIBCXX_* not found`，再看宽度与负载（`docs/handover/pitfalls.cn.md` 同一天改的那一行）。
否则每个手搓 ssh 的人都会得到 `0/N`，并把它误判成负载。

## 补记（22:51，这份脚本被**另一条车道**改了，不是这一条）

本页上面的读数取于 22:44–22:52，脚本当时还是我引的那三处。22:51 之后脚本变成
**"整个远程执行都把 `$LibStdCxxDir` 放第一位"**——同一份文件、唯一副本，所以被改的是同一个对象，
只是改的人不是我：

```
export CC=clang CXX=clang++
export LD_LIBRARY_PATH='$LibStdCxxDir'":`$C/lib:`${LD_LIBRARY_PATH:-}"
echo "[camel-remote] libstdc++: $LibStdCxxDir first on LD_LIBRARY_PATH (a command run by hand over ssh needs the same)"
```

对这份记录有三件事要说清：

1. **这一笔没有改过脚本**（它不在版本控制里，也不在我这棵工作树里）；上面第 1 条引文是**改前**的原文。
2. 改后的形状**包含**本页的结论，而且比它更强：构建、plan 与每一个 checker 现在都把
   `$LibStdCxxDir` 放第一位，于是 `-Changed` / `-UpdateGolden` 那处不对称**自动消失**
   （它们继承同一条导出），plan 分支的 `CAMEL_TEST_EXTRA_LIB_DIRS` 变成纯冗余。
3. 改后跑过一次真目标：`-Plan feat/docs_links -Tier fast -Jobs 4` →
   `[SUMMARY] total: 17, pass: 17, fail: 0`、退出 0，日志第一屏就是那行 `[camel-remote] libstdc++: …`；
   改前同一棵树的 `-Plan feat/transforms/split -Tier fast -Jobs 4` 是
   `[SUMMARY] total: 19, pass: 19, fail: 0`。两次都绿，所以这条改动**不是**在某次红上做的修补。

留给下一个人的一句：那条新注释里 "Everything else in this script ran **without** it" 不成立——
`LD_LIBRARY_PATH="$C/lib"` 一直都在，而且实测够用（本页第 2、3 条读数）。
新写法更好（它把"手搓 ssh 该 paste 什么"直接打进输出），但它不是从"什么都没带"改过来的：
把它当成对一次真红的修复来读，就会把 22:44 那份读数当成假的。